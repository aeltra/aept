/* test.h - minimal TAP-style assertions for the aept test suite
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_H_7BF97F
#define TEST_H_7BF97F

#include <stdio.h>
#include <string.h>

static int test_count;
static int test_failed;

static void test_ok(int pass, const char *label)
{
    test_count++;
    printf("%sok %d - %s\n", pass ? "" : "not ", test_count, label);
    if (!pass)
        test_failed++;
}

/* Compare a possibly-NULL string against a possibly-NULL expectation. */
static void test_str_eq(const char *got, const char *want, const char *label)
{
    int pass = (got == NULL && want == NULL) ||
               (got != NULL && want != NULL && strcmp(got, want) == 0);

    test_ok(pass, label);

    if (!pass)
        printf("#   got:  %s\n#   want: %s\n",
               got  ? got  : "(NULL)",
               want ? want : "(NULL)");
}

static void test_int_eq(int got, int want, const char *label)
{
    int pass = (got == want);

    test_ok(pass, label);

    if (!pass)
        printf("#   got:  %d\n#   want: %d\n", got, want);
}

/* Print the TAP plan and return the process exit status. */
static int test_summary(void)
{
    printf("1..%d\n", test_count);
    return test_failed ? 1 : 0;
}

#endif
