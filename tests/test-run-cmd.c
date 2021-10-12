#include "../common.h"

static char **dup_argv(const char *const *argv,
                       unsigned long argc)
{
        char *located_exe_file, **copy;
        unsigned long i;

        if ((located_exe_file = locate_file(argv[0UL])) == NULL)
                return NULL;

        copy = xmalloc(sizeof(*copy) * (argc + 1UL));
        copy[0UL] = located_exe_file;
        for (i = 1UL; i < argc; i++)
                copy[i] = xstrdup(argv[i]);
        copy[i] = NULL;

        return copy;
}

static void free_argv(char **argv,
                      unsigned long argc)
{
        unsigned long i;

        for (i = 0UL; i < argc; i++)
                xfree(argv[i]);
        xfree(argv);
}

static int test_1(void)
{
        static const char *const argv[] = {
                "cat",
                "-",
                NULL
        };
        static const unsigned long argc = (sizeof(argv) /
                                           sizeof(argv[0UL])) - 1UL;
        static const char letters[] = "^Abc&+abc091";
        char **copy;
        child_ctx_t ctx_mem;
        char *obuf = NULL;
        unsigned long i, osize = 0UL;
        int rc = 1;

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);
                goto out;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        ctx_mem.flags = IO_BOTH;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;
        ctx_mem.ibuf = (char *) xmalloc(1UL << 20UL);
        ctx_mem.isize = 1UL << 20UL;

        for (i = 0UL;
             i < ctx_mem.isize - 1UL;
             i++) {
                ctx_mem.ibuf[i] = letters[i % (sizeof(letters) - 1UL)];
        }
        ctx_mem.ibuf[i] = '\n';

        if (run_cmd(&ctx_mem) < 0) {
                printf("FAIL [run_cmd failed]\n");
                goto out_free_ctx;
        }

        if (obuf == NULL) {
                printf("FAIL [empty obuf]\n");
                goto out_free_ctx;
        }

        if (osize != ctx_mem.isize ||
            memcmp(obuf, ctx_mem.ibuf, ctx_mem.isize) != 0) {
                printf("FAIL [contents mismatch]\n");
                goto out_free_obuf;
        }

        printf("PASS\n"
               "     IN BUF = %.16s...[%lu bytes]\n"
               "    OUT BUF = %.16s...[%lu bytes]\n",
               ctx_mem.ibuf, ctx_mem.isize,
               obuf, osize);

        rc = 0;

out_free_obuf:
        xfree(obuf);
out_free_ctx:
        xfree(ctx_mem.ibuf);
        free_argv(copy, argc);
out:
        return rc;
}


int main(void)
{
        return test_1();
}
