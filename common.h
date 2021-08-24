#ifndef COMMON_H
#define COMMON_H

/*
	EPERM		 1
	ENOENT		 2
	ESRCH		 3
	EINTR		 4
	EIO		 5
	ENXIO		 6
	E2BIG		 7
	ENOEXEC		 8
	EBADF		 9
	ECHILD		10
	EAGAIN		11
	ENOMEM		12
	EACCES		13
	EFAULT		14
	ENOTBLK		15
	EBUSY		16
	EEXIST		17
	EXDEV		18
	ENODEV		19
	ENOTDIR		20
	EISDIR		21
	EINVAL		22
	ENFILE		23
	EMFILE		24
	ENOTTY		25
	ETXTBSY		26
	EFBIG		27
	ENOSPC		28
	ESPIPE		29
	EROFS		30
	EMLINK		31
	EPIPE		32
	EDOM		33
	ERANGE		34
 */
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* util.c */

void *xmalloc(unsigned long size);
void *xrealloc(void *ptr, unsigned long size);
void  xfree(void *ptr);
char *xstrdup(const char *s);

char *locate_file(const char *name);

int load_file(const char *path, void **basep, unsigned long *sizep);
void unload_file(void *base, unsigned long size);

typedef struct {
        char *base, *pos;
        unsigned long capacity;
        char _mem[sizeof(void *) << 4UL];
} dbuf_t;

void  dbuf_init(dbuf_t *dbuf);
char *dbuf_alloc(dbuf_t *dbuf, unsigned long size);
int   dbuf_printf(dbuf_t *dbuf, const char *fmt, ...);
void  dbuf_free(dbuf_t *dbuf);

/* parse.c */

typedef struct {
        unsigned long linenum;
        char *filename;
        unsigned long info;
} linemarker_t;

int is_eol(const char *chp, const char *const limit);
int is_ws(char ch);
int read_linemarker(const char *chp,
                    const char *const limit,
                    linemarker_t *lm,
                    const char **nxtp);

#endif
