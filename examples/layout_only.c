/*
 * The layout engine on its own: this file links cf_layout and nothing else, so
 * a build of it fails the moment the engine needs a window, a font or a
 * backend. Everything it cannot see into - a label's text - arrives through a
 * measure callback.
 */
#include <stdio.h>

#include "cf/layout.h"

#define LABEL_WIDTH 40.0f
#define LABEL_HEIGHT 16.0f

/* Standing in for a text run: the engine asks what it would weigh at its
 * narrowest and at its natural width, and gets the same height both times. */
static cf_size measure_label(void *user_data, cf_available available, cf_intrinsic kind) {
    (void)user_data;
    (void)available;

    return (cf_size){
        .width = kind == CF_INTRINSIC_MIN_CONTENT ? LABEL_WIDTH / 2.0f : LABEL_WIDTH,
        .height = LABEL_HEIGHT,
    };
}

int main(void) {
    /* Two columns of 60 points, each followed by a track that takes what is
     * left over: the pattern is written once and repeated twice. */
    const cf_track pattern[] = {
        {CF_PT(60.0f), CF_PT(60.0f)},
        {CF_AUTO, CF_FR(1.0f)},
    };
    cf_track columns[4];
    const uint32_t column_count = cf_tracks_repeat(columns, 4u, pattern, 2u, 2u);

    cf_style root = cf_style_default();
    root.width = CF_PT(240.0f);
    root.direction = CF_DIRECTION_COLUMN;
    root.row_gap = CF_PT(8.0f);

    cf_style grid = cf_style_default();
    grid.display = CF_DISPLAY_GRID;
    grid.column_tracks = columns;
    grid.column_count = column_count;
    grid.column_gap = CF_PT(8.0f);
    grid.row_gap = CF_PT(8.0f);

    cf_style first = cf_style_default();
    first.row_start = 1u;
    first.column_start = 1u;
    first.height = CF_PT(30.0f);

    cf_style second = cf_style_default();
    second.row_start = 1u;
    second.column_start = 3u;
    second.width = CF_PT(40.0f);
    second.height = CF_PT(30.0f);

    cf_style third = cf_style_default();
    third.column_span = 2u;
    third.height = CF_PT(30.0f);

    cf_style caption = cf_style_default();
    caption.height = CF_PT(LABEL_HEIGHT);

    const cf_style styles[] = {root, grid, first, second, third, caption};
    const uint32_t node_count = (uint32_t)(sizeof styles / sizeof styles[0]);

    const cf_measure_fn measures[] = {
        NULL, NULL, NULL, NULL, NULL, measure_label,
    };

    /* Node 0 holds the grid and the caption; the grid holds three items. */
    const uint32_t child_start[] = {0u, 2u, 5u, 5u, 5u, 5u, 5u};
    const uint32_t child_slots[] = {1u, 5u, 2u, 3u, 4u};

    cf_rect rects[sizeof styles / sizeof styles[0]];

    cf_arena scratch;
    cf_arena_init(&scratch, 4096u);

    cf_layout(&scratch,
              &(cf_tree){
                  .styles = styles,
                  .measures = measures,
                  .child_start = child_start,
                  .child_slots = child_slots,
                  .node_count = node_count,
              },
              0u, (cf_available){.width = CF_PT(240.0f), .height = CF_AUTO}, (cf_point){0.0f, 0.0f},
              rects);

    static const char *const names[] = {"root", "grid", "first", "second", "third", "caption"};

    for (uint32_t slot = 0u; slot < node_count; ++slot) {
        printf("%-8s %6.1f %6.1f %6.1f %6.1f\n", names[slot], rects[slot].x, rects[slot].y,
               rects[slot].width, rects[slot].height);
    }

    cf_arena_clear(&scratch);
    cf_arena_free(&scratch);

    return 0;
}
