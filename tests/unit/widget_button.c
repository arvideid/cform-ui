#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The button is the dispatcher's first customer: every case builds one
 * declaration, drives the pointer events a click is made of and reads the
 * callback the widget fires.
 */
typedef struct clicks {
    int count;
    uint32_t last_slot;
    bool saw_ctx;
} clicks;

static void on_press(cf_ctx *ctx, uint32_t slot, void *user_data) {
    clicks *seen = user_data;

    seen->count++;
    seen->last_slot = slot;
    seen->saw_ctx = ctx != NULL;
}

/* The same pen-distance table the text tests answer through, so the label's
 * box is known without a font file: four glyphs at ten, one line of twenty. */
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

static const cf_text_ops mono_ops = {mono_metrics, mono_advance, mono_has_glyph};
static const cf_font mono = {.handle = NULL, .ops = &mono_ops};

static bool tone_is(cf_opt_color color, uint8_t red, uint8_t green, uint8_t blue) {
    return color.present && color.value.red == red && color.value.green == green &&
           color.value.blue == blue;
}

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

/* One frame of the loop a build runs: begin, declare, reconcile, resolve,
 * lay out. The widgets are declared inside the frame - the bytes they keep
 * live in the arena cf_frame_begin emptied. */
static void frame_of(cf_ctx *ctx, clicks *seen, bool disabled) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node label = cf_label(ctx, cf_string_of("push"), &stack, ctx->theme->on_ink);
    const cf_node children[] = {label};
    cf_node button = cf_button(ctx, "push", on_press, seen, children, 1u);
    button.disabled = disabled;
    const cf_node roots[] = {button};
    cf_node root = {.style = cf_style_default()};
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(200.0f);
    root.children = roots;
    root.child_count = 1u;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

static uint32_t child_of(const cf_ctx *ctx, uint32_t slot, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[slot] + index];
}

static void a_click_fires_on_the_release_that_ends_inside(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    clicks seen = {0};

    frame_of(&ctx, &seen, false);

    const uint32_t button_slot = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t label_slot = child_of(&ctx, button_slot, 0u);

    /* The label's rect starts at the button's padding, so the press lands on
     * the text and climbs to the button that owns it. */
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 20.0f, .y = 10.0f}});

    CF_CHECK(cf_pressed(&ctx, button_slot) && !cf_pressed(&ctx, label_slot),
             "the press books the button, not the label it started on");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 20.0f, .y = 10.0f}});

    CF_CHECK(seen.count == 1, "the click fires once");
    CF_CHECK(seen.last_slot == button_slot, "the callback names the button's slot");
    CF_CHECK(seen.saw_ctx, "the callback carries the context");

    cf_ctx_free(&ctx);
}

static void a_release_off_the_button_leaves_the_click_unfired(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    clicks seen = {0};

    frame_of(&ctx, &seen, false);

    const uint32_t button_slot = child_of(&ctx, ctx.root_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 20.0f, .y = 10.0f}});
    cf_input(&ctx,
             (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 150.0f, .y = 150.0f}});

    CF_CHECK(cf_pressed(&ctx, button_slot), "the press outlives the pointer leaving the rect");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 150.0f, .y = 150.0f}});

    CF_CHECK(seen.count == 0, "the release off the rect is not a click");
    CF_CHECK(!cf_pressed(&ctx, button_slot), "the press ends with the release all the same");

    cf_ctx_free(&ctx);
}

static void a_disabled_button_neither_presses_nor_fires(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    clicks seen = {0};

    frame_of(&ctx, &seen, true);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 20.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 20.0f, .y = 10.0f}});

    CF_CHECK(seen.count == 0, "a switched-off button fires nothing");
    CF_CHECK(!cf_pressed(&ctx, child_of(&ctx, ctx.root_slot, 0u)), "and books no press");

    cf_ctx_free(&ctx);
}

static void the_button_declares_the_theme_s_resting_box(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    clicks seen = {0};

    frame_of(&ctx, &seen, false);

    const uint32_t button_slot = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t label_slot = child_of(&ctx, button_slot, 0u);
    const cf_style resting = ctx.layout.styles[button_slot];

    CF_CHECK(tone_is(resting.background, 23, 23, 23), "the button rests on the theme's accent");
    CF_CHECK(tone_is(ctx.layout.styles[label_slot].color, 250, 250, 250),
             "the label carries the tone the build stated");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget button\n");

    CF_RUN(a_click_fires_on_the_release_that_ends_inside);
    CF_RUN(a_release_off_the_button_leaves_the_click_unfired);
    CF_RUN(a_disabled_button_neither_presses_nor_fires);
    CF_RUN(the_button_declares_the_theme_s_resting_box);

    return CF_REPORT();
}
