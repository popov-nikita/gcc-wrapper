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

        if (ci_mem.mode == '\0' || ci_mem.mode == 'E' ||
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

static char *mangle_filename(const char *i_file,
                             const char *o_file)
{
        char *res;
        unsigned long reslen;
        const char *dot, *saved;
        int dot_found;

        reslen = strlen(o_file);
        dot = o_file + reslen;
        dot_found = 0;
        while (dot > o_file) {
                dot--;
                if (*dot == '.') {
                        dot_found = 1;
                        break;
                } else if (*dot == '/') {
                        break;
                }
        }

        if (dot_found) {
                reslen = (unsigned long) (dot - o_file);
        }
        res = xmalloc(reslen + sizeof(".pp"));
        memcpy(res, o_file, reslen);
        memcpy(res + reslen, ".pp", sizeof(".pp")); /*  Includes '\0' */
        reslen += sizeof(".pp") - 1UL;

        /* Find proper suffix in input file path */
        dot = saved = i_file + strlen(i_file);
        dot_found = 0;
        while (dot > i_file) {
                dot--;
                if (*dot == '.') {
                        dot_found = 1;
                        break;
                } else if (*dot == '/') {
                        break;
                }
        }

        if (dot_found) {
                unsigned long sfxlen;

                sfxlen = (unsigned long) (saved - dot);
                res = xrealloc(res, reslen + sfxlen + 1UL);
                memcpy(res + reslen,
                       dot,
                       sfxlen + 1UL);
        } else {
                /* Add fake suffix */
                res = xrealloc(res, reslen + sizeof(".unk"));
                memcpy(res + reslen,
                       ".unk",
                       sizeof(".unk"));
        }

        return res;
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

static void doit_i(const char *i_file,
                   const char *o_file,
                   const char *const data,
                   unsigned long size)
{
        dbuf_t *buffer;
        long buffer_sz;
        char *mangled_nm;
        int fd;

        /* Something goes wrong on processing linemarkers?
           Skip. */
        if ((buffer = process_linemarkers(data, size)) == NULL)
                return;

        /* Nothing to write */
        if ((buffer_sz = buffer->pos - buffer->base) <= 0L) {
                dbuf_free(buffer); xfree(buffer);
                return;
        }

        mangled_nm = mangle_filename(i_file, o_file);

        if ((fd = open(mangled_nm,
                       O_CREAT | O_WRONLY | O_EXCL,
                       0644)) >= 0) {
                if (safe_write(fd,
                               buffer->base,
                               (unsigned long) buffer_sz) != buffer_sz) {
                        print_error_msg(-1, 0,
                                        "GCC-WRAPPER: Failed to write %s",
                                        mangled_nm);
                        unlink(mangled_nm);
                }
                close(fd);
        }

        xfree(mangled_nm);
        dbuf_free(buffer); xfree(buffer);
}

static int doit(comm_info_t *ci,
                const char *cc,
                const char *cpp)
{
        struct ext_entry {
                char *ext;
                unsigned long extlen;
                char *optval;
        };
        static const struct ext_entry ext_mapping[] = {
                { ".c",   sizeof(".c") - 1UL,   "cpp-output" },
                { ".i",   sizeof(".i") - 1UL,   "cpp-output" },
                { ".s",   sizeof(".s") - 1UL,   "assembler" },
                { ".S",   sizeof(".S") - 1UL,   "assembler" },
                { ".sx",  sizeof(".sx") - 1UL,  "assembler" },
                { ".cc",  sizeof(".cc") - 1UL,  "c++-cpp-output" },
                { ".ii",  sizeof(".ii") - 1UL,  "c++-cpp-output" },
                { ".cp",  sizeof(".cp") - 1UL,  "c++-cpp-output" },
                { ".cxx", sizeof(".cxx") - 1UL, "c++-cpp-output" },
                { ".cpp", sizeof(".cpp") - 1UL, "c++-cpp-output" },
                { ".CPP", sizeof(".CPP") - 1UL, "c++-cpp-output" },
                { ".c++", sizeof(".c++") - 1UL, "c++-cpp-output" },
                { ".C",   sizeof(".C") - 1UL,   "c++-cpp-output" },
                { NULL,   0UL,                  NULL },
        };
        const struct ext_entry *entry = ext_mapping;
        char *obuf = NULL;
        unsigned long osize = 0UL;
        child_ctx_t ctx_mem;
        int is_success;
        char mode_buf[3] = { '-', '\0', '\0' };
        unsigned long pathlen;

        ci->argv[0] = xstrdup(cpp);
        extend_argv(ci, "-o-", NULL);
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * (ci->argc + 1UL));
        ci->argv[ci->argc] = NULL;

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = ci->argv;
        ctx_mem.flags = IO_FROM;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;

        is_success = run_cmd(&ctx_mem) == 0 && obuf != NULL;

        xfree(ci->argv[0]); ci->argv[0] = NULL;
        xfree(ci->argv[--ci->argc]);
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * ci->argc);

        if (!is_success)
                return -1;

        if (fini_arg_data(ci,
                          obuf,
                          osize) < 0) {
                /* Couldn't happen for correct invocations of GCC */
                xfree(obuf);
                return -1;
        }

        pathlen = strlen(ci->i_file);
        while (entry->ext != NULL) {
                if (entry->extlen <= pathlen &&
                    strcmp(ci->i_file + (pathlen - entry->extlen),
                           entry->ext) == 0)
                        break;
                entry++;
        }

        mode_buf[1] = ci->mode;
        ci->argv[0] = xstrdup(cc);
        if (entry->ext != NULL) {
                extend_argv(ci,
                            "-x",
                            entry->optval,
                            NULL);
        }
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

        is_success = run_cmd(&ctx_mem) == 0;

        xfree(ci->argv[0]); ci->argv[0] = NULL;
        ci->argv = xrealloc(ci->argv,
                            sizeof(char *) * ci->argc);

        if (is_success) {
                struct stat ist_mem, ost_mem;

                /* Proceed only if both input and output
                   paths designate real file */
                memset(&ist_mem, 0, sizeof(ist_mem));
                memset(&ost_mem, 0, sizeof(ost_mem));
                if (stat(ci->i_file, &ist_mem) == 0 &&
                    S_ISREG(ist_mem.st_mode) &&
                    stat(ci->o_file, &ost_mem) == 0 &&
                    S_ISREG(ost_mem.st_mode)) {
                        doit_i(ci->i_file,
                               ci->o_file,
                               obuf,
                               osize);
                }
        }

        xfree(ci->i_file); ci->i_file = NULL;
        xfree(obuf);

        return is_success ? 0 : -1;
}

int main(int argc, char *argv[])
{
        const char *cc, *cpp;
        char *located_cc, *located_cpp;
        comm_info_t ci_mem;
        int ret_code;

        if ((cc = getenv("REAL_CC")) == NULL)
                cc = "gcc";

        if ((cpp = getenv("REAL_CPP")) == NULL)
                cpp = "cpp";

        if ((located_cc = locate_file(cc)) == NULL) {
                print_error_msg(-1,
                                0,
                                "Failed to locate %s",
                                cc);
                return ESRCH;
        }

        if ((located_cpp = locate_file(cpp)) == NULL) {
                print_error_msg(-1,
                                0,
                                "Failed to locate %s",
                                cpp);
                xfree(located_cc);
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

                xfree(ctx_mem.argv);
                xfree(located_cc);
                xfree(located_cpp);

                return (ret_code == 0) ? 0 : ECHILD;
        }

        ret_code = doit(&ci_mem,
                        located_cc,
                        located_cpp);

        while (ci_mem.argc--)
                xfree(ci_mem.argv[ci_mem.argc]);
        xfree(ci_mem.argv);
        xfree(ci_mem.o_file);
        xfree(located_cc);
        xfree(located_cpp);

        return (ret_code == 0) ? 0 : EINVAL;
}
