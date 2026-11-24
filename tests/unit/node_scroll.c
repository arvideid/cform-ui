#include "cf/node.h"
#include "cform_test.h"

/* The scrollbar math runs against one geometry throughout: a 12-wide track,
 * 200 long, beside a viewport that shows 100 of 400 units of content. */
static cf_scrollbar bar_of(void) {
    return (cf_scrollbar){.viewport = 100.0f,
                          .content = 400.0f,
                          .track = {.x = 5.0f, .y = 10.0f, .width = 12.0f, .height = 200.0f}};
}

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static void a_fresh_block_starts_at_the_start(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    const cf_name list = cf_intern_text(&ctx.names, "list");
    cf_scroll *scroll = cf_scroll_of(&ctx, list);

    CF_CHECK(scroll != NULL, "the block is served");
    CF_CHECK(scroll != NULL && scroll->offset == 0.0f, "a fresh block holds no offset");
    CF_CHECK(cf_scroll_of(&ctx, CF_NO_NAME) == NULL, "a nameless request is refused");

    cf_ctx_free(&ctx);
}

static void an_offset_never_leaves_the_scrollable_range(void) {
    CF_NEAR(cf_scroll_clamp(-5.0f, 100.0f, 400.0f), 0.0f);
    CF_NEAR(cf_scroll_clamp(500.0f, 100.0f, 400.0f), 300.0f);
    CF_NEAR(cf_scroll_clamp(150.0f, 100.0f, 400.0f), 150.0f);
    CF_NEAR(cf_scroll_clamp(-5.0f, 100.0f, 100.0f), 0.0f);
    CF_NEAR(cf_scroll_clamp(50.0f, 100.0f, 80.0f), 0.0f);
}

static void the_thumb_fills_the_track_when_everything_fits(void) {
    const cf_scrollbar bar = {.viewport = 100.0f,
                              .content = 100.0f,
                              .track = {.x = 5.0f, .y = 10.0f, .width = 12.0f, .height = 200.0f}};

    const cf_rect thumb = cf_scrollbar_thumb(bar, 42.0f);

    CF_NEAR(thumb.x, 5.0f);
    CF_NEAR(thumb.y, 10.0f);
    CF_NEAR(thumb.width, 12.0f);
    CF_NEAR(thumb.height, 200.0f);
}

static void the_thumb_scales_with_the_visible_share_and_sits_by_the_offset(void) {
    const cf_scrollbar bar = bar_of();

    /* A quarter of the content visible is a quarter of the track. */
    const cf_rect start = cf_scrollbar_thumb(bar, 0.0f);

    CF_NEAR(start.height, 50.0f);
    CF_NEAR(start.y, 10.0f);

    const cf_rect middle = cf_scrollbar_thumb(bar, 150.0f);

    CF_NEAR(middle.y, 85.0f);

    const cf_rect end = cf_scrollbar_thumb(bar, 300.0f);

    CF_NEAR(end.y + end.height, 210.0f);
    CF_NEAR(cf_scrollbar_thumb(bar, 1000.0f).y, end.y);
}

static void a_very_long_content_keeps_the_thumb_grabbable(void) {
    const cf_scrollbar bar = {.viewport = 100.0f,
                              .content = 100000.0f,
                              .track = {.x = 5.0f, .y = 10.0f, .width = 12.0f, .height = 200.0f}};

    const cf_rect start = cf_scrollbar_thumb(bar, 0.0f);
    const cf_rect end = cf_scrollbar_thumb(bar, 99900.0f);

    CF_NEAR(start.height, 16.0f);
    CF_NEAR(end.y + end.height, 210.0f);
}

static void a_drag_moves_the_offset_by_the_track_ratio(void) {
    const cf_scrollbar bar = bar_of();

    /* The thumb at offset 150 covers y 85..135, so the grab sits at its
     * center and the free track scales every pixel to two units. */
    const cf_point grab = {.x = 6.0f, .y = 110.0f};

    CF_NEAR(cf_scroll_drag(bar, grab, (cf_point){.x = 6.0f, .y = 110.0f}, 150.0f), 150.0f);
    CF_NEAR(cf_scroll_drag(bar, grab, (cf_point){.x = 6.0f, .y = 185.0f}, 150.0f), 300.0f);
    CF_NEAR(cf_scroll_drag(bar, grab, (cf_point){.x = 6.0f, .y = 60.0f}, 150.0f), 50.0f);
    CF_NEAR(cf_scroll_drag(bar, grab, (cf_point){.x = 6.0f, .y = 1000.0f}, 150.0f), 300.0f);
    CF_NEAR(cf_scroll_drag(bar_of(), grab, grab, -20.0f), 0.0f);
}

static void a_drag_on_a_track_that_cannot_scroll_moves_nothing(void) {
    const cf_scrollbar fits = {.viewport = 100.0f,
                               .content = 100.0f,
                               .track = {.x = 5.0f, .y = 10.0f, .width = 12.0f, .height = 200.0f}};

    CF_NEAR(cf_scroll_drag(fits, (cf_point){.x = 6.0f, .y = 110.0f},
                           (cf_point){.x = 6.0f, .y = 500.0f}, 40.0f),
            0.0f);

    /* A track with no length has no thumb to drag; the offset comes back
     * the way it went in, clamped to the range the content allows. */
    const cf_scrollbar flat = {.viewport = 100.0f,
                               .content = 400.0f,
                               .track = {.x = 5.0f, .y = 10.0f, .width = 12.0f, .height = 0.0f}};

    CF_NEAR(cf_scroll_drag(flat, (cf_point){.x = 6.0f, .y = 110.0f},
                           (cf_point){.x = 6.0f, .y = 500.0f}, 40.0f),
            40.0f);
}

static void a_block_survives_the_rebuilds_of_the_node_that_scrolls(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name list = cf_intern_text(&ctx.names, "list");
    const cf_node scroller = {.key = list};
    cf_scroll *scroll = NULL;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &scroller);
    scroll = cf_scroll_of(&ctx, list);

    if (scroll != NULL) {
        scroll->offset = 42.0f;
    }

    /* The scroller rebuilds, retires, and is declared again; the name kept
     * asking, so the offset rides through all of it. */
    cf_frame_begin(&ctx);
    cf_diff(&ctx, NULL);
    scroll = cf_scroll_of(&ctx, list);

    CF_CHECK(scroll != NULL, "the block survives the frame that retired its node");

    if (scroll != NULL) {
        scroll->offset = 42.0f;
    }

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &scroller);
    scroll = cf_scroll_of(&ctx, list);

    CF_CHECK(scroll != NULL && scroll->offset == 42.0f,
             "the rebuilt scroller answers the offset it kept");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node scroll\n");

    CF_RUN(a_fresh_block_starts_at_the_start);
    CF_RUN(an_offset_never_leaves_the_scrollable_range);
    CF_RUN(the_thumb_fills_the_track_when_everything_fits);
    CF_RUN(the_thumb_scales_with_the_visible_share_and_sits_by_the_offset);
    CF_RUN(a_very_long_content_keeps_the_thumb_grabbable);
    CF_RUN(a_drag_moves_the_offset_by_the_track_ratio);
    CF_RUN(a_drag_on_a_track_that_cannot_scroll_moves_nothing);
    CF_RUN(a_block_survives_the_rebuilds_of_the_node_that_scrolls);

    return CF_REPORT();
}
