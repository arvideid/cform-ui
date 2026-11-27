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
 * The score each category of the corpus has to hold, in the order the fixtures
 * arrive.
 *
 * The list above pins identities, which is the stronger rule for the twelve cases
 * it names and no rule at all for the rest: a suite whose 46 answers quietly
 * become 40 while another gains six is invisible to an aggregate, and it is
 * invisible to a reviewer reading a diff of one file. The rows below are the
 * counts this tree prints today, so a category can only lose ground behind a
 * sentence in the diff saying that it did. Raising a floor is what closing a
 * known difference looks like, and `cases` moving without a row moving is what a
 * corpus that gained or lost a fixture looks like - either way this file has to
 * be read, which is the point.
 */
typedef struct suite_score {
    const char *suite;
    uint32_t cases;
    uint32_t floor;
} suite_score;

static const suite_score suite_scores[] = {
    {"absoluteposition", 34u, 34u},
    {"aligncontent", 64u, 64u},
    {"alignitems", 29u, 29u},
    {"alignself", 5u, 5u},
    {"androidnewsfeed", 1u, 1u},
    {"aspectratio", 1u, 1u},
    {"auto", 5u, 5u},
    {"border", 5u, 5u},
    {"boxsizing", 46u, 46u},
    {"dimension", 2u, 2u},
    {"display", 6u, 6u},
    {"flexbasisfitcontent", 6u, 6u},
    {"flexdirection", 53u, 53u},
    {"flex", 10u, 10u},
    {"flexwrap", 23u, 23u},
    {"gap", 32u, 31u},
    {"intrinsicsize", 5u, 5u},
    {"justifycontent", 28u, 28u},
    {"margin", 34u, 34u},
    {"minmaxdimension", 24u, 24u},
    {"padding", 7u, 7u},
    {"percentage", 24u, 24u},
    {"rounding", 13u, 3u},
    {"sizeoverflow", 3u, 3u},
    {"staticposition", 62u, 61u},
};

static const uint32_t suite_score_count = (uint32_t)(sizeof suite_scores / sizeof suite_scores[0]);

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

static void report_suite(uint32_t first, uint32_t last, uint32_t matched, uint32_t *below) {
    const size_t length = suite_length(cases[first].name);
    printf("  %-20.*s %4u of %4u\n", (int)length, cases[first].name, matched, last - first);

    /* A category nobody wrote a row for is a category nobody is watching, so the
     * corpus growing is an edit to this file rather than a passing test run. */
    const suite_score *score = NULL;

    for (uint32_t i = 0u; i < suite_score_count && score == NULL; ++i) {
        if (strlen(suite_scores[i].suite) == length &&
            strncmp(suite_scores[i].suite, cases[first].name, length) == 0) {
            score = &suite_scores[i];
        }
    }

    if (score == NULL) {
        printf("    no row names this suite\n");
        ++*below;
        return;
    }

    if (score->cases != last - first) {
        printf("    the suite states %u fixtures and its row says %u\n", last - first,
               score->cases);
        ++*below;
    }

    if (matched < score->floor) {
        printf("    %u landed and the floor is %u\n", matched, score->floor);
        ++*below;
    }
}

int main(void) {
    uint32_t matched = 0u;
    uint32_t first = 0u;
    uint32_t suite_matched = 0u;
    uint32_t unexpected = 0u;
    uint32_t known_misses = 0u;
    uint32_t below_floor = 0u;

    printf("yoga conformance\n");
    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the corpus needs scratch");

    for (uint32_t i = 0u; i < case_count; ++i) {
        if (i > first && !same_suite(&cases[first], &cases[i])) {
            report_suite(first, i, suite_matched, &below_floor);
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

    report_suite(first, case_count, suite_matched, &below_floor);
    printf("  %u of %u fixtures match\n", matched + suite_matched, case_count);

    /* Nothing outside the list misses, and every name in it still misses
     * exactly once: the score is pinned to the identities, not to a floor a
     * swap of one fix and one break would keep. */
    CF_CHECK(unexpected == 0u, "no fixture outside the known differences moves");
    CF_CHECK(known_misses == known_difference_count,
             "every known difference still misses and the list names each once");
    /* And each category holds its own line, so a gain in one cannot pay for a loss in
     * another - which is the failure an aggregate score hides. */
    CF_CHECK(below_floor == 0u, "every suite holds the floor written for it");
    CF_CHECK(suite_score_count == 25u, "the table covers one row per suite in the corpus");

    cf_arena_free(&scratch);

    return CF_REPORT();
}
