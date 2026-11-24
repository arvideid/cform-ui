#include "cf/style.h"
#include "cform_test.h"

static void percent_resolves_against_the_reference_it_is_given(void) {
    CF_NEAR(cf_length_resolve(CF_PERCENT(25.0f), 200.0f), 50.0f);
    CF_NEAR(cf_length_resolve(CF_PERCENT(25.0f), 100.0f), 25.0f);
    CF_NEAR(cf_length_resolve(CF_PX(12.5f), 4096.0f), 12.5f);
}

static void auto_and_fr_hold_no_length(void) {
    CF_CHECK(cf_length_is_auto(CF_AUTO), "auto is the length that asks for nothing");
    CF_CHECK(!cf_length_is_auto(CF_PX(0.0f)), "zero pixels is not auto");
    CF_NEAR(cf_length_resolve(CF_AUTO, 300.0f), 0.0f);
    CF_NEAR(cf_length_resolve(CF_FR(1.0f), 300.0f), 0.0f);
}

static void clamp_lets_the_minimum_have_the_last_word(void) {
    CF_NEAR(cf_size_clamp(100.0f, CF_PX(120.0f), CF_PX(80.0f), CF_AUTO), 120.0f);
    CF_NEAR(cf_size_clamp(100.0f, CF_PX(20.0f), CF_PX(80.0f), CF_AUTO), 80.0f);
    CF_NEAR(cf_size_clamp(100.0f, CF_AUTO, CF_AUTO, CF_AUTO), 100.0f);
}

static void clamp_bounds_are_lengths_too(void) {
    CF_NEAR(cf_size_clamp(100.0f, CF_AUTO, CF_PERCENT(50.0f), CF_PX(120.0f)), 60.0f);
    CF_NEAR(cf_size_clamp(10.0f, CF_PERCENT(50.0f), CF_AUTO, CF_PX(120.0f)), 60.0f);
    /* Against an extent nobody has sized yet a bound bounds nothing, so the
     * value survives instead of collapsing to zero. */
    CF_NEAR(cf_size_clamp(100.0f, CF_PERCENT(50.0f), CF_AUTO, CF_AUTO), 100.0f);
}

static void clamp_reads_a_negative_extent_as_zero(void) {
    CF_NEAR(cf_size_clamp(-24.0f, CF_AUTO, CF_AUTO, CF_AUTO), 0.0f);
    /* The minimum is still a floor the caller asked for, so it wins over the
     * zero floor rather than being clamped away. */
    CF_NEAR(cf_size_clamp(-24.0f, CF_PX(8.0f), CF_AUTO, CF_AUTO), 8.0f);
}

static void a_declaration_holds_a_length_only_when_it_can_be_measured(void) {
    float value = -1.0f;

    CF_CHECK(cf_length_of(CF_PX(8.0f), CF_AUTO, &value), "pixels are always a length");
    CF_NEAR(value, 8.0f);

    value = -1.0f;
    CF_CHECK(cf_length_of(CF_PERCENT(25.0f), CF_PX(200.0f), &value),
             "a percentage of a sized extent is a length");
    CF_NEAR(value, 50.0f);

    value = 0.0f;
    CF_CHECK(!cf_length_of(CF_PERCENT(25.0f), CF_AUTO, &value),
             "a percentage of an extent still being measured is not");
    CF_NEAR(value, 0.0f);

    CF_CHECK(!cf_length_of(CF_PERCENT(25.0f), CF_PERCENT(200.0f), &value),
             "a percentage of a percentage describes no box");
    CF_CHECK(!cf_length_of(CF_AUTO, CF_PX(200.0f), &value), "auto asks for nothing");
    CF_CHECK(!cf_length_of(CF_FR(1.0f), CF_PX(200.0f), &value),
             "a share of leftover space is not a measurement");
}

static void a_box_resolves_every_side_against_the_inline_size(void) {
    const cf_box padding = {
        .left = CF_PX(4.0f),
        .top = CF_PERCENT(10.0f),
        .right = CF_AUTO,
        .bottom = CF_PERCENT(50.0f),
    };

    const cf_edges resolved = cf_box_resolve(padding, 100.0f);

    CF_NEAR(resolved.left, 4.0f);
    /* Both percentages read the container's width, never the axis they sit on. */
    CF_NEAR(resolved.top, 10.0f);
    CF_NEAR(resolved.right, 0.0f);
    CF_NEAR(resolved.bottom, 50.0f);
}

static void negative_padding_and_border_read_as_zero(void) {
    /* A negative inset would put the content box outside the frame that paints
     * it. A margin keeps its sign, and reading it is the caller's job. */
    const cf_box shrunk = {
        .left = CF_PX(-8.0f),
        .top = CF_PERCENT(-50.0f),
        .right = CF_PX(3.0f),
        .bottom = CF_AUTO,
    };

    const cf_edges resolved = cf_box_resolve(shrunk, 100.0f);

    CF_NEAR(resolved.left, 0.0f);
    CF_NEAR(resolved.top, 0.0f);
    CF_NEAR(resolved.right, 3.0f);
    CF_NEAR(resolved.bottom, 0.0f);
}

static void direction_separates_the_axis_from_the_packing_edge(void) {
    CF_CHECK(cf_direction_is_row(CF_DIRECTION_ROW), "row packs horizontally");
    CF_CHECK(cf_direction_is_row(CF_DIRECTION_ROW_REVERSE), "row-reverse still packs horizontally");
    CF_CHECK(!cf_direction_is_row(CF_DIRECTION_COLUMN), "column packs vertically");
    CF_CHECK(!cf_direction_is_row(CF_DIRECTION_COLUMN_REVERSE), "column-reverse packs vertically");

    CF_CHECK(!cf_direction_is_reverse(CF_DIRECTION_ROW), "row packs from the leading edge");
    CF_CHECK(cf_direction_is_reverse(CF_DIRECTION_ROW_REVERSE),
             "row-reverse packs from the far edge");
    CF_CHECK(!cf_direction_is_reverse(CF_DIRECTION_COLUMN), "column packs from the leading edge");
    CF_CHECK(cf_direction_is_reverse(CF_DIRECTION_COLUMN_REVERSE),
             "column-reverse packs from the far edge");
}

static void default_holds_the_css_initial_values_zero_cannot_express(void) {
    const cf_style unset = (cf_style){0};
    const cf_style style = cf_style_default();

    CF_NEAR(style.flex_shrink, 1.0f);
    CF_NEAR(unset.flex_shrink, 0.0f);
    /* The zero margin is auto, which would let an untouched item absorb every
     * pixel of leftover space beside it; CSS's initial margin is zero. */
    CF_CHECK(cf_length_is_auto(unset.margin.left), "the zero margin asks for space");
    CF_CHECK(!cf_length_is_auto(style.margin.left), "the default margin is a length");
    CF_NEAR(cf_length_resolve(style.margin.left, 300.0f), 0.0f);
    CF_NEAR(cf_length_resolve(style.margin.bottom, 300.0f), 0.0f);
    CF_CHECK(style.flex_grow == unset.flex_grow, "nothing grows until asked");
    CF_CHECK(style.direction == CF_DIRECTION_ROW, "the zero direction is a row");
    CF_CHECK(style.wrap == CF_NO_WRAP, "the zero style lays one line");
    CF_CHECK(style.align_items == CF_ALIGN_AUTO, "items inherit the container's alignment");
    CF_CHECK(cf_length_is_auto(style.width) && cf_length_is_auto(style.height),
             "the zero size hugs its content on both axes");
}

static void repeat_expands_a_pattern_whole_or_not_at_all(void) {
    const cf_track pattern[] = {
        {CF_PX(10.0f), CF_PX(10.0f)},
        {CF_AUTO, CF_FR(1.0f)},
    };
    cf_track tracks[5];

    const uint32_t count = cf_tracks_repeat(tracks, 5u, pattern, 2u, 3u);

    /* The fifth slot cannot hold a third pattern, so it holds none of it, and
     * what comes back is tracks rather than repetitions. */
    CF_CHECK(count == 4u, "a pattern that does not fit is left out");
    CF_NEAR(tracks[0].min.value, 10.0f);
    CF_CHECK(tracks[3].min.kind == CF_LENGTH_AUTO && tracks[3].max.kind == CF_LENGTH_FR,
             "the pattern repeats in order");

    CF_CHECK(cf_tracks_repeat(tracks, 4u, pattern, 0u, 3u) == 0u,
             "an empty pattern repeats to nothing");
    CF_CHECK(cf_tracks_repeat(NULL, 4u, pattern, 2u, 3u) == 0u, "no array writes nothing");
}

/*
 * The marks an editor paints travel through the style, so a widget that cannot
 * know its own focus at build time still gets a caret only while it holds the
 * keyboard. A state that says nothing about a mark leaves whatever the
 * declaration stated, and the later enum bits overwrite the earlier ones.
 */
static void a_variant_hands_out_only_the_marks_it_states(void) {
    const cf_color ink = {10u, 10u, 10u, 255u};
    const cf_color band = {191u, 219u, 254u, 255u};
    const cf_variants field = {
        .base = {0},
        .hovered = {.background = {.present = true, .value = {250u, 250u, 250u, 255u}}},
        .focused = {.caret = {.present = true, .value = ink},
                    .selection = {.present = true, .value = band}},
    };

    const cf_style resting = cf_style_varied(cf_style_default(), &field, CF_INTERACT_NONE);

    CF_CHECK(!resting.caret.present && !resting.selection.present,
             "a box at rest shows no editing marks");

    const cf_style hovered = cf_style_varied(cf_style_default(), &field, CF_INTERACT_HOVERED);

    CF_CHECK(hovered.background.present, "the state that speaks about a tone is applied");
    CF_CHECK(!hovered.caret.present, "and the marks it says nothing about stay unstated");

    const cf_style focused = cf_style_varied(cf_style_default(), &field, CF_INTERACT_FOCUSED);

    CF_CHECK(focused.caret.present && focused.caret.value.red == ink.red,
             "the focus brings the caret with it");
    CF_CHECK(focused.selection.present && focused.selection.value.red == band.red,
             "and the tone the selection is banded in");

    /* A declaration that stated its own mark keeps it across a state silent
     * about marks, and loses it only to a state that names one. */
    cf_style declared = cf_style_default();
    declared.caret = (cf_opt_color){.present = true, .value = {200u, 0u, 0u, 255u}};

    const cf_style pressed = cf_style_varied(declared, &field, CF_INTERACT_PRESSED);

    CF_CHECK(pressed.caret.present && pressed.caret.value.red == 200u,
             "a state silent about the caret leaves the declared one");

    const cf_variants later = {.focused = {.caret = {.present = true, .value = ink}},
                               .disabled = {.caret = {.present = true, .value = band}}};
    const cf_style both =
        cf_style_varied(declared, &later, CF_INTERACT_FOCUSED | CF_INTERACT_DISABLED);

    CF_CHECK(both.caret.value.red == band.red, "the later bit has the last word");
}

int main(void) {
    printf("style\n");
    CF_RUN(percent_resolves_against_the_reference_it_is_given);
    CF_RUN(auto_and_fr_hold_no_length);
    CF_RUN(clamp_lets_the_minimum_have_the_last_word);
    CF_RUN(clamp_bounds_are_lengths_too);
    CF_RUN(clamp_reads_a_negative_extent_as_zero);
    CF_RUN(a_declaration_holds_a_length_only_when_it_can_be_measured);
    CF_RUN(a_box_resolves_every_side_against_the_inline_size);
    CF_RUN(negative_padding_and_border_read_as_zero);
    CF_RUN(direction_separates_the_axis_from_the_packing_edge);
    CF_RUN(default_holds_the_css_initial_values_zero_cannot_express);
    CF_RUN(repeat_expands_a_pattern_whole_or_not_at_all);
    CF_RUN(a_variant_hands_out_only_the_marks_it_states);
    return CF_REPORT();
}
