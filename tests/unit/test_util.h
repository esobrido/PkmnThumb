/*
 * Minimal assertion helpers for host unit tests.
 *
 * Each test file is a standalone program with its own main(); scripts/test.sh
 * compiles each tests/unit/test_NAME.c against the module sources and runs it. Exit
 * status is 0 only when all checks passed, so a failure fails the build.
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>

static int t_checks;
static int t_fails;

#define CHECK(cond)                                                          \
    do {                                                                     \
        t_checks++;                                                          \
        if (!(cond)) {                                                       \
            t_fails++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

#define CHECK_EQ(got, want)                                                  \
    do {                                                                     \
        long _g = (long)(got), _w = (long)(want);                            \
        t_checks++;                                                          \
        if (_g != _w) {                                                      \
            t_fails++;                                                       \
            printf("  FAIL %s:%d: %s == %s (got %ld, want %ld)\n", __FILE__,  \
                   __LINE__, #got, #want, _g, _w);                           \
        }                                                                    \
    } while (0)

/* Print the summary and return the process exit status. */
static int test_report(const char *name) {
    printf("%s: %d checks, %d failed\n", name, t_checks, t_fails);
    return t_fails ? 1 : 0;
}

#endif
