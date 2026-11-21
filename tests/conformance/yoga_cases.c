/* The Yoga fixtures need the widest tree the corpus format can name. */
#define MAX_NODES 17u

#include <string.h>

#include "layout_case.h"

/*
 * Where the ported corpus stands today. The floor only moves up: a fixture
 * that regresses fails the build, and a fixture that starts matching is the
 * floor's to be raised beside. docs/conformance.md publishes the number, the
 * suites it is made of and the behaviour each miss needs.
 */
#define CONFORMANCE_FLOOR 453u

/*
 * Written by tools/gentest from Yoga's generated fixtures: every case the
 * reference engine measured, with the box it measured. Each row leads with the
 * upstream suite it came from, in file order, so a run of rows sharing that
 * first word is one fixture file and one line of the table reads back to it.
 */
static const layout_case cases[] = {
#include "yoga_cases_data.h"
};

static const uint32_t case_count = (uint32_t)(sizeof cases / sizeof cases[0]);

static size_t suite_length(const char *name) {
    const char *space = strchr(name, ' ');

    return space == NULL ? strlen(name) : (size_t)(space - name);
}

static bool same_suite(const layout_case *a, const layout_case *b) {
    const size_t length = suite_length(a->name);

    return length == suite_length(b->name) && strncmp(a->name, b->name, length) == 0;
}

static void report_suite(uint32_t first, uint32_t last, uint32_t matched) {
    printf("  %-20.*s %4u of %4u\n", (int)suite_length(cases[first].name), cases[first].name,
           matched, last - first);
}

int main(void) {
    uint32_t matched = 0u;
    uint32_t first = 0u;
    uint32_t suite_matched = 0u;

    printf("yoga conformance\n");
    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the corpus needs scratch");

    for (uint32_t i = 0u; i < case_count; ++i) {
        if (i > first && !same_suite(&cases[first], &cases[i])) {
            report_suite(first, i, suite_matched);
            matched += suite_matched;
            first = i;
            suite_matched = 0u;
        }

        suite_matched += layout_case_run(&cases[i]) ? 1u : 0u;
    }

    report_suite(first, case_count, suite_matched);
    printf("  %u of %u fixtures match\n", matched + suite_matched, case_count);

    CF_CHECK(matched + suite_matched >= CONFORMANCE_FLOOR, "the corpus stands at least this high");

    cf_arena_free(&scratch);

    return CF_REPORT();
}
