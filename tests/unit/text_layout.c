#include "cf/layout.h"
#include "cf/text.h"
#include "cform_test.h"

/*
 * Ten points per codepoint, twenty per line: the arithmetic the assertions
 * below read. The measure slot is the one seam layout and text share, so
 * this executable links both and asks what a wrapped label contributes.
 */
static cf_text_metrics mono_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float mono_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font;
    (void)codepoint;
    (void)previous;
    return 10.0f;
}

static bool mono_has_glyph(void *font, uint32_t codepoint) {
    (void)font;
    (void)codepoint;
    return true;
}

static const cf_text_ops mono_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = mono_has_glyph};
static const cf_font mono_font = {.handle = NULL, .ops = &mono_ops};
static const cf_font mono_stack[] = {mono_font};

static const char content_text[] = "aa bb cc";

static cf_text_content content = {
    .stack = {.fonts = mono_stack, .count = 1u},
    .text = (cf_string){.chars = content_text, .length = 8u},
};

/* Slot-indexed like the tree itself: the root measures nothing, the leaf
 * is the text. */
static const cf_measure_fn measures[] = {NULL, cf_text_measure};
static void *measure_data[] = {NULL, &content};

static void a_text_node_hugs_the_lines_its_width_leaves(void) {
    cf_style styles[2] = {cf_style_default(), cf_style_default()};
    styles[0].width = CF_PX(25.0f);
    styles[0].direction = CF_DIRECTION_COLUMN;

    const uint32_t child_start[] = {0u, 1u};
    const uint32_t child_slots[] = {1u};
    const cf_tree tree = {.styles = styles,
                          .measures = measures,
                          .measure_data = measure_data,
                          .child_start = child_start,
                          .child_slots = child_slots,
                          .node_count = 2u};

    cf_arena scratch;
    CF_CHECK(cf_arena_init(&scratch, 4096u), "the walk needs an arena");

    cf_rect rects[2];
    cf_layout(&scratch, &tree, 0u, (cf_available){.width = CF_PX(100.0f), .height = CF_AUTO},
              (cf_point){0.0f, 0.0f}, rects);

    CF_NEAR(rects[1].height, 60.0f); /* three lines at the width the column left */
    CF_NEAR(rects[0].height, 60.0f); /* the column hugs what its child wrapped to */

    cf_arena_free(&scratch);
}

int main(void) {
    printf("text layout\n");

    CF_RUN(a_text_node_hugs_the_lines_its_width_leaves);

    return CF_REPORT();
}
