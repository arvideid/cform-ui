/* The Yoga fixtures need the widest tree the corpus format can name. */
#define MAX_NODES 17u

#include <string.h>

#include "layout_case.h"

/*
 * The fixtures the corpus is known not to match, named rather than counted.
 * Each is off by the whole-pixel grade Chrome snapped its answer key to, not a
 * layout rule; docs/conformance.md publishes the score they cost. Naming them
 * instead of asserting a floor is what stops a fixture that starts passing from
 * cancelling one that silently breaks. Closing a difference means deleting its
 * name here and raising the published score in the same commit.
 */
static const char *const known_differences[] = {
    "gap column_gap_wrap_align_stretch",
    "rounding rounding_flex_basis_flex_grow_row_prime_number_width",
    "rounding rounding_flex_basis_flex_grow_row_width_of_100",
    "rounding rounding_flex_basis_overrides_main_size",
    "rounding rounding_fractional_input_1",
    "rounding rounding_fractional_input_2",
    "rounding rounding_fractional_input_3",
    "rounding rounding_fractional_input_4",
    "rounding rounding_inner_node_controversy_combined",
    "rounding rounding_inner_node_controversy_horizontal",
    "rounding rounding_inner_node_controversy_vertical",
    "staticposition static_position_align_center_amalgamation",
};

static const uint32_t known_difference_count =
    (uint32_t)(sizeof known_differences / sizeof known_differences[0]);

static bool is_known_difference(const char *name) {
    for (uint32_t i = 0u; i < known_difference_count; ++i) {
        if (strcmp(name, known_differences[i]) == 0) {
            return true;
        }
    }

    return false;
}

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
    uint32_t unexpected = 0u;
    uint32_t known_misses = 0u;

    printf("yoga conformance\n");
    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the corpus needs scratch");

    for (uint32_t i = 0u; i < case_count; ++i) {
        if (i > first && !same_suite(&cases[first], &cases[i])) {
            report_suite(first, i, suite_matched);
            matched += suite_matched;
            first = i;
            suite_matched = 0u;
        }

        const bool landed = layout_case_run(&cases[i]);
        const bool known = is_known_difference(cases[i].name);

        /* A miss the list does not carry, or a carried name that now lands,
         * is the corpus moving out of step with this file: either way the
         * score no longer describes what the list says it does, so it fails. */
        if (!landed && !known) {
            ++unexpected;
        } else if (!landed) {
            ++known_misses;
        } else if (known) {
            ++unexpected;
        }

        suite_matched += landed ? 1u : 0u;
    }

    report_suite(first, case_count, suite_matched);
    printf("  %u of %u fixtures match\n", matched + suite_matched, case_count);

    /* Nothing outside the list misses, and every name in it still misses
     * exactly once: the score is pinned to the identities, not to a floor a
     * swap of one fix and one break would keep. */
    CF_CHECK(unexpected == 0u, "no fixture outside the known differences moves");
    CF_CHECK(known_misses == known_difference_count,
             "every known difference still misses and the list names each once");

    cf_arena_free(&scratch);

    return CF_REPORT();
}
