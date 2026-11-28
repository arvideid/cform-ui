#include "cf/node.h"

#include <math.h>

#include "cform_test.h"

/* The variant system and the resolver are the frame's styling half: the pure
 * cases run on styles alone, the frame cases drive declarations through one
 * context and read the view the resolver leaves. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static cf_node node_of(cf_name key) {
    return (cf_node){.key = key};
}

/* A node the theme styles: a definite box so the frame cases know its rect. */
static cf_node styled_of(cf_name key, cf_role role) {
    cf_node node = node_of(key);
    node.role = role;
    node.style = cf_style_default();
    node.style.width = CF_PX(100.0f);
    node.style.height = CF_PX(40.0f);
    node.style.background = (cf_opt_color){.present = true, .value = CF_COLOR(20, 20, 20)};

    return node;
}

static bool color_is(cf_opt_color color, uint8_t red) {
    return color.present && color.value.red == red && color.value.green == red &&
           color.value.blue == red;
}

/* The style the resolver left a slot, read the way a caller outside src/node has to: from
 * the view this frame answered with, and no style at all where there is no view. */
static cf_style frame_style(const cf_ctx *ctx, uint32_t slot) {
    const cf_tree *view = cf_frame_tree(ctx);

    return view != NULL && slot < view->node_count ? view->styles[slot] : (cf_style){0};
}

static void a_variant_states_only_its_differences(void) {
    const cf_variants variants = {
        .hovered = {.background = {.present = true, .value = CF_COLOR(40, 40, 40)}}};

    cf_style base = cf_style_default();
    base.background = (cf_opt_color){.present = true, .value = CF_COLOR(20, 20, 20)};
    base.padding = (cf_box){
        .left = CF_PX(12.0f), .top = CF_PX(12.0f), .right = CF_PX(12.0f), .bottom = CF_PX(12.0f)};

    const cf_style style = cf_style_varied(base, &variants, (uint32_t)CF_INTERACT_HOVERED);
    CF_CHECK(color_is(style.background, 40), "the state's fill replaces the resting one");
    CF_CHECK(style.padding.left.kind == CF_LENGTH_PX && style.padding.left.value == 12.0f,
             "a field the variant leaves unset inherits the declared box");
}

static void later_states_overwrite_earlier_ones(void) {
    const cf_variants variants = {
        .hovered = {.background = {.present = true, .value = CF_COLOR(40, 40, 40)}},
        .pressed = {.background = {.present = true, .value = CF_COLOR(60, 60, 60)}},
        .focused = {.background = {.present = true, .value = CF_COLOR(80, 80, 80)}},
        .disabled = {.background = {.present = true, .value = CF_COLOR(100, 100, 100)}}};

    const cf_style hovered_pressed =
        cf_style_varied(cf_style_default(), &variants,
                        (uint32_t)CF_INTERACT_HOVERED | (uint32_t)CF_INTERACT_PRESSED);
    CF_CHECK(color_is(hovered_pressed.background, 60), "pressed wins over hovered");

    const cf_style pressed_focused =
        cf_style_varied(cf_style_default(), &variants,
                        (uint32_t)CF_INTERACT_PRESSED | (uint32_t)CF_INTERACT_FOCUSED);
    CF_CHECK(color_is(pressed_focused.background, 80), "focused wins over pressed");

    const cf_style everything =
        cf_style_varied(cf_style_default(), &variants,
                        (uint32_t)CF_INTERACT_HOVERED | (uint32_t)CF_INTERACT_PRESSED |
                            (uint32_t)CF_INTERACT_FOCUSED | (uint32_t)CF_INTERACT_DISABLED);
    CF_CHECK(color_is(everything.background, 100), "disabled reads disabled however it is hovered");
}

static void unset_variants_leave_the_base_alone(void) {
    const cf_variants variants = {0};

    cf_style base = cf_style_default();
    base.background = (cf_opt_color){.present = true, .value = CF_COLOR(20, 20, 20)};

    const uint32_t every_state = (uint32_t)CF_INTERACT_HOVERED | (uint32_t)CF_INTERACT_PRESSED |
                                 (uint32_t)CF_INTERACT_FOCUSED | (uint32_t)CF_INTERACT_DISABLED;
    const cf_style style = cf_style_varied(base, &variants, every_state);
    CF_CHECK(color_is(style.background, 20), "a zeroed variant set changes nothing");

    const cf_style unstyled = cf_style_varied(base, NULL, every_state);
    CF_CHECK(color_is(unstyled.background, 20), "no variant set at all changes nothing");
}

static void the_default_theme_reads_the_neutral_scale(void) {
    const cf_theme *theme = cf_theme_default();
    CF_CHECK(theme != NULL, "the theme exists");
    CF_CHECK(theme != NULL && color_is(theme->page.background, 245), "the page is the field");
    CF_CHECK(theme != NULL && color_is(theme->bar.background, 229), "the bar is the well");
    CF_CHECK(theme != NULL && color_is(theme->body.background, 250), "the body is the card");
    CF_CHECK(theme != NULL && color_is(theme->variants[CF_ROLE_BUTTON].base.background, 23),
             "the button rests on the accent");
    CF_CHECK(theme != NULL &&
                 (theme->variants[CF_ROLE_BUTTON].base.margin.left.kind == CF_LENGTH_PX &&
                  theme->variants[CF_ROLE_BUTTON].base.margin.left.value == 0.0f),
             "the resting box leaves its placing to the container");
    CF_CHECK(theme != NULL && color_is(theme->variants[CF_ROLE_BUTTON].disabled.background, 229),
             "a disabled button drops to the well");
}

/*
 * A resting box that refuses to give ground is a box that runs past its
 * container: a themed row whose widths do not fit slides its last card out of
 * the clip instead of reflowing it, so a narrower window loses content rather
 * than repacking it. CSS's initial is that every item shrinks, so every base
 * the theme hands out states the factor instead of leaving the zero.
 */
static void a_themed_box_gives_ground_when_the_row_runs_out(void) {
    const cf_theme *theme = cf_theme_default();

    if (theme == NULL) {
        CF_CHECK(false, "the theme exists");
        return;
    }

    CF_CHECK(theme->page.flex_shrink > 0.0f, "the page shrinks with its container");
    CF_CHECK(theme->bar.flex_shrink > 0.0f, "the well shrinks with its container");
    CF_CHECK(theme->body.flex_shrink > 0.0f, "the card shrinks with its container");

    const cf_role families[] = {CF_ROLE_BUTTON, CF_ROLE_FIELD, CF_ROLE_SCROLL, CF_ROLE_IMAGE};

    for (uint32_t i = 0u; i < sizeof families / sizeof families[0]; ++i) {
        CF_CHECK(theme->variants[families[i]].base.flex_shrink > 0.0f,
                 "a widget base shrinks with the container that holds it");
    }
}

static void a_focused_node_resolves_its_focused_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_use_theme(&ctx, cf_theme_default());

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    /* The field declares the theme's resting box, the way a widget function
     * starts: the fill and hairline come with it, the focus variant only
     * recolors the border. */
    cf_node field = styled_of(CF_NO_NAME, CF_ROLE_FIELD);
    field.style = cf_theme_default()->variants[CF_ROLE_FIELD].base;
    field.style.width = CF_PX(100.0f);
    field.style.height = CF_PX(40.0f);
    /* A real cf_field declares this for its caller: the role paints the focus
     * variant, it does not say the keyboard may land here. */
    field.focusable = true;

    const cf_node children[] = {field};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    CF_CHECK(root_slot != CF_NO_LINK, "the diff claims the declaration");

    const uint32_t slot = 1u;
    cf_focus(&ctx, cf_node_id(&ctx, slot));

    cf_frame_resolve(&ctx);

    const cf_style resolved = frame_style(&ctx, slot);
    CF_CHECK(color_is(resolved.border_color, 10), "focus recolors the border to ink");
    CF_CHECK(color_is(resolved.background, 250), "the focused field keeps its fill");

    cf_ctx_free(&ctx);
}

static void a_hovered_node_resolves_after_the_pointer_moves(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_use_theme(&ctx, cf_theme_default());

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node button = styled_of(CF_NO_NAME, CF_ROLE_BUTTON);

    const cf_node children[] = {button};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the first frame claims the declaration");
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(60.0f)},
                    (cf_point){0.0f, 0.0f});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {50.0f, 30.0f}});

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the second frame keeps the slot");
    cf_frame_resolve(&ctx);

    const cf_style resolved = frame_style(&ctx, 1u);
    CF_CHECK(color_is(resolved.background, 46), "the hovered button lightens");

    cf_ctx_free(&ctx);
}

static void a_disabled_node_resolves_its_disabled_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_use_theme(&ctx, cf_theme_default());

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node button = styled_of(CF_NO_NAME, CF_ROLE_BUTTON);
    button.disabled = true;

    const cf_node children[] = {button};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");
    cf_frame_resolve(&ctx);

    const cf_style resolved = frame_style(&ctx, 1u);
    CF_CHECK(color_is(resolved.background, 229), "the disabled button drops to the well");
    CF_CHECK(color_is(resolved.border_color, 212), "the disabled button edges in the hairline");

    cf_ctx_free(&ctx);
}

static void a_node_without_a_role_keeps_its_declared_style(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_use_theme(&ctx, cf_theme_default());

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node box = styled_of(CF_NO_NAME, CF_ROLE_NONE);
    box.style.background = (cf_opt_color){.present = true, .value = CF_COLOR(7, 7, 7)};

    const cf_node children[] = {box};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");
    cf_frame_resolve(&ctx);

    const cf_style resolved = frame_style(&ctx, 1u);
    CF_CHECK(color_is(resolved.background, 7), "the declared fill stands");
    CF_CHECK(resolved.radius == 0.0f, "the theme's radii never reach a roleless box");

    cf_ctx_free(&ctx);
}

static void resolve_without_a_theme_or_a_view_is_a_no_op(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* No theme: a build that styles every box itself. */
    cf_frame_begin(&ctx);
    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");
    cf_frame_resolve(&ctx);
    CF_CHECK(!frame_style(&ctx, 0u).background.present, "a themeless frame resolves nothing");

    /* No view: a frame the diff never built. */
    cf_ctx empty;
    context_of(&empty);
    cf_use_theme(&empty, cf_theme_default());
    cf_frame_resolve(&empty);
    CF_CHECK(cf_frame_tree(&empty) == NULL, "a frame without a diff publishes no view");

    cf_ctx_free(&empty);
    cf_ctx_free(&ctx);
}

/*
 * What a host that starts without a design system and names one later gets: the frame before
 * the naming resolves nothing, the frame after wears the theme, and taking it back leaves the
 * page with exactly what it declared. A resolved style lives in the frame that computed it, so
 * there is no cache here that could keep an un-named theme answering.
 */
static void a_theme_named_between_frames_styles_the_frame_after(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    /* A switch off is the state a resting box has no variant for: the theme's well is what
     * arrives, and nothing else in this file's fixtures could answer for it being absent. */
    cf_node button = styled_of(CF_NO_NAME, CF_ROLE_BUTTON);
    button.style = cf_style_default();
    button.style.width = CF_PX(100.0f);
    button.style.height = CF_PX(40.0f);
    button.disabled = true;

    const cf_node children[] = {button};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_frame_tree(&ctx) == NULL, "a frame that has diffed nothing hands out no view");
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the first frame claims the declaration");
    cf_frame_resolve(&ctx);
    CF_CHECK(!frame_style(&ctx, 1u).background.present,
             "a page with no theme named wears nothing it did not declare");

    cf_use_theme(&ctx, cf_theme_default());

    cf_frame_begin(&ctx);
    CF_CHECK(cf_frame_tree(&ctx) == NULL, "the frame break takes the view it published back");
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the next frame keeps the slot");
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).background, 229),
             "the theme named between the frames styles the frame after it");

    cf_use_theme(&ctx, NULL);

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "and the frame after that keeps it");
    cf_frame_resolve(&ctx);
    CF_CHECK(!frame_style(&ctx, 1u).background.present,
             "a theme taken back leaves the declaration standing again");

    cf_ctx_free(&ctx);
}

/*
 * The other direction a host changes its design system: not a table named where there was none, but
 * one replaced by a second that says less. A resolve kept across frames would answer with the first
 * table's numbers whatever the second one stated, so the second leaves the very field the first
 * overrode unset - the frame after the naming has to fall back to the declaration, and the frame
 * after that has to pick the first table up again.
 */
static void a_second_theme_replaces_the_first_whole(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_theme *const first = cf_theme_default();

    /* Built from the shipped table rather than from nothing: a theme is read field by field, and
     * the only difference this case needs is one state said and the same state left alone. */
    cf_theme sparse = *first;
    sparse.variants[CF_ROLE_BUTTON].disabled = (cf_variant){0};

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    /* styled_of declares the box it fills when no state overrides it, so the answer the second
     * table has to give is a number this file wrote rather than one it inherited. */
    cf_node button = styled_of(CF_NO_NAME, CF_ROLE_BUTTON);
    button.disabled = true;

    const cf_node children[] = {button};
    root.children = children;
    root.child_count = 1u;

    cf_use_theme(&ctx, first);

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the first frame claims the declaration");
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).background, 229),
             "the first table drops a switched-off box to its own well");

    cf_use_theme(&ctx, &sparse);
    CF_CHECK(cf_theme_of(&ctx) == &sparse, "the context reads the table it was just named");

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the frame after the naming keeps the slot");
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).background, 20),
             "and a table silent about that state leaves the declaration standing");

    cf_use_theme(&ctx, first);

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "and the frame after that keeps it");
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).background, 229),
             "naming the first table back brings its numbers with it");

    cf_ctx_free(&ctx);
}

static void a_state_change_between_frames_changes_the_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_use_theme(&ctx, cf_theme_default());

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node field = styled_of(CF_NO_NAME, CF_ROLE_FIELD);
    field.style = cf_theme_default()->variants[CF_ROLE_FIELD].base;
    field.style.width = CF_PX(100.0f);
    field.style.height = CF_PX(40.0f);
    /* A real cf_field declares this for its caller: the role paints the focus
     * variant, it does not say the keyboard may land here. */
    field.focusable = true;

    cf_node second = styled_of(CF_NO_NAME, CF_ROLE_FIELD);
    second.style = cf_theme_default()->variants[CF_ROLE_FIELD].base;
    second.style.width = CF_PX(100.0f);
    second.style.height = CF_PX(40.0f);
    second.focusable = true;

    const cf_node children[] = {field, second};
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");
    cf_focus(&ctx, cf_node_id(&ctx, 1u));
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).border_color, 10), "the focused field reads ink");

    /* The focus goes to the other field rather than nowhere: a box holds it until
     * a control takes it, which is how a dialog hands it back. */
    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the rebuild keeps the slot");
    cf_focus(&ctx, cf_node_id(&ctx, 2u));
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(frame_style(&ctx, 1u).border_color, 212),
             "the blurred field reads the hairline again");
    CF_CHECK(color_is(frame_style(&ctx, 2u).border_color, 10),
             "and the field that took it reads ink");

    cf_ctx_free(&ctx);
}

static void flipping_the_disabled_switch_marks_the_node_dirty(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node button = styled_of(CF_NO_NAME, CF_ROLE_BUTTON);

    const cf_node children[] = {button};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");

    /* A fresh slot arrives dirty by design; cleanliness is the unchanged
     * rebuild's answer. */
    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the rebuild keeps the slot");
    CF_CHECK(!cf_dirty(&ctx, 1u), "an unchanged rebuild is clean");

    cf_frame_begin(&ctx);
    button.disabled = true;
    const cf_node children_again[] = {button};
    root.children = children_again;
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the rebuild keeps the slot");
    CF_CHECK(cf_dirty(&ctx, 1u), "a flipped switch is a changed node");

    cf_ctx_free(&ctx);
}

/*
 * The arithmetic on the pairs the definition is anchored on: black against white is 21 by
 * construction, a tone against itself is 1 rather than infinite, and a middle grey is the 3.95
 * every WCAG table prints rather than the 5.0 a linear blend of the bytes would answer. The curve
 * is the whole of that difference, which is why the rule is written against the specification's
 * numbers and not a cheaper approximation of them.
 */
static void the_ratio_is_the_one_the_definition_gives(void) {
    const float black_white = cf_contrast(CF_COLOR(0, 0, 0), CF_COLOR(255, 255, 255));
    CF_CHECK(fabsf(black_white - 21.0f) < 0.05f, "black on white reads 21");
    CF_CHECK(fabsf(cf_contrast(CF_COLOR(255, 255, 255), CF_COLOR(0, 0, 0)) - black_white) < 0.001f,
             "and it reads the same whichever tone is named first");

    const cf_color tone = CF_COLOR(123, 45, 67);
    CF_CHECK(fabsf(cf_contrast(tone, tone) - 1.0f) < 0.001f, "a tone against itself reads 1");

    const float mid_grey = cf_contrast(CF_COLOR(128, 128, 128), CF_COLOR(255, 255, 255));
    CF_CHECK(fabsf(mid_grey - 3.95f) < 0.05f, "the middle grey of every contrast table reads 3.95");

    /* Half cover over white is the grey a painter blends, byte for byte: the ratio is of what
     * arrives on the screen, not of the tone a theme wrote down. */
    const cf_color half_ink = {0u, 0u, 0u, 128u};
    const float through = cf_contrast(half_ink, CF_COLOR(255, 255, 255));
    CF_CHECK(fabsf(through - mid_grey) < 0.2f, "a half-covered black reads about as grey as it is");
    CF_CHECK(fabsf(through - cf_contrast(CF_COLOR(127, 127, 127), CF_COLOR(255, 255, 255))) <
                 0.001f,
             "and reads the blended byte exactly");
}

/* The better of a system's two reading tones on one fill. */
static float best_tone_contrast(const cf_theme *theme, cf_color fill) {
    const float ink = cf_contrast(theme->ink, fill);
    const float carried = cf_contrast(theme->on_ink, fill);

    return ink > carried ? ink : carried;
}

/*
 * Every fill the shipped table hands out carries readable text in at least one of the two tones the
 * same table names. 4.5 is WCAG-AA for body text, and a design system that clears it on a button's
 * resting state and not on its pressed one has a label that goes unread at exactly the moment a
 * person commits to it. The walk runs over the table rather than over a list of pairs someone
 * picked, so a tone a theme states later is checked the frame it arrives in.
 */
static void every_fill_the_table_states_carries_one_of_its_tones(void) {
    const cf_theme *const theme = cf_theme_default();
    const cf_style *const surfaces[] = {&theme->page, &theme->bar, &theme->body};

    for (uint32_t i = 0u; i < sizeof surfaces / sizeof *surfaces; ++i) {
        CF_CHECK(surfaces[i]->background.present, "the shipped system paints all three surfaces");
        CF_CHECK(best_tone_contrast(theme, surfaces[i]->background.value) >= 4.5f,
                 "and text reads on each of them");
    }

    const cf_role families[] = {CF_ROLE_BUTTON, CF_ROLE_FIELD, CF_ROLE_LIST,
                                CF_ROLE_SCROLL, CF_ROLE_IMAGE, CF_ROLE_CHECKBOX};

    for (uint32_t f = 0u; f < sizeof families / sizeof *families; ++f) {
        const cf_variants *const variants = &theme->variants[families[f]];

        if (variants->base.background.present) {
            CF_CHECK(best_tone_contrast(theme, variants->base.background.value) >= 4.5f,
                     "a family's resting fill carries one of the two tones");
        }

        /* The states in the order the resolver applies them, so the fill checked is the fill that
         * would be painted: a pressed box reads as pressed. */
        const cf_variant *const states[] = {&variants->checked, &variants->hovered,
                                            &variants->pressed, &variants->focused,
                                            &variants->disabled};

        for (uint32_t s = 0u; s < sizeof states / sizeof *states; ++s) {
            if (!states[s]->background.present) {
                continue;
            }

            CF_CHECK(best_tone_contrast(theme, states[s]->background.value) >= 4.5f,
                     "every fill a family can be painted in carries one of the two tones");
        }
    }

    /* The refusal, on a table built to trip it. A walk that can only say yes is a fixture, so this
     * is the shape the rule has to refuse: two reading tones that both sit near the field's own
     * tone, which is the mistake a theme picks when its author reads a swatch rather than a number.
     */
    cf_theme dim = *theme;
    dim.ink = CF_COLOR(120, 120, 120);
    dim.on_ink = CF_COLOR(150, 150, 150);
    CF_CHECK(best_tone_contrast(&dim, theme->page.background.value) < 4.5f,
             "and a fill no tone can carry is refused by the same walk");

    /* The other half of asking for the better tone: a table with one reading tone that clears and
     * one that does not is a system the walk has to accept, since the two tones exist so that
     * either side of a fill can be carried. Refusing this one would be the rule overreaching - and
     * taking only the first tone would fail it the other way round. */
    cf_theme one = dim;
    one.on_ink = CF_COLOR(23, 23, 23);
    CF_CHECK(cf_contrast(one.ink, theme->page.background.value) < 4.5f,
             "its ink is the tone that cannot carry the field");
    CF_CHECK(best_tone_contrast(&one, theme->page.background.value) >= 4.5f,
             "and one tone that reads is enough for the page to pass");
}

/*
 * A mark that only has to be noticeable - a focus edge, a caret, a band under a run - clears 3:1,
 * and both sides of an edge count: a ring that reads against the box it borders and vanishes into
 * the surface behind it says nothing about where the keyboard stands. The button's ring is the one
 * tone in the table chosen to sit between two fills rather than on one, so it is the pair worth
 * naming.
 */
static void the_marks_a_page_leaves_are_noticeable(void) {
    const cf_theme *const theme = cf_theme_default();
    const cf_variants *const button = &theme->variants[CF_ROLE_BUTTON];
    const cf_variants *const field = &theme->variants[CF_ROLE_FIELD];
    const cf_color ink = theme->ink;

    CF_CHECK(button->focused.border_color.present, "the button states a focus ring");
    CF_CHECK(cf_contrast(button->focused.border_color.value, button->base.background.value) >= 3.0f,
             "the ring reads on the accent it borders");

    const cf_style *const surfaces[] = {&theme->page, &theme->bar, &theme->body};

    for (uint32_t i = 0u; i < sizeof surfaces / sizeof *surfaces; ++i) {
        CF_CHECK(cf_contrast(button->focused.border_color.value, surfaces[i]->background.value) >=
                     3.0f,
                 "and on every surface a focused box can be resting on");
    }

    /* The field books three marks when it takes the keyboard, and each is a different kind of ask:
     * the edge says where the box is, the caret where the next byte lands, the band which bytes are
     * chosen - so each is checked against what a person reads beside it rather than against one
     * tone. */
    CF_CHECK(cf_contrast(field->focused.border_color.value, field->base.background.value) >= 3.0f,
             "a focused field's edge reads on its own well");
    CF_CHECK(cf_contrast(field->focused.caret.value, field->base.background.value) >= 3.0f,
             "and its caret reads on the same well");
    CF_CHECK(cf_contrast(ink, field->focused.selection.value) >= 4.5f,
             "the bytes a selection band carries stay readable as text");
    CF_CHECK(cf_contrast(field->focused.marked.value, field->base.background.value) >= 3.0f,
             "and a run an input method is deciding is marked rather than hidden");
    CF_CHECK(cf_contrast(ink, field->focused.marked.value) >= 3.0f,
             "the same run's letters stay legible against the line under them");
}

/* Whether a computed ratio is the number a comment claims, to the tenth the note states. */
static bool reads(float actual, float claimed) {
    return fabsf(actual - claimed) < 0.05f;
}

/*
 * The ratios src/theme/default.c quotes in its own comments, computed. A note that states a number
 * is read as a checked number, so it either arrives from the arithmetic or it is not written: these
 * are the claims, and the case is what keeps them true when a tone moves. Four of the five were
 * wrong when the arithmetic arrived - the accents were understated and the secondary tone and the
 * page's own pair overstated - which is the whole reason the claim is pinned to a number rather
 * than left in prose.
 */
static void the_ratios_the_table_quotes_are_the_ones_it_computes(void) {
    const cf_theme *const theme = cf_theme_default();

    CF_CHECK(reads(cf_contrast(theme->ink, theme->body.background.value), 19.0f),
             "the scale's note: ink on the card is 19.0:1");
    CF_CHECK(
        reads(cf_contrast(theme->on_ink, theme->variants[CF_ROLE_BUTTON].base.background.value),
              17.2f),
        "the scale's note: the card's tone on the accent is 17.2:1");
    CF_CHECK(reads(cf_contrast(theme->muted, theme->body.background.value), 7.5f),
             "the scale's note: secondary text on the card is 7.5:1");
    CF_CHECK(reads(cf_contrast(theme->ink, theme->variants[CF_ROLE_FIELD].focused.selection.value),
                   13.9f),
             "the scale's note: ink on the selection band is 13.9:1");
    CF_CHECK(reads(cf_contrast(theme->ink, theme->page.background.value), 18.2f),
             "the list's note: ink on the page's own tone is 18.2:1");
}

int main(void) {
    printf("theme resolve\n");
    CF_RUN(a_variant_states_only_its_differences);
    CF_RUN(later_states_overwrite_earlier_ones);
    CF_RUN(unset_variants_leave_the_base_alone);
    CF_RUN(the_default_theme_reads_the_neutral_scale);
    CF_RUN(a_themed_box_gives_ground_when_the_row_runs_out);
    CF_RUN(a_focused_node_resolves_its_focused_variant);
    CF_RUN(a_hovered_node_resolves_after_the_pointer_moves);
    CF_RUN(a_disabled_node_resolves_its_disabled_variant);
    CF_RUN(a_node_without_a_role_keeps_its_declared_style);
    CF_RUN(resolve_without_a_theme_or_a_view_is_a_no_op);
    CF_RUN(a_theme_named_between_frames_styles_the_frame_after);
    CF_RUN(a_second_theme_replaces_the_first_whole);
    CF_RUN(a_state_change_between_frames_changes_the_variant);
    CF_RUN(flipping_the_disabled_switch_marks_the_node_dirty);
    CF_RUN(the_ratio_is_the_one_the_definition_gives);
    CF_RUN(every_fill_the_table_states_carries_one_of_its_tones);
    CF_RUN(the_marks_a_page_leaves_are_noticeable);
    CF_RUN(the_ratios_the_table_quotes_are_the_ones_it_computes);

    return CF_REPORT();
}
