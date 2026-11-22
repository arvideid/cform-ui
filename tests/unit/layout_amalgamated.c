/*
 * The engine as one header: this case includes the generated cf_layout.h and no
 * cf/ header, so a stripped guard, a system include the packer dropped or a
 * declaration that reached outside the packed set fails to compile instead of
 * passing by finding the directory on the include path.
 */
#include "cf_layout.h"
#include "cform_test.h"

static void the_packed_header_lays_out_a_row(void) {
    cf_style root = cf_style_default();
    root.width = CF_PX(200.0f);
    root.height = CF_PX(100.0f);

    cf_style fixed = cf_style_default();
    fixed.width = CF_PX(60.0f);

    cf_style growing = cf_style_default();
    growing.flex_grow = 1.0f;

    const cf_style styles[] = {root, fixed, growing};
    const uint32_t child_start[] = {0u, 2u, 2u, 2u};
    const uint32_t child_slots[] = {1u, 2u};
    cf_rect rects[3];

    cf_arena scratch;
    cf_arena_init(&scratch, 1024u);

    cf_layout(&scratch,
              &(cf_tree){.styles = styles,
                         .child_start = child_start,
                         .child_slots = child_slots,
                         .node_count = 3u},
              0u, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(100.0f)},
              (cf_point){0.0f, 0.0f}, rects);

    CF_NEAR(rects[0].width, 200.0f);
    CF_NEAR(rects[1].width, 60.0f);
    CF_NEAR(rects[2].x, 60.0f);
    CF_NEAR(rects[2].width, 140.0f);
    CF_NEAR(rects[2].height, 100.0f);

    cf_arena_clear(&scratch);
    cf_arena_free(&scratch);
}

/* The track helper the packed header carries is the grid's repeat: a pattern
 * stated once, read out as the flat array the engine wants. */
static void the_packed_header_repeats_a_track_pattern(void) {
    const cf_track pattern[] = {
        {CF_PX(40.0f), CF_PX(40.0f)},
        {CF_AUTO, CF_FR(1.0f)},
    };
    cf_track columns[4];

    const uint32_t count = cf_tracks_repeat(columns, 4u, pattern, 2u, 2u);

    CF_CHECK(count == 4u, "the pattern filled the array it was given");
    CF_NEAR(columns[2].min.value, 40.0f);
    CF_NEAR(columns[3].max.value, 1.0f);
}

int main(void) {
    CF_RUN(the_packed_header_lays_out_a_row);
    CF_RUN(the_packed_header_repeats_a_track_pattern);

    return CF_REPORT();
}
