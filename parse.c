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

dbuf_t *process_linemarkers(const char *const data,
                            unsigned long size)
{
        const char *chp = data, *const limit = data + size, *nxt;
        dbuf_t *buffer;
        /* We cannot change bytes below that index in @buffer */
        unsigned long no_change_idx = 0UL;
        char *filename = NULL;
        unsigned long linenum = 1UL;

        buffer = xmalloc(sizeof(*buffer)); dbuf_init(buffer);

        for (; chp < limit; chp = nxt) {
                linemarker_t lm_mem;
                long linelen;

                memset(&lm_mem, 0, sizeof(lm_mem));
                if (read_linemarker(chp, limit, &lm_mem, &nxt) == 0) {
                        if (filename == NULL ||
                            strcmp(filename,
                                   lm_mem.filename) != 0) {
                                xfree(filename);
                                filename = lm_mem.filename;

                                lm_mem.filename = NULL;

                                no_change_idx = (unsigned long) (buffer->pos -
                                                                 buffer->base);

                                goto next_line;
                        } else {
                                xfree(lm_mem.filename);
                                lm_mem.filename = NULL;
                        }

                        if (lm_mem.linenum < linenum) {
                                unsigned long to_strip;
                                char *p;

                                to_strip = linenum - lm_mem.linenum;
                                p = buffer->pos;

                                /* Do not allow rogue linemarkers
                                   to affect lines of other files. */
                                while (p > buffer->base + no_change_idx &&
                                       to_strip > 0UL) {
                                        p--;

                                        if (*p == '\n') {
                                                *p = ' '; to_strip--;
                                        }
                                }

                                ;
                        } else {
                                /* Pretend we've put enough empty lines
                                   to the file */
                                ;
                        }

                next_line:
                        /* We always synchronize linenum
                           with linemarkers */
                        linenum = lm_mem.linenum;

                        if (nxt < limit) nxt++;
                        continue;
                } else {
                        /* Assume normal line here */
                        for (nxt = chp; !is_eol(nxt, limit); nxt++) ;
                        if (nxt < limit) nxt++;
                }

                if ((linelen = (long) (nxt - chp)) > (long) INT_MAX ||
                    dbuf_printf(buffer, "%.*s", (int) linelen, chp) < 0) {
                        int printlen;

                        if (linelen < 80L)
                                printlen = (int) linelen;
                        else
                                printlen = 80;

                        print_error_msg(-1, 0,
                                        "Failed to print line:\n"
                                        "%.*s",
                                        printlen, chp);

                        dbuf_free(buffer);
                        xfree(buffer); buffer = NULL;
                        goto out;
                }

                if (linenum == ULONG_MAX) {
                        int printlen;

                        if (linelen < 80L)
                                printlen = (int) linelen;
                        else
                                printlen = 80;

                        print_error_msg(-1, 0,
                                        "Linenum overflow on line:\n"
                                        "%.*s",
                                        printlen, chp);

                        dbuf_free(buffer);
                        xfree(buffer); buffer = NULL;
                        goto out;
                }

                linenum++;
        }

out:
        xfree(filename); filename = NULL;

        return buffer;
}

struct block_desc {
        int ch;
        unsigned long indent;
};

static struct block_desc *push_block_desc(dbuf_t *blocks,
                                          int ch,
                                          unsigned long indent)
{
        struct block_desc *desc;

        desc = (struct block_desc *) dbuf_alloc(blocks, sizeof(*desc));
        if (desc == NULL) {
                print_error_msg(-1, 0,
                                "Failed to push block description:\n"
                                "    %c, %lu\n"
                                "In function:\n"
                                "    %s",
                                ch, indent, __func__);
                _exit(ENOENT);
        }

        memset(desc, 0, sizeof(*desc));
        desc->ch = ch; desc->indent = indent;

        /* Overflow check is done by dbuf_alloc */
        blocks->pos += sizeof(*desc);

        return desc;
}

static struct block_desc *pop_block_desc(dbuf_t *blocks,
                                         int ch)
{
        struct block_desc *desc;

        if (ch == ')')
                ch = '(';
        else if (ch == '}')
                ch = '{';
        else {
                print_error_msg(-1, 0,
                                "Unknown character:\n"
                                "    %c\n"
                                "In function:\n"
                                "    %s",
                                ch, __func__);
                _exit(EINVAL);
        }

        if (blocks->pos == blocks->base) {
                print_error_msg(-1, 0,
                                "No more stack entries.\n"
                                "In function:\n"
                                "    %s",
                                __func__);
                _exit(ENOENT);
        }

        desc = (struct block_desc *) blocks->pos - 1;

        if (ch != desc->ch) {
                print_error_msg(-1, 0,
                                "Wrong block type:\n"
                                "    expected [%c], actual [%c]\n"
                                "In function:\n"
                                "    %s",
                                ch, desc->ch, __func__);
                _exit(ESRCH);
        }

        blocks->pos = (char *) desc;

        return (blocks->pos == blocks->base) ? NULL : (desc - 1);
}

static int skip_comment(char **chpp,
                        char *const limit)
{
        char *chp = *chpp;
        int is_skipped = 0;

        if (chp < limit && *chp++ == '/' &&
            chp < limit && (*chp == '*' || *chp == '/')) {
                is_skipped = 1;

                if (*chp++ == '*') {
                        int is_terminated = 0;

                        /* Seek '*' '/'
                           skipping everything until that */
                        while (chp < limit) {
                                if (*chp++ == '*' && chp < limit &&
                                    *chp == '/') {
                                        is_terminated = 1;
                                        chp++; break;
                                }
                        }

                        if (!is_terminated) {
                                print_error_msg(-1, 0,
                                                "Incomplete multiline "
                                                "comment detected.\n"
                                                "In function:\n"
                                                "    %s",
                                                __func__);
                                _exit(EINVAL);
                        }
                } else {
                        /* Eliminate the comment but keep NL
                           character */
                        while (!is_eol(chp, limit)) chp++;
                }
        }

        if (is_skipped) *chpp = chp;

        return is_skipped;
}

static char get_character(char **chpp,
                          char *const limit)
{
        char *chp = *chpp, ret;

        ret = '\0';

        while (skip_comment(&chp, limit) ||
               (chp < limit && is_ws(*chp) && (chp++, 1))) ret = ' ';

        /* Enumerated characters are handled specially.
           So it is desirable to omit whitespaces
           preceding them */
        if (chp < limit &&
            (ret == '\0' || (ret == ' ' && (*chp == '\n' ||
                                            *chp == ';'  ||
                                            *chp == '{'  ||
                                            *chp == '}'  ||
                                            *chp == '('  ||
                                            *chp == ')')))) ret = *chp++;

        *chpp = chp;
        return ret;
}

static void unget_character(char **chpp,
                            char ch,
                            char *const limit)
{
        char *chp = *chpp;

        if (chp > limit) {
                *--chp = ch;
                *chpp = chp;
        } else {
                print_error_msg(-1, 0,
                                "No head space for character.\n"
                                "In function:\n"
                                "    %s",
                                __func__);
                _exit(ENOMEM);
        }
}

dbuf_t *adjust_style(char *const data,
                     unsigned long size)
{
        char *chp = data, *const limit = data + size, ch;
        dbuf_t blocks_mem, *const blocks = &blocks_mem, *buffer;
        enum {
                /* At the start of a new line. Substate #1.
                   We are allowed here to add extra NL character
                   (used perhaps for semantical grouping of statements). */
                S_NL1,
                /* At the start of a new line. Substate #2.
                   Skip all whitespace characters.
                   Add fixed amount of space characters ' '
                   just before the initial token in the line */
                S_NL2,
                /* Main program text.
                   No line data exists in the output buffer */
                S_TEXT1,
                /* Main program text.
                   Some line data has been printed */
                S_TEXT2,
                /* This is quoted text.
                   It should be transmitted to output buffer
                   without any changes. */
                S_QUOTED,
        } state = S_NL2;

        unsigned long linelen = 0UL, blk_indent = 0UL;
        struct block_desc *current;

        buffer = xmalloc(sizeof(*buffer));
        dbuf_init(blocks);
        dbuf_init(buffer);

        current = push_block_desc(blocks, '$', 0UL);

        for (ch = get_character(&chp, limit); ch != '\0';) {
                switch (state) {
                case S_NL1:
                        if (ch == '\n') {
                                dbuf_putc(buffer, '\n');

                                state = S_NL2;

                                goto next;
                        }
                        /* FALLTHRU */
                case S_NL2:
                        if (ch == '\n' ||
                            ch == ' ')
                                goto next;

                        for (linelen = 0UL;
                             linelen < current->indent;
                             linelen++) dbuf_putc(buffer, ' ');

                        state = S_TEXT1;
                        /* FALLTHRU */
                case S_TEXT1:
                case S_TEXT2:
                        if (ch == '\n') {
                                unget_character(&chp, ' ', data);

                                goto next;
                        }

                        if (ch == ';') {
                                dbuf_putc(buffer, ';'); linelen++;
                                dbuf_putc(buffer, '\n');

                                if ((ch = get_character(&chp, limit)) == '\n')
                                        ch = get_character(&chp, limit);

                                state = S_NL1;

                                continue;
                        }

                        if (ch == '{') {
                                if (state == S_TEXT2) {
                                        dbuf_putc(buffer, ' '); linelen++;
                                }

                                dbuf_putc(buffer, '{'); linelen++;
                                dbuf_putc(buffer, '\n');

                                blk_indent += 4UL;

                                current = push_block_desc(blocks,
                                                          '{',
                                                          blk_indent);

                                if ((ch = get_character(&chp, limit)) == '\n')
                                        ch = get_character(&chp, limit);

                                state = S_NL1;

                                continue;
                        }

                        if (ch == '}') {
                                current = pop_block_desc(blocks,
                                                         '}');

                                if (state == S_TEXT2) {
                                        dbuf_putc(buffer, '\n');
                                        for (linelen = 0UL;
                                             linelen < current->indent;
                                             linelen++) dbuf_putc(buffer, ' ');
                                } else {
                                        assert(linelen == blk_indent);

                                        if (current->indent > linelen) {
                                                for (;
                                                     linelen < current->indent;
                                                     linelen++) {
                                                        dbuf_putc(buffer, ' ');
                                                }
                                        } else {
                                                unsigned long delta;

                                                delta = (linelen -
                                                         current->indent);
                                                linelen -= delta;
                                                buffer->pos -= delta;
                                        }
                                }

                                blk_indent -= 4UL;

                                dbuf_putc(buffer, '}'); linelen++;

                                if ((ch = get_character(&chp, limit)) == '\n') {
                                        dbuf_putc(buffer, '\n');
                                        ch = get_character(&chp, limit);

                                        state = S_NL1;
                                } else {
                                        state = S_TEXT2;
                                }

                                continue;
                        }

                        if (ch == '(') {
                                if (state == S_TEXT2) {
                                        dbuf_putc(buffer, ' '); linelen++;
                                }

                                dbuf_putc(buffer, '('); linelen++;

                                current = push_block_desc(blocks,
                                                          '(',
                                                          linelen);

                                if ((ch = get_character(&chp, limit)) == '\n') {
                                        dbuf_putc(buffer, '\n');
                                        ch = get_character(&chp, limit);

                                        state = S_NL1;
                                } else {
                                        state = S_TEXT2;
                                }

                                continue;
                        }

                        if (ch == ')') {
                                current = pop_block_desc(blocks,
                                                         ')');

                                dbuf_putc(buffer, ')'); linelen++;

                                if ((ch = get_character(&chp, limit)) == '\n') {
                                        unget_character(&chp, ' ', data);
                                        ch = get_character(&chp, limit);
                                }

                                state = S_TEXT2;

                                continue;
                        }

                        if (ch == '"' || ch == '\'') {
                                dbuf_putc(buffer, ch); linelen++;

                                state = S_QUOTED;

                                continue;
                        }

                        dbuf_putc(buffer, ch); linelen++;

                        state = S_TEXT2;

                        goto next;

                case S_QUOTED:
                        while (chp < limit && *chp != ch) {
                                dbuf_putc(buffer, *chp); linelen++;

                                if (*chp++ == '\\' && chp < limit) {
                                        dbuf_putc(buffer, *chp++); linelen++;
                                }
                        }

                        if (chp >= limit) {
                                print_error_msg(-1, 0,
                                                "Incomplete quoted "
                                                "text detected.\n"
                                                "In function:\n"
                                                "    %s",
                                                __func__);
                                _exit(EINVAL);
                        }

                        dbuf_putc(buffer, *chp++); linelen++;

                        state = S_TEXT2;

                        goto next;
                }
        next:
                ch = get_character(&chp, limit);
        }

        dbuf_free(blocks);

        return buffer;
}
