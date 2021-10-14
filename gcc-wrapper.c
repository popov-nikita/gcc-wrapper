#include "common.h"

typedef struct {
        char **argv;
        unsigned long argc;
        char *i_file;
        char *o_file;
        int mode;
} comm_info_t;

static int init_arg_data(int argc,
                         const char *const argv[],
                         comm_info_t *ci)
{
        comm_info_t ci_mem;
        const char *const *cur = argv + 1;
        const char *const *const end = argv + argc;

        memset(&ci_mem, 0, sizeof(ci_mem));
        /* Placeholder for executable path. */
        ci_mem.argv = xmalloc(sizeof(char *));
        ci_mem.argc = 1UL;
        ci_mem.argv[0] = NULL;

        for (; cur < end; cur++) {
                const char *sval = *cur;

                if (sval[0] == '-') {
                        if (sval[1] == 'o') {
                                if (ci_mem.o_file != NULL)
                                        goto fail;

                                if (sval[2] == '\0') {
                                        if (++cur >= end)
                                                goto fail;
                                        sval = *cur;
                                } else {
                                        sval += 2;
                                }

                                ci_mem.o_file = xstrdup(sval);
                                continue;
                        }

                        if ((sval[1] == 'c' ||
                             sval[1] == 'S' ||
                             sval[1] == 'E') && sval[2] == '\0') {
                                if (ci_mem.mode != '\0')
                                        goto fail;

                                ci_mem.mode = sval[1];
                                continue;
                        }
                }

                ci_mem.argv = xrealloc(ci_mem.argv,
                                       sizeof(char *) * ++ci_mem.argc);
                ci_mem.argv[ci_mem.argc - 1UL] = xstrdup(sval);
        }

        if (ci_mem.mode == '\0' ||
            ci_mem.o_file == NULL)
                goto fail;

        *ci = ci_mem;
        return 0;
 fail:
        while (ci_mem.argc--)
                xfree(ci_mem.argv[ci_mem.argc]);
        xfree(ci_mem.argv);
        xfree(ci_mem.o_file);

        return -1;
}

static int fini_arg_data(comm_info_t *ci,
                         const char *buf,
                         unsigned long size)
{
        const char *unused;
        linemarker_t lm_mem;
        char **cur, **slot;

        memset(&lm_mem, 0, sizeof(lm_mem));
        if (read_linemarker(buf,
                            buf + size,
                            &lm_mem,
                            &unused) < 0 ||
            lm_mem.filename == NULL)
                return -1;
        (void) unused;

        if (strcmp(lm_mem.filename, "<stdin>") == 0) {
                xfree(lm_mem.filename);
                lm_mem.filename = xstrdup("-");
        }

        /* We need to ensure that input file exists within original ARGV
           verbatim */
        for (slot = NULL, cur = ci->argv + 1;
             cur < ci->argv + ci->argc;
             cur++) {
                if (strcmp(*cur, lm_mem.filename) == 0) {
                        if (slot != NULL) {
                                slot = NULL;
                                break;
                        } else {
                                slot = cur;
                        }
                }
        }

        if (slot == NULL) {
                xfree(lm_mem.filename);
                return -1;
        }

        xfree(*slot);
        for (cur = slot + 1;
             cur < ci->argv + ci->argc;
             cur++) {
                *(cur - 1) = *cur;
        }
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * --ci->argc);
        ci->i_file = lm_mem.filename;

        return 0;
}

static void extend_argv(comm_info_t *ci,
                        ...)
{
        va_list ap;
        const char *s;

        va_start(ap, ci);
        while ((s = va_arg(ap, const char *)) != NULL) {
                ci->argv = xrealloc(ci->argv,
                                    sizeof(char *) * ++ci->argc);
                ci->argv[ci->argc - 1UL] = xstrdup(s);
        }
        va_end(ap);
}

static void doit_i(const comm_info_t *ci,
                   const char *buf,
                   unsigned long size)
{
        print_error_msg(-1,
                        0,
                        "Mode: %c; Input file: %s; Outpuf file: %s; Buf size = %lu",
                        ci->mode,
                        ci->i_file,
                        ci->o_file,
                        size);
}

static int doit(comm_info_t *ci)
{
        char *obuf = NULL;
        unsigned long osize = 0UL;
        child_ctx_t ctx_mem;
        struct stat st_mem;
        int ret_code;
        char mode_buf[3] = { '-', '\0', '\0' };

        extend_argv(ci, "-E", "-o-", NULL);
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * (ci->argc + 1UL));
        ci->argv[ci->argc] = NULL;

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = ci->argv;
        ctx_mem.flags = IO_FROM;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;

        ret_code = run_cmd(&ctx_mem) < 0 || obuf == NULL;

        xfree(ci->argv[--ci->argc]);
        xfree(ci->argv[--ci->argc]);
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * ci->argc);
        if (ret_code)
                return -1;

        if (fini_arg_data(ci,
                          obuf,
                          osize) < 0) {
                /* Couldn't happen for correct invocations of GCC */
                xfree(obuf);
                return -1;
        }

        memset(&st_mem, 0, sizeof(st_mem));
        if (stat(ci->i_file, &st_mem) == 0 &&
            S_ISREG(st_mem.st_mode)) {
                doit_i(ci, obuf, osize);
        }

        xfree(ci->i_file); ci->i_file = NULL;

        mode_buf[1] = ci->mode;
        extend_argv(ci,
                    "-fpreprocessed",
                    mode_buf,
                    "-o",
                    ci->o_file,
                    "-",
                    NULL);
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * (ci->argc + 1UL));
        ci->argv[ci->argc] = NULL;

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = ci->argv;
        ctx_mem.flags = IO_TO;
        ctx_mem.ibuf = obuf;
        ctx_mem.isize = osize;

        ret_code = run_cmd(&ctx_mem) < 0;

        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * ci->argc);
        xfree(obuf);

        return ret_code ? -1 : 0;
}

int main(int argc, char *argv[])
{
        const char *cc;
        char *located_cc;
        comm_info_t ci_mem;
        int ret_code;

        if ((cc = getenv("REAL_CC")) == NULL)
                cc = "gcc";

        if ((located_cc = locate_file(cc)) == NULL) {
                print_error_msg(-1,
                                0,
                                "Failed to locate %s",
                                cc);
                return ESRCH;
        }

        if (getenv("X_NO_I_FILES") != NULL ||
            init_arg_data(argc,
                          (const char *const *) argv,
                          &ci_mem) < 0) {
                child_ctx_t ctx_mem;

                memset(&ctx_mem, 0, sizeof(ctx_mem));
                ctx_mem.argv = xmalloc((unsigned long) (argc + 1) *
                                       sizeof(char *));
                ctx_mem.argv[0] = located_cc;
                memcpy(&ctx_mem.argv[1],
                       &argv[1],
                       (unsigned long) argc * sizeof(char *));
                ctx_mem.flags = IO_NONE;

                ret_code = run_cmd(&ctx_mem);

                xfree(ctx_mem.argv); xfree(located_cc);

                return (ret_code == 0) ? 0 : ECHILD;
        }

        ci_mem.argv[0] = xstrdup(located_cc);

        ret_code = doit(&ci_mem);

        while (ci_mem.argc--)
                xfree(ci_mem.argv[ci_mem.argc]);
        xfree(ci_mem.argv);
        xfree(ci_mem.o_file);
        xfree(located_cc);

        return (ret_code == 0) ? 0 : EINVAL;
}
