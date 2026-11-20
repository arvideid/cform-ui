#include <stdio.h>

#include "cf/layout.h"
#include "cform_test.h"

#define MAX_NODES 6u
#define RECT_TOLERANCE 0.01f

#define ROOT UINT32_MAX
#define ZERO_BOX {CF_PX(0.0f), CF_PX(0.0f), CF_PX(0.0f), CF_PX(0.0f)}
#define BOX(each) {CF_PX(each), CF_PX(each), CF_PX(each), CF_PX(each)}
#define EDGES(left, top, right, bottom) {CF_PX(left), CF_PX(top), CF_PX(right), CF_PX(bottom)}
#define SIDES(left, top, right, bottom) {(left), (top), (right), (bottom)}
#define RECT(x, y, width, height)                                                                  \
    (cf_rect) {                                                                                    \
        (x), (y), (width), (height)                                                                \
    }

/*
 * A style as it would be written after cf_style_default(), whose two
 * non-initial-zero fields a constant initializer cannot call: margins of 0
 * rather than auto, and a flex-shrink of 1. A row that declares no interesting
 * field at all calls cf_style_default() instead of overriding these.
 */
#define STYLE(...)                                                                                 \
    (cf_style) {                                                                                   \
        .margin = ZERO_BOX, .flex_shrink = 1.0f, __VA_ARGS__                                       \
    }
/* A row that states its margins writes them here rather than overriding. */
#define MARGIN(margin_box, ...)                                                                    \
    (cf_style) {                                                                                   \
        .margin = margin_box, .flex_shrink = 1.0f, __VA_ARGS__                                     \
    }
#define SIZE(box_width, box_height) STYLE(.width = CF_PX(box_width), .height = CF_PX(box_height))
#define FILL STYLE(.flex_grow = 1.0f)
#define GROWN(...) STYLE(.flex_grow = 1.0f, __VA_ARGS__)
#define COLUMN(...) STYLE(.direction = CF_DIRECTION_COLUMN, __VA_ARGS__)
#define ABSOLUTE(...) STYLE(.position = CF_POSITION_ABSOLUTE, __VA_ARGS__)
#define GRID(...) STYLE(.display = CF_DISPLAY_GRID, __VA_ARGS__)
/* A track is minmax(min, max); a fixed one repeats its length in both slots. */
#define TRACK(min_length, max_length) {(min_length), (max_length)}
#define TRACKS(...)                                                                                \
    (const cf_track[]) {                                                                           \
        __VA_ARGS__                                                                                \
    }
#define FIXED(pixels) TRACK(CF_PX(pixels), CF_PX(pixels))
#define AVAILABLE(offer_width, offer_height)                                                       \
    (cf_available) {                                                                               \
        .width = (offer_width), .height = (offer_height)                                           \
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
static cf_measure_fn measures[MAX_NODES];
static void *measure_data[MAX_NODES];
static uint32_t child_start[MAX_NODES + 1u];
static uint32_t child_slots[MAX_NODES * MAX_NODES];
static cf_rect rects[MAX_NODES];
static char mismatch[160];

/*
 * Children are the nodes naming this one as their parent, in the order the
 * case lists them, so declaration order stays paint order. The tree is rebuilt
 * per case into the same arrays because cf_tree reads parallel pointers, not
 * an owned structure.
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
        .measures = measures,
        .measure_data = measure_data,
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

static void run(const layout_case *c) {
    const cf_tree tree = tree_of(c);

    printf("  - %s\n", c->name);
    const int before = cf_failures;
    cf_layout(&scratch, &tree, 0u, c->available, (cf_point){0.0f, 0.0f}, rects);

    for (uint32_t slot = 0u; slot < c->node_count; ++slot) {
        CF_CHECK(rect_is_expected(c->nodes[slot].expected, rects[slot], c->name, slot), mismatch);
    }

    printf("      %s\n", cf_failures == before ? "ok" : "FAILED");
}

int main(void) {
    printf("layout conformance cases\n");
    CF_CHECK(cf_arena_init(&scratch, 16384u), "the corpus needs scratch");

    /* A case is data: the tree it declares, the box it is offered, and the rect
     * every node must land in. The corpus is the 0.2 layout suite, restated
     * against the flexbox rules, the children that leave the flow, and the grid
     * subset the track solver implements. */
    const layout_case cases[] = {
#include "layout_cases_data.h"
    };

    for (uint32_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i) {
        run(&cases[i]);
    }

    printf("  %u cases\n", (uint32_t)(sizeof cases / sizeof cases[0]));
    cf_arena_free(&scratch);

    return CF_REPORT();
}
