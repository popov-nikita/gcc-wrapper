#ifndef		COMMON_H
#define		COMMON_H	1

#include	<stdarg.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/mman.h>
#include	<sys/wait.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

static int is_whitespace(char c)
{
	return (c == ' '  || c == '\f' ||
	        c == '\r' || c == '\t' || c == '\v');
}

static int is_end_of_line(const char *p, const char *const limit)
{
	return p >= limit || *p == '\n';
}

enum exit_code {
	E_SUCCESS   = 0,
	E_SRCH      = 1,
	E_MAL_FILE  = 2,
	E_NOMEM     = 100,
	E_IO        = 101,
	E_PROC      = 102,
};

int load_file(const char *path, void **basep, unsigned long *sizep);
void unload_file(void *base, unsigned long size);

char *get_basename(const char *path);
char *locate_bin_file(const char *bname);

void *xmalloc(unsigned long size);
void *xrealloc(void *ptr, unsigned long size);
char *xstrdup(const char *s);
void xfree(void *ptr);

typedef struct {
	char *base, *pos;
	unsigned long capacity;
	char _mem[sizeof(void *) << 4UL];
} dyn_buf_t;

void dyn_buf_init(dyn_buf_t *buf);
char *dyn_buf_reserve(dyn_buf_t *buf, unsigned long to_reserve);
int dyn_buf_printf(dyn_buf_t *buf, const char *fmt, ...);
void dyn_buf_free(dyn_buf_t *buf);

dyn_buf_t *process_linemarkers(const char *const base, unsigned long size);
unsigned long trim_whitespaces(char *const base, unsigned long size);

extern const char *prog_basename;

#endif
