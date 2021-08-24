#include "../common.h"

static const char *const inputs_[] = {
        "# 1 \"\\\\\\\"\"",
        "# 1 \"<built-in>\"",
        "# 1 \"<command-line>\"",
        "# 31 \"<command-line>\"",
        "# 1 \"/usr/include/stdc-predef.h\" 1 3 4",
        "# 32 \"<command-line>\" 2",
        "# 1 \"\\\\\\\"\"",
        "# 1 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 1 3 4",
        "# 149 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 149 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 216 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 328 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 426 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 437 \"/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h\" 3 4",
        "# 2 \"\\\\\\\"\" 2",
        "# 3 \"\\\\\\\"\"",
        "# 18446744073709551616 \"dummy.c\"",
        "# 42 \"source-file.c\" 0",
        "# 42 \"source-file.c\" 65",
        "# 42 \"\\\"source-file.c 1     ",
        "# -1 \"dummy.c\"",
};

static const int x_retvals_[] = {
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        -1,
        -1,
        -1,
        -1,
        -1,
};

static const unsigned long x_linenums_[] = {
        1UL,
        1UL,
        1UL,
        31UL,
        1UL,
        32UL,
        1UL,
        1UL,
        149UL,
        149UL,
        216UL,
        328UL,
        426UL,
        437UL,
        2UL,
        3UL,
        0UL,
        0UL,
        0UL,
        0UL,
        0UL,
};

static const char *const x_filenames_[] = {
        "\\\"",
        "<built-in>",
        "<command-line>",
        "<command-line>",
        "/usr/include/stdc-predef.h",
        "<command-line>",
        "\\\"",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "/usr/lib/gcc/x86_64-linux-gnu/7/include/stddef.h",
        "\\\"",
        "\\\"",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
};

static const unsigned long x_infos_[] = {
        0x0UL,
        0x0UL,
        0x0UL,
        0x0UL,
        0xdUL,
        0x2UL,
        0x0UL,
        0xdUL,
        0xcUL,
        0xcUL,
        0xcUL,
        0xcUL,
        0xcUL,
        0xcUL,
        0x2UL,
        0x0UL,
        0UL,
        0UL,
        0UL,
        0UL,
        0UL,
};

/* Ensure we didn't miss anything in test data */
static int precheck(void)
{
        const unsigned long size_1 = sizeof(inputs_) / sizeof(inputs_[0]);
        const unsigned long size_2 = sizeof(x_retvals_) / sizeof(x_retvals_[0]);
        const unsigned long size_3 = sizeof(x_linenums_) / sizeof(x_linenums_[0]);
        const unsigned long size_4 = sizeof(x_filenames_) / sizeof(x_filenames_[0]);
        const unsigned long size_5 = sizeof(x_infos_) / sizeof(x_infos_[0]);

        return (size_1 == size_2 &&
                size_2 == size_3 &&
                size_3 == size_4 &&
                size_4 == size_5);
}

int main(void)
{
        int result = 0;
        unsigned long i;

        if (!precheck()) {
                printf("ERROR: Inconsistency in the test data\n");
                return 1;
        }

        for (i = 0UL;
             i < sizeof(inputs_) / sizeof(inputs_[0]);
             i++) {
                const char *input, *x_filename;
                int x_retval, retval;
                unsigned long x_linenum, x_info;
                const char *limit, *nxt;
                linemarker_t lm_mem;

                input      = inputs_[i];
                x_retval   = x_retvals_[i];
                x_linenum  = x_linenums_[i];
                x_filename = x_filenames_[i];
                x_info     = x_infos_[i];

                nxt   = NULL;
                limit = input + strlen(input);

                memset(&lm_mem, 0, sizeof(lm_mem));

                retval = read_linemarker(input, limit, &lm_mem, &nxt);

                if (retval != x_retval) {
                        printf("ERROR: Wrong retval for the following test:\n"
                               "    %s\n"
                               "Expected: %d\n"
                               "  Actual: %d\n",
                               input, x_retval, retval);
                        result = 1;
                        goto next;
                }

                if (retval < 0) {
                        printf("XFAIL: %s\n",
                               input);
                        goto next;
                }

                if (lm_mem.linenum != x_linenum) {
                        printf("ERROR: Wrong linenum for the following test:\n"
                               "    %s\n"
                               "Expected: %lu\n"
                               "  Actual: %lu\n",
                               input, x_linenum, lm_mem.linenum);
                        result = 1;
                        goto next;
                }

                if (strcmp(lm_mem.filename, x_filename) != 0) {
                        printf("ERROR: Wrong filename for the following test:\n"
                               "    %s\n"
                               "Expected: %s\n"
                               "  Actual: %s\n",
                               input, x_filename, lm_mem.filename);
                        result = 1;
                        goto next;
                }

                if (lm_mem.info != x_info) {
                        printf("ERROR: Wrong info for the following test:\n"
                               "    %s\n"
                               "Expected: 0x%lx\n"
                               "  Actual: 0x%lx\n",
                               input, x_info, lm_mem.info);
                        result = 1;
                        goto next;
                }

                if (nxt != limit) {
                        printf("ERROR: Wrong next pointer for the following test:\n"
                               "    %s\n"
                               "Expected: %p\n"
                               "  Actual: %p\n",
                               input, limit, nxt);
                        result = 1;
                        goto next;
                }

                printf("PASS: %s\n"
                       "          (linenum, filename, info)\n"
                       "Expected: (%lu, %s, 0x%lx)\n"
                       "  Actual: (%lu, %s, 0x%lx)\n",
                       input,
                       x_linenum, x_filename, x_info,
                       lm_mem.linenum, lm_mem.filename, lm_mem.info);

        next:
                if (retval == 0)
                        xfree(lm_mem.filename);
        }

        if (result)
                printf("Some tests were failed. See logs above\n");
        else
                printf("All tests have been passed\n");

        return result;
}
