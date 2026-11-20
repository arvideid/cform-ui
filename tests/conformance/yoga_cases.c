/* The Yoga fixtures need the widest tree the corpus format can name. */
#define MAX_NODES 17u

#include "layout_case.h"

/*
 * Where the ported corpus stands today. The floor only moves up: a fixture
 * that regresses fails the build, and a fixture that starts matching is the
 * floor's to be raised beside. docs/conformance.md publishes the number, the
 * suites it is made of and the behaviour each miss needs.
 */
#define CONFORMANCE_FLOOR 267u

/*
 * Written by tools/gentest from Yoga's generated fixtures: every case the
 * reference engine measured, with the box it measured. The suite name leads
 * each case, so a miss reads back to upstream in one step.
 */
static const layout_case cases[] = {
#include "yoga_cases_data.h"
};

int main(void) {
    const uint32_t count = (uint32_t)(sizeof cases / sizeof cases[0]);
    const uint32_t matched = layout_corpus_run("yoga conformance", cases, count);

    CF_CHECK(matched >= CONFORMANCE_FLOOR, "the corpus stands at least this high");

    return CF_REPORT();
}
