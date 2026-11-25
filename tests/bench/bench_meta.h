#ifndef CF_BENCH_META_H
#define CF_BENCH_META_H

#include <stdio.h>

/*
 * Every rig opens with a `meta` block so a number in a review has the
 * compiler, dialect and workload attached to it, and prints each measurement
 * as a `result` line so a script can grep the log without parsing prose.
 * Both formats are one flat `key=value` shape on stdout; a table in
 * `docs/bench.md` is the human view a reviewer types from the run.
 */

static void bench_meta(const char *rig, const char *workload) {
    char number[64];

    printf("meta format=1\n");
    printf("meta rig=%s workload=%s\n", rig, workload);
    snprintf(number, sizeof number, "%ld", (long)__STDC_VERSION__);
    printf("meta c_std=%s\n", number);
    snprintf(number, sizeof number, "%u", (unsigned)(8u * sizeof(void *)));
    printf("meta bits=%s\n", number);
#ifdef __VERSION__
    printf("meta compiler=%s\n", __VERSION__);
#endif
}

/* One measured row. Four digits carry a millisecond figure the reviewer
 * recognises from `docs/bench.md`, and a byte count stays exact under the
 * same format because callers pass a whole number of bytes. */
static void bench_result(const char *key, double value, const char *unit) {
    printf("result %s=%.4f %s\n", key, value, unit);
}

#endif
