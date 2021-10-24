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
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* util.c */

void *xmalloc(unsigned long size);
void *xrealloc(void *ptr, unsigned long size);
void xfree(void *ptr);
char *xstrdup(const char *s);


long safe_read(int fd, char *buf, unsigned long size);
long safe_write(int fd, const char *buf, unsigned long size);
int create_file_mapping(const char *path,
                        void **basep,
                        unsigned long *sizep);
void delete_file_mapping(void *base,
                         unsigned long size);
char *locate_file(const char *name);


typedef struct {
        char *base, *pos;
        unsigned long capacity;
        union {
                char internal_buf[sizeof(void *) << 4UL];
                unsigned long align_ul;
                void *align_ptr;
        };
} dbuf_t;

void dbuf_init(dbuf_t *dbuf);
char *dbuf_alloc(dbuf_t *dbuf, unsigned long size);
int dbuf_putc(dbuf_t *dbuf, int c);
int dbuf_printf(dbuf_t *dbuf, const char *fmt, ...);
void dbuf_free(dbuf_t *dbuf);


void print_error_msg(int fd,
                     int error_kind,
                     const char *fmt,
                     ...);


typedef struct {
        char **argv; /* The vector of standard arguments to child process.
                        argv[0] should be real path to binary (see locate_file).
                        Must end with NULL element as
                        required by execve OS handler. */
        enum {
                IO_NONE = 0, /* Do not alter child's stdin & stdout */
                IO_TO   = 1, /* Feed child with @ibuf content */
                IO_FROM = 2, /* Extract child's output to @obuf_p */
                IO_BOTH = IO_FROM | IO_TO, /* Full control of the child */
        } flags;
        char **obuf_p; /* The pointer to allocated buffer of data
                          written by child to stdout */
        unsigned long *osize_p; /* It's size */
        char *ibuf; /* Buffer of data
                       the child process may read from its STDIN. */
        unsigned long isize; /* It's size */
} child_ctx_t;

int run_cmd(const child_ctx_t *ctx);


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
dbuf_t *process_linemarkers(const char *const data,
                            unsigned long size);
dbuf_t *adjust_style(const char *const data,
                     unsigned long size);

#endif
