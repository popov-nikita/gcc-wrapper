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

/*
	Trims whitespaces leaving only one instance between adjacent tokens
	(except initial whitespaces in a line).
	Comment is considered one whitespace character.
	Everything is done in-place.
	Returns size of trimmed content.
 */
unsigned long trim_whitespaces(char *const data, unsigned long size)
{
	char *src, *dst, *saved_dst, *const limit = data + size, quote = '\0';
	int seen_token = 0, init_whitespaces = 1;
	enum {
		S_WHITESPACE,
		S_TOKEN,
		S_IN_QUOTES,
		S_IN_ML_COMMENT,
		S_IN_OL_COMMENT,
	} state = S_WHITESPACE;

	for (src = saved_dst = dst = data;
	     src < limit;
	     src++) {
		switch (state) {
		case S_WHITESPACE:
			if (is_ws(*src)) {
				if (init_whitespaces)
					*dst++ = *src;
				continue;
			}

			init_whitespaces = 0;

			if (*src == '\n') {
				if (!seen_token)
					dst = saved_dst;
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_whitespaces = 1;
				continue;
			}

			if (*src == '/') {
				char lookahead = (src + 1 < limit) ? *(src + 1) : '\0';

				if (lookahead == '*') {
					src++;
					state = S_IN_ML_COMMENT;
					continue;
				} else if (lookahead == '/') {
					src++;
					state = S_IN_OL_COMMENT;
					continue;
				}
			}

			if (seen_token)
				*dst++ = ' ';

			seen_token = 1;
			if (*src == '"' || *src == '\'') {
				quote = *src;
				state = S_IN_QUOTES;
			} else {
				state = S_TOKEN;
			}
			*dst++ = *src;
			continue;

		case S_TOKEN:
			if (is_ws(*src)) {
				state = S_WHITESPACE;
				continue;
			}

			if (*src == '\n') {
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_whitespaces = 1;
				state = S_WHITESPACE;
				continue;
			}

			if (*src == '/') {
				char lookahead = (src + 1 < limit) ? *(src + 1) : '\0';

				if (lookahead == '*') {
					src++;
					state = S_IN_ML_COMMENT;
					continue;
				} else if (lookahead == '/') {
					src++;
					state = S_IN_OL_COMMENT;
					continue;
				}
			}

			if (*src == '"' || *src == '\'') {
				quote = *src;
				state = S_IN_QUOTES;
			} else {
				;
			}
			*dst++ = *src;
			continue;

		case S_IN_QUOTES:
			/* Handle escaping */
			if (*src == '\\') {
				if (src + 1 < limit)
					*dst++ = *src++;
			} else if (*src == quote) {
				quote = '\0';
				state = S_TOKEN;
			}

			*dst++ = *src;
			continue;

		case S_IN_ML_COMMENT:
			if (*src == '\n') {
				if (!seen_token)
					dst = saved_dst;
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_whitespaces = 1;
			} else if (*src == '*') {
				char lookahead = (src + 1 < limit) ? *(src + 1) : '\0';

				if (lookahead == '/') {
					src++;
					state = S_WHITESPACE;
				}
			}

			continue;

		case S_IN_OL_COMMENT:
			if (*src == '\n') {
				if (!seen_token)
					dst = saved_dst;
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_whitespaces = 1;
				state = S_WHITESPACE;
			}

			continue;
		}
	}

	if (!seen_token)
		dst = saved_dst;

	return (unsigned long) (dst - data);
}

/* Removes extra empty lines leaving only one */
unsigned long shrink_lines(char *const data, unsigned long size)
{
	char *src, *dst, *const limit = data + size;
	unsigned long nl_count;

	for (src = dst = data, nl_count = 0UL;
	     src < limit;
	     src++) {
		if (*src == '\n') {
			if (nl_count < 2UL) {
				nl_count++;
				*dst++ = *src;
			} else {
				; /* No copy */
			}
		} else {
			nl_count = 0UL;
			*dst++ = *src;
		}
	}

	return (unsigned long) (dst - data);
}
