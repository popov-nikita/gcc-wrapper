#include "common.h"

void log_failure(int fd, int err_num, const char *fmt, ...)
{
        va_list ap;
        char _mem[4096], *p;
        unsigned long nr_avail;
        long rv;
        int saved_errno;

        saved_errno = errno;

        if (fd < 0)
                fd = STDERR_FILENO;
        if (err_num < 0)
                err_num = saved_errno;

        p = _mem;
        nr_avail = sizeof(_mem);

        if (err_num != 0) {
                const char *err_dsc;

                errno = 0;
                err_dsc = strerror(err_num);
                if (err_dsc == NULL || errno != 0)
                        err_dsc = "Unknown error";

                rv = snprintf(p,
                              nr_avail,
                              "Failed with \"%s\"\n",
                              err_dsc);
                if (rv <= 0L || (unsigned long) rv >= nr_avail)
                        goto out;
                p += rv;
                nr_avail -= (unsigned long) rv;
        }

        va_start(ap, fmt);
        rv = vsnprintf(p,
                       nr_avail,
                       fmt,
                       ap);
        va_end(ap);
        if (rv <= 0L || (unsigned long) rv >= nr_avail)
                goto out;
        p += rv;
        nr_avail -= (unsigned long) rv;

        if (p[-1L] != '\n') {
                *p++ = '\n';
                nr_avail--;
        }

        p = _mem;
        nr_avail = sizeof(_mem) - nr_avail;
        do {
                rv = write(fd, p, nr_avail);
                if (rv <= 0L || (unsigned long) rv > nr_avail)
                        break;

                p += rv;
                nr_avail -= (unsigned long) rv;
        } while (nr_avail > 0UL);

out:
        errno = saved_errno;
}

void *xmalloc(unsigned long size)
{
        void *ret;

        ret = malloc(size);

        if (ret == NULL) {
                log_failure(-1,
                            0,
                            "Failed to allocate %lu bytes of memory",
                            size);
                _exit(ENOMEM);
        }

        return ret;
}

void *xrealloc(void *ptr, unsigned long size)
{
        void *ret;

        ret = realloc(ptr, size);

        if (ret == NULL) {
                log_failure(-1,
                            0,
                            "Failed to re-allocate %lu bytes of memory",
                            size);
                xfree(ptr);
                _exit(ENOMEM);
        }

        return ret;
}

void  xfree(void *ptr)
{
        if (ptr)
                free(ptr);
}

char *xstrdup(const char *s)
{
        char *d;
        unsigned long size;

        if (s == NULL)
                return 0;

        size = strlen(s) + 1UL;
        d = xmalloc(size);
        memcpy(d, s, size);

        return d;
}

/* Using $PATH environment variable locates real path of target binary */
char *locate_file(const char *name)
{
        unsigned long name_len;
        char *resolved_name = NULL;
        const char *path_env, *s, *e;
        int is_resolved, seen_zero_seg = 0;

        if (name == NULL || *name == '\0')
                return resolved_name;

        /* Names starting with "/", "./", "../" or being one of
           ".", ".." should be handled separately */
        is_resolved = (name[0] == '/' ||
                       (name[0] == '.' && (name[1] == '\0' ||
                                           name[1] == '/'  ||
                                           (name[1] == '.'  && (name[2] == '\0' ||
                                                                name[2] == '/')))));
        if (is_resolved) {
                if (access(name, X_OK) == 0)
                        resolved_name = xstrdup(name);
                return resolved_name;
        }

        if ((path_env = getenv("PATH")) == NULL)
                return resolved_name;

        name_len = strlen(name);

        for (s = path_env; *s != '\0'; s = e + !!*e) {
                const char *seg;
                unsigned long seg_size;
                char *buf, *last;

                for (e = s; *e != '\0' && *e != ':'; e++) ;

                if (s == e) {
                        if (seen_zero_seg)
                                continue;
                        seg = ".";
                        seg_size = 1UL;
                        seen_zero_seg = 1;
                } else {
                        seg = s;
                        seg_size = (unsigned long) (e - s);
                }

                last = buf = xmalloc(seg_size + 1UL + name_len + 1UL);
                memcpy(last, seg, seg_size),  last += seg_size;
                *last = '/',                  last += 1;
                memcpy(last, name, name_len), last += name_len;
                *last = '\0';

                if (access(buf, X_OK) == 0) {
                        return resolved_name = buf;
                }

                xfree(buf);
        }

        if (!seen_zero_seg && (*path_env == '\0' ||
                               *(s - 1)  == ':')) {
                char *buf;

                seen_zero_seg = 1;
                buf = xmalloc(1UL + 1UL + name_len + 1UL);
                buf[0] = '.';
                buf[1] = '/';
                strcpy(buf + 2, name);

                if (access(buf, X_OK) == 0)
                        resolved_name = buf;
                else
                        xfree(buf);
        }

        return resolved_name;
}

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

        memset(&st_mem, 0, sizeof(st_mem));
        if (fstat(fd, &st_mem) < 0 ||
            st_mem.st_size <= 0L)
                goto out_close;

        size = (unsigned long) st_mem.st_size;
        base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0L);
        if (base == MAP_FAILED)
                goto out_close;

        *basep = base;
        *sizep = size;
        rc = 0;

out_close:
        close(fd);
out:
        return rc;
}

/* Removes file mapping */
void unload_file(void *base, unsigned long size)
{
        munmap(base, size);
}

void  dbuf_init(dbuf_t *dbuf)
{
        if (dbuf == NULL)
                return;
        dbuf->pos = dbuf->base = dbuf->_mem;
        dbuf->capacity = sizeof(dbuf->_mem) / sizeof(dbuf->_mem[0]);
}

char *dbuf_alloc(dbuf_t *dbuf, unsigned long size)
{
        unsigned long old_size;

        if (dbuf == NULL)
                return NULL;

        old_size = (unsigned long) (dbuf->pos - dbuf->base);
        size += old_size;

        /* Overflow? */
        if (size < old_size)
                return NULL;

        /* Too large? */
        if (size > ULONG_MAX / 2UL)
                return NULL;

        if (size > dbuf->capacity) {
                while (size > (dbuf->capacity *= 2UL)) ;

                if (dbuf->base == dbuf->_mem) {
                        dbuf->base = xmalloc(dbuf->capacity);
                        memcpy(dbuf->base, dbuf->_mem, old_size);
                } else {
                        dbuf->base = xrealloc(dbuf->base, dbuf->capacity);
                }

                dbuf->pos = dbuf->base + old_size;
        }

        return dbuf->pos;
}

int   dbuf_printf(dbuf_t *dbuf, const char *fmt, ...)
{
        va_list ap;
        char _mem[1024], *ptr;
        int ret_val;
        unsigned long size;

        va_start(ap, fmt);
        ret_val = vsnprintf(_mem, sizeof(_mem), fmt, ap);
        va_end(ap);

        if (ret_val < 0)
                return -1;

        size = (unsigned long) ret_val + 1UL;

        /* This allocation is accounted in @dbuf itself.
           It will go away when we free the whole dynamic buffer. */
        if ((ptr = dbuf_alloc(dbuf, size)) == NULL)
                return -1;

        if (size <= sizeof(_mem)) {
                memcpy(ptr, _mem, size);
        } else {
                va_start(ap, fmt);
                ret_val = vsnprintf(ptr, size, fmt, ap);
                va_end(ap);

                if (ret_val < 0)
                        return -1;

                /* Sanity check */
                if ((unsigned long) ret_val + 1UL != size)
                        return -1;
        }

        dbuf->pos += ret_val;

        return ret_val;
}

void  dbuf_free(dbuf_t *dbuf)
{
        if (dbuf == NULL)
                return;

        if (dbuf->base != dbuf->_mem) {
                xfree(dbuf->base);
                dbuf->base = dbuf->_mem;
                dbuf->capacity = sizeof(dbuf->_mem) / sizeof(dbuf->_mem[0]);
        }

        dbuf->pos = dbuf->base;
}

static void run_child(char *argv[],
                      int dat_fd,
                      int log_fd)
{
        extern char **environ;

        /* dup2 duplicates file descriptor
           with CLOEXEC bit cleared for the copy. */
        if (dup2(dat_fd, STDOUT_FILENO) < 0) {
                log_failure(log_fd,
                            -1,
                            "In %s\nAt call \"dup2\"",
                            __func__);
                goto fail;
        }

        /* CLOEXEC bit ensures our parent will see
           the EOF in log pipe. */
        fcntl(dat_fd, F_SETFD, FD_CLOEXEC);
        fcntl(log_fd, F_SETFD, FD_CLOEXEC);

        execve(argv[0], argv, environ);

        log_failure(log_fd,
                    -1,
                    "In %s\nAt call \"execve\"",
                    __func__);

fail:
        _exit(-1);
}

int run_cmd(char *argv[],
            char **bufp,
            unsigned long *sizep)
{
        int log_fd[2] = { -1, -1 };
        int dat_fd[2] = { -1, -1 };
        int status = 0, exit_code;
        long pid;
        char _mem[4096], *p;
        unsigned long nr_avail;
        long rv;
        char *buf = NULL, *tmp;
        unsigned long size = 0UL;

        /*
          We create two pipes here:
          + One pipe replaces stdout of the child process;
          + Other pipe signals whether system error has occured.
            during process creation.
          Log pipe is going away upon successful call to execve()
          which is ensured with CLOEXEC file descriptor bit.
         */
        if (pipe(dat_fd) < 0) {
                log_failure(-1,
                            -1,
                            "In %s\nAt call \"pipe(dat_fd)\"",
                            __func__);
                return -1;
        }

        if (pipe(log_fd) < 0) {
                log_failure(-1,
                            -1,
                            "In %s\nAt call \"pipe(log_fd)\"",
                            __func__);
                close(dat_fd[0]);
                close(dat_fd[1]);
                return -1;
        }

        if ((pid = fork()) < 0L) {
                log_failure(-1,
                            -1,
                            "In %s\nAt call \"fork\"",
                            __func__);
                close(dat_fd[0]);
                close(dat_fd[1]);
                close(log_fd[0]);
                close(log_fd[1]);
                return -1;
        }

        if (pid == 0L) {
                close(dat_fd[0]);
                close(log_fd[0]);
                run_child(argv, dat_fd[1], log_fd[1]);
                /* Unreachable */
                for (;;) ;
        }

        close(dat_fd[1]);
        close(log_fd[1]);

        /* We avoid printing error messages to console in the child process
           so no message interleaving takes place. */
        p = _mem;
        nr_avail = sizeof(_mem);
        do {
                rv = read(log_fd[0], p, nr_avail);

                if (rv < 0L || (unsigned long) rv > nr_avail) {
                        int ignored;

                        log_failure(-1,
                                    -1,
                                    "In %s\nAt call \"read(log_fd)\"",
                                    __func__);

                        kill(pid, SIGKILL);
                        waitpid(pid, &ignored, 0);

                        close(dat_fd[0]);
                        close(log_fd[0]);
                        return -1;
                }

                if (rv == 0L)
                        break;

                p += rv;
                nr_avail -= (unsigned long) rv;
        } while (nr_avail > 0UL);

        close(log_fd[0]);

        if (p != _mem) {
                int ignored;

                kill(pid, SIGKILL);
                waitpid(pid, &ignored, 0);

                close(dat_fd[0]);

                /* Print error message produced by our child */
                log_failure(-1,
                            0,
                            "%.*s",
                            (int) (p - _mem),
                            _mem);
                return -1;
        }

        do {
                unsigned long this_size;

                p = _mem;
                nr_avail = sizeof(_mem);
                do {
                        rv = read(dat_fd[0], p, nr_avail);

                        if (rv < 0L || (unsigned long) rv > nr_avail) {
                                int ignored;

                                log_failure(-1,
                                            -1,
                                            "In %s\nAt call \"read(dat_fd)\"",
                                            __func__);

                                kill(pid, SIGKILL);
                                waitpid(pid, &ignored, 0);

                                close(dat_fd[0]);

                                if (buf)
                                        free(buf);
                                return -1;
                        }

                        if (rv == 0L)
                                break;

                        p += rv;
                        nr_avail -= (unsigned long) rv;
                } while (nr_avail > 0UL);

                if ((this_size = sizeof(_mem) - nr_avail) == 0UL)
                        break;

                tmp = realloc(buf, size + this_size);
                if (tmp == NULL) {
                        int ignored;

                        log_failure(-1,
                                    -1,
                                    "In %s\nAt call \"realloc\"",
                                    __func__);

                        kill(pid, SIGKILL);
                        waitpid(pid, &ignored, 0);

                        close(dat_fd[0]);

                        if (buf)
                                free(buf);
                        return -1;
                }
                buf = tmp;

                memcpy(buf + size, _mem, this_size);
                size += this_size;
                /* If nr_avail == 0 then
                   there may be more data.
                   Anyway, at least one read is required
                   to find it out. */
        } while (nr_avail == 0UL);

        close(dat_fd[0]);

        if ((rv = waitpid(pid, &status, 0)) < 0L) {
                log_failure(-1,
                            -1,
                            "In %s\nAt call \"waitpid\"",
                            __func__);

                kill(pid, SIGKILL);

                if (buf)
                        free(buf);
                return -1;
        }

        if (rv != pid || !(WIFEXITED(status) || WIFSIGNALED(status))) {
                log_failure(-1,
                            EFAULT,
                            "In %s\nAt call \"waitpid\"",
                            __func__);

                kill(pid, SIGKILL);

                if (buf)
                        free(buf);
                return -1;
        }

        if (WIFSIGNALED(status)) {
                log_failure(-1,
                            0,
                            "Child %ld is killed by a signal\n",
                            pid);

                if (buf)
                        free(buf);
                return -1;
	}

        if ((exit_code = WEXITSTATUS(status)) != 0) {
                log_failure(-1,
                            0,
                            "Child %ld has returned %d\n",
                            pid,
                            exit_code);

                if (buf)
                        free(buf);
                return -1;
        }

        *bufp = buf;
        *sizep = size;
        return 0;
}
