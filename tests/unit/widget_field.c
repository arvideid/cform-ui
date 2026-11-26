#include <string.h>

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

/*
 * One frame of the loop: a field over the build's buffer, declared inside
 * the frame - the bytes it keeps live in the arena cf_frame_begin emptied.
 * `keys` is the root's own key handler, for the cases that need to know which
 * chords the field below it let through.
 */
static void frame_watched(cf_ctx *ctx, cf_text_edit *text, bool disabled, cf_event_fn keys,
                          void *key_data) {
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
    root.handlers.key = keys;
    root.handlers.key_data = key_data;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

static void frame_of(cf_ctx *ctx, cf_text_edit *text, bool disabled) {
    frame_watched(ctx, text, disabled, NULL, NULL);
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

/* One key that carries no text, with whatever modifiers a desk would have put on it. */
static void hold_with(cf_ctx *ctx, cf_key key, uint32_t mods) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)key, .mods = mods});
}

static void hold(cf_ctx *ctx, cf_key key, bool shift) {
    hold_with(ctx, key, shift ? (uint32_t)CF_MOD_SHIFT : 0u);
}

static void press(cf_ctx *ctx, float x, float y) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = x, .y = y}});
}

/* A move the pointer makes while its button is still held down. */
static void drag(cf_ctx *ctx, float x, float y) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = x, .y = y}});
}

/* The field focused with `run` in it and the caret at the start: where a reader
 * begins before a word key has moved anything. */
static void type_from_home(cf_ctx *ctx, const char *run) {
    press(ctx, 10.0f, 10.0f);
    type(ctx, run, (uint32_t)strlen(run));
    hold(ctx, CF_KEY_HOME, false);
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
    CF_CHECK(text.refusals == 5u,
             "and every run turned away is counted where the page can read it");

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

/*
 * The desk the field moves bytes through, scripted: a run the test fills and
 * reads back, the two refusals a real clipboard answers with, and the counters
 * that say how often the field asked. Nothing here needs a window, so the
 * contract the SDL port keeps is checked against a pair that keeps it.
 */
typedef struct fake_board {
    char text[32];
    uint32_t length;
    bool refuses_read;
    bool refuses_write;
    uint32_t reads;
    uint32_t writes;
} fake_board;

static cf_string board_read(void *user_data, char *out, uint32_t capacity) {
    fake_board *board = user_data;

    board->reads++;

    if (board->refuses_read) {
        return (cf_string){0};
    }

    cf_string answer = {.chars = NULL, .length = board->length};

    if (out != NULL && capacity > 0u) {
        const uint32_t taken = board->length < capacity ? board->length : capacity;
        memcpy(out, board->text, taken);
        answer = (cf_string){.chars = out, .length = taken};
    }

    return answer;
}

static bool board_write(void *user_data, cf_string utf8) {
    fake_board *board = user_data;

    board->writes++;

    if (board->refuses_write || utf8.chars == NULL || utf8.length > sizeof board->text) {
        return false;
    }

    memcpy(board->text, utf8.chars, utf8.length);
    board->length = utf8.length;

    return true;
}

/* The pair over one board, and the bytes the desk is said to hold. */
static cf_clipboard board_of(fake_board *board) {
    return (cf_clipboard){.read = board_read, .write = board_write, .user_data = board};
}

static void host_holds(fake_board *board, const char *run) {
    board->length = (uint32_t)strlen(run);
    memcpy(board->text, run, board->length);
}

/* The page's own ear: a root handler counting the keys the field let through. */
static void count_keys(cf_event *event, void *user_data) {
    (void)event;
    (*(uint32_t *)user_data)++;
}

/* One letter key with the modifiers a desk would have put on it. */
static void chord(cf_ctx *ctx, char letter, uint32_t mods) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)letter, .mods = mods});
}

/* A field focused with `run` typed into it and the last `count` scalars selected -
 * the state every copy and cut case starts from, reached with the same keys a hand
 * uses rather than by writing the marks by hand. */
static void select_tail(cf_ctx *ctx, const char *run, uint32_t count) {
    press(ctx, 10.0f, 10.0f);
    type(ctx, run, (uint32_t)strlen(run));

    for (uint32_t i = 0u; i < count; ++i) {
        hold(ctx, CF_KEY_LEFT, true);
    }
}

static void copy_puts_the_selection_where_the_host_keeps_bytes(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'c', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(board.writes == 1u, "copy asks the host once");
    CF_CHECK(board.length == 2u && board.text[0] == 'b' && board.text[1] == 'c',
             "the selected run lands in the host's bytes");
    CF_CHECK(text.length == 3u && text.caret == 1u && text.mark == 3u,
             "and copy leaves the page holding exactly what it copied");

    cf_ctx_free(&ctx);
}

static void cut_offers_the_bytes_before_it_drops_them(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'x', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(board.length == 2u && board.text[0] == 'b' && board.text[1] == 'c',
             "the host has the run the buffer is about to lose");
    CF_CHECK(text.length == 1u && bytes[0] == 'a', "and only then does the range go");
    CF_CHECK(text.caret == 1u && text.mark == 1u, "with the caret where the bytes were");

    cf_ctx_free(&ctx);
}

static void a_refused_write_leaves_the_range_selected(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {.refuses_write = true};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'x', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(board.writes == 1u, "the field asked, once, and was refused");
    CF_CHECK(text.length == 3u && bytes[1] == 'b' && bytes[2] == 'c',
             "bytes the host would not take stay in the buffer");
    CF_CHECK(text.caret == 1u && text.mark == 3u,
             "and stay selected, which is the state a retry can use");

    cf_ctx_free(&ctx);
}

static void paste_inserts_what_the_host_holds_over_the_selection(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    host_holds(&board, "xy");
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abcd", 4u);
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 2u && bytes[0] == 'x' && bytes[1] == 'y',
             "the host's run takes the place of the selected range");
    CF_CHECK(text.caret == 2u && text.mark == 2u, "and the caret stands after it, alone");
    CF_CHECK(board.reads == 2u,
             "the length is asked for before the bytes, so the run crosses once");

    cf_ctx_free(&ctx);
}

static void a_host_run_too_long_for_the_buffer_lands_nowhere(void) {
    char bytes[8] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 4u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    host_holds(&board, "wxyzq");
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && bytes[0] == 'a' && bytes[2] == 'c',
             "a run the buffer cannot hold is refused whole, not cut to fit");

    cf_ctx_free(&ctx);
}

static void a_host_with_no_text_leaves_the_page_alone(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {.refuses_read = true};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(board.reads > 0u, "the field did ask the host");
    CF_CHECK(text.length == 3u && bytes[1] == 'b',
             "and an empty desk writes nothing over what was selected");
    CF_CHECK(text.caret == 1u && text.mark == 3u, "leaving the range for the user to act on");

    cf_ctx_free(&ctx);
}

static void either_primary_modifier_carries_the_chord_and_alt_does_not(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    uint32_t seen = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_watched(&ctx, &text, false, count_keys, &seen);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'c', (uint32_t)CF_MOD_META);

    CF_CHECK(board.length == 2u && board.text[0] == 'b',
             "a command key carries the shortcut where control does on another desk");
    CF_CHECK(seen == 0u, "a chord the field serves stops there and reaches no handler above");

    chord(&ctx, 'c', (uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_ALT);
    chord(&ctx, 'q', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(board.writes == 1u, "an alt chord is the host's, and a letter with no meaning here is "
                                 "nobody's to serve");
    CF_CHECK(seen == 2u, "and both are let through to whoever above is listening");

    cf_ctx_free(&ctx);
}

static void a_page_with_no_host_buffer_leaves_the_chord_unstopped(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    uint32_t seen = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_watched(&ctx, &text, false, count_keys, &seen);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'x', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && bytes[1] == 'b', "with no clipboard a cut key deletes nothing");
    CF_CHECK(text.caret == 1u && text.mark == 3u,
             "and the selection it would have moved stays put");
    CF_CHECK(seen == 1u,
             "the chord the field cannot serve is left to the page rather than swallowed");

    cf_ctx_free(&ctx);
}

/*
 * The field's own history: what its keys did, kept well enough to walk back through.
 * The cases are the ones the backlog names - type, undo, redo, and a step that stops at
 * the edge of the ring - beside the two rules that make the ring worth its bytes: a
 * caret move is not a step, and the states it holds are bounded by what an image of the
 * buffer costs.
 */
static void stepping_back_returns_what_the_keys_took(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    type(&ctx, "de", 2u);

    CF_CHECK(text.length == 5u, "two runs, one value");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 3u && memcmp(bytes, "abc", 3) == 0, "one step back drops the last run");
    CF_CHECK(text.caret == 3u && text.mark == 3u, "and the caret stands where that run left it");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 0u, "the next step reaches where the typing started");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 0u, "and the one after has nothing older to return");

    chord(&ctx, 'z', ctrl | (uint32_t)CF_MOD_SHIFT);

    CF_CHECK(text.length == 3u && bytes[0] == 'a', "holding shift turns the walk around");

    chord(&ctx, 'y', ctrl);

    CF_CHECK(text.length == 5u && memcmp(bytes, "abcde", 5) == 0,
             "and the key beside it walks to the newest state");

    cf_ctx_free(&ctx);
}

static void an_edit_after_a_step_back_ends_the_forward_walk(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "a", 1u);
    type(&ctx, "b", 1u);
    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 1u && bytes[0] == 'a', "back over the second run");

    type(&ctx, "c", 1u);

    CF_CHECK(text.length == 2u && bytes[1] == 'c', "and a new run continues from the step");

    chord(&ctx, 'z', ctrl | (uint32_t)CF_MOD_SHIFT);

    CF_CHECK(text.length == 2u && bytes[1] == 'c', "the run the step cancelled is gone for good");

    chord(&ctx, 'z', ctrl);

    /* Without the branch being cut, the newest image lands past the stale redo tail and
     * this step walks back onto the run the user cancelled instead of the one before
     * their own typing. */
    CF_CHECK(text.length == 1u && bytes[0] == 'a',
             "and the walk back steps over what survived, not what the branch replaced");

    cf_ctx_free(&ctx);
}

static void a_step_forward_returns_to_where_the_marks_stood(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    hold(&ctx, CF_KEY_LEFT, false);
    hold(&ctx, CF_KEY_LEFT, true);
    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 0u, "the run is stepped over");

    chord(&ctx, 'z', ctrl | (uint32_t)CF_MOD_SHIFT);

    /* Moving the caret wrote no image, so coming forward lands the user back where they
     * were working rather than where the typed run left them. */
    CF_CHECK(text.length == 3u && text.caret == 1u && text.mark == 2u,
             "and coming forward again brings the value back with the selection on it");

    cf_ctx_free(&ctx);
}

static void a_field_rebuilt_over_a_smaller_buffer_forgets_the_old_layout(void) {
    static const char seed[] = "abcdefghij";
    char wide[32] = {0};
    memcpy(wide, seed, sizeof(seed) - 1u);
    cf_text_edit big = {.chars = wide, .length = 10u, .caret = 10u, .mark = 10u, .capacity = 32u};
    char tight[8] = {0};
    cf_text_edit small = {.chars = tight, .length = 0u, .capacity = 8u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &big, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "x", 1u);

    /* The same field, declared over a buffer with a different capacity. The images the
     * block holds are laid out a stride the new buffer cannot read, and the byte count
     * each header reports belongs to that layout - read under the new one it asks the
     * field to put more bytes back than its buffer holds. */
    frame_of(&ctx, &small, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "y", 1u);
    type(&ctx, "z", 1u);

    chord(&ctx, 'z', ctrl);

    CF_CHECK(small.length == 1u && tight[0] == 'y',
             "only the runs the smaller buffer typed are walked back over");

    /* A step past the edge reaches the slot where the wide layout's bytes sit. Read as a
     * header, the text it copied spells a count far past the end of this buffer, and
     * putting that many bytes back would hand the field's stack to whoever owns the
     * memory beside it; the walk refuses, because after the reset this layout holds no
     * image there at all. */
    chord(&ctx, 'z', ctrl);

    CF_CHECK(small.length == 0u, "and the walk stops at the buffer the field was given");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(small.length == 0u, "and a step past the edge invents no image of the old layout");

    cf_ctx_free(&ctx);
}

static void a_caret_move_costs_no_step(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold(&ctx, CF_KEY_HOME, false);
    type(&ctx, "c", 1u);

    CF_CHECK(text.length == 3u && bytes[0] == 'c', "the run landed where the caret was moved");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 2u && memcmp(bytes, "ab", 2) == 0, "one step back returns the run");
    CF_CHECK(text.caret == 0u && text.mark == 0u,
             "and leaves the caret where the user last put it, not where the run left it");

    chord(&ctx, 'z', ctrl);

    CF_CHECK(text.length == 0u, "two steps back is the empty buffer: moving the caret was no step");

    cf_ctx_free(&ctx);
}

static void a_history_outlives_the_frame_that_wrote_it(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);

    /* Two frames the arena emptied and the build declared the same field in, with no key
     * between them: keyed state is kept for the frame that last asked for it, so a field
     * that wants a history has to ask for it every time it is declared. */
    frame_of(&ctx, &text, false);
    frame_of(&ctx, &text, false);

    CF_CHECK(text.length == 3u, "the rebuild carries the value it was given");

    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 0u, "and the step back works three frames after the typing");

    cf_ctx_free(&ctx);
}

static void a_buffer_too_large_to_copy_keeps_no_history(void) {
    static char bytes[8192];
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 8192u};
    uint32_t seen = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_watched(&ctx, &text, false, count_keys, &seen);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u, "a field with no history still edits what it holds");
    CF_CHECK(seen == 1u, "and leaves a chord it cannot serve to whoever is above");
    CF_CHECK(cf_state_bytes(&ctx) == 0u, "asking for no ring means the heap holds no block");

    cf_ctx_free(&ctx);
}

static void the_ring_holds_the_last_few_runs_and_no_more(void) {
    char bytes[64] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 64u};
    const uint32_t ctrl = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);

    for (uint32_t run = 0u; run < 40u; ++run) {
        type(&ctx, "q", 1u);
    }

    CF_CHECK(text.length == 40u, "the value is the buffer's own, not the ring's");

    uint32_t steps = 0u;
    uint32_t held = text.length;

    while (steps < 40u) {
        chord(&ctx, 'z', ctrl);

        if (text.length == held) {
            break;
        }

        held = text.length;
        steps++;
    }

    CF_CHECK(steps == 15u, "sixteen images hold fifteen steps back");
    CF_CHECK(held == 25u, "and the runs older than that fell out as the ring filled");
    CF_CHECK(cf_state_bytes(&ctx) <= 4096u, "the whole history stayed inside its budget");

    cf_ctx_free(&ctx);
}

static void bytes_a_cut_took_come_back_with_their_selection(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    select_tail(&ctx, "abc", 2u);
    chord(&ctx, 'x', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 1u, "the cut took the selected run");

    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && memcmp(bytes, "abc", 3) == 0, "and one step puts the bytes back");
    CF_CHECK(text.caret == 1u && text.mark == 3u, "with the range they were cut from selected");
    CF_CHECK(board.length == 2u, "while what the host was handed stays where it was left");

    cf_ctx_free(&ctx);
}

/*
 * The word keys move over the runs the field already walks one scalar at a time.
 * Three classes divide a buffer here - a blank, a letter or digit, anything else -
 * and a key starting in a run finishes it, while a key starting in the gap crosses
 * the gap and finishes the run beyond it, because the reader asking for the next
 * word has no use for a caret parked between the two.
 */
static void a_word_key_steps_the_run_it_stands_in(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    const uint32_t word = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    type_from_home(&ctx, "let me in");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 3u && text.mark == 3u, "the first step spends the word it stands in");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 6u, "the next crosses the blank and finishes the word beyond it");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 9u, "and the last lands past the final letter");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 9u && text.mark == 9u, "past the end the key spends itself on nothing");

    cf_ctx_free(&ctx);
}

static void a_word_key_comes_back_over_the_same_runs(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    const uint32_t word = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "let me in", 9u);

    /* Typing leaves the caret at the end, so the first back key owns the scalar behind
     * it - the last letter of the last word, not the gap after the buffer. */
    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 7u, "a back word stops at the start of the run behind the caret");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 4u, "crossing the blank reaches the word before it");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 0u, "and the walk ends at the front of the buffer");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 0u, "where a further key keeps it rather than wrapping");

    /* The two walks meet: forward from the front lands where back from the end does. */
    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 3u, "and the same runs answer from the other side");

    cf_ctx_free(&ctx);
}

static void a_word_key_crosses_the_punctuation_between_two_words(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    const uint32_t word = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    type_from_home(&ctx, "foo.bar");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 3u, "the run of letters ends where the dot begins");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 4u, "and the dot is a run of its own, not a thing to slide past");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 7u, "so the word after it is one key further");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 4u, "coming back stops before the word it left");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 3u, "then before the mark between the words");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 0u, "and at the front of what is left");

    cf_ctx_free(&ctx);
}

static void shift_with_a_word_key_selects_the_run_it_ends_on(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    const uint32_t word = (uint32_t)CF_MOD_CONTROL;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    type_from_home(&ctx, "let me in");

    hold_with(&ctx, CF_KEY_RIGHT, word | (uint32_t)CF_MOD_SHIFT);
    CF_CHECK(text.caret == 3u && text.mark == 0u, "the shift holds the mark and takes the run");

    hold_with(&ctx, CF_KEY_RIGHT, word | (uint32_t)CF_MOD_SHIFT);
    CF_CHECK(text.caret == 6u && text.mark == 0u, "and every further word key widens the range");

    hold_with(&ctx, CF_KEY_RIGHT, word);
    CF_CHECK(text.caret == 6u && text.mark == 6u,
             "an arrow without the shift collapses it to the end the word key reached");

    hold_with(&ctx, CF_KEY_LEFT, word);
    CF_CHECK(text.caret == 4u, "and only then does it step back over the run it was standing in");

    cf_ctx_free(&ctx);
}

static void a_word_key_answers_either_primary_modifier_and_not_alt(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    type_from_home(&ctx, "let me in");

    hold_with(&ctx, CF_KEY_RIGHT, (uint32_t)CF_MOD_META);
    CF_CHECK(text.caret == 3u, "the command key moves a word where control does on another desk");

    hold_with(&ctx, CF_KEY_RIGHT, (uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_ALT);
    CF_CHECK(text.caret == 4u, "and an alt in the chord leaves the arrow stepping one scalar");

    hold_with(&ctx, CF_KEY_RIGHT, 0u);
    CF_CHECK(text.caret == 5u, "with nothing held it steps the one after that");

    cf_ctx_free(&ctx);
}

static void the_chord_holds_every_byte_the_buffer_carries(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    uint32_t seen = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_watched(&ctx, &text, false, count_keys, &seen);
    press(&ctx, 10.0f, 10.0f);
    chord(&ctx, 'a', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.caret == 0u && text.mark == 0u,
             "an empty buffer is left holding its own nothing");
    CF_CHECK(seen == 0u, "and the chord the field serves reaches no handler above");

    type(&ctx, "let me in", 9u);
    hold(&ctx, CF_KEY_HOME, false);
    chord(&ctx, 'a', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.caret == 9u && text.mark == 0u,
             "the whole value is selected, whichever end an arrow was last put at");

    chord(&ctx, 'a', 0u);
    CF_CHECK(seen == 1u, "a letter with no modifier is the host's text, not a chord to serve");

    type(&ctx, "x", 1u);

    CF_CHECK(text.length == 1u && bytes[0] == 'x', "and the next run typed goes over all of it");
    CF_CHECK(text.caret == 1u && text.mark == 1u, "with the caret after what landed");

    chord(&ctx, 'a', (uint32_t)CF_MOD_META);
    CF_CHECK(text.caret == 1u && text.mark == 0u, "and the command key carries it as control does");

    cf_ctx_free(&ctx);
}

/*
 * The refusal a page can see. A full buffer takes nothing, which is the right rule for
 * bytes the build owns - half a UTF-8 sequence is bytes nothing reads back - but a key
 * that landed nowhere looks exactly like a key that was never sent unless the field
 * says otherwise. `refusals` is that statement, and it names only the runs turned away
 * for want of room.
 */
static void a_run_that_will_not_fit_says_so_in_the_buffer(void) {
    char bytes[8] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 8u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abcdefg", 7u);
    type(&ctx, "h", 1u);

    CF_CHECK(text.length == 8u && text.refusals == 0u,
             "filling the buffer to its last byte refuses nothing");

    type(&ctx, "i", 1u);

    CF_CHECK(text.length == 8u && text.refusals == 1u,
             "the byte past the end is turned away and said");

    /* The whole value selected and one run typed: the range the keys hold is room the
     * run is given, so nothing was refused. */
    chord(&ctx, 'a', (uint32_t)CF_MOD_CONTROL);
    type(&ctx, "z", 1u);

    CF_CHECK(text.length == 1u && bytes[0] == 'z' && text.refusals == 1u,
             "a run the selection made room for lands and costs no refusal");

    host_holds(&board, "eleven bytes");
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 1u && text.refusals == 2u,
             "and a pasted run too long for the buffer is refused whole, counted once");

    /* A run carrying a break is refused by the row rule rather than the room: two
     * different asks, and the page is told about the one it can act on. */
    type(&ctx, "a\nb", 3u);

    CF_CHECK(text.length == 1u && text.refusals == 2u,
             "a run refused for its break is no room refusal");

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
    CF_RUN(copy_puts_the_selection_where_the_host_keeps_bytes);
    CF_RUN(cut_offers_the_bytes_before_it_drops_them);
    CF_RUN(a_refused_write_leaves_the_range_selected);
    CF_RUN(paste_inserts_what_the_host_holds_over_the_selection);
    CF_RUN(a_host_run_too_long_for_the_buffer_lands_nowhere);
    CF_RUN(a_host_with_no_text_leaves_the_page_alone);
    CF_RUN(either_primary_modifier_carries_the_chord_and_alt_does_not);
    CF_RUN(a_page_with_no_host_buffer_leaves_the_chord_unstopped);
    CF_RUN(stepping_back_returns_what_the_keys_took);
    CF_RUN(an_edit_after_a_step_back_ends_the_forward_walk);
    CF_RUN(a_step_forward_returns_to_where_the_marks_stood);
    CF_RUN(a_field_rebuilt_over_a_smaller_buffer_forgets_the_old_layout);
    CF_RUN(a_caret_move_costs_no_step);
    CF_RUN(a_history_outlives_the_frame_that_wrote_it);
    CF_RUN(a_buffer_too_large_to_copy_keeps_no_history);
    CF_RUN(the_ring_holds_the_last_few_runs_and_no_more);
    CF_RUN(bytes_a_cut_took_come_back_with_their_selection);
    CF_RUN(a_word_key_steps_the_run_it_stands_in);
    CF_RUN(a_word_key_comes_back_over_the_same_runs);
    CF_RUN(a_word_key_crosses_the_punctuation_between_two_words);
    CF_RUN(shift_with_a_word_key_selects_the_run_it_ends_on);
    CF_RUN(a_word_key_answers_either_primary_modifier_and_not_alt);
    CF_RUN(the_chord_holds_every_byte_the_buffer_carries);
    CF_RUN(a_run_that_will_not_fit_says_so_in_the_buffer);

    return CF_REPORT();
}
