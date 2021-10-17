#include "common.h"

/******************************
 * Augmented memory allocator *
 ******************************/

void *xmalloc(unsigned long size)
{
        void *ret;

        ret = malloc(size);

        if (ret == NULL) {
                print_error_msg(-1,
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
                print_error_msg(-1,
                                0,
                                "Failed to re-allocate %lu bytes of memory",
                                size);
                xfree(ptr);
                _exit(ENOMEM);
        }

        return ret;
}

void xfree(void *ptr)
{
        if (ptr)
                free(ptr);
}

char *xstrdup(const char *s)
{
        char *d;
        unsigned long size;

        if (s == NULL) {
                print_error_msg(-1,
                                0,
                                "Invalid usage: "
                                "NULL passed to strdup in the argument");
                _exit(EINVAL);
        }

        size = strlen(s) + 1UL;
        d = xmalloc(size);
        memcpy(d, s, size);

        return d;
}

/********************************
 * File system helper functions *
 ********************************/

long safe_read(int fd, char *buf, unsigned long size)
{
        long rv = 0L, total = 0L;
        /*
          Certain read semantics are assumed:
          + Positive value which is returned by read()
          designates amount of valid bytes in the given buffer.
          + Beyond that, buffer may contain junk.
          Calling application must be ready for this.
          + -1 as retvalue doesn't lift the validity of previous data.
          It simply designates an error. It can be a temporar error
          which won't repeat on next read:
          for instance, EAGAIN for NONBLOCK files
          or permanent which is repeatable:
          for instance, EPERM for lack of permissions.
          So this point allows us to ignore possible final error
          after sequence of successful reads: either the error
          goes away or it will be picked on next read.
          + -1 doesn't alters file position.
          This is true for the Linux implementation.
        */

        if (fd < 0 ||
            buf == NULL ||
            size == 0UL) {
                errno = EINVAL;
                return -1L;
        }

        while (size > 0UL) {
                rv = read(fd, buf, size);

                if (rv <= 0L || (unsigned long) rv > size ||
                    total > LONG_MAX - rv)
                        break;

                buf += rv;
                size -= (unsigned long) rv;
                total += rv;
        }

        if (total > 0L || rv == 0L) {
                errno = 0;
                return total;
        } else if (rv > 0L) {
                errno = ERANGE;
        }

        return -1L;
}

long safe_write(int fd, const char *buf, unsigned long size)
{
        long rv = 0L, total = 0L;

        /*
          We generally follow the same strategy as for safe_read
          except one point:
          + If the very first write() attempt results in 0L returned,
          treat it as failure.
        */

        if (fd < 0 ||
            buf == NULL ||
            size == 0UL) {
                errno = EINVAL;
                return -1L;
        }

        while (size > 0UL) {
                rv = write(fd, buf, size);

                if (rv <= 0L || (unsigned long) rv > size ||
                    total > LONG_MAX - rv)
                        break;

                buf += rv;
                size -= (unsigned long) rv;
                total += rv;
        }

        if (total > 0L) {
                errno = 0;
                return total;
        } else if (rv == 0L) {
                errno = ENOSPC;
        } else if (rv > 0L) {
                errno = ERANGE;
        }

        return -1L;
}

/* Creates private RW mapping of the file specified with @path.
   Return -1 on failure (for any reason), 0 - on success.
   Provides mapping data (base address + size) via pointers @basep, @sizep. */
int create_file_mapping(const char *path,
                        void **basep,
                        unsigned long *sizep)
{
        int fd, rc = -1;
        void *base;
        unsigned long size;
        struct stat st_mem;

        if (path == NULL || *path == '\0' ||
            basep == NULL ||
            sizep == NULL)
                goto out;

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

void delete_file_mapping(void *base,
                         unsigned long size)
{
        if (base != NULL && size > 0UL)
                munmap(base, size);
}

/* Using $PATH environment variable locates real path of target binary */
char *locate_file(const char *name)
{
        unsigned long name_len;
        char *resolved_name = NULL;
        const char *path_env, *s, *e;
        int seen_zero_seg = 0;

        if (name == NULL || *name == '\0')
                return resolved_name;

        /* Names starting with "/", "./", "../" or being one of
           ".", ".." should be handled separately */
        if (name[0] == '/' ||
            (name[0] == '.' && (name[1] == '\0' ||
                                name[1] == '/'  ||
                                (name[1] == '.'  && (name[2] == '\0' ||
                                                     name[2] == '/'))))) {
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

/****************************************
 * Dynamic (self-expandable) buffer API *
 ****************************************/

void dbuf_init(dbuf_t *dbuf)
{
        if (dbuf == NULL)
                return;
        dbuf->pos = dbuf->base = dbuf->internal_buf;
        dbuf->capacity = sizeof(dbuf->internal_buf) / sizeof(dbuf->internal_buf[0]);
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

                if (dbuf->base == dbuf->internal_buf) {
                        dbuf->base = xmalloc(dbuf->capacity);
                        memcpy(dbuf->base, dbuf->internal_buf, old_size);
                } else {
                        dbuf->base = xrealloc(dbuf->base, dbuf->capacity);
                }

                dbuf->pos = dbuf->base + old_size;
        }

        return dbuf->pos;
}

int dbuf_putc(dbuf_t *dbuf, int c)
{
        char *ptr;

        if (c < 0 || c >= 256)
                return -1;

        if ((ptr = dbuf_alloc(dbuf, 1UL)) == NULL)
                return -1;

        *((unsigned char *) ptr) = (unsigned char) c;

        dbuf->pos += 1;

        return 0;
}

int dbuf_printf(dbuf_t *dbuf, const char *fmt, ...)
{
        va_list ap;
        char scratch_mem[1024], *ptr;
        int ret_val;
        unsigned long size;

        va_start(ap, fmt);
        ret_val = vsnprintf(scratch_mem, sizeof(scratch_mem), fmt, ap);
        va_end(ap);

        if (ret_val < 0)
                return -1;

        size = (unsigned long) ret_val + 1UL;

        /* This allocation is accounted in @dbuf itself.
           It will go away when we free the whole dynamic buffer. */
        if ((ptr = dbuf_alloc(dbuf, size)) == NULL)
                return -1;

        if (size <= sizeof(scratch_mem)) {
                memcpy(ptr, scratch_mem, size);
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

void dbuf_free(dbuf_t *dbuf)
{
        if (dbuf == NULL)
                return;

        if (dbuf->base != dbuf->internal_buf) {
                xfree(dbuf->base);
                dbuf->base = dbuf->internal_buf;
                dbuf->capacity = sizeof(dbuf->internal_buf) / sizeof(dbuf->internal_buf[0]);
        }

        dbuf->pos = dbuf->base;
}

/**************************
 * General purpose logger *
 **************************/

void print_error_msg(int fd,
                     int error_kind,
                     const char *fmt,
                     ...)
{
        va_list ap;
        char scratch_mem[4096], *tail = scratch_mem;
        unsigned long tail_room = sizeof(scratch_mem);
        int rv, old_err_num = errno;

        if (fd < 0)
                fd = STDERR_FILENO;
        if (error_kind < 0)
                error_kind = old_err_num;

        if (error_kind != 0) {
                const char *error_dsc;

                errno = 0;
                error_dsc = strerror(error_kind);
                if (error_dsc == NULL || errno != 0)
                        error_dsc = "Unknown error";

                rv = snprintf(tail,
                              tail_room,
                              "Failed with \"%s\"\n",
                              error_dsc);
                if (rv <= 0 || (unsigned long) rv >= tail_room)
                        goto out;
                tail += rv, tail_room -= (unsigned long) rv;
        }

        va_start(ap, fmt);
        rv = vsnprintf(tail, tail_room, fmt, ap);
        va_end(ap);
        if (rv <= 0 || (unsigned long) rv >= tail_room)
                goto out;
        tail += rv, tail_room -= (unsigned long) rv;

        if (tail[-1] != '\n')
                *tail++ = '\n', tail_room--;

        safe_write(fd, scratch_mem, sizeof(scratch_mem) - tail_room);

out:
        errno = old_err_num;
}

/*******************************************
 * Enhanced (and convenient) child spawner *
 *******************************************/

static void run_child(char **argv,
                      int log_fd,
                      int in_fd,
                      int out_fd)
{
        extern char **environ;

        /* CLOEXEC bit ensures our parent will see
           the EOF in log pipe. */
        fcntl(log_fd, F_SETFD, FD_CLOEXEC);

        if (in_fd >= 0) {
                /* dup2 duplicates file descriptor
                   with CLOEXEC bit cleared for the copy. */
                if (dup2(in_fd, STDIN_FILENO) < 0) {
                        print_error_msg(log_fd,
                                        -1,
                                        "In %s\n"
                                        "At \"dup2(in_fd, STDIN_FILENO)\"",
                                        __func__);
                        goto fail;
                }

                
                fcntl(in_fd, F_SETFD, FD_CLOEXEC);
        }

        if (out_fd >= 0) {
                if (dup2(out_fd, STDOUT_FILENO) < 0) {
                        print_error_msg(log_fd,
                                        -1,
                                        "In %s\n"
                                        "At \"dup2(out_fd, STDOUT_FILENO)\"",
                                        __func__);
                        goto fail;
                }

                fcntl(out_fd, F_SETFD, FD_CLOEXEC);
        }

        execve(argv[0], argv, environ);

        print_error_msg(log_fd,
                        -1,
                        "In %s\n"
                        "At \"execve\"",
                        __func__);

fail:
        _exit(-1);
}

static int communicate_child(int *wfd,
                             int *rfd,
                             char **wbuf,
                             char **rbuf,
                             unsigned long *wsize,
                             unsigned long *rsize)
{
        struct pollfd pbuf[2U];
        unsigned int pcount = 0U;
        int need_close, revents;
        long n;

        memset(pbuf, 0, sizeof(pbuf));

        if (*wfd >= 0) {
                pbuf[0U].fd = *wfd;
                pbuf[0U].events = POLLOUT;
                pcount++;
        }

        if (*rfd >= 0) {
                pbuf[pcount].fd = *rfd;
                pbuf[pcount].events = POLLIN;
                pcount++;
        }

        if (poll(pbuf, pcount, -1) < 0) {
                print_error_msg(-1,
                                -1,
                                "In %s\n"
                                "At \"poll\"",
                                __func__);
                return -1;
        }

        if (*wfd >= 0 && (revents = pbuf[0U].revents) != 0) {
                need_close = 0;

                if ((revents & POLLERR) != 0) {
                        need_close = 1;
                } else if ((revents & POLLOUT) != 0) {
                        while (*wsize > 0UL && (n = safe_write(*wfd,
                                                               *wbuf,
                                                               *wsize)) > 0L) {
                                *wbuf += n;
                                *wsize -= (unsigned long) n;
                        }

                        if (*wsize == 0UL || errno == EPIPE) {
                                need_close = 1;
                        } else if (errno != EAGAIN) {
                                print_error_msg(-1,
                                                -1,
                                                "In %s\n"
                                                "At \"write(*wfd)\"",
                                                __func__);
                                return -1;
                        }
                } else {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "Spurious value from \"poll\": %d",
                                        __func__,
                                        revents);
                        return -1;
                }

                if (need_close) {
                        close(*wfd); *wfd = -1;
                }
        }

        if (*rfd >= 0 && (revents = pbuf[pcount - 1U].revents) != 0) {
                need_close = 0;

                if ((revents & POLLIN) != 0) {
                        char scratch_mem[4096], *tmp;
                        unsigned long old_rsize;

                        do {
                                n = safe_read(*rfd,
                                              scratch_mem,
                                              sizeof(scratch_mem));

                                if (n < 0L && errno == EAGAIN)
                                        break;

                                if (n == 0L) {
                                        need_close = 1; break;
                                }

                                if (n < 0L) {
                                        print_error_msg(-1,
                                                        -1,
                                                        "In %s\n"
                                                        "At \"read(*rfd)\"",
                                                        __func__);
                                        return -1;
                                }

                                old_rsize = *rsize;
                                *rsize += (unsigned long) n;
                                if (*rsize <= old_rsize) {
                                        print_error_msg(-1,
                                                        ERANGE,
                                                        "In %s\n"
                                                        "At \"read(*rfd)\"",
                                                        __func__);
                                        return -1;
                                }

                                if ((tmp = realloc(*rbuf, *rsize)) == NULL) {
                                        print_error_msg(-1,
                                                        -1,
                                                        "In %s\n"
                                                        "At \"realloc\"",
                                                        __func__);
                                        return -1;
                                }
                                *rbuf = tmp;
                                memcpy(*rbuf + old_rsize, scratch_mem,
                                       (unsigned long) n);

                                /* If n == sizeof(scratch_mem) then
                                   there may be more data.
                                   Anyway, at least one read is required
                                   to find it out. */
                        } while ((unsigned long) n == sizeof(scratch_mem));
                } else {
                        /* Probably, POLLHUP without any data */
                        need_close = 1;
                }

                if (need_close) {
                        close(*rfd); *rfd = -1;
                }
        }

        return 0;
}

int run_cmd(const child_ctx_t *ctx)
{
        int all_fds[6] = { -1, -1, -1, -1, -1, -1 };
        int *const log_fds = all_fds;
        int *const in_fds  = all_fds + 2;
        int *const out_fds = all_fds + 4;
        pid_t child_id = -1, waitee_id;
        int ret_code, status;

        char *obuf = NULL; /* Data received from the child. */
        unsigned long osize = 0UL; /* The amount of data produced by our child. */
        char *ibuf = ctx->ibuf;
        unsigned long isize = ctx->isize;
        char scratch_mem[4096];
        long n;

        static int initialized = 0;

        if ((ctx->flags & ~IO_BOTH) != 0) {
                print_error_msg(-1,
                                0,
                                "In %s\n"
                                "Invalid flags %d",
                                __func__,
                                ctx->flags);
                goto fail;
        }

        if ((ctx->flags & IO_TO) != 0 &&
            (ctx->ibuf == NULL) != (ctx->isize == 0UL)) {
                print_error_msg(-1,
                                0,
                                "In %s\n"
                                "Parameters (IO_TO) contradict each other",
                                __func__);
                goto fail;
        }

        if ((ctx->flags & IO_FROM) != 0 &&
            (ctx->obuf_p == NULL || ctx->osize_p == NULL)) {
                print_error_msg(-1,
                                0,
                                "In %s\n"
                                "Parameters (IO_FROM) are invalid",
                                __func__);
                goto fail;
        }

        if (!initialized) {
                struct sigaction sa_mem, *const sa = &sa_mem;

                memset(sa, 0, sizeof(*sa));
                sigemptyset(&sa->sa_mask);
                sa->sa_handler = SIG_IGN;
                sa->sa_flags = 0;

                if (sigaction(SIGPIPE, sa, NULL) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"sigaction(SIGPIPE, SIG_IGN)\"",
                                        __func__);
                        goto fail;
                }

                initialized = 1;
        }

        /*
          We create up to three pipes here:
          + One pipe replaces stdout of the child process;
          + Other pipe is used in place of stdin of the child process;
          + Log pipe signals whether system error has occured
          during process creation.
          It is used for synchronization since we cannot rely on
          exit codes: in general case, external program is capable of
          exiting with arbitrary codes.
          So it would be impossible to distinguish if something went wrong
          because of our miscalculations.
          Log pipe is going away upon successful call to execve()
          which is ensured with CLOEXEC file descriptor bit.
         */

        if (pipe(log_fds) < 0) {
                print_error_msg(-1,
                                -1,
                                "In %s\n"
                                "At \"pipe(log_fds)\"",
                                __func__);
                goto fail;
        }

        if (log_fds[0] < 0 ||
            log_fds[1] < 0) {
                print_error_msg(-1,
                                EBADF,
                                "In %s\n"
                                "At \"pipe(log_fds)\"",
                                __func__);
                goto fail;
        }

        if ((ctx->flags & IO_TO) != 0) {
                if (pipe(in_fds) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"pipe(in_fds)\"",
                                        __func__);
                        goto fail;
                }

                if (in_fds[0] < 0 ||
                    in_fds[1] < 0) {
                        print_error_msg(-1,
                                        EBADF,
                                        "In %s\n"
                                        "At \"pipe(in_fds)\"",
                                        __func__);
                        goto fail;
                }

                /* We have to set our ends of pipe to non-blocking mode
                   to avoid dead-lock. */
                if ((status = fcntl(in_fds[1], F_GETFL)) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"fcntl(in_fds[1], F_GETFL)\"",
                                        __func__);
                        goto fail;
                }

                if (fcntl(in_fds[1], F_SETFL, status | O_NONBLOCK) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"fcntl(in_fds[1], F_SETFL)\"",
                                        __func__);
                        goto fail;
                }
        }

        if ((ctx->flags & IO_FROM) != 0) {
                if (pipe(out_fds) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"pipe(out_fds)\"",
                                        __func__);
                        goto fail;
                }

                if (out_fds[0] < 0 ||
                    out_fds[1] < 0) {
                        print_error_msg(-1,
                                        EBADF,
                                        "In %s\n"
                                        "At \"pipe(out_fds)\"",
                                        __func__);
                        goto fail;
                }

                if ((status = fcntl(out_fds[0], F_GETFL)) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"fcntl(out_fds[0], F_GETFL)\"",
                                        __func__);
                        goto fail;
                }

                if (fcntl(out_fds[0], F_SETFL, status | O_NONBLOCK) < 0) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"fcntl(out_fds[0], F_SETFL)\"",
                                        __func__);
                        goto fail;
                }
        }

        if ((child_id = fork()) < 0) {
                print_error_msg(-1,
                                -1,
                                "In %s\nAt \"fork\"",
                                __func__);
                goto fail;
        }

        if (child_id == 0) {
                close(log_fds[0]);
                log_fds[0] = -1;

                if (in_fds[1] >= 0) {
                        close(in_fds[1]);
                        in_fds[1] = -1;
                }

                if (out_fds[0] >= 0) {
                        close(out_fds[0]);
                        out_fds[0] = -1;
                }

                run_child(ctx->argv,
                          log_fds[1],
                          in_fds[0],
                          out_fds[1]);

                /* Unreachable */
                for (;;) ;
        }

        close(log_fds[1]);
        log_fds[1] = -1;

        if (in_fds[0] >= 0) {
                close(in_fds[0]);
                in_fds[0] = -1;
        }

        if (out_fds[1] >= 0) {
                close(out_fds[1]);
                out_fds[1] = -1;
        }

        /* We avoid printing error messages to console in the child process
           so no message interleaving takes place. */
        if ((n = safe_read(log_fds[0],
                           scratch_mem,
                           sizeof(scratch_mem))) != 0L) {
                if (n < 0L) {
                        print_error_msg(-1,
                                        -1,
                                        "In %s\n"
                                        "At \"safe_read(log_fds[0])\"",
                                        __func__);
                } else {
                        /* Print error message produced by our child */
                        print_error_msg(-1,
                                        0,
                                        "%.*s",
                                        (int) n,
                                        scratch_mem);
                }

                goto fail;
        }

        close(log_fds[0]);
        log_fds[0] = -1;

        while (in_fds[1] >= 0 || out_fds[0] >= 0) {
                if (communicate_child(&in_fds[1], &out_fds[0],
                                      &ibuf, &obuf,
                                      &isize, &osize) < 0)
                        goto fail;
        }

        if ((waitee_id = waitpid(child_id, &status, 0)) < 0) {
                print_error_msg(-1,
                                -1,
                                "In %s\n"
                                "At \"waitpid\"",
                                __func__);
                goto fail;
        }

        if (waitee_id != child_id || !(WIFEXITED(status) || WIFSIGNALED(status))) {
                print_error_msg(-1,
                                EFAULT,
                                "In %s\n"
                                "At \"waitpid\"",
                                __func__);
                goto fail;
        }

        child_id = -1;

        if (WIFSIGNALED(status)) {
                print_error_msg(-1,
                                0,
                                "Child %d is killed by a signal",
                                waitee_id);
                goto fail;
        }

        if ((ret_code = WEXITSTATUS(status)) != 0) {
                print_error_msg(-1,
                                0,
                                "Child %d has returned %d\n",
                                waitee_id,
                                ret_code);
                goto fail;
        }

        if ((ctx->flags & IO_FROM) != 0) {
                *ctx->obuf_p = obuf;
                *ctx->osize_p = osize;
        }

        return 0;

fail:
        if (child_id > 0) {
                int ignored;

                kill(child_id, SIGKILL);
                waitpid(child_id, &ignored, 0);
                child_id = -1;
        }

        for (n = 0L;
             n < (long) (sizeof(all_fds) / sizeof(all_fds[0]));
             n++) {
                if (all_fds[n] >= 0) {
                        close(all_fds[n]);
                        all_fds[n] = -1;
                }
        }

        if (obuf != NULL) {
                free(obuf);
                obuf = NULL; osize = 0UL;
        }

        return -1;
}
