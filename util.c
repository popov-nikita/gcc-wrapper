#include	"common.h"

/* Creates private RW mapping of the file specified with @path.
   Return -1 on failure (for any reason), 0 - on success.
   Provides mapping data (base address + size) via pointers @basep, @sizep. */
int load_file(const char *path, void **basep, unsigned long *sizep)
{
	int fd, rc = -1;
	void *base;
	unsigned long size;
	struct stat st_mem;

	if ((fd = open(path, O_RDONLY)) < 0)
		goto out;

	if ((memset(&st_mem, 0, sizeof(st_mem)), fstat(fd, &st_mem)) < 0 ||
	    st_mem.st_size <= 0)
		goto out_close;

	size = (unsigned long) st_mem.st_size;
	if ((base = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
		goto out_close;

	*basep = base;
	*sizep = size;
	rc = 0;

out_close:
	close(fd);
out:
	return rc;
}

/* Wrapper around `munmap` call to remove loaded file */
void unload_file(void *base, unsigned long size)
{
	munmap(base, size);
}

char *get_basename(const char *path)
{
	const char *s, *e;
	char *res, *p;

	if (!path)
		return 0;

	for (e = s = path + strlen(path);
	     s >= path && *s != '/';
	     s--)
		if (*s == '.')
			e = s;
	s++;

	p = res = xmalloc((unsigned long) (e - s) + 1UL);
	for (; s < e;)
		*p++ = *s++;
	*p = '\0';

	return res;
}

/* Using $PATH environment variable locates real path of target binary */
char *locate_bin_file(const char *bname)
{
	const char *path, *s, *e;
	char *resolved = 0, *tmp;
	unsigned long blen;

	if (!bname ||
	    !(blen = strlen(bname)) ||
	    !(path = getenv("PATH")))
		return resolved;

	for (s = path; *s; s = e + !!*e) {
		char *p;

		for (e = s; *e && *e != ':'; e++) ;

		if (s == e) {
			tmp = xmalloc(1UL + 1UL + blen + 1UL);
			tmp[0] = '.';
			p = tmp + 1;
		} else {
			tmp = xmalloc((unsigned long) (e - s) + 1UL + blen + 1UL);
			memcpy(tmp, s, (unsigned long) (e - s));
			p = tmp + (unsigned long) (e - s);
		}
		*p++ = '/';
		memcpy(p, bname, blen);
		*(p += blen) = '\0';
		if (access(tmp, X_OK) == 0) {
			resolved = tmp;
			break;
		}
		xfree(tmp);
	}

	if (!resolved &&
	    (!*path || *(s - 1) == ':')) {
		tmp = xmalloc(1UL + 1UL + blen + 1UL);
		tmp[0] = '.';
		tmp[1] = '/';
		strcpy(tmp + 2, bname);
		if (access(tmp, X_OK) == 0)
			resolved = tmp;
		else
			xfree(tmp);
	}

	return resolved;
}

static void abort_no_mem(unsigned long size)
{
	static const char msg_1[] = "Failed to allocate ";
	static const char msg_2[] = " bytes of memory\n";
	char buf[64], *p = buf, *s;
	unsigned long len;
	long unused;

	unused = write(STDERR_FILENO, msg_1, sizeof(msg_1) - 1), (void) unused;

	do {
		int d = (int) (size % 10UL) + '0';
		*p++ = d;
		size /= 10UL;
	} while (p < buf + sizeof(buf) && size > 0);

	len = (unsigned long) (p - buf);

	for (p--, s = buf; p > s; p--, s++) {
		char tmp;
		tmp = *p;
		*p = *s;
		*s = tmp;
	}

	unused = write(STDERR_FILENO, buf, len), (void) unused;

	unused = write(STDERR_FILENO, msg_2, sizeof(msg_2) - 1), (void) unused;

	_exit(E_NOMEM);
}

void *xmalloc(unsigned long size)
{
	void *ptr;

	if (size >= (~0UL >> 1) ||
	    !(ptr = malloc(size))) {
		abort_no_mem(size);
	}

	return ptr;
}

void *xrealloc(void *ptr, unsigned long size)
{
	void *tmp_ptr;

	if (size >= (~0UL >> 1) ||
	    !(tmp_ptr = realloc(ptr, size))) {
		xfree(ptr);
		abort_no_mem(size);
	}

	return tmp_ptr;
}

char *xstrdup(const char *s)
{
	char *d;
	unsigned long slen;

	if (!s)
		return 0;

	slen = strlen(s) + 1UL;
	d = xmalloc(slen);
	memcpy(d, s, slen);

	return d;
}

void xfree(void *ptr)
{
	if (ptr)
		free(ptr);
}

void dyn_buf_init(dyn_buf_t *buf)
{
	if (!buf)
		return;
	buf->pos = buf->base = buf->_mem;
	buf->capacity = sizeof(buf->_mem) / sizeof(buf->_mem[0]);
}

/* Ensures, @buf is able to hold at least @to_reserve bytes of data.
   Returns pointer to the start of allocated space. */
char *dyn_buf_reserve(dyn_buf_t *buf, unsigned long to_reserve)
{
	unsigned long cur_sz, req_sz;

	if (!buf)
		return 0;
	cur_sz = buf->pos - buf->base;
	req_sz = cur_sz + to_reserve;
	if (req_sz > buf->capacity) {
		while (req_sz > (buf->capacity *= 2UL)) ;
		if (buf->base == buf->_mem) {
			buf->base = xmalloc(buf->capacity);
			memcpy(buf->base, buf->_mem, cur_sz);
			buf->pos = buf->base + cur_sz;
		} else {
			buf->base = xrealloc(buf->base, buf->capacity);
			buf->pos = buf->base + cur_sz;
		}
	}

	return buf->pos;
}

int dyn_buf_printf(dyn_buf_t *buf, const char *fmt, ...)
{
	char scratch_mem[1024], *pos;
	va_list ap;
	int rv;
	unsigned long req_sz;

	if (!buf)
		return -1;
	va_start(ap, fmt);
	rv = vsnprintf(scratch_mem, sizeof(scratch_mem), fmt, ap);
	va_end(ap);

	if (rv < 0)
		return -1;
	req_sz = (unsigned long) rv + 1UL;
	pos = dyn_buf_reserve(buf, req_sz);
	if (req_sz <= sizeof(scratch_mem)) {
		memcpy(pos, scratch_mem, req_sz);
	} else {
		va_start(ap, fmt);
		rv = vsnprintf(pos, req_sz, fmt, ap);
		va_end(ap);

		if  (rv < 0)
			return -1;

		if ((unsigned long) rv + 1UL != req_sz)
			return -1;
	}

	buf->pos += req_sz - 1UL;
	return rv;
}

void dyn_buf_free(dyn_buf_t *buf)
{
	if (!buf)
		return;
	if (buf->base != buf->_mem) {
		xfree(buf->base);
		buf->base = buf->_mem;
		buf->capacity = sizeof(buf->_mem) / sizeof(buf->_mem[0]);
	}
	buf->pos = buf->base;
}

/* Routines handling linemarker directive in preprocessed code.
   Each has following structure:
   '#' <unsigned number> '"' <quoted string> '"' {<unsigned number>}
 */

typedef struct {
	unsigned long linenum;
	char *filename;
	unsigned long flags;
} linemarker_t;

static int parse_ul(const char *p,
                    const char *const limit,
                    unsigned long *valp,
                    const char **nextp)
{
	unsigned long old_val, val = 0UL;

	for (;
	     !is_end_of_line(p, limit) && '0' <= *p && *p <= '9';
	     p++) {
		old_val = val;
		val = val * 10UL + (unsigned long) (*p - '0');
		if (val < old_val)
			return -1; /* Too many digits: 
                                      UL type can't hold this number */
	}

	if (!is_end_of_line(p, limit) &&
	    !is_whitespace(*p))
		return -1; /* No whitespace (or NL) is found after sequence of digits */

	*valp = val;
	*nextp = p;
	return 0;
}

static int parse_quoted_string(const char *p,
                               const char *const limit,
                               char quote,
                               char **valp,
                               const char **nextp)
{
	const char *src;
	char *val, *dst;
	unsigned long nalloc = 1UL;

	if (*p != quote)
		return -1;

	src = ++p;
	for (; !is_end_of_line(p, limit) && *p != quote; nalloc++, p++)
		if (*p == '\\') {
			p++;
			if (is_end_of_line(p, limit))
				return -1; /* Invalid escaping with backslash */
		}

	if (is_end_of_line(p, limit))
		return -1; /* Couldn't find terminating quote character */

	val = dst = xmalloc(nalloc);

	for (; src < p;) {
		if (*src == '\\')
			src++;
		*dst++ = *src++;
	}
	*dst = '\0';

	*valp = val;
	*nextp = p + 1; /* Skip terminating quote character */
	return 0;
}

static int read_linemarker(const char *p,
                           const char *const limit,
                           linemarker_t *lm,
                           const char **nextp)
{
	linemarker_t lm_mem;
	const char *next;
	enum {
		S_X_HASH,     /* Expecting '#' character */
		S_X_LINENUM,  /* Expecting integer which is linenum */
		S_X_FILENAME, /* Expecting string which is filename */
		S_X_FLAG,     /* Expecting integer which is flag */
		S_FAIL,       /* Failed to parse a linemarker */
	} state = S_X_HASH;
	memset(&lm_mem, 0, sizeof(lm_mem));

	for (; state != S_FAIL && !is_end_of_line(p, limit); p = next) {
		for (next = p;
		     !is_end_of_line(next, limit) && is_whitespace(*next);
		     next++) ;
		if (next != p)
			continue;

		switch (state) {

		case S_X_HASH: {
			if (*p != '#')
				state = S_FAIL;
			else
				state = S_X_LINENUM;
			break;
		}

		case S_X_LINENUM: {
			if (parse_ul(p, limit, &lm_mem.linenum, &next) == 0) {
				state = S_X_FILENAME;
				continue;
			}

			state = S_FAIL;
			break;
		}

		case S_X_FILENAME: {
			if (parse_quoted_string(p, limit, '"', &lm_mem.filename, &next) == 0) {
				state = S_X_FLAG;
				continue;
			}

			state = S_FAIL;
			break;
		}

		case S_X_FLAG: {
			unsigned long flag;

			if (parse_ul(p, limit, &flag, &next) == 0) {
				if (sizeof(lm_mem.flags) * 8UL >= flag &&
				    flag >= 1UL)
					lm_mem.flags |= 1UL << (flag - 1UL);
				else
					state = S_FAIL;
				continue;
			}

			state = S_FAIL;
			break;
		}

		case S_FAIL: break;

		}

		next = p + 1;
	}

	if (state == S_X_FLAG) {
		*lm = lm_mem;
		*nextp = next;
		return 0;
	} else {
		if (lm_mem.filename)
			xfree(lm_mem.filename);
		return -1;
	}
}

dyn_buf_t *process_linemarkers(const char *const base, unsigned long size)
{
	dyn_buf_t *buf;
	const char *p = base, *limit = base + size, *next;
	const char *filename;
	unsigned long linenum;
	int skip = 0;

	buf = xmalloc(sizeof(*buf));
	dyn_buf_init(buf);

	for (filename = 0, linenum = 1;
	     p < limit;
	     p = next) {
		linemarker_t lm_mem;
		memset(&lm_mem, 0, sizeof(lm_mem));

		if (read_linemarker(p, limit, &lm_mem, &next) == 0) {
			if (!filename)
				filename = lm_mem.filename;
			else {
				skip = strcmp(lm_mem.filename, filename) != 0;
				xfree(lm_mem.filename);
			}

			if (!skip) {
				if (lm_mem.linenum < linenum) {
					unsigned long nr_stripped = linenum - lm_mem.linenum;
					char *p = buf->pos;

					while (--p >= buf->base)
						if (*p == '\n') {
							*p = ' ';
							if (--nr_stripped == 0UL)
								break;
						}

					if (nr_stripped != 0UL) {
						/* File is malformed */
						if (filename)
							xfree((void *) filename);
						dyn_buf_free(buf);
						xfree(buf);
						return 0;
					}

					linenum = lm_mem.linenum;
				} else {
					for (;
					     linenum < lm_mem.linenum;
					     linenum++)
						dyn_buf_printf(buf, "\n");
				}
			}

			if (next < limit)
				next++;
			continue;
		} else {
			for (next = p;
			     !is_end_of_line(next, limit);
			     next++) ;
			if (next < limit)
				next++;
		}

		if (skip)
			continue;
		dyn_buf_printf(buf, "%.*s", (int) (next - p), p);
		linenum++;
	}

	if (filename)
		xfree((void *) filename);

	return buf;
}

/*
	Trims whitespaces leaving only one instance between adjacent tokens
	(except initial whitespaces in a line).
	Comment is considered one whitespace character.
	Everything is done in-place.
	Returns size of trimmed content.
 */
unsigned long trim_whitespaces(char *const base, unsigned long size)
{
	char *src, *dst, *saved_dst, *const limit = base + size, quote = '\0';
	int seen_token = 0, init_ws = 1;
	enum {
		S_WS,
		S_TOKEN,
		S_IN_QUOTES,
		S_IN_ML_COMMENT,
		S_IN_OL_COMMENT,
	} state = S_WS;

	for (src = saved_dst = dst = base;
	     src < limit;
	     src++) {
		switch (state) {
		case S_WS:
			if (is_whitespace(*src)) {
				if (init_ws)
					*dst++ = *src;
				continue;
			}

			init_ws = 0;

			if (*src == '\n') {
				if (!seen_token)
					dst = saved_dst;
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_ws = 1;
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
			if (is_whitespace(*src)) {
				state = S_WS;
				continue;
			}

			if (*src == '\n') {
				*dst++ = '\n';
				saved_dst = dst;

				seen_token = 0;
				init_ws = 1;
				state = S_WS;
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
				init_ws = 1;
			} else if (*src == '*') {
				char lookahead = (src + 1 < limit) ? *(src + 1) : '\0';

				if (lookahead == '/') {
					src++;
					state = S_WS;
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
				init_ws = 1;
				state = S_WS;
			}

			continue;
		}
	}

	if (!seen_token)
		dst = saved_dst;

	return (unsigned long) (dst - base);
}
