#include "../common.h"

typedef struct {
        const char *const *argv;
        int xrc;
} test_case_t;

static const char *const cmd_sh_argv[] = {
        "/bin/sh",
        "-c",
        "echo \"Hello, World!\"; exit 0",
        NULL
};

static const char *const cmd_true_argv[] = {
        "/bin/true",
        NULL
};

static const char *const cmd_false_argv[] = {
        "/bin/false",
        NULL
};

static const char *const cmd_stdio_h_argv[] = {
        "/usr/include/stdio.h",
        NULL
};

static const char *const cmd_cat_argv[] = {
        "/bin/cat",
        "../util.c",
        NULL
};

static test_case_t cases[] = {
        { cmd_sh_argv,       0 },
        { cmd_true_argv,     0 },
        { cmd_false_argv,   -1 },
        { cmd_stdio_h_argv, -1 },
        { cmd_cat_argv,      0 },
};

static void hex_dump(const char *buf, unsigned long size)
{
        static const char hex_digits[] = "0123456789ABCDEF";
        const unsigned long print_limit = 32UL;
        const unsigned char *p = (const unsigned char *) buf;
        int mark_truncated = 0;

        if (size > print_limit) {
                size = print_limit;
                mark_truncated = 1;
        }

        while (size--) {
                unsigned int c = *p++;
                int u, l;

                u = hex_digits[(c & 0xf0U) >> 4U];
                l = hex_digits[(c & 0x0fU)];

                printf("%c%c", u, l);
        }

        if (mark_truncated)
                printf("<TRUNCATED>");
}

int main(void)
{
        const unsigned long tnum = sizeof(cases) / sizeof(cases[0]);
        test_case_t *t, *const tend = cases + tnum;
        char *buf;
        unsigned long size;
        int result = 0;

        for (t = cases; t < tend; t++) {
                int rc;

                printf("**********\nTesting %s... ", t->argv[0]);

                if (access(t->argv[0], F_OK) != 0) {
                        printf("SKIP\n");
                        continue;
                }

                buf = NULL;
                size = 0UL;
                rc = run_cmd((char **)t->argv, &buf, &size);
                if (rc != t->xrc) {
                        result = 1;
                        printf("FAIL [Expecting return code %d, got %d]\n",
                               t->xrc, rc);
                        if (buf)
                                free(buf);
                        continue;
                }

                if (rc < 0) {
                        printf("XFAIL\n");
                        continue;
                }

                printf("PASS\n");
                if (buf) {
                        printf("Contents:");
                        hex_dump(buf, size);
                        printf("\n");
                        free(buf);
                }
        }

        return result;
}
