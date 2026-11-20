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

#define NODE_COUNT 10000u
#define COLUMNS 100u
#define DEEP_LAYOUTS 100u
#define GRID_LAYOUTS 3u
#define CELL_HEIGHT 20.0f
#define SCRATCH_CAPACITY 1048576u
#define CLOCK_TO_MS (1000.0 / (double)CLOCKS_PER_SEC)

static cf_style styles[NODE_COUNT];
static uint32_t child_start[NODE_COUNT + 1u];
static uint32_t child_slots[NODE_COUNT];
static cf_track columns[COLUMNS];
static cf_rect rects[NODE_COUNT];
static cf_arena scratch;

static cf_tree tree_of(void) {
    return (cf_tree){
        .styles = styles,
        .child_start = child_start,
        .child_slots = child_slots,
        .node_count = NODE_COUNT,
    };
}

/*
 * A complete binary tree of alternating rows and columns, a third of the
 * branches growing into the leftover space and a seventh claiming half the
 * width: the shape a widget tree is close to, and the deep case.
 */
static void build_deep_tree(void) {
    uint32_t edge = 0u;

    for (uint32_t slot = 0u; slot < NODE_COUNT; ++slot) {
        cf_style style = cf_style_default();
        style.direction = slot % 2u == 0u ? CF_DIRECTION_ROW : CF_DIRECTION_COLUMN;
        style.column_gap = CF_PX(4.0f);
        style.row_gap = CF_PX(4.0f);

        if (slot > 0u && slot % 3u == 0u) {
            style.flex_grow = 1.0f;
        }

        if (slot > 0u && slot % 7u == 0u) {
            style.width = CF_PERCENT(50.0f);
        }

        styles[slot] = style;
        child_start[slot] = edge;

        for (uint32_t child = 2u * slot + 1u; child <= 2u * slot + 2u; ++child) {
            if (child < NODE_COUNT) {
                child_slots[edge++] = child;
            }
        }
    }

    child_start[NODE_COUNT] = edge;
}

/*
 * One grid container and as many cells as fit: the wide, flat case a table of
 * widgets makes, where placement and track sizing see the whole child list at
 * once. Fractional columns and content rows mean every cell is sized from the
 * tracks and every track is sized from the cells.
 */
static void build_flat_grid(void) {
    for (uint32_t column = 0u; column < COLUMNS; ++column) {
        columns[column] = (cf_track){.min = CF_AUTO, .max = CF_FR(1.0f)};
    }

    cf_style grid = cf_style_default();
    grid.display = CF_DISPLAY_GRID;
    grid.column_tracks = columns;
    grid.column_count = COLUMNS;
    grid.column_gap = CF_PX(4.0f);
    grid.row_gap = CF_PX(4.0f);
    styles[0] = grid;

    for (uint32_t slot = 1u; slot < NODE_COUNT; ++slot) {
        cf_style cell = cf_style_default();
        cell.height = CF_PX(CELL_HEIGHT);

        if (slot % 5u == 0u) {
            cell.width = CF_PX(60.0f);
        }

        styles[slot] = cell;
        child_start[slot] = NODE_COUNT - 1u;
        child_slots[slot - 1u] = slot;
    }

    child_start[0] = 0u;
    child_start[NODE_COUNT] = NODE_COUNT - 1u;
}

static double layout_repeatedly(const cf_tree *tree, cf_available offer, uint32_t layouts) {
    const clock_t started = clock();

    for (uint32_t i = 0u; i < layouts; ++i) {
        cf_arena_clear(&scratch);
        cf_layout(&scratch, tree, 0u, offer, (cf_point){0.0f, 0.0f}, rects);
    }

    return (double)(clock() - started) * CLOCK_TO_MS / (double)layouts;
}

int main(void) {
    const cf_available offer = {.width = CF_PX(1280.0f), .height = CF_PX(720.0f)};

    CF_CHECK(cf_arena_init(&scratch, SCRATCH_CAPACITY), "the rigs need scratch");

    build_deep_tree();
    const cf_tree deep = tree_of();
    const double deep_ms = layout_repeatedly(&deep, offer, DEEP_LAYOUTS);

    CF_NEAR(rects[0].width, 1280.0f);
    printf("  deep flex tree, %u nodes: %6.3f ms per layout\n", NODE_COUNT, deep_ms);

    build_flat_grid();
    const cf_tree grid = tree_of();
    const double grid_ms = layout_repeatedly(&grid, offer, GRID_LAYOUTS);

    CF_CHECK(rects[2u].x > 0.0f, "the second cell sits past the first column");
    printf("  flat grid, %u cells: %6.3f ms per layout\n", NODE_COUNT, grid_ms);

    cf_arena_free(&scratch);

    return CF_REPORT();
}
