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
    /* A column, so the field stretches across the page: the width the box keeps
     * is the one the clicks are measured along. */
    root.style.direction = CF_DIRECTION_COLUMN;
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

/* One run of what the keyboard wrote. The device settles shift, caps lock, the
 * layout and the input method, and hands over the bytes they agreed on. */
static void type(cf_ctx *ctx, const char *run, uint32_t length) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_TEXT, .text = {.chars = run, .length = length}});
}

/* One key that carries no text, with or without the shift held. */
static void hold(cf_ctx *ctx, cf_key key, bool shift) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_KEY_DOWN,
                             .key = (uint32_t)key,
                             .mods = shift ? (uint32_t)CF_MOD_SHIFT : 0u});
}

static void press(cf_ctx *ctx, float x, float y) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = x, .y = y}});
}

/* A move the pointer makes while its button is still held down. */
static void drag(cf_ctx *ctx, float x, float y) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = x, .y = y}});
}

static void the_focused_field_takes_the_keys_it_is_given(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    const uint32_t field = child_of(&ctx, ctx.root_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_focused(&ctx, field), "the press focuses the field");

    type(&ctx, "h", 1u);
    type(&ctx, "i", 1u);

    CF_CHECK(text.length == 2u && bytes[0] == 'h' && bytes[1] == 'i',
             "what the keyboard wrote lands in the buffer in order");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)CF_KEY_BACKSPACE});

    CF_CHECK(text.length == 1u && bytes[0] == 'h', "backspace drops the last scalar");

    cf_ctx_free(&ctx);
}

static void a_run_appends_whatever_bytes_it_needs(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    const char acute[2] = {(char)0xC3u, (char)0xA9u};

    type(&ctx, acute, 2u);

    CF_CHECK(text.length == 2u && (unsigned char)bytes[0] == 0xC3u &&
                 (unsigned char)bytes[1] == 0xA9u,
             "a two-byte run lands whole");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)CF_KEY_BACKSPACE});

    CF_CHECK(text.length == 0u, "and backspace drops it whole");

    cf_ctx_free(&ctx);
}

static void the_field_stops_at_the_capacity_its_buffer_stated(void) {
    char bytes[4] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 4u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    for (uint32_t i = 0u; i < 8u; ++i) {
        type(&ctx, "a", 1u);
    }

    CF_CHECK(text.length == 4u && bytes[3] == 'a', "the buffer fills and the rest refuse");
    CF_CHECK(bytes[0] == 'a' && bytes[1] == 'a' && bytes[2] == 'a',
             "what landed is what was typed");

    /* Too long for what is left, so it is refused whole rather than cut: half a
     * sequence is bytes nothing can read back. */
    const char over[3] = {'b', (char)0xC3u, (char)0xA9u};

    type(&ctx, over, 3u);

    CF_CHECK(text.length == 4u && bytes[3] == 'a', "a run that does not fit does not half land");

    cf_ctx_free(&ctx);
}

static void a_field_without_focus_edits_nothing(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    type(&ctx, "x", 1u);

    CF_CHECK(text.length == 0u, "written text goes only where focus books it");

    cf_ctx_free(&ctx);
}

static void a_disabled_field_neither_focuses_nor_edits(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, true);

    const uint32_t field = child_of(&ctx, ctx.root_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    type(&ctx, "x", 1u);

    CF_CHECK(!cf_focused(&ctx, field), "a switched-off field takes no focus");
    CF_CHECK(text.length == 0u, "and edits nothing");

    cf_ctx_free(&ctx);
}

/*
 * The caret and the selection are buffer state, so the cases read them off the
 * same struct the field writes: what a frame paints is the pair, and what a
 * click or a key moves is the pair, and nothing between the two has to be
 * told.
 */
static void the_arrows_move_the_caret_and_shift_extends_the_selection(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);

    CF_CHECK(text.caret == 3u && text.mark == 3u, "typing leaves the caret after the bytes");

    hold(&ctx, CF_KEY_HOME, false);

    CF_CHECK(text.caret == 0u && text.mark == 0u, "home drops the selection at the start");

    hold(&ctx, CF_KEY_RIGHT, true);

    CF_CHECK(text.caret == 1u && text.mark == 0u,
             "shift+right takes the next scalar into what is selected");

    hold(&ctx, CF_KEY_END, true);

    CF_CHECK(text.caret == 3u && text.mark == 0u, "and shift+end reaches the last byte");

    hold(&ctx, CF_KEY_LEFT, false);

    CF_CHECK(text.caret == 0u && text.mark == 0u,
             "an arrow with nothing held collapses the range to the end it points at");

    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 1u && text.mark == 1u, "and the next one steps from there");

    cf_ctx_free(&ctx);
}

static void an_arrow_steps_a_whole_scalar(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);

    const char acute[2] = {(char)0xC3u, (char)0xA9u};

    type(&ctx, "a", 1u);
    type(&ctx, acute, 2u);
    type(&ctx, "b", 1u);

    CF_CHECK(text.length == 4u, "three scalars in four bytes");

    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 1u, "the first step leaves the one-byte scalar");

    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 3u, "and the next one leaves the two-byte scalar whole");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 2u && bytes[0] == 'a' && bytes[1] == 'b',
             "backspace from past it takes the whole scalar, not the byte that ends it");

    cf_ctx_free(&ctx);
}

static void what_is_selected_gives_way_to_what_is_typed(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abcd", 4u);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, true);
    hold(&ctx, CF_KEY_RIGHT, true);

    type(&ctx, "X", 1u);

    CF_CHECK(text.length == 3u && bytes[0] == 'X' && bytes[1] == 'c',
             "the range goes and what was written lands in its place");
    CF_CHECK(text.caret == 1u && text.mark == 1u, "and the caret stands after it, alone");

    cf_ctx_free(&ctx);
}

/* A buffer that is full is not a buffer nothing can be typed into: the range
 * held selected is room the run is given. */
static void a_full_buffer_takes_what_the_selection_frees(void) {
    char bytes[4] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 4u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abcd", 4u);
    type(&ctx, "z", 1u);

    CF_CHECK(text.length == 4u && bytes[3] == 'd', "with nothing selected there is no room");

    hold(&ctx, CF_KEY_HOME, true);
    type(&ctx, "xy", 2u);

    CF_CHECK(text.length == 2u && bytes[0] == 'x' && bytes[1] == 'y',
             "and the moment a range is selected, the run that frees it fits");

    cf_ctx_free(&ctx);
}

static void the_edit_keys_cut_the_selection_or_the_scalar_they_name(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 2u && bytes[1] == 'b', "backspace takes the scalar before the caret");

    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_DELETE, false);

    CF_CHECK(text.length == 1u && bytes[0] == 'b', "delete takes the one after it");

    type(&ctx, "cd", 2u);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_END, true);
    hold(&ctx, CF_KEY_DELETE, false);

    CF_CHECK(text.length == 0u, "and either one clears a range instead");

    cf_ctx_free(&ctx);
}

/* The mono table costs every scalar ten points, and the box insets its content
 * by eight and a border: so the glyphs of "abc" stand from x 9 to x 39 and the
 * boundary between the second and the third is at 29. */
static void a_press_places_the_caret_and_a_drag_selects_to_where_it_went(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    hold(&ctx, CF_KEY_HOME, false);

    press(&ctx, 24.0f, 10.0f);

    CF_CHECK(text.caret == 2u && text.mark == 2u,
             "a press inside the second glyph's advance stands before the third");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 24.0f, .y = 10.0f}});
    drag(&ctx, 40.0f, 10.0f);

    CF_CHECK(text.caret == 2u, "a move outside a press selects nothing");

    press(&ctx, 2.0f, 10.0f);

    CF_CHECK(text.caret == 0u && text.mark == 0u,
             "and a press before the first glyph is the start");

    drag(&ctx, 40.0f, 10.0f);

    CF_CHECK(text.caret == 3u && text.mark == 0u,
             "the drag that follows holds the mark and reaches past the last byte");

    cf_ctx_free(&ctx);
}

static void a_key_that_carries_no_text_edits_nothing(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, &text, false);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    type(&ctx, "A", 1u);

    /* SDL's left shift, left control and caps lock: numbers naming a key, and
     * encoding one as if it were a character is what filled the field with
     * glyphs nobody typed. The capital arrived in the run above instead. */
    const uint32_t keys[] = {0x400000e1u, 0x400000e0u, 0x40000039u};

    for (uint32_t i = 0u; i < 3u; ++i) {
        cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = keys[i]});
    }

    CF_CHECK(text.length == 1u && bytes[0] == 'A',
             "the keys that change what another key means write nothing of their own");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget field\n");

    CF_RUN(the_focused_field_takes_the_keys_it_is_given);
    CF_RUN(a_run_appends_whatever_bytes_it_needs);
    CF_RUN(the_field_stops_at_the_capacity_its_buffer_stated);
    CF_RUN(a_field_without_focus_edits_nothing);
    CF_RUN(a_disabled_field_neither_focuses_nor_edits);
    CF_RUN(a_key_that_carries_no_text_edits_nothing);
    CF_RUN(the_arrows_move_the_caret_and_shift_extends_the_selection);
    CF_RUN(an_arrow_steps_a_whole_scalar);
    CF_RUN(what_is_selected_gives_way_to_what_is_typed);
    CF_RUN(a_full_buffer_takes_what_the_selection_frees);
    CF_RUN(the_edit_keys_cut_the_selection_or_the_scalar_they_name);
    CF_RUN(a_press_places_the_caret_and_a_drag_selects_to_where_it_went);

    return CF_REPORT();
}
