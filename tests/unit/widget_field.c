#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The field is the first widget that edits what the build owns: every case
 * presses it, types into it and reads the buffer the declaration handed it.
 */

/* The same pen-distance table the text tests answer through, so the label's
 * box is known without a font file. */
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

/* One frame of the loop: a field over the build's buffer, declared inside
 * the frame - the bytes it keeps live in the arena cf_frame_begin emptied. */
static void frame_of(cf_ctx *ctx, cf_text_edit *text, bool disabled) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node field = cf_field(ctx, "entry", text, &stack);
    field.disabled = disabled;
    const cf_node roots[] = {field};
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

static void the_focused_field_takes_the_keys_it_is_given(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    const uint32_t field = child_of(&ctx, ctx.root_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_focused(&ctx, field), "the press focuses the field");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 'h'});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 'i'});

    CF_CHECK(text.length == 2u && bytes[0] == 'h' && bytes[1] == 'i',
             "the scalars land in the buffer in order");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 8u});

    CF_CHECK(text.length == 1u && bytes[0] == 'h', "backspace drops the last scalar");

    cf_ctx_free(&ctx);
}

static void a_scalar_appends_whatever_bytes_it_needs(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 233u});

    CF_CHECK(text.length == 2u && (unsigned char)bytes[0] == 0xC3u &&
                 (unsigned char)bytes[1] == 0xA9u,
             "a two-byte scalar lands whole");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 8u});

    CF_CHECK(text.length == 0u, "and backspace drops it whole");

    cf_ctx_free(&ctx);
}

static void the_field_stops_at_the_capacity_its_buffer_stated(void) {
    char bytes[4] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 4u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    for (uint32_t i = 0u; i < 8u; ++i) {
        cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 'a'});
    }

    CF_CHECK(text.length == 4u && bytes[3] == 'a', "the buffer fills and the rest refuse");
    CF_CHECK(bytes[0] == 'a' && bytes[1] == 'a' && bytes[2] == 'a',
             "what landed is what was typed");

    cf_ctx_free(&ctx);
}

static void a_field_without_focus_edits_nothing(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 'x'});

    CF_CHECK(text.length == 0u, "keys go only where focus books them");

    cf_ctx_free(&ctx);
}

static void a_disabled_field_neither_focuses_nor_edits(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, true);

    const uint32_t field = child_of(&ctx, ctx.root_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 'x'});

    CF_CHECK(!cf_focused(&ctx, field), "a switched-off field takes no focus");
    CF_CHECK(text.length == 0u, "and edits nothing");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget field\n");

    CF_RUN(the_focused_field_takes_the_keys_it_is_given);
    CF_RUN(a_scalar_appends_whatever_bytes_it_needs);
    CF_RUN(the_field_stops_at_the_capacity_its_buffer_stated);
    CF_RUN(a_field_without_focus_edits_nothing);
    CF_RUN(a_disabled_field_neither_focuses_nor_edits);

    return CF_REPORT();
}
