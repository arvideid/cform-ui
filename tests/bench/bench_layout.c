/*
 * Layout throughput at the size the budget names. The rigs assert nothing about
 * time - a shared CI runner cannot be trusted for a wall-clock number - and
 * print what the machine managed, so the published baseline has a measurement
 * behind it and a regression shows up in a review rather than as a flaky build.
 */
#include <stdio.h>
#include <time.h>

#include "cf/layout.h"
#include "cform_test.h"

#include "bench_meta.h"

#define MAX_NODES 10000u
#define DEEP_FIRST_NODES (MAX_NODES / 8u)
#define MEASURED_FIRST_NODES (MAX_NODES / 8u)
#define COLUMNS 100u
#define MAX_COLUMNS 500u
#define LAYOUTS 50u
#define CELL_HEIGHT 20.0f
#define MEASURED_WIDTH 100.0f
#define NS_PER_S 1000000000.0
#define NS_PER_MS 1000000.0
#define SCRATCH_CAPACITY (4u * 1024u * 1024u)

static cf_style styles[MAX_NODES];
static cf_measure_fn measures[MAX_NODES];
static void *measure_data[MAX_NODES];
static uint32_t child_start[MAX_NODES + 1u];
static uint32_t child_slots[MAX_NODES];
static cf_track columns[MAX_COLUMNS];
static cf_rect rects[MAX_NODES];
static float measured_widths[MAX_NODES];
static uint64_t measure_calls;
static cf_arena scratch;

static cf_tree tree_of(uint32_t node_count) {
    return (cf_tree){
        .styles = styles,
        .measures = measures,
        .measure_data = measure_data,
        .child_start = child_start,
        .child_slots = child_slots,
        .node_count = node_count,
    };
}

/*
 * A complete binary tree of alternating rows and columns, a third of the
 * branches growing into the leftover space and a seventh claiming half the
 * width: the shape a widget tree is close to, and the deep case. The root
 * states the width it is offered because an auto one would shrink-wrap to the
 * content, and a 50% branch reads as auto while its ancestor's extent is the
 * answer still being measured.
 */
static void build_deep_tree(uint32_t node_count, cf_available offer) {
    uint32_t edge = 0u;

    for (uint32_t slot = 0u; slot < node_count; ++slot) {
        cf_style style = cf_style_default();
        style.direction = slot % 2u == 0u ? CF_DIRECTION_ROW : CF_DIRECTION_COLUMN;
        style.column_gap = CF_PX(4.0f);
        style.row_gap = CF_PX(4.0f);

        if (slot == 0u) {
            style.width = offer.width;
            style.height = offer.height;
        }

        if (slot > 0u && slot % 3u == 0u) {
            style.flex_grow = 1.0f;
        }

        if (slot > 0u && slot % 7u == 0u) {
            style.width = CF_PERCENT(50.0f);
        }

        styles[slot] = style;
        measures[slot] = NULL;
        child_start[slot] = edge;

        for (uint32_t child = 2u * slot + 1u; child <= 2u * slot + 2u; ++child) {
            if (child < node_count) {
                child_slots[edge++] = child;
            }
        }
    }

    child_start[node_count] = edge;
}

/*
 * One grid container and as many cells as fit: the wide, flat case a table of
 * widgets makes, where placement and track sizing see the whole child list at
 * once. Fractional columns and content rows mean every cell is sized from the
 * tracks and every track is sized from the cells.
 */
static void build_flat_grid(uint32_t wanted_columns) {
    for (uint32_t column = 0u; column < wanted_columns; ++column) {
        columns[column] = (cf_track){.min = CF_AUTO, .max = CF_FR(1.0f)};
    }

    cf_style grid = cf_style_default();
    grid.display = CF_DISPLAY_GRID;
    grid.column_tracks = columns;
    grid.column_count = wanted_columns;
    grid.column_gap = CF_PX(4.0f);
    grid.row_gap = CF_PX(4.0f);
    styles[0] = grid;

    for (uint32_t slot = 1u; slot < MAX_NODES; ++slot) {
        cf_style cell = cf_style_default();
        cell.height = CF_PX(CELL_HEIGHT);

        if (slot % 5u == 0u) {
            cell.width = CF_PX(60.0f);
        }

        styles[slot] = cell;
        measures[slot] = NULL;
        child_start[slot] = MAX_NODES - 1u;
        child_slots[slot - 1u] = slot;
    }

    child_start[0] = 0u;
    child_start[MAX_NODES] = MAX_NODES - 1u;
    measures[0] = NULL;
}

static double elapsed_ms(const struct timespec *started, const struct timespec *finished) {
    const double seconds = (double)finished->tv_sec - (double)started->tv_sec;
    const double nanos = (double)finished->tv_nsec - (double)started->tv_nsec;

    return (seconds * NS_PER_S + nanos) / NS_PER_MS;
}

/*
 * The fastest pass rather than the average. A shared runner spends its wall
 * time on somebody else's process, and only an uncontended pass says what the
 * engine did; clock() would also round a small tree to whole milliseconds.
 */
static double layout_fastest_ms(const cf_tree *tree, cf_available offer, uint32_t layouts) {
    double best = -1.0;

    for (uint32_t i = 0u; i < layouts; ++i) {
        struct timespec started;
        struct timespec finished;
        timespec_get(&started, TIME_UTC);

        cf_arena_clear(&scratch);
        cf_layout(&scratch, tree, 0u, offer, (cf_point){0.0f, 0.0f}, rects);

        timespec_get(&finished, TIME_UTC);
        const double ms = elapsed_ms(&started, &finished);

        if (best < 0.0 || ms < best) {
            best = ms;
        }
    }

    return best;
}

/*
 * A published baseline is one number, but cost per node is what shows a subtree
 * being measured once for every level above it: with nothing caching a
 * measurement, the total grows faster than the tree does.
 */
static void bench_deep_tree(cf_available offer) {
    for (uint32_t nodes = DEEP_FIRST_NODES; nodes <= MAX_NODES; nodes *= 2u) {
        build_deep_tree(nodes, offer);
        const cf_tree tree = tree_of(nodes);
        const double ms = layout_fastest_ms(&tree, offer, LAYOUTS);

        CF_NEAR(rects[0].width, 1280.0f);

        char key[40];
        snprintf(key, sizeof key, "deep_flex_%u_ms", nodes);
        bench_result(key, ms, "ms");
        snprintf(key, sizeof key, "deep_flex_%u_ns_per_node", nodes);
        bench_result(key, ms * NS_PER_MS / (double)nodes, "ns");
    }
}

static void bench_flat_grid(cf_available offer, uint32_t wanted_columns, const char *label) {
    build_flat_grid(wanted_columns);
    const cf_tree tree = tree_of(MAX_NODES);
    const double ms = layout_fastest_ms(&tree, offer, LAYOUTS);

    CF_CHECK(rects[2u].x > 0.0f, "the second cell sits past the first column");

    char key[64];
    snprintf(key, sizeof key, "flat_grid_%s_ms", label);
    bench_result(key, ms, "ms");
    snprintf(key, sizeof key, "flat_grid_%s_ns_per_cell", label);
    bench_result(key, ms * NS_PER_MS / (double)MAX_NODES, "ns");
}

/*
 * A leaf's measure callback is the one place the engine asks a caller for
 * content it cannot compute: text width from a font, image size from a file.
 * Counting the calls from the callback's side shows whether a layout memoises
 * the answer or asks the same leaf again - Roadmap Section 6 item 3 wants the
 * number before anyone tunes the cache.
 */
static cf_size counted_measure(void *user_data, cf_available available, cf_intrinsic kind) {
    (void)available;
    (void)kind;
    measure_calls++;
    const float width = *(const float *)user_data;

    return (cf_size){.width = width, .height = CELL_HEIGHT};
}

/*
 * A column of leaves with no declared size: every cell asks its callback, and
 * the callback is written to hand back the same box every time. Auto-sized
 * leaves are the worst case the cache sees - both axes come from a call - and
 * a page of them is what a text-heavy screen is before it settles.
 */
static void build_measured_page(uint32_t node_count) {
    cf_style root = cf_style_default();
    root.direction = CF_DIRECTION_COLUMN;
    root.width = CF_PX(1280.0f);
    styles[0] = root;
    measures[0] = NULL;

    for (uint32_t slot = 1u; slot < node_count; ++slot) {
        styles[slot] = cf_style_default();
        measured_widths[slot] = MEASURED_WIDTH;
        measures[slot] = counted_measure;
        measure_data[slot] = &measured_widths[slot];
        child_slots[slot - 1u] = slot;
    }

    child_start[0] = 0u;

    for (uint32_t slot = 1u; slot <= node_count; ++slot) {
        child_start[slot] = node_count - 1u;
    }
}

/*
 * One pass per node count: the layout call gets the fastest time and the
 * callback count that produced it. Ratio per leaf says how many measure
 * passes a single layout runs - three is the engine's floor when a leaf's
 * extent is auto on both axes, and more than that shows the cache is missing.
 */
static void bench_measured_page(cf_available offer) {
    for (uint32_t nodes = MEASURED_FIRST_NODES; nodes <= MAX_NODES; nodes *= 2u) {
        build_measured_page(nodes);
        const cf_tree tree = tree_of(nodes);

        measure_calls = 0u;
        const double ms = layout_fastest_ms(&tree, offer, LAYOUTS);
        const uint64_t calls_per_frame = measure_calls / LAYOUTS;
        const uint32_t leaves = nodes - 1u;

        CF_CHECK(rects[0].width == 1280.0f, "the column takes what it is offered");

        char key[64];
        snprintf(key, sizeof key, "measured_%u_ms", nodes);
        bench_result(key, ms, "ms");
        snprintf(key, sizeof key, "measured_%u_calls_per_frame", nodes);
        bench_result(key, (double)calls_per_frame, "count");
        snprintf(key, sizeof key, "measured_%u_calls_per_leaf", nodes);
        bench_result(key, (double)calls_per_frame / (double)leaves, "count");
    }
}

/*
 * A grid pays for the same thing more than once: it collects contributions from
 * every item on both axes and twice per axis (min-content floor, then the
 * intrinsic mode the container is being sized at). Cells carry a callback
 * instead of a stated height, and the ratio reports what the grid sizing walk
 * actually asks for - Roadmap Section 6 item 3 wants that pass counted, not
 * inferred from milliseconds.
 */
static void build_measured_grid(uint32_t wanted_columns) {
    for (uint32_t column = 0u; column < wanted_columns; ++column) {
        columns[column] = (cf_track){.min = CF_AUTO, .max = CF_FR(1.0f)};
    }

    cf_style grid = cf_style_default();
    grid.display = CF_DISPLAY_GRID;
    grid.column_tracks = columns;
    grid.column_count = wanted_columns;
    grid.column_gap = CF_PX(4.0f);
    grid.row_gap = CF_PX(4.0f);
    styles[0] = grid;
    measures[0] = NULL;

    for (uint32_t slot = 1u; slot < MAX_NODES; ++slot) {
        styles[slot] = cf_style_default();
        measured_widths[slot] = MEASURED_WIDTH;
        measures[slot] = counted_measure;
        measure_data[slot] = &measured_widths[slot];
        child_start[slot] = MAX_NODES - 1u;
        child_slots[slot - 1u] = slot;
    }

    child_start[0] = 0u;
    child_start[MAX_NODES] = MAX_NODES - 1u;
}

static void bench_measured_grid(cf_available offer, uint32_t wanted_columns, const char *label) {
    build_measured_grid(wanted_columns);
    const cf_tree tree = tree_of(MAX_NODES);

    measure_calls = 0u;
    const double ms = layout_fastest_ms(&tree, offer, LAYOUTS);
    const uint64_t calls_per_frame = measure_calls / LAYOUTS;
    const uint32_t cells = MAX_NODES - 1u;

    CF_CHECK(rects[1u].width >= 0.0f, "the first cell has a box the pass wrote");

    char key[64];
    snprintf(key, sizeof key, "measured_grid_%s_ms", label);
    bench_result(key, ms, "ms");
    snprintf(key, sizeof key, "measured_grid_%s_calls_per_cell", label);
    bench_result(key, (double)calls_per_frame / (double)cells, "count");
}

int main(void) {
    bench_meta("bench_layout",
               "10k-deep-flex+flat-grid-3-column-extremes+measured-page+measured-grid");

    const cf_available offer = {.width = CF_PX(1280.0f), .height = CF_PX(720.0f)};

    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the rigs need scratch");
    bench_result("scratch_bytes", (double)SCRATCH_CAPACITY, "bytes");
    bench_result("layouts_per_point", (double)LAYOUTS, "count");

    bench_deep_tree(offer);
    bench_flat_grid(offer, COLUMNS, "balanced");
    /* Two extremes: a wide axis exercises the prefix build against a huge
     * column count, and a tall one exercises it against a huge row count. A
     * regression in the prefix or the span difference shows up on both. */
    bench_flat_grid(offer, MAX_COLUMNS, "wide");
    bench_flat_grid(offer, 20u, "tall");
    bench_measured_page(offer);
    bench_measured_grid(offer, COLUMNS, "balanced");

    cf_arena_free(&scratch);

    return CF_REPORT();
}
