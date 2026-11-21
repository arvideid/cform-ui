#include "cf/text.h"
#include "cform_test.h"

/*
 * A font of ten points per codepoint and twenty per line: every answer the
 * tests read is arithmetic, so a broken rule cannot hide inside a raster.
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

/* AV sits tight: the pair costs four less than its glyphs. */
static float kerned_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font;
    return previous == 'A' && codepoint == 'V' ? 6.0f : 10.0f;
}

static const cf_text_ops kerned_ops = {
    .metrics = mono_metrics, .advance = kerned_advance, .has_glyph = mono_has_glyph};
static const cf_font kerned_font = {.handle = NULL, .ops = &kerned_ops};
static const cf_font kerned_stack[] = {kerned_font};

/* The primary draws latin alone; the fallback answers for the rest at eight
 * points, so a switch shows up in the width. */
static bool latin_has_glyph(void *font, uint32_t codepoint) {
    (void)font;
    return codepoint < 0x80u;
}

static float wide_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font;
    (void)codepoint;
    (void)previous;
    return 8.0f;
}

static const cf_text_ops latin_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = latin_has_glyph};
static const cf_text_ops wide_ops = {
    .metrics = mono_metrics, .advance = wide_advance, .has_glyph = mono_has_glyph};
static const cf_font latin_font = {.handle = NULL, .ops = &latin_ops};
static const cf_font wide_font = {.handle = NULL, .ops = &wide_ops};
static const cf_font fallback_stack[] = {latin_font, wide_font};

static cf_text_content content_of(cf_string text) {
    return (cf_text_content){.stack = {.fonts = mono_stack, .count = 1u}, .text = text};
}

static cf_size wrapped(cf_text_content content, float width) {
    return cf_text_measure(&content, (cf_available){.width = CF_PX(width), .height = CF_AUTO},
                           CF_INTRINSIC_MAX_CONTENT);
}

static cf_size intrinsic(cf_text_content content, cf_intrinsic kind) {
    return cf_text_measure(&content, (cf_available){.width = CF_AUTO, .height = CF_AUTO}, kind);
}

static void max_content_hugs_the_natural_width(void) {
    const cf_size size = intrinsic(content_of(cf_string_of("aa bb")), CF_INTRINSIC_MAX_CONTENT);

    CF_NEAR(size.width, 50.0f);
    CF_NEAR(size.height, 20.0f);
}

static void min_content_is_the_widest_run(void) {
    const cf_size size = intrinsic(content_of(cf_string_of("aa bb ccc")), CF_INTRINSIC_MIN_CONTENT);

    CF_NEAR(size.width, 30.0f);
    CF_NEAR(size.height, 60.0f); /* three runs, one line each at that width */
}

static void a_definite_width_wraps_the_lines_that_fit(void) {
    const cf_size size = wrapped(content_of(cf_string_of("aa bb cc")), 25.0f);

    CF_NEAR(size.width, 20.0f);
    CF_NEAR(size.height, 60.0f); /* aa, bb, cc */
}

static void a_break_at_a_space_leaves_the_space_behind(void) {
    const cf_size size = wrapped(content_of(cf_string_of("aa bb")), 30.0f);

    CF_NEAR(size.width, 20.0f);
    CF_NEAR(size.height, 40.0f); /* aa | bb: the space belongs to no line */
}

static void the_texts_own_breaks_end_lines(void) {
    const cf_size crlf = wrapped(content_of(cf_string_of("aa\r\nbb")), 100.0f);

    CF_NEAR(crlf.width, 20.0f);
    CF_NEAR(crlf.height, 40.0f); /* CR and LF together are one break */

    const cf_size lf = wrapped(content_of(cf_string_of("aa\nbb")), 100.0f);

    CF_NEAR(lf.width, 20.0f);
    CF_NEAR(lf.height, 40.0f);
}

static void a_run_wider_than_the_line_breaks_inside_itself(void) {
    const cf_size size = wrapped(content_of(cf_string_of("aaaaaa")), 30.0f);

    CF_NEAR(size.width, 30.0f);
    CF_NEAR(size.height, 40.0f);
}

static void a_hyphen_and_an_ideograph_end_their_lines(void) {
    const cf_size hyphen = wrapped(content_of(cf_string_of("aa-bb")), 40.0f);

    CF_NEAR(hyphen.width, 30.0f); /* aa- stays whole, bb follows */
    CF_NEAR(hyphen.height, 40.0f);

    const cf_size ideographs = wrapped(content_of(cf_string_of("\xE6\xBC\xA2\xE5\xAD\x97")), 15.0f);

    CF_NEAR(ideographs.width, 10.0f); /* a break may follow each one */
    CF_NEAR(ideographs.height, 40.0f);
}

static void the_cap_closes_the_last_line_with_the_ellipsis(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc dd"));
    content.max_lines = (cf_opt_u32){.present = true, .value = 2u};
    content.ellipsis = true;

    const cf_size size = wrapped(content, 25.0f);

    CF_NEAR(size.width, 20.0f); /* one glyph beside the ellipsis */
    CF_NEAR(size.height, 40.0f);
}

static void capped_lines_without_the_ellipsis_just_stop(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc dd"));
    content.max_lines = (cf_opt_u32){.present = true, .value = 2u};

    const cf_size size = wrapped(content, 25.0f);

    CF_NEAR(size.width, 20.0f);
    CF_NEAR(size.height, 40.0f);
}

static void a_text_fully_consumed_wears_no_ellipsis(void) {
    cf_text_content content = content_of(cf_string_of("aa bb"));
    content.max_lines = (cf_opt_u32){.present = true, .value = 2u};
    content.ellipsis = true;

    const cf_size size = wrapped(content, 25.0f);

    CF_NEAR(size.width, 20.0f);
    CF_NEAR(size.height, 40.0f);
}

static void spaces_at_line_edges_are_not_content(void) {
    const cf_size padded = wrapped(content_of(cf_string_of("  aa  ")), 100.0f);

    CF_NEAR(padded.width, 20.0f);
    CF_NEAR(padded.height, 20.0f);

    const cf_size blank = wrapped(content_of(cf_string_of("   ")), 100.0f);

    CF_NEAR(blank.width, 0.0f);
    CF_NEAR(blank.height, 0.0f);

    const cf_size empty = wrapped(content_of(cf_string_of("")), 100.0f);

    CF_NEAR(empty.width, 0.0f);
    CF_NEAR(empty.height, 0.0f);
}

static void a_fallback_takes_the_glyphs_its_primary_lacks(void) {
    const cf_text_content content = {
        .stack = {.fonts = fallback_stack, .count = 2u},
        .text = cf_string_of("ab\xC3\xA9"
                             "cd"),
    };
    const cf_size size = intrinsic(content, CF_INTRINSIC_MAX_CONTENT);

    CF_NEAR(size.width,
            48.0f); /* a, b and cd from the primary, the accented one from the fallback */
    CF_NEAR(size.height, 20.0f);
}

static void kerning_rides_along_a_run(void) {
    const cf_text_content content = {
        .stack = {.fonts = kerned_stack, .count = 1u},
        .text = cf_string_of("AVA"),
    };
    const cf_size size = intrinsic(content, CF_INTRINSIC_MAX_CONTENT);

    CF_NEAR(size.width, 26.0f); /* A 10, V 6 after the A, A 10 */
}

static void a_zero_line_cap_leaves_nothing(void) {
    cf_text_content content = content_of(cf_string_of("aa"));
    content.max_lines = (cf_opt_u32){.present = true, .value = 0u};

    const cf_size size = wrapped(content, 100.0f);

    CF_NEAR(size.width, 0.0f);
    CF_NEAR(size.height, 0.0f);
}

static void a_walk_without_a_font_measures_nothing(void) {
    const cf_text_content content = {.text = cf_string_of("aa")};
    const cf_size size = wrapped(content, 100.0f);

    CF_NEAR(size.width, 0.0f);
    CF_NEAR(size.height, 0.0f);
}

int main(void) {
    printf("text measure\n");

    CF_RUN(max_content_hugs_the_natural_width);
    CF_RUN(min_content_is_the_widest_run);
    CF_RUN(a_definite_width_wraps_the_lines_that_fit);
    CF_RUN(a_break_at_a_space_leaves_the_space_behind);
    CF_RUN(the_texts_own_breaks_end_lines);
    CF_RUN(a_run_wider_than_the_line_breaks_inside_itself);
    CF_RUN(a_hyphen_and_an_ideograph_end_their_lines);
    CF_RUN(the_cap_closes_the_last_line_with_the_ellipsis);
    CF_RUN(capped_lines_without_the_ellipsis_just_stop);
    CF_RUN(a_text_fully_consumed_wears_no_ellipsis);
    CF_RUN(spaces_at_line_edges_are_not_content);
    CF_RUN(a_fallback_takes_the_glyphs_its_primary_lacks);
    CF_RUN(kerning_rides_along_a_run);
    CF_RUN(a_zero_line_cap_leaves_nothing);
    CF_RUN(a_walk_without_a_font_measures_nothing);

    return CF_REPORT();
}
