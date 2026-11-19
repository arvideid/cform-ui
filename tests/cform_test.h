/*
 * cform_test.h - a minimal, dependency-free assertion harness.
 *
 * Each test executable includes this header, defines `static void` test cases
 * and runs them from `main` with CF_RUN. CF_REPORT returns a non-zero exit
 * code if any assertion failed, which is exactly how CTest decides pass/fail.
 *
 * Assertions are always live (they never compile out with NDEBUG, unlike
 * <assert.h>), so Release test runs still check the layout math.
 *
 * Deliberately small: if the suite grows, this file is the only thing to swap
 * for Unity or CMocka - the CF_RUN/CTest wiring stays identical.
 */
#ifndef CFORM_TEST_H
#define CFORM_TEST_H

#include <stdio.h>

static int cf_failures = 0;

/* Generic condition check. */
#define CF_CHECK(cond, msg)                                             \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));  \
            cf_failures++;                                              \
        }                                                               \
    } while (0)

/* Floating-point equality within a small tolerance (layout uses floats). */
#define CF_NEAR(actual, expected)                                       \
    do {                                                                \
        double cf_a_ = (double)(actual);                                \
        double cf_b_ = (double)(expected);                              \
        double cf_d_ = cf_a_ - cf_b_;                                    \
        if (cf_d_ < 0.0) cf_d_ = -cf_d_;                                 \
        if (cf_d_ > 0.0001) {                                           \
            printf("    FAIL %s:%d: %s == %f, expected %f\n",           \
                   __FILE__, __LINE__, #actual, cf_a_, cf_b_);          \
            cf_failures++;                                              \
        }                                                               \
    } while (0)

/* Run one test case and label the output. */
#define CF_RUN(test_fn)                                                 \
    do {                                                                \
        int cf_before_ = cf_failures;                                    \
        printf("  - %s\n", #test_fn);                                    \
        test_fn();                                                       \
        printf("      %s\n",                                            \
               cf_failures == cf_before_ ? "ok" : "FAILED");            \
    } while (0)

/* Print a summary and return the process exit code (0 = all passed). */
#define CF_REPORT()                                                     \
    (cf_failures == 0                                                   \
        ? (printf("  all assertions passed\n"), 0)                       \
        : (printf("  %d assertion(s) failed\n", cf_failures), 1))

#endif
