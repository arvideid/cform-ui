#include "cf/node.h"

#include "cform_test.h"

/* The variant system and the resolver are the frame's styling half: the pure
 * cases run on styles alone, the frame cases drive declarations through one
 * context and read the view the resolver leaves. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
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

static void a_focused_node_resolves_its_focused_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

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

    const cf_node children[] = {field};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    CF_CHECK(root_slot != CF_NO_LINK, "the diff claims the declaration");

    const uint32_t slot = 1u;
    cf_focus(&ctx, slot);

    cf_frame_resolve(&ctx);

    const cf_style resolved = ctx.layout.styles[slot];
    CF_CHECK(color_is(resolved.border_color, 10), "focus recolors the border to ink");
    CF_CHECK(color_is(resolved.background, 250), "the focused field keeps its fill");

    cf_ctx_free(&ctx);
}

static void a_hovered_node_resolves_after_the_pointer_moves(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

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

    const cf_style resolved = ctx.layout.styles[1u];
    CF_CHECK(color_is(resolved.background, 46), "the hovered button lightens");

    cf_ctx_free(&ctx);
}

static void a_disabled_node_resolves_its_disabled_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

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

    const cf_style resolved = ctx.layout.styles[1u];
    CF_CHECK(color_is(resolved.background, 229), "the disabled button drops to the well");
    CF_CHECK(color_is(resolved.border_color, 212), "the disabled button edges in the hairline");

    cf_ctx_free(&ctx);
}

static void a_node_without_a_role_keeps_its_declared_style(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

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

    const cf_style resolved = ctx.layout.styles[1u];
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
    const cf_style before = ctx.layout.styles[0u];
    cf_frame_resolve(&ctx);
    CF_CHECK(ctx.layout.styles[0u].background.present == before.background.present,
             "a themeless frame resolves nothing");

    /* No view: a frame the diff never built. */
    cf_ctx empty;
    context_of(&empty);
    empty.theme = cf_theme_default();
    cf_frame_resolve(&empty);
    CF_CHECK(empty.layout.styles == NULL, "a frame without a diff has no view to rewrite");

    cf_ctx_free(&empty);
    cf_ctx_free(&ctx);
}

static void a_state_change_between_frames_changes_the_variant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    cf_node root = node_of(CF_NO_NAME);
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(60.0f);

    cf_node field = styled_of(CF_NO_NAME, CF_ROLE_FIELD);
    field.style = cf_theme_default()->variants[CF_ROLE_FIELD].base;
    field.style.width = CF_PX(100.0f);
    field.style.height = CF_PX(40.0f);

    const cf_node children[] = {field};
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");
    cf_focus(&ctx, 1u);
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(ctx.layout.styles[1u].border_color, 10), "the focused field reads ink");

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the rebuild keeps the slot");
    cf_focus(&ctx, 0u);
    cf_frame_resolve(&ctx);
    CF_CHECK(color_is(ctx.layout.styles[1u].border_color, 212),
             "the blurred field reads the hairline again");

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

int main(void) {
    printf("theme resolve\n");
    CF_RUN(a_variant_states_only_its_differences);
    CF_RUN(later_states_overwrite_earlier_ones);
    CF_RUN(unset_variants_leave_the_base_alone);
    CF_RUN(the_default_theme_reads_the_neutral_scale);
    CF_RUN(a_focused_node_resolves_its_focused_variant);
    CF_RUN(a_hovered_node_resolves_after_the_pointer_moves);
    CF_RUN(a_disabled_node_resolves_its_disabled_variant);
    CF_RUN(a_node_without_a_role_keeps_its_declared_style);
    CF_RUN(resolve_without_a_theme_or_a_view_is_a_no_op);
    CF_RUN(a_state_change_between_frames_changes_the_variant);
    CF_RUN(flipping_the_disabled_switch_marks_the_node_dirty);

    return CF_REPORT();
}
