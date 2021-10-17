#include "common.h"

/** Routines handling linemarker directive in preprocessed code.
    Each linemarker has following structure:
    '#' <unsigned number> '"' <quoted string> '"' {<unsigned number>}
**/

int is_eol(const char *chp, const char *const limit)
{
        return chp >= limit || *chp == '\n';
}

int is_ws(char ch)
{
        return (ch == ' '  || ch == '\f' ||
                ch == '\r' || ch == '\t' || ch == '\v');
}

static int parse_ul(const char *chp,
                    const char *const limit,
                    unsigned long *valp,
                    const char   **nxtp)
{
        unsigned long old_val, val = 0UL;

        for (;
             !is_eol(chp, limit) && '0' <= *chp && *chp <= '9';
             chp++) {
                old_val = val;
                val = val * 10UL + (unsigned long) (*chp - '0');
                /* Check if UL type can't hold this number */
                if (val < old_val)
                        return -1;
        }

        /* Check if no whitespace (or NL) is found after sequence of digits */
        if (!is_eol(chp, limit) && !is_ws(*chp))
                return -1;

        *valp = val;
        *nxtp = chp;
        return 0;
}

static int parse_quoted_string(const char *chp,
                               const char *const limit,
                               char quote,
                               char       **valp,
                               const char **nxtp)
{
        const char *src;
        char *val, *dst;
        unsigned long nalloc = 1UL;

        if (*chp != quote)
                return -1;

        src = ++chp;
        for (; !is_eol(chp, limit) && *chp != quote; nalloc++, chp++) {
                if (*chp == '\\') {
                        chp++;
                        if (is_eol(chp, limit))
                                return -1; /* Invalid escaping with backslash */
                }
        }

        if (is_eol(chp, limit))
                return -1; /* Couldn't find terminating quote character */

        val = dst = xmalloc(nalloc);

        for (; src < chp;) {
                if (*src == '\\')
                        src++;
                *dst++ = *src++;
        }
        *dst = '\0';

        *valp = val;
        *nxtp = chp + 1; /* Skip terminating quote character */
        return 0;
}

int read_linemarker(const char *chp,
                    const char *const limit,
                    linemarker_t *lm,
                    const char **nxtp)
{
        linemarker_t lm_mem;
        const char *nxt;
        enum {
                S_X_HASH,     /* Expecting '#' character */
                S_X_LINENUM,  /* Expecting integer which is linenum */
                S_X_FILENAME, /* Expecting string which is filename */
                S_X_FLAG,     /* Expecting integer which is flag */
                S_FAIL,       /* Failed to parse a linemarker */
        } state = S_X_HASH;
        memset(&lm_mem, 0, sizeof(lm_mem));

        for (; state != S_FAIL && !is_eol(chp, limit); chp = nxt) {
                for (nxt = chp;
                     !is_eol(nxt, limit) && is_ws(*nxt);
                     nxt++) ;
                if (nxt != chp)
                        continue;

                switch (state) {

                case S_X_HASH: {
                        if (*chp != '#')
                                state = S_FAIL;
                        else
                                state = S_X_LINENUM;
                        break;
                }

                case S_X_LINENUM: {
                        if (parse_ul(chp, limit, &lm_mem.linenum, &nxt) == 0) {
                                state = S_X_FILENAME;
                                continue;
                        }

                        state = S_FAIL;
                        break;
                }

                case S_X_FILENAME: {
                        if (parse_quoted_string(chp, limit, '"', &lm_mem.filename, &nxt) == 0) {
                                state = S_X_FLAG;
                                continue;
                        }

                        state = S_FAIL;
                        break;
                }

                case S_X_FLAG: {
                        unsigned long flag;

                        if (parse_ul(chp, limit, &flag, &nxt) == 0) {
                                if (sizeof(lm_mem.info) * 8UL >= flag &&
                                    flag >= 1UL)
                                        lm_mem.info |= 1UL << (flag - 1UL);
                                else
                                        state = S_FAIL;
                                continue;
                        }

                        state = S_FAIL;
                        break;
                }

                case S_FAIL: break;

                }

                nxt = chp + 1;
        }

        if (state == S_X_FLAG) {
                *lm = lm_mem;
                *nxtp = nxt;
                return 0;
        } else {
                if (lm_mem.filename)
                        xfree(lm_mem.filename);
                return -1;
        }
}

struct line_desc {
        char *filename;
        unsigned long linenum;
};

static struct line_desc *push_line_desc(dbuf_t *stack,
                                        const char *filename,
                                        unsigned long linenum)
{
        struct line_desc *desc;

        desc = (struct line_desc *) dbuf_alloc(stack, sizeof(*desc));

        if (desc == NULL)
                return NULL;

        desc->filename = xstrdup(filename);
        desc->linenum = linenum;

        stack->pos += sizeof(*desc);

        return desc;
}

static struct line_desc *pop_line_desc(dbuf_t *stack)
{
        struct line_desc *desc, *prev;

        if (((struct line_desc *) stack->pos -
             (struct line_desc *) stack->base) < 2L)
                return NULL;

        desc = (struct line_desc *) stack->pos - 2L;
        prev = desc + 1L;

        xfree(prev->filename); prev->filename = NULL;
        stack->pos = (char *) prev;

        return desc;
}

dbuf_t *process_linemarkers(const char *const data,
                            unsigned long size)
{
#define LMFL_NEW 0x1UL
#define LMFL_RET 0x2UL
#define LMFL_BOTH (LMFL_NEW | LMFL_RET)

        const char *chp = data, *const limit = data + size, *nxt;
        dbuf_t stack_mem, *stack = &stack_mem, *dbuf;
        struct line_desc *current;
        linemarker_t lm_mem;
        unsigned int skip_count = 0U;
        /* Byte index in @dbuf contents below
           which we cannot strip the newlines off */
        unsigned long guard_idx = 0UL;

        /* Read initial linemarker to initialize the stack */
        memset(&lm_mem, 0, sizeof(lm_mem));
        if (read_linemarker(chp, limit, &lm_mem, &nxt) < 0) {
                print_error_msg(-1, 0,
                                "No initial linemarker");
                return NULL;
        }

        if (nxt < limit) nxt++;
        chp = nxt;

        /* Check that initial linemarker is sane:
           + No stack-control flags (1, 2) are present
           + Linenum must be 1 - we are at the very start
         */
        if (lm_mem.linenum != 1UL || (lm_mem.info & LMFL_BOTH) != 0UL) {
                print_error_msg(-1, 0,
                                "Initial linemarker is malformed:\n"
                                "   FILENAME = %s\n"
                                "    LINENUM = %lu\n"
                                "       INFO = 0x%lx",
                                lm_mem.filename,
                                lm_mem.linenum,
                                lm_mem.info);
                xfree(lm_mem.filename);
                return NULL;
        }

        dbuf_init(stack);
        if ((current = push_line_desc(stack,
                                      lm_mem.filename,
                                      lm_mem.linenum)) == NULL) {
                print_error_msg(-1, 0,
                                "Failed to push initial linemarker:\n"
                                "   FILENAME = %s\n"
                                "    LINENUM = %lu\n"
                                "       INFO = 0x%lx",
                                lm_mem.filename,
                                lm_mem.linenum,
                                lm_mem.info);
                dbuf_free(stack);
                xfree(lm_mem.filename);
                return NULL;
        }

        xfree(lm_mem.filename); lm_mem.filename = NULL;

        dbuf = xmalloc(sizeof(*dbuf)); dbuf_init(dbuf);

        for (; chp < limit; chp = nxt) {
                long linelen;

                memset(&lm_mem, 0, sizeof(lm_mem));

                if (read_linemarker(chp, limit, &lm_mem, &nxt) == 0) {
                        if ((lm_mem.info & LMFL_BOTH) == LMFL_BOTH) {
                                print_error_msg(-1, 0,
                                                "Malformed linemarker:\n"
                                                "   FILENAME = %s\n"
                                                "    LINENUM = %lu\n"
                                                "       INFO = 0x%lx",
                                                lm_mem.filename,
                                                lm_mem.linenum,
                                                lm_mem.info);
                                xfree(lm_mem.filename);
                                dbuf_free(dbuf);
                                xfree(dbuf); dbuf = NULL;
                                goto out;
                        } else if ((lm_mem.info & LMFL_NEW) != 0UL) {
                                if (lm_mem.linenum != 1UL) {
                                        print_error_msg(-1, 0,
                                                        "Linemarker [NEW] has "
                                                        "wrong linenum:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                if (skip_count == UINT_MAX) {
                                        print_error_msg(-1, 0,
                                                        "Overflow in "
                                                        "skip_count "
                                                        "caused by:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                } else if (skip_count > 0U) {
                                        skip_count++;

                                        goto skip_linemarker;
                                }

                                current = push_line_desc(stack,
                                                         lm_mem.filename,
                                                         lm_mem.linenum);
                                if (current == NULL) {
                                        print_error_msg(-1, 0,
                                                        "Failed to push "
                                                        "linemarker:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                guard_idx = (unsigned long) (dbuf->pos -
                                                             dbuf->base);
                        } else if ((lm_mem.info & LMFL_RET) != 0UL) {
                                if (skip_count > 1U) {
                                        skip_count--;

                                        goto skip_linemarker;
                                } else if (skip_count == 1U) {
                                        print_error_msg(-1, 0,
                                                        "Underflow in "
                                                        "skip_count "
                                                        "caused by:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                current = pop_line_desc(stack);
                                if (current == NULL) {
                                        print_error_msg(-1, 0,
                                                        "Inconsistency in "
                                                        "the sequence of "
                                                        "linemarkers:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                if (current->linenum >= lm_mem.linenum ||
                                    strcmp(current->filename,
                                           lm_mem.filename) != 0) {
                                        print_error_msg(-1, 0,
                                                        "Linemarker [RET] "
                                                        "contradicts "
                                                        "previous data:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                guard_idx = (unsigned long) (dbuf->pos -
                                                             dbuf->base);
                        } else {
                                if (skip_count <= 1U) {
                                        if (strcmp(current->filename,
                                                   lm_mem.filename) != 0)
                                                skip_count = 1U;
                                        else
                                                skip_count = 0U;
                                }

                                if (skip_count > 0U)
                                        goto skip_linemarker;
                        }

                        if (lm_mem.linenum < current->linenum) {
                                unsigned long to_strip;
                                char *p;

                                to_strip = current->linenum - lm_mem.linenum;
                                p = dbuf->pos;

                                while (p > dbuf->base + guard_idx) {
                                        p--;

                                        if (*p == '\n') {
                                                *p = ' ';
                                                if (--to_strip == 0UL)
                                                        break;
                                        }
                                }

                                if (to_strip > 0UL) {
                                        /* We've encountered malicious
                                           linemarker */
                                        print_error_msg(-1, 0,
                                                        "Linemarker:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx\n"
                                                        "instructs "
                                                        "to cut newlines "
                                                        "below checkpoint:\n"
                                                        "    %lu",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info,
                                                        guard_idx);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }

                                current->linenum = lm_mem.linenum;
                        } else {
                                int is_success = 1;

                                for (;
                                     current->linenum < lm_mem.linenum;
                                     current->linenum++) {
                                        if (dbuf_putc(dbuf, '\n') < 0) {
                                                is_success = 0;
                                                break;
                                        }
                                }

                                if (!is_success) {
                                        print_error_msg(-1, 0,
                                                        "Failed to put "
                                                        "newline due to "
                                                        "the linemarker:\n"
                                                        "   FILENAME = %s\n"
                                                        "    LINENUM = %lu\n"
                                                        "       INFO = 0x%lx",
                                                        lm_mem.filename,
                                                        lm_mem.linenum,
                                                        lm_mem.info);
                                        xfree(lm_mem.filename);
                                        dbuf_free(dbuf);
                                        xfree(dbuf); dbuf = NULL;
                                        goto out;
                                }
                        }

                skip_linemarker:
                        if (nxt < limit) nxt++;

                        xfree(lm_mem.filename); lm_mem.filename = NULL;

                        continue; /* for (; chp < limit; chp = nxt) */
                } else {
                        /* read_linemarker(chp, limit, &lm_mem, &nxt) != 0 
                           Assume normal line here */
                        for (nxt = chp; !is_eol(nxt, limit); nxt++) ;
                        if (nxt < limit) nxt++;
                }

                if (skip_count > 0U)
                        continue;

                if ((linelen = (long) (nxt - chp)) > (long) INT_MAX ||
                    dbuf_printf(dbuf, "%.*s", (int) linelen, chp) < 0) {
                        int printlen;

                        if (linelen < 80L)
                                printlen = (int) linelen;
                        else
                                printlen = 80;

                        print_error_msg(-1, 0,
                                        "Failed to print line\n:"
                                        "%.*s",
                                        printlen, chp);

                        dbuf_free(dbuf);
                        xfree(dbuf); dbuf = NULL;
                        goto out;
                }

                current->linenum++;
        }

out:
        for (current = (struct line_desc *) stack->base;
             current < (struct line_desc *) stack->pos;
             current++) {
                xfree(current->filename);
        }

        dbuf_free(stack);

        return dbuf;
#undef LMFL_BOTH
#undef LMFL_NEW
#undef LMFL_RET
}
