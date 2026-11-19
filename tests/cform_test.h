/*
 * Assertions stay live in Release builds (unlike <assert.h> under NDEBUG) so
 * CI keeps checking the layout math. If the suite grows, swap this file for
 * Unity or CMocka; the CF_RUN / CTest wiring is unchanged.
 */
#ifndef CFORM_TEST_H
#define CFORM_TEST_H

#include <stdio.h>

static int cf_failures = 0;

#define CF_CHECK(cond, msg)                                             \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));  \
            cf_failures++;                                              \
        }                                                               \
    } while (0)

/* Layout math is float; exact equality is unreliable. */
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

#define CF_RUN(test_fn)                                                 \
    do {                                                                \
        int cf_before_ = cf_failures;                                    \
        printf("  - %s\n", #test_fn);                                    \
        test_fn();                                                       \
        printf("      %s\n",                                            \
               cf_failures == cf_before_ ? "ok" : "FAILED");            \
    } while (0)

#define CF_REPORT()                                                     \
    (cf_failures == 0                                                   \
        ? (printf("  all assertions passed\n"), 0)                       \
        : (printf("  %d assertion(s) failed\n", cf_failures), 1))

#endif
