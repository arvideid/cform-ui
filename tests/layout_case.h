#ifndef CFORM_TESTS_LAYOUT_CASE_H
#define CFORM_TESTS_LAYOUT_CASE_H

/*
 * A layout corpus is data: the box a root is offered and, per node, its style,
 * its parent's slot and the border box the pass owes it. Trees are rebuilt
 * per case into the same arrays, because cf_tree reads parallel pointers
 * rather than an owned structure and a corpus should not need a heap.
 *
 * A corpus with deeper trees than MAX_NODES raises the cap by defining it
 * before including this header; nothing else in the row format changes.
 */
#ifndef MAX_NODES
#define MAX_NODES 6u
#endif

/* Line and item tables grow with the widest tree, not the number of cases, so
 * one arena serves a whole corpus and no case is measured with a full one. */
#define SCRATCH_CAPACITY 65536u

#include <stdio.h>

#include "cf/layout.h"
#include "cform_test.h"

#define ROOT UINT32_MAX
#define RECT_TOLERANCE 0.01f

#define ZERO_BOX {CF_PX(0.0f), CF_PX(0.0f), CF_PX(0.0f), CF_PX(0.0f)}
#define SIDES(left, top, right, bottom) {(left), (top), (right), (bottom)}
#define RECT(x, y, width, height)                                                                  \
    (cf_rect) {                                                                                    \
        (x), (y), (width), (height)                                                                \
    }
#define AVAILABLE(offer_width, offer_height)                                                       \
    (cf_available) {                                                                               \
        .width = (offer_width), .height = (offer_height)                                           \
    }

/*
 * A style as it reads after cf_style_default(), whose two non-initial-zero
 * fields a constant initializer cannot state: margins of 0 rather than margins
 * that auto-absorb every bit of leftover space, and a flex-shrink of 1. A
 * corpus row is static data, so it writes those two out rather than calling.
 */
#define STYLE(...)                                                                                 \
    (cf_style) {                                                                                   \
        .margin = ZERO_BOX, .flex_shrink = 1.0f, __VA_ARGS__                                       \
    }

/*
 * A row from the Yoga corpus states every declaration that node carries,
 * including the three Yoga starts from and cf_style's zero does not give: a
 * column container, lines packed from the cross start, and margins of 0.
 * Nothing is defaulted here, because a generated row is read against the
 * reference engine's configuration rather than this engine's.
 */
#define YOGA_STYLE(...)                                                                            \
    (cf_style) {                                                                                   \
        __VA_ARGS__                                                                                \
    }

/* One node of a case and the border box the pass owes it. */
typedef struct case_node {
    cf_style style;
    uint32_t parent;
    cf_rect expected;
} case_node;

typedef struct layout_case {
    const char *name;
    cf_available available;
    uint32_t node_count;
    case_node nodes[MAX_NODES];
} layout_case;

static cf_arena scratch;
static cf_style styles[MAX_NODES];
static uint32_t child_start[MAX_NODES + 1u];
static uint32_t child_slots[MAX_NODES * MAX_NODES];
static cf_rect rects[MAX_NODES];
static char mismatch[200];

/*
 * Children are the nodes naming this one as their parent, in the order the
 * case lists them, so declaration order stays paint order.
 */
static cf_tree tree_of(const layout_case *c) {
    uint32_t edge = 0u;

    for (uint32_t slot = 0u; slot < c->node_count; ++slot) {
        styles[slot] = c->nodes[slot].style;
        child_start[slot] = edge;

        for (uint32_t child = 0u; child < c->node_count; ++child) {
            if (c->nodes[child].parent == slot) {
                child_slots[edge++] = child;
            }
        }
    }

    child_start[c->node_count] = edge;

    return (cf_tree){
        .styles = styles,
        .child_start = child_start,
        .child_slots = child_slots,
        .node_count = c->node_count,
    };
}

static bool close_enough(float got, float want) {
    const float difference = got > want ? got - want : want - got;

    return difference < RECT_TOLERANCE;
}

static bool rect_is_expected(cf_rect want, cf_rect got, const char *name, uint32_t slot) {
    if (close_enough(got.x, want.x) && close_enough(got.y, want.y) &&
        close_enough(got.width, want.width) && close_enough(got.height, want.height)) {
        return true;
    }

    snprintf(mismatch, sizeof mismatch, "%s: node %u is %g %g %g %g and wanted %g %g %g %g", name,
             slot, got.x, got.y, got.width, got.height, want.x, want.y, want.width, want.height);

    return false;
}

/* Runs one case and says whether every node landed where it was owed. The
 * mismatch is printed either way, so a caller decides what a miss is worth. */
static bool layout_case_run(const layout_case *c) {
    const cf_tree tree = tree_of(c);
    bool matched = true;

    cf_layout(&scratch, &tree, 0u, c->available, (cf_point){0.0f, 0.0f}, rects);

    for (uint32_t slot = 0u; slot < c->node_count; ++slot) {
        if (!rect_is_expected(c->nodes[slot].expected, rects[slot], c->name, slot)) {
            printf("    %s\n", mismatch);
            matched = false;
        }
    }

    return matched;
}

/*
 * Runs the corpus and prints what missed. A conformance tier publishes a score
 * from the return value and a unit tier only asks whether the count came back
 * whole; both read the same miss lines, so a lower score is diagnosable
 * without rerunning the corpus under a debugger.
 */
static uint32_t layout_corpus_run(const char *title, const layout_case *cases, uint32_t count) {
    uint32_t matched = 0u;

    printf("%s\n", title);
    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the corpus needs scratch");

    for (uint32_t i = 0u; i < count; ++i) {
        matched += layout_case_run(&cases[i]) ? 1u : 0u;
    }

    printf("  %u of %u cases match\n", matched, count);
    cf_arena_free(&scratch);

    return matched;
}

#endif
