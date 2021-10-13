#include "../common.h"

static char **dup_argv(const char *const *argv,
                       unsigned long argc)
{
        char *located_exe_file, **copy;
        unsigned long i;

        if (argv[0UL][0] == '/') {
                located_exe_file = xstrdup(argv[0UL]);
        } else {
                located_exe_file = locate_file(argv[0UL]);
        }

        if (located_exe_file == NULL)
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

static void print_test_header(const char *const *argv,
                              unsigned long argc)
{
        char scratch_mem[1024];
        unsigned long total_size = 0UL, i;

        for (i = 0UL; i < argc; i++) {
                unsigned long this_size;

                this_size = strlen(argv[i]);

                if (this_size >= sizeof(scratch_mem) - total_size)
                        break;

                memcpy(&scratch_mem[total_size],
                       argv[i],
                       this_size);
                total_size += this_size + 1UL;
                scratch_mem[total_size - 1UL] = ' ';
        }

        if (total_size > 0UL) {
                scratch_mem[total_size - 1UL] = '\0';
                printf("TEST: %s\n", scratch_mem);
        }
}

static void dump_buf(unsigned char *buf,
                     unsigned long size)
{
        static char hex_digits[16] = "0123456789abcdef";
        int add_ellipsis, is_printable = 1;
        unsigned long i;

        if ((add_ellipsis = size > 16UL) != 0)
                size = 16UL;

        for (i = 0UL; i < size; i++) {
                if (buf[i] < 0x20U ||
                    buf[i] > 0x7eU) {
                        is_printable = 0;
                        break;
                }
        }

        if (is_printable) {
                printf("[plain] ");
                if (size == 0UL) {
                        printf("<zero-length string>");
                } else {
                        printf("%.*s", (int) size, (char *) buf);
                }
        } else {
                unsigned int lower, upper;

                printf("[hex]   ");
                for (i = 0UL; i < size; i++) {
                        lower = ((unsigned int) buf[i]) & 0xfU;
                        upper = ((unsigned int) buf[i]) >> 4U;
                        putchar(hex_digits[upper]);
                        putchar(hex_digits[lower]);
                }
        }

        if (add_ellipsis) {
                putchar('.'); putchar('.'); putchar('.');
        }
}

static int test_with_cat(void)
{
        static const char *const argv[] = {
                "cat",
                "-"
        };
        static const unsigned long argc = sizeof(argv) / sizeof(argv[0UL]);

        char **copy;
        child_ctx_t ctx_mem;
        char *obuf = NULL; /* Data from child. */
        unsigned long i, osize = 0UL; /* Size of such data. */
        const unsigned long isize = 1UL << 20UL;
        int rc;

        print_test_header(argv, argc);

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);

                return 1;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        ctx_mem.flags = IO_BOTH;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;
        ctx_mem.ibuf = (char *) xmalloc(isize);
        ctx_mem.isize = isize;

        for (i = 0UL;
             i < isize;
             ctx_mem.ibuf[i++] = 'X') ;

        if (run_cmd(&ctx_mem) < 0 || obuf == NULL) {
                printf("FAIL [API run_cmd failed]\n");

                xfree(ctx_mem.ibuf);
                free_argv(copy, argc);
                return 1;
        }

        rc = (osize != isize || memcmp(obuf, ctx_mem.ibuf, isize) != 0);

        if (rc) {
                printf("FAIL [Contents of buffers mismatch]\n");
                printf("    IBUF: ");
                dump_buf((unsigned char *) ctx_mem.ibuf, isize);
                printf(" [%lu bytes]\n", isize);

                printf("    OBUF: ");
                dump_buf((unsigned char *) obuf, osize);
                printf(" [%lu bytes]\n", osize);
        } else {
                printf("PASS\n");
        }

        xfree(obuf);
        xfree(ctx_mem.ibuf);
        free_argv(copy, argc);
        return rc;
}

static int test_with_sh(void)
{
        static const char *const argv[] = {
                "sh",
                "-c",
                "echo -n \"Hello, world\"; exit 0"
        };
        static const unsigned long argc = sizeof(argv) / sizeof(argv[0UL]);

        static const char x_obuf[] = "Hello, world";
        static const unsigned long x_osize = sizeof(x_obuf) - 1UL;

        char **copy;
        child_ctx_t ctx_mem;
        char *obuf = NULL; /* Data from child. */
        unsigned long osize = 0UL; /* Size of such data. */
        int rc;

        print_test_header(argv, argc);

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);

                return 1;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        ctx_mem.flags = IO_BOTH;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;
        ctx_mem.ibuf = NULL; /* Child will see EOF on stdin early. */
        ctx_mem.isize = 0UL;

        if (run_cmd(&ctx_mem) < 0 || obuf == NULL) {
                printf("FAIL [API run_cmd failed]\n");

                free_argv(copy, argc);
                return 1;
        }

        rc = (osize != x_osize || memcmp(obuf, x_obuf, x_osize) != 0);

        if (rc) {
                printf("FAIL [Contents of buffers mismatch]\n");
                printf("    expected: ");
                dump_buf((unsigned char *) x_obuf, x_osize);
                printf(" [%lu bytes]\n", x_osize);

                printf("      actual: ");
                dump_buf((unsigned char *) obuf, osize);
                printf(" [%lu bytes]\n", osize);
        } else {
                printf("PASS\n");
        }

        xfree(obuf);
        free_argv(copy, argc);
        return rc;
}

static int test_with_stdio_h(void)
{
        static const char *const argv[] = {
                "/usr/include/stdio.h"
        };
        static const unsigned long argc = sizeof(argv) / sizeof(argv[0UL]);

        char **copy;
        child_ctx_t ctx_mem;

        print_test_header(argv, argc);

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);

                return 1;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        /* We are to see failure reported via log pipe. */
        ctx_mem.flags = IO_NONE;
        ctx_mem.obuf_p = NULL;
        ctx_mem.osize_p = NULL;
        ctx_mem.ibuf = NULL;
        ctx_mem.isize = 0UL;

        if (run_cmd(&ctx_mem) == 0) {
                printf("FAIL [API run_cmd succeeded]\n");

                free_argv(copy, argc);
                return 1;
        }

        printf("PASS\n");

        free_argv(copy, argc);
        return 0;
}

static int test_with_true(void)
{
        static const char *const argv[] = {
                "true"
        };
        static const unsigned long argc = sizeof(argv) / sizeof(argv[0UL]);

        char **copy;
        child_ctx_t ctx_mem;
        char *obuf = NULL; /* Data from child. */
        unsigned long i, osize = 0UL; /* Size of such data. */
        const unsigned long isize = 1UL << 20UL;

        print_test_header(argv, argc);

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);

                return 1;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        ctx_mem.flags = IO_BOTH;
        ctx_mem.obuf_p = &obuf;
        ctx_mem.osize_p = &osize;
        ctx_mem.ibuf = (char *) xmalloc(isize);
        ctx_mem.isize = isize;

        for (i = 0UL;
             i < isize;
             ctx_mem.ibuf[i++] = 'X') ;

        if (run_cmd(&ctx_mem) < 0) {
                printf("FAIL [API run_cmd failed]\n");

                xfree(ctx_mem.ibuf);
                free_argv(copy, argc);
                return 1;
        }

        if (obuf != NULL) {
                printf("FAIL [Unexpected obuf]\n");
                printf("    OBUF: ");
                dump_buf((unsigned char *) obuf, osize);
                printf(" [%lu bytes]\n", osize);

                xfree(obuf);
                xfree(ctx_mem.ibuf);
                free_argv(copy, argc);
                return 1;
        }

        printf("PASS\n");

        xfree(ctx_mem.ibuf);
        free_argv(copy, argc);
        return 0;
}

static int test_with_false(void)
{
        static const char *const argv[] = {
                "false"
        };
        static const unsigned long argc = sizeof(argv) / sizeof(argv[0UL]);

        char **copy;
        child_ctx_t ctx_mem;

        print_test_header(argv, argc);

        if ((copy = dup_argv(argv, argc)) == NULL) {
                printf("FAIL [Failed to locate \"%s\"]\n",
                       argv[0]);

                return 1;
        }

        memset(&ctx_mem, 0, sizeof(ctx_mem));
        ctx_mem.argv = copy;
        /* "false" utility is sure to fail */
        ctx_mem.flags = IO_NONE;
        ctx_mem.obuf_p = NULL;
        ctx_mem.osize_p = NULL;
        ctx_mem.ibuf = NULL;
        ctx_mem.isize = 0UL;

        if (run_cmd(&ctx_mem) == 0) {
                printf("FAIL [API run_cmd succeeded]\n");

                free_argv(copy, argc);
                return 1;
        }

        printf("PASS\n");

        free_argv(copy, argc);
        return 0;
}

int main(void)
{
        /* Add new tests here */
        static int (*const tests[])(void) = {
                test_with_cat,
                test_with_sh,
                test_with_stdio_h,
                test_with_true,
                test_with_false
        };
        static const unsigned long nr_tests = sizeof(tests) / sizeof(tests[0]);
        int result = 0;
        unsigned long i;

        for (i = 0UL; i < nr_tests; i++) {
                if ((tests[i])() != 0)
                        result = 1;
        }

        return result;
}
