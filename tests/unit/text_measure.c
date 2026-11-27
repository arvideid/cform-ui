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
static const cf_font wide_stack[] = {wide_font};

/* A face that draws the capital A and nothing else. Stacked over the kerned one it
 * splits an AV pair across an edge, which is the only way to ask whether a font
 * may charge kerning for a letter another font drew. */
static bool only_a_has_glyph(void *font, uint32_t codepoint) {
    (void)font;
    return codepoint == 'A';
}

static const cf_text_ops only_a_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = only_a_has_glyph};
static const cf_font only_a_font = {.handle = NULL, .ops = &only_a_ops};
static const cf_font kerning_stack[] = {only_a_font, kerned_font};

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

static void lines_carry_the_bytes_and_widths_the_walk_laid(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc dd ee"));
    cf_text_line lines[8u];

    const uint32_t count = cf_text_lines(&content, 30.0f, lines, 8u);

    CF_CHECK(count == 5u, "five runs, one line each at that width");
    CF_CHECK(lines[0].text.length == 2u, "a line's bytes are its own");
    CF_CHECK(lines[0].text.chars[0] == 'a' && lines[0].text.chars[1] == 'a', "the first run first");
    CF_NEAR(lines[3].width, 20.0f);
    CF_CHECK(lines[3].text.chars[0] == 'd', "the fourth run is dd");
    CF_CHECK(lines[4].text.chars[0] == 'e', "the last run is ee");
    CF_CHECK(lines[0].ellipsis == false, "an uncapped walk closes nothing");
}

static void the_lines_the_walk_laid_close_with_the_ellipsis(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc dd ee ff gg hh"));
    content.max_lines = (cf_opt_u32){.present = true, .value = 2u};
    content.ellipsis = true;

    cf_text_line lines[8u];
    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 8u);

    CF_CHECK(count == 2u, "the cap holds the line count");
    CF_CHECK(lines[0].ellipsis == false, "only the last line closes");
    CF_CHECK(lines[1].ellipsis, "the capped walk closes its last line");
    CF_NEAR(lines[1].width, 90.0f); /* dd ee ff, the ellipsis inside its own width */
    CF_CHECK(lines[1].text.length == 8u, "the closed line ends at the glyphs it kept");
    CF_CHECK(lines[1].text.chars[7] == 'f', "dd ee ff");

    const cf_size size = wrapped(content, 100.0f);
    CF_NEAR(size.width, 90.0f);
    CF_NEAR(size.height, 40.0f);
}

static void a_capacity_smaller_than_the_walk_only_fills_what_fits(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc"));
    cf_text_line lines[1u];

    const uint32_t count = cf_text_lines(&content, 30.0f, lines, 1u);

    CF_CHECK(count == 3u, "the count is what the walk laid, not what it filled");
    CF_CHECK(lines[0].text.length == 2u && lines[0].text.chars[1] == 'a', "the first line filled");
}

static void a_forced_break_ends_its_line_before_the_break(void) {
    cf_text_content content = content_of(cf_string_of("aa\nbb"));
    cf_text_line lines[4u];

    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 4u);

    CF_CHECK(count == 2u, "the text's own break splits the walk");
    CF_CHECK(lines[0].text.length == 2u, "the break byte belongs to no line");
    CF_NEAR(lines[0].width, 20.0f);
    CF_CHECK(lines[1].text.chars[0] == 'b', "the next line follows the break");
}

/* Only the text's own breaks reach a walk that refuses to wrap. */
static void a_walk_that_refuses_to_wrap_lays_one_line_of_all_it_holds(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc"));
    content.nowrap = true;
    cf_text_line lines[4u];

    const cf_size offered = wrapped(content, 25.0f);

    CF_NEAR(offered.width, 80.0f); /* the whole value, though the box is a quarter as wide */
    CF_NEAR(offered.height, 20.0f);

    const cf_size tight = intrinsic(content, CF_INTRINSIC_MIN_CONTENT);

    CF_NEAR(tight.width, 80.0f); /* nothing breaks it, so nothing bounds it tighter */
    CF_NEAR(tight.height, 20.0f);

    CF_CHECK(cf_text_lines(&content, 25.0f, lines, 4u) == 1u, "one line holds all eight glyphs");

    cf_text_content forced = content_of(cf_string_of("aa\nbb"));
    forced.nowrap = true;
    const cf_size own_breaks = wrapped(forced, 25.0f);

    CF_NEAR(own_breaks.width, 20.0f);
    CF_NEAR(own_breaks.height, 40.0f); /* a break the text states still ends a line */
}

/* The caret's distance and the click's offset answer each other. */
static void an_offset_answers_the_distance_and_the_distance_the_offset(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc"));
    cf_text_line lines[4u];
    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 4u);

    CF_CHECK(count == 1u, "the whole value lies on one line at that width");

    CF_NEAR(cf_text_caret_x(&content, &lines[0], 0u), 0.0f);
    CF_NEAR(cf_text_caret_x(&content, &lines[0], 3u), 30.0f);
    CF_NEAR(cf_text_caret_x(&content, &lines[0], 99u), 80.0f); /* clamped to the end of the ink */

    CF_CHECK(cf_text_offset_at(&content, &lines[0], 4.0f) == 0u,
             "the left half of a glyph is before it");
    CF_CHECK(cf_text_offset_at(&content, &lines[0], 5.0f) == 1u, "the right half is after it");
    CF_CHECK(cf_text_offset_at(&content, &lines[0], 25.0f) == 3u,
             "a click between runs lands after the word");
    CF_CHECK(cf_text_offset_at(&content, &lines[0], 1000.0f) == 8u, "past the ink is the end");

    /* A line the walk never laid borrows bytes from somewhere else: the answer
     * is nothing rather than an offset far past the content's own. */
    const cf_text_line foreign = {
        .text = {content.text.chars + 9u, 1u}, .width = 10.0f, .ellipsis = false};

    CF_NEAR(cf_text_caret_x(&content, &foreign, 9u), 0.0f);
    CF_CHECK(cf_text_offset_at(&content, &foreign, 5.0f) == 0u,
             "a line outside the content is refused");
}

/* Where the caret falls among the lines a walk laid. */
static void an_offset_names_the_line_that_holds_it(void) {
    cf_text_content content = content_of(cf_string_of("aa\nbb"));
    cf_text_line lines[4u];
    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 4u);

    CF_CHECK(count == 2u, "the text's break lays two lines");
    CF_CHECK(cf_text_line_of(&content, lines, count, 0u) == 0u, "its first byte is on the first");
    CF_CHECK(cf_text_line_of(&content, lines, count, 2u) == 0u,
             "a byte before the break still sits on the first");
    CF_CHECK(cf_text_line_of(&content, lines, count, 3u) == 1u,
             "the byte after it opens the second");
    CF_CHECK(cf_text_line_of(&content, lines, count, 99u) == 1u, "past every line is the last one");
}

/* A window slides a walk only as far as it must to hold the caret. */
static void a_box_slides_along_its_value_only_to_hold_the_caret(void) {
    cf_text_content content = content_of(cf_string_of("aa bb cc"));
    cf_text_line lines[4u];
    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 4u);

    CF_NEAR(cf_text_scroll(&content, lines, count, (cf_size){30.0f, 20.0f}).x, 0.0f);

    content.caret = (cf_opt_u32){.present = true, .value = 3u};
    /* A caret the window already holds moves nothing. */
    CF_NEAR(cf_text_scroll(&content, lines, count, (cf_size){30.0f, 20.0f}).x, 0.0f);

    content.caret = (cf_opt_u32){.present = true, .value = 5u};
    const cf_point along = cf_text_scroll(&content, lines, count, (cf_size){30.0f, 20.0f});

    CF_NEAR(along.x, 20.0f); /* the caret sits at 50 and the window shows 30 */
    CF_NEAR(along.y, 0.0f);

    cf_text_content tall = content_of(cf_string_of("aa\nbb"));
    tall.caret = (cf_opt_u32){.present = true, .value = 4u};
    cf_text_line second[4u];
    const uint32_t rows = cf_text_lines(&tall, 100.0f, second, 4u);
    const cf_point down = cf_text_scroll(&tall, second, rows, (cf_size){30.0f, 20.0f});

    CF_NEAR(down.x, 0.0f);
    CF_NEAR(down.y, 20.0f); /* the second line is one row below a window that holds one */
}

/*
 * A conforming decoder refuses an overlong form, a surrogate and a value past
 * U+10FFFF, consuming only the lead byte. The mono walk then charges each byte
 * the refusal leaves behind as its own codepoint, so a malformed sequence costs
 * the walk more than the single glyph it stands for - and a well-formed one
 * still costs exactly one.
 */
static void malformed_sequences_walk_byte_by_byte(void) {
    const cf_size overlong =
        intrinsic(content_of(cf_string_of("\xC0\xAF")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(overlong.width, 20.0f); /* C0 AF is the overlong spelling of '/': two bytes walked */

    const cf_size surrogate =
        intrinsic(content_of(cf_string_of("\xED\xA0\x80")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(surrogate.width, 30.0f); /* ED A0 80 is the non-scalar U+D800: three bytes walked */

    const cf_size past_max =
        intrinsic(content_of(cf_string_of("\xF5\x80\x80\x80")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(past_max.width, 40.0f); /* F5 80 80 80 is the first value past U+10FFFF */
}

/* The shortest well-formed spelling of a value is one codepoint, so hardening
 * the decoder must not split the multibyte range it already accepted. */
static void well_formed_multibyte_stays_whole(void) {
    const cf_size two_byte =
        intrinsic(content_of(cf_string_of("\xC3\xA9")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(two_byte.width, 10.0f); /* C3 A9 is U+00E9 */

    const cf_size three_byte =
        intrinsic(content_of(cf_string_of("\xE0\xA0\x80")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(three_byte.width, 10.0f); /* E0 A0 80 is U+0800, the shortest three-byte form */

    const cf_size four_byte =
        intrinsic(content_of(cf_string_of("\xF0\x9F\x98\x80")), CF_INTRINSIC_MAX_CONTENT);
    CF_NEAR(four_byte.width, 10.0f); /* F0 9F 98 80 is U+1F600 */
}

/* The runs a line is drawn in, counted even past what the sink can hold - the
 * same over-count the walk that fills lines reports. */
typedef struct run_sink {
    cf_text_run runs[8u];
    uint32_t count;
} run_sink;

static void collect_runs(void *user_data, const cf_text_run *run) {
    run_sink *sink = user_data;

    sink->count++;
    if (sink->count <= 8u) {
        sink->runs[sink->count - 1u] = *run;
    }
}

/*
 * A line the fallback split is drawn by more than one font. Its runs carry the
 * bytes each font drew and the pen distance it stands at, so the widths add up
 * to the line's own and a painter never has to re-derive the measure.
 */
static void the_runs_of_a_line_split_where_the_font_changes(void) {
    /* "ab", the ideograph U+65E5, "c". The ideograph closes the literal it
     * starts, or its last byte would swallow the following letter. */
    const cf_text_content content = {
        .stack = {.fonts = fallback_stack, .count = 2u},
        .text = cf_string_of("ab\xe6\x97\xa5"
                             "c"),
    };
    cf_text_line lines[4u];
    const uint32_t count = cf_text_lines(&content, 100.0f, lines, 4u);
    run_sink sink = {0};

    CF_CHECK(count == 1u, "the whole line fits at that width");

    cf_text_each_run(&content, &lines[0], collect_runs, &sink);

    CF_CHECK(sink.count == 3u, "latin, the ideograph, latin again");
    CF_CHECK(sink.runs[0].text.length == 2u, "the primary keeps the letters it draws");
    CF_NEAR(sink.runs[0].x, 0.0f);
    CF_NEAR(sink.runs[0].width, 20.0f);
    CF_CHECK(sink.runs[0].font.ops == &latin_ops, "and names the font that measured them");
    CF_CHECK(sink.runs[1].text.length == 3u, "the fallback takes the three bytes it drew");
    CF_NEAR(sink.runs[1].x, 20.0f);
    CF_NEAR(sink.runs[1].width, 8.0f);
    CF_CHECK(sink.runs[1].font.ops == &wide_ops, "the face that has the glyph");
    CF_NEAR(sink.runs[2].x, 28.0f);
    CF_NEAR(sink.runs[2].width, 10.0f);
    CF_CHECK(sink.runs[2].font.ops == &latin_ops, "the primary again once the ideograph is past");
    CF_NEAR(sink.runs[0].width + sink.runs[1].width + sink.runs[2].width, lines[0].width);
}

/*
 * Kerning belongs to the font that drew both letters of a pair, so a run edge
 * costs the pair its kern. The walk that measured the line already applies that
 * rule, and the runs have to agree with it or the drawn line would be wider
 * than the box that was sized for it.
 */
static void a_run_edge_gives_way_to_the_kerning_a_pair_would_have_had(void) {
    cf_text_line lines[4u];
    const cf_text_content whole = {
        .stack = {.fonts = kerned_stack, .count = 1u},
        .text = cf_string_of("AV"),
    };
    run_sink kept = {0};

    CF_CHECK(cf_text_lines(&whole, 100.0f, lines, 4u) == 1u, "the pair lies on one line");

    cf_text_each_run(&whole, &lines[0], collect_runs, &kept);

    CF_CHECK(kept.count == 1u, "one font drew both letters, so one run carries them");
    CF_NEAR(kept.runs[0].width, 16.0f); /* A 10, V 6 after the A */

    run_sink split = {0};
    const cf_text_content straddled = {
        .stack = {.fonts = kerning_stack, .count = 2u},
        .text = cf_string_of("AV"),
    };

    CF_CHECK(cf_text_lines(&straddled, 100.0f, lines, 4u) == 1u, "and so does the pair apart");

    cf_text_each_run(&straddled, &lines[0], collect_runs, &split);

    CF_CHECK(split.count == 2u, "the face that drew the A has no V, so the pair splits");
    CF_NEAR(split.runs[0].width, 10.0f);
    CF_NEAR(split.runs[1].width, 10.0f); /* the V sits loose: its font never drew the A */
    CF_NEAR(split.runs[0].width + split.runs[1].width, lines[0].width);
}

/*
 * The sign that closes a capped line belongs to whichever font the stack sends
 * it to. Where that font is the one already drawing the line's last letters,
 * the cap rides in the same run, so a page that never falls back is still one
 * command per line. Where it is another face, the sign stands in a run of no
 * bytes, because the glyph is not in the text and the room it measures is.
 */
static void the_sign_that_closes_a_line_belongs_to_the_font_that_draws_it(void) {
    cf_text_line lines[4u];
    const cf_text_content same_font = {
        .stack = {.fonts = wide_stack, .count = 1u},
        .text = cf_string_of("abcd"),
        .max_lines = (cf_opt_u32){.present = true, .value = 1u},
        .ellipsis = true,
    };
    run_sink joined = {0};

    CF_CHECK(cf_text_lines(&same_font, 30.0f, lines, 4u) == 1u, "the cap holds one line");

    cf_text_each_run(&same_font, &lines[0], collect_runs, &joined);

    CF_CHECK(joined.count == 1u, "the face that drew the letters draws the sign too");
    CF_CHECK(joined.runs[0].text.length == 2u, "the bytes the walk kept");
    CF_NEAR(joined.runs[0].width, 24.0f); /* ab at 8 each, the cap inside the same run */
    CF_CHECK(joined.runs[0].ellipsis, "and the run says it closes the line");
    CF_NEAR(joined.runs[0].width, lines[0].width);

    run_sink apart = {0};
    const cf_text_content third_font = {
        .stack = {.fonts = fallback_stack, .count = 2u},
        .text = cf_string_of("abcd"),
        .max_lines = (cf_opt_u32){.present = true, .value = 1u},
        .ellipsis = true,
    };

    CF_CHECK(cf_text_lines(&third_font, 30.0f, lines, 4u) == 1u, "the same cap on the same bytes");

    cf_text_each_run(&third_font, &lines[0], collect_runs, &apart);

    CF_CHECK(apart.count == 2u, "a sign the primary cannot draw opens a run of its own");
    CF_CHECK(apart.runs[0].font.ops == &latin_ops, "the letters keep their font");
    CF_NEAR(apart.runs[0].width, 20.0f);
    CF_CHECK(apart.runs[0].ellipsis == false, "the letters' run closes nothing by itself");
    CF_NEAR(apart.runs[1].x, 20.0f);
    CF_NEAR(apart.runs[1].width, 8.0f);
    CF_CHECK(apart.runs[1].text.length == 0u, "the sign's run carries no bytes");
    CF_CHECK(apart.runs[1].font.ops == &wide_ops, "the face the stack sends the sign to");
    CF_CHECK(apart.runs[1].ellipsis, "and it is the run that closes the line");
    CF_NEAR(apart.runs[0].width + apart.runs[1].width, lines[0].width);
}

/*
 * A run is read out of the content that laid the line, so anything that could
 * not have come from that walk visits nothing - a painter handed a stale line
 * draws no garbage, exactly as a layout handed one gets no rect.
 */
static void a_line_this_content_never_laid_opens_no_run(void) {
    const cf_text_content content = {
        .stack = {.fonts = mono_stack, .count = 1u},
        .text = cf_string_of("aa"),
    };
    const cf_text_content fontless = {.text = content.text};
    const cf_text_line whole = {.text = content.text, .width = 20.0f, .ellipsis = false};
    const cf_text_line foreign = {
        .text = {content.text.chars + 9u, 1u}, .width = 10.0f, .ellipsis = false};
    const cf_text_line blank = {.text = {content.text.chars, 0u}, .width = 0.0f, .ellipsis = false};
    run_sink sink = {0};

    cf_text_each_run(NULL, &whole, collect_runs, &sink);
    CF_CHECK(sink.count == 0u, "no content visits nothing");

    cf_text_each_run(&fontless, &whole, collect_runs, &sink);
    CF_CHECK(sink.count == 0u, "a stack that answers for no glyph draws no run");

    cf_text_each_run(&content, &foreign, collect_runs, &sink);
    CF_CHECK(sink.count == 0u, "bytes outside the content are refused");

    cf_text_each_run(&content, NULL, collect_runs, &sink);
    CF_CHECK(sink.count == 0u, "no line visits nothing");

    cf_text_each_run(&content, &blank, collect_runs, &sink);
    CF_CHECK(sink.count == 0u, "a line with no bytes has no run to draw");

    cf_text_each_run(&content, &whole, NULL, &sink);
    CF_CHECK(sink.count == 0u, "a walk with no visitor is asked nothing");
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
    CF_RUN(lines_carry_the_bytes_and_widths_the_walk_laid);
    CF_RUN(the_lines_the_walk_laid_close_with_the_ellipsis);
    CF_RUN(a_capacity_smaller_than_the_walk_only_fills_what_fits);
    CF_RUN(a_forced_break_ends_its_line_before_the_break);
    CF_RUN(a_walk_that_refuses_to_wrap_lays_one_line_of_all_it_holds);
    CF_RUN(an_offset_answers_the_distance_and_the_distance_the_offset);
    CF_RUN(an_offset_names_the_line_that_holds_it);
    CF_RUN(a_box_slides_along_its_value_only_to_hold_the_caret);
    CF_RUN(malformed_sequences_walk_byte_by_byte);
    CF_RUN(well_formed_multibyte_stays_whole);
    CF_RUN(the_runs_of_a_line_split_where_the_font_changes);
    CF_RUN(a_run_edge_gives_way_to_the_kerning_a_pair_would_have_had);
    CF_RUN(the_sign_that_closes_a_line_belongs_to_the_font_that_draws_it);
    CF_RUN(a_line_this_content_never_laid_opens_no_run);

    return CF_REPORT();
}
