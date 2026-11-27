#include <string.h>

#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/* The box this frame laid for a slot, or a rect of no extent where it laid none. A caller outside
 * src/node cannot index rect storage, so the tier reads the answer the frame gives. */
static cf_rect box_of(const cf_ctx *ctx, uint32_t slot) {
    const cf_rect *box = cf_frame_rect(ctx, slot);

    return box != NULL ? *box : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
}

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
 * One frame of the loop: a field over the build's buffer, declared inside the frame -
 * the bytes it keeps live in the arena cf_frame_begin emptied. `keys` is the root's own
 * key handler, for the cases that need to know which chords the field below it let
 * through. `ask` and `moved` are the doors cf_field leaves its caller beside the keys it
 * keeps for itself, which is where a page puts what to do with a value once it has moved
 * and once it is done with.
 *
 * `mode` is that word the build says, and `height` the box it pins the field in: a case
 * about the mode has to be able to state both, because the mode decides what a row is
 * whatever the box turned out to be. A height of none leaves the box to the measure, so a
 * case can read the height a field asks for.
 */
static void frame_at(cf_ctx *ctx, cf_text_edit *text, bool disabled, cf_field_mode mode,
                     float height, cf_event_fn keys, void *key_data, cf_event_fn ask,
                     void *ask_data, cf_event_fn moved, void *moved_data) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node field = cf_field(ctx, "entry", text, &stack, mode);
    field.disabled = disabled;
    field.handlers.activate = ask;
    field.handlers.activate_data = ask_data;
    field.handlers.change = moved;
    field.handlers.change_data = moved_data;

    if (height > 0.0f) {
        field.style.height = CF_PX(height);
    }

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

/* The field of the mode this API shipped with, in a box the value's own rows make. */
static void frame_doors(cf_ctx *ctx, cf_text_edit *text, bool disabled, cf_event_fn keys,
                        void *key_data, cf_event_fn ask, void *ask_data, cf_event_fn moved,
                        void *moved_data) {
    frame_at(ctx, text, disabled, CF_FIELD_SINGLE, 0.0f, keys, key_data, ask, ask_data, moved,
             moved_data);
}

static void frame_watched(cf_ctx *ctx, cf_text_edit *text, bool disabled, cf_event_fn keys,
                          void *key_data) {
    frame_doors(ctx, text, disabled, keys, key_data, NULL, NULL, NULL, NULL);
}

static void frame_of(cf_ctx *ctx, cf_text_edit *text, bool disabled) {
    frame_watched(ctx, text, disabled, NULL, NULL);
}

/*
 * Two boxes that take the keyboard, stacked: the page for the one ask a single field cannot serve,
 * which is a hand that moves the keyboard onto the box below it. Both state their height, so the
 * rows are known and a press can be aimed at one of them without measuring anything.
 */
static void frame_pair(cf_ctx *ctx, cf_text_edit *first, cf_text_edit *second) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node a = cf_field(ctx, "first", first, &stack, CF_FIELD_SINGLE);
    cf_node b = cf_field(ctx, "second", second, &stack, CF_FIELD_SINGLE);
    a.style.height = CF_PX(20.0f);
    b.style.height = CF_PX(20.0f);

    const cf_node roots[] = {a, b};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(200.0f);
    root.children = roots;
    root.child_count = 2u;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

/*
 * The same page one frame later with the box on top gone out of it: the name the build stopped
 * asking for retires, and only the repair at the end of the diff can hand back what that box was
 * deciding. The box left standing is a field, so the page still takes written text.
 */
static void frame_after_the_first_leaves(cf_ctx *ctx, cf_text_edit *second) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node b = cf_field(ctx, "second", second, &stack, CF_FIELD_SINGLE);
    b.style.height = CF_PX(20.0f);

    const cf_node roots[] = {b};
    cf_node root = {.style = cf_style_default()};
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

/*
 * What an input method says it is holding: the whole run, in place of the one the event before
 * staged. An empty run is the method finished with it without choosing anything out of it, and a
 * device sends the keys that made it beside the run, as it does for any typed character.
 */
static void compose(cf_ctx *ctx, const char *run) {
    cf_input(ctx, (cf_event){
                      .type = CF_EVENT_COMPOSITION,
                      .text = {.chars = run, .length = run != NULL ? (uint32_t)strlen(run) : 0u}});
}

/* The hand letting go where it pressed. A press without its release leaves the capture on the box
 * that answered it, so the next press would be served by that node rather than the one under it. */
static void lift(cf_ctx *ctx, float x, float y) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = x, .y = y}});
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

    const cf_id field = cf_node_id(&ctx, child_of(&ctx, cf_frame_root(&ctx), 0u));

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_focused(&ctx, field), "the press focuses the field");

    type(&ctx, "h", 1u);
    type(&ctx, "i", 1u);

    CF_CHECK(text.length == 2u && bytes[0] == 'h' && bytes[1] == 'i',
             "what the keyboard wrote lands in the buffer in order");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)CF_KEY_BACKSPACE});

    CF_CHECK(text.length == 1u && bytes[0] == 'h', "backspace drops the last character");

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

    const cf_id field = cf_node_id(&ctx, child_of(&ctx, cf_frame_root(&ctx), 0u));

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
             "shift+right takes the next character into what is selected");

    hold(&ctx, CF_KEY_END, true);

    CF_CHECK(text.caret == 3u && text.mark == 0u, "and shift+end reaches the last byte");

    hold(&ctx, CF_KEY_LEFT, false);

    CF_CHECK(text.caret == 0u && text.mark == 0u,
             "an arrow with nothing held collapses the range to the end it points at");

    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 1u && text.mark == 1u, "and the next one steps from there");

    cf_ctx_free(&ctx);
}

static void an_arrow_steps_a_whole_character(void) {
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

    CF_CHECK(text.length == 4u, "three characters in four bytes");

    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 1u, "the first step leaves the one-byte character");

    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 3u, "and the next one leaves the two-byte character whole");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 2u && bytes[0] == 'a' && bytes[1] == 'b',
             "backspace from past it takes the whole character, not the byte that ends it");

    cf_ctx_free(&ctx);
}

/*
 * The bytes the cluster cases below write: a family, which is three pictures and the two
 * joiners that tie them, an e with its accent written onto it, and a flag from two
 * regional indicators. They are spelled in bytes because every claim here is about a byte
 * count - what one arrow and one delete owe a character the reader sees as one thing.
 */
static const char joined_family[] = "\xF0\x9F\x91\xA8"
                                    "\xE2\x80\x8D"
                                    "\xF0\x9F\x91\xA9"
                                    "\xE2\x80\x8D"
                                    "\xF0\x9F\x91\xA7";
static const char accented_e[] = "e"
                                 "\xCC\x81";
static const char flag_pair[] = "\xF0\x9F\x87\xBA"
                                "\xF0\x9F\x87\xB8";

static void an_accent_leaves_the_field_under_the_letter_it_sits_on(void) {
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
    type(&ctx, accented_e, 3u);
    type(&ctx, "x", 1u);

    CF_CHECK(text.length == 4u, "the accent costs bytes and no character");

    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 3u, "one arrow crosses the letter with the accent on it");

    hold(&ctx, CF_KEY_LEFT, false);

    CF_CHECK(text.caret == 0u, "and the same key comes back over both");

    hold(&ctx, CF_KEY_RIGHT, true);

    CF_CHECK(text.caret == 3u && text.mark == 0u, "shift holds the pair as one selection");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 1u && bytes[0] == 'x', "and one delete leaves no mark behind");

    cf_ctx_free(&ctx);
}

static void a_joined_sequence_enters_and_leaves_the_field_as_one_character(void) {
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
    type(&ctx, joined_family, 18u);
    type(&ctx, "z", 1u);

    CF_CHECK(text.length == 19u, "the family costs eighteen bytes and one character");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 18u, "the plain letter beside it goes on its own");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 0u, "and the next delete takes the joined pictures whole");

    type(&ctx, joined_family, 18u);
    type(&ctx, "z", 1u);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_DELETE, false);

    CF_CHECK(text.length == 1u && bytes[0] == 'z', "delete from the front does the same");

    type(&ctx, joined_family, 18u);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, true);

    CF_CHECK(text.caret == 18u && text.mark == 0u, "and a selection reaches past all of it");

    type(&ctx, "Q", 1u);

    CF_CHECK(text.length == 2u && bytes[0] == 'Q' && bytes[1] == 'z',
             "so what is written replaces the character and not a piece of it");

    cf_ctx_free(&ctx);
}

static void two_indicator_letters_are_one_character_too(void) {
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
    type(&ctx, flag_pair, 8u);
    type(&ctx, "!", 1u);

    CF_CHECK(text.length == 9u, "the flag is eight bytes of the buffer and one character");

    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 8u, "the pair is one step, not two");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 1u && bytes[0] == '!', "and one delete takes the flag whole");

    type(&ctx, flag_pair, 8u);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, true);
    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 1u && bytes[0] == '!', "as does the selection it leaves");

    cf_ctx_free(&ctx);
}

/*
 * The word key moves between runs of one class and steps the clusters along the way, so
 * the jump ends past an accent rather than under it - which is the difference between a
 * word key and one that leaves a mark with nothing to sit on.
 */
static void a_word_key_leaves_no_mark_behind_at_the_end_of_its_run(void) {
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
    type(&ctx, "caf", 3u);
    type(&ctx, accented_e, 3u);
    type(&ctx, "!", 1u);

    CF_CHECK(text.length == 7u, "four letters, one of them carrying an accent");

    hold(&ctx, CF_KEY_HOME, false);
    hold_with(&ctx, CF_KEY_RIGHT, (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.caret == 6u, "the jump ends past the accent, not under it");

    hold_with(&ctx, CF_KEY_LEFT, (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.caret == 0u, "and the same key comes back over the whole run");

    cf_ctx_free(&ctx);
}

/*
 * A click measures along the ink and answers a scalar boundary, which for a joined
 * picture is a place inside one character. The keys still owe the person the whole
 * character: the caret moves out of it before it moves on.
 */
static void a_caret_a_click_placed_inside_a_character_moves_out_of_it(void) {
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
    type(&ctx, joined_family, 18u);

    /* Five scalars at ten points each, the content inset by eight and a border: the
     * third picture's ink stands between x 29 and x 39, and a point inside it answers
     * one of the boundaries about it. */
    press(&ctx, 36.0f, 10.0f);

    CF_CHECK(text.caret > 0u && text.caret < 18u, "the press lands inside the sequence");

    hold(&ctx, CF_KEY_LEFT, false);

    CF_CHECK(text.caret == 0u, "one arrow brings the caret out in front of it");

    hold(&ctx, CF_KEY_RIGHT, false);

    CF_CHECK(text.caret == 18u, "and the other takes the whole character past it");

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

static void the_edit_keys_cut_the_selection_or_the_character_they_name(void) {
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

/*
 * A build may seed a value with more breaks than the field keeps rows for: the count a text walk
 * returns is the count of the value's rows, while the array behind it holds FIELD_VISIBLE_LINES of
 * them. A point below the rows held used to name a row past that array, and the only honest answer
 * is the last row the field can see - the same answer a point below a short value gives. A point
 * above it still lands where it stands, so the limit did not become the rule.
 */
static void a_press_below_the_rows_the_field_holds_stands_on_the_last_one(void) {
    char bytes[64];
    memset(bytes, 0, sizeof bytes);

    /* Twelve rows of three bytes, so a row begins every fourth byte and the eighth begins at 28. */
    for (uint32_t row = 0u; row < 12u; ++row) {
        bytes[row * 4u] = 'a';
        bytes[row * 4u + 1u] = 'b';
        bytes[row * 4u + 2u] = 'c';

        if (row + 1u < 12u) {
            bytes[row * 4u + 3u] = '\n';
        }
    }

    cf_text_edit text = {.chars = bytes, .length = 47u, .capacity = 64u, .caret = 0u, .mark = 0u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    /* The value is laid before the press, because the walk reads the frame's rects. */
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 30.0f);

    CF_CHECK(text.caret < 28u, "a press in the rows the field holds stands in one of them");

    press(&ctx, 10.0f, 190.0f);

    CF_CHECK(text.caret == 28u,
             "a press below the rows held reads the last row held, not the memory past it");

    cf_ctx_free(&ctx);
}

/*
 * A multiline field with the same value, pushed `above` points down the page by a box of its own.
 * The row tests need this because the page they were written on had the field at its top, where the
 * box's own origin and the page's agree and a walk that counted the distance from both lands on the
 * same row anyway.
 */
static void frame_tall_of(cf_ctx *ctx, cf_text_edit *text, float above) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node field = cf_field(ctx, "entry", text, &stack, CF_FIELD_MULTILINE);

    cf_node gap = {.style = cf_style_default()};
    gap.style.height = CF_PX(above);

    const cf_node roots[] = {gap, field};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(400.0f);
    root.children = roots;
    root.child_count = 2u;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(400.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

/*
 * Where a click lands is a question about the box it landed in. A walk that measured the press from
 * the page and then from the box as well would answer one row higher for every point the box sat
 * lower - which is what a field at the top of a page cannot show, and what the multiline box in the
 * middle of a real page hits on the first click.
 */
static void a_press_lands_on_the_row_of_the_box_rather_than_the_row_of_the_page(void) {
    static const char value[] = "abc\ndef\nghi\njkl\nmno\npqr\nstu\nvwx\nyz0\n123\n456\n789";

    char near_bytes[64];
    char far_bytes[64];
    memset(near_bytes, 0, sizeof near_bytes);
    memset(far_bytes, 0, sizeof far_bytes);
    memcpy(near_bytes, value, sizeof value - 1u);
    memcpy(far_bytes, value, sizeof value - 1u);

    cf_text_edit near = {
        .chars = near_bytes, .length = sizeof value - 1u, .capacity = sizeof near_bytes};
    cf_text_edit far = {
        .chars = far_bytes, .length = sizeof value - 1u, .capacity = sizeof far_bytes};

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    /* The box at the top of the page, and a press one hundred and thirty points down it. */
    frame_tall_of(&ctx, &near, 0.0f);
    press(&ctx, 10.0f, 130.0f);

    const uint32_t stood_on = near.caret;

    CF_CHECK(stood_on != 0u && stood_on < 28u,
             "a press down the rows of a box at the top of the page stands in one of them");

    /* The same box a hundred points further down the page, pressed a hundred points further down -
     * the same place in the box, named from where the page is. */
    far.caret = 0u;
    far.mark = 0u;
    frame_tall_of(&ctx, &far, 100.0f);
    press(&ctx, 10.0f, 230.0f);

    CF_CHECK(far.caret == stood_on,
             "and the same place in a box the page carried lower lands in the same row");

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

/* What the change door was handed, so a test can tell which key moved the bytes and
 * how many runs the door was asked about. */
typedef struct moved_note {
    uint32_t calls;
    uint32_t target;
    uint32_t key;
    uint32_t bytes;
} moved_note;

static void count_change(cf_event *event, void *user_data) {
    moved_note *note = user_data;

    note->calls++;
    note->target = event->target;
    note->key = event->key;
    note->bytes = event->text.length;
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

/*
 * Two keys a focused field is handed and answers the other way round. The space bar is
 * the character the device sends beside it, so the field keeps it and no ask to act
 * comes out of the box a person is still typing into; Enter is the value being done
 * with, and the page's own door on the field is how it hears that. A key with a
 * modifier on it is a chord somebody bound elsewhere, so it is neither.
 */
static void a_space_in_a_field_is_a_character_and_enter_is_the_ask(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    uint32_t asks = 0u;
    uint32_t through = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_doors(&ctx, &text, false, count_keys, &through, count_keys, &asks, NULL, NULL);
    press(&ctx, 10.0f, 10.0f);

    type(&ctx, "a b", 3u);

    CF_CHECK(text.length == 3u && bytes[1] == ' ', "the space arrives as a byte from the device");

    hold_with(&ctx, CF_KEY_SPACE, 0u);

    CF_CHECK(asks == 0u, "the space bar the field types asks the field to do nothing");
    CF_CHECK(through == 0u, "and the key stops at the box that kept it");
    CF_CHECK(text.length == 3u, "a key down with no text writes no byte of its own");

    hold_with(&ctx, CF_KEY_ENTER, 0u);

    CF_CHECK(asks == 1u, "Enter is the ask the page hears through its own door");
    CF_CHECK(through == 1u, "and the key still climbs, so a form above can bind it too");

    hold_with(&ctx, CF_KEY_SPACE, (uint32_t)CF_MOD_SHIFT);

    CF_CHECK(asks == 1u && through == 1u,
             "a shift on the space bar changes the shift, not the key");

    hold_with(&ctx, CF_KEY_SPACE, (uint32_t)CF_MOD_CONTROL);
    hold_with(&ctx, CF_KEY_ENTER, (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(asks == 1u, "a chord is nobody's plain ask to act");
    CF_CHECK(through == 3u, "and both are left to whoever above bound them");

    cf_ctx_free(&ctx);
}

/*
 * The door a page puts on a field to hear that its value moved. What counts as a move is
 * the whole contract: a run is one change however many bytes it carried, and the keys that
 * only move where the caret stands - an arrow, Home, a select-all - leave the bytes the
 * page reads untouched, so a form that re-validates on this door does not re-validate on
 * every step through the value. A run that was refused is no change either.
 */
static void the_change_door_hears_the_bytes_and_not_the_caret(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_doors(&ctx, &text, false, NULL, NULL, NULL, NULL, count_change, &moved);
    press(&ctx, 10.0f, 10.0f);

    type(&ctx, "abc", 3u);

    CF_CHECK(moved.calls == 1u, "one run the keyboard wrote is one change");
    CF_CHECK(moved.bytes == 3u, "and the door is asked on the event the bytes came in with");

    type(&ctx, "d", 1u);

    CF_CHECK(moved.calls == 2u && text.length == 4u, "the next key is the next change");

    hold(&ctx, CF_KEY_LEFT, false);
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_END, true);
    chord(&ctx, 'a', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 2u, "the keys that move the caret and the selection say nothing");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(moved.calls == 3u && text.length == 0u,
             "the key that took the selected bytes is one change");
    CF_CHECK(moved.key == (uint32_t)CF_KEY_BACKSPACE,
             "and the door is handed the key that made it");

    /* More bytes than an empty buffer has room for: refused whole, so the value the page
     * reads is the one it read before the key. */
    type(&ctx, "012345678901234567", 18u);

    CF_CHECK(moved.calls == 3u && text.length == 0u && text.refusals == 1u,
             "a run that lands nowhere reports no change");

    cf_ctx_free(&ctx);
}

/*
 * The other ways the field's own keys move the value: the cut that hands bytes to the
 * host, the paste that takes them back, and the two directions of the history. Copy is
 * here because it is the one chord that trades with the host and changes nothing, and the
 * spent steps are here because a key at the end of the history is a key the field used up
 * rather than an edit the page has to look at.
 */
static void cutting_pasting_and_stepping_the_history_move_the_value(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_doors(&ctx, &text, false, NULL, NULL, NULL, NULL, count_change, &moved);
    select_tail(&ctx, "abcd", 2u);

    CF_CHECK(moved.calls == 1u, "the run that filled the buffer is one change");

    chord(&ctx, 'c', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 1u && text.length == 4u, "copy hands bytes out and moves nothing");

    chord(&ctx, 'x', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 2u && text.length == 2u, "cut takes the selection, so it moved");

    host_holds(&board, "ef");
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 3u && text.length == 4u, "and a paste puts bytes in");

    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);
    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);
    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 6u && text.length == 0u,
             "each step back that returned bytes is a change of its own");

    chord(&ctx, 'z', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 6u, "a step past the oldest state is a spent key, not a change");

    chord(&ctx, 'y', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(moved.calls == 7u && text.length == 4u, "and the step forward moved them again");

    cf_ctx_free(&ctx);
}

/*
 * Who answers the door, and who does not. The ask is of the node whose widget edited the
 * value - a container above it is not asked, because it can read the same buffer in its
 * own build - and a field switched off edits nothing, so it has no change to report.
 */
static void the_change_door_answers_the_box_whose_value_moved(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_doors(&ctx, &text, false, NULL, NULL, NULL, NULL, count_change, &moved);

    const uint32_t field = child_of(&ctx, cf_frame_root(&ctx), 0u);

    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);

    CF_CHECK(moved.calls == 1u && moved.target == field,
             "the ask names the box whose buffer the run went into");

    /* A run refused for the break it carries: no byte of the value moved, so the door
     * stays shut on it the way it does on a run that would not fit. */
    type(&ctx, "a\nb", 3u);

    CF_CHECK(moved.calls == 1u && text.length == 2u,
             "a run refused for its break says so nowhere but the buffer");

    cf_ctx_free(&ctx);

    /* The same door on a field that takes no keys: switched off, it focuses on nothing and
     * edits nothing, so it reports nothing. */
    memset(bytes, 0, sizeof bytes);
    text = (cf_text_edit){.chars = bytes, .length = 0u, .capacity = 16u};
    moved = (moved_note){0};

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts again");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_doors(&ctx, &text, true, NULL, NULL, NULL, NULL, count_change, &moved);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);

    CF_CHECK(moved.calls == 0u && text.length == 0u, "a field switched off reports no change");

    cf_ctx_free(&ctx);
}

/*
 * One read of what a mode asks a box for: the field declared, the value typed into it, and
 * the height the next build's layout gave it. Typing moves the buffer and not the box, so
 * the answer only shows up once the field is declared again over the value it now holds -
 * which is exactly the sequence a page that keeps its field alive goes through.
 */
static float field_box_height(cf_ctx *ctx, cf_text_edit *text, cf_field_mode mode, float height,
                              const char *run) {
    frame_at(ctx, text, false, mode, height, NULL, NULL, NULL, NULL, NULL, NULL);
    press(ctx, 10.0f, 10.0f);
    type(ctx, run, (uint32_t)strlen(run));
    frame_at(ctx, text, false, mode, height, NULL, NULL, NULL, NULL, NULL, NULL);

    return box_of(ctx, child_of(ctx, cf_frame_root(ctx), 0u)).height;
}

/*
 * The rows a field lays are the mode's doing, and the first thing the mode says is what the
 * value asks of its box: the same bytes in the same 200-point column keep one row's height
 * when the field refuses to wrap and take as many rows as the width breaks them into when
 * it does. The mono pen moves ten points a scalar and the field spends eighteen points of
 * the column on its border and padding, so thirty bytes of ordinary English lay two rows in
 * a wrapping field and one in a field that refuses to be laid out over more.
 */
static void a_multiline_field_asks_for_the_height_its_rows_need(void) {
    static const char value[] = "the quick brown fox jumps over";
    char flat_bytes[64] = {0};
    char tall_bytes[64] = {0};
    cf_text_edit flat = {.chars = flat_bytes, .length = 0u, .capacity = 64u};
    cf_text_edit tall = {.chars = tall_bytes, .length = 0u, .capacity = 64u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    const float single = field_box_height(&ctx, &flat, CF_FIELD_SINGLE, 0.0f, value);

    cf_ctx_free(&ctx);

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts again");
        return;
    }

    ctx.theme = cf_theme_default();
    const float multi = field_box_height(&ctx, &tall, CF_FIELD_MULTILINE, 0.0f, value);

    cf_ctx_free(&ctx);

    CF_CHECK(single > 0.0f && multi > single,
             "the same bytes ask a wrapping field for more height than a nowrap one");
    CF_CHECK(multi - single == 20.0f, "and the difference is whole rows of the line height");
}

/*
 * The whole difference between the two modes on one key. A single-line field leaves Enter
 * unstopped, which is how a form's save handler hears the value is done with; a multiline
 * one writes the break the rows are made of and stops, so the same keystroke never both
 * breaks a line and submits the buffer. The break is bytes the field wrote, so the change
 * door runs for it like it runs for any other run.
 */
static void enter_writes_a_row_in_one_mode_and_asks_in_the_other(void) {
    char bytes[16] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 16u};
    uint32_t asks = 0u;
    uint32_t through = 0u;
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_MULTILINE, 0.0f, count_keys, &through, count_keys, &asks,
             count_change, &moved);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold_with(&ctx, CF_KEY_ENTER, 0u);

    CF_CHECK(text.length == 3u && bytes[2] == '\n', "Enter writes the break a row is made of");
    CF_CHECK(text.caret == 3u && text.mark == 3u, "and the caret comes to stand past it");
    CF_CHECK(asks == 0u && through == 0u,
             "the key that broke a line asks the page to save nothing, and climbs no further");
    CF_CHECK(moved.calls == 2u, "a break the field wrote is a change the page hears");

    /* A chord is still a binding rather than a character, so it is left to whoever above
     * made it - the same rule the space bar keeps in both modes. */
    hold_with(&ctx, CF_KEY_ENTER, (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && through == 1u && asks == 0u,
             "and a chord on Enter breaks no row and saves nothing");

    cf_ctx_free(&ctx);
}

/*
 * Up and down name rows, so the mode says whether the field has any to step between. A
 * single-line field stops for neither arrow: a page that binds them to its own history of
 * values still gets them. A multiline one answers them along the rows it paints, and keeps
 * the distance the caret had travelled across its own row - the same question a click
 * answers in reverse, which is what makes a step down a column land under the word the eye
 * was reading instead of at the row's edge.
 */
static void the_vertical_arrows_belong_to_the_mode_and_not_the_page(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    uint32_t through = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_MULTILINE, 0.0f, count_keys, &through, NULL, NULL, NULL,
             NULL);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold_with(&ctx, CF_KEY_ENTER, 0u);
    type(&ctx, "cd", 2u);

    CF_CHECK(text.length == 5u && text.caret == 5u, "the second run landed on the new row");

    hold(&ctx, CF_KEY_UP, false);

    CF_CHECK(text.caret == 2u && text.mark == 2u && through == 0u,
             "one arrow steps back onto the row above, at the distance it had travelled");

    hold(&ctx, CF_KEY_DOWN, true);

    CF_CHECK(text.caret == 5u && text.mark == 2u,
             "shift keeps the mark where the walk began, so the arrow reaches a row of the");

    /* Past the last row there is nowhere to go, and the arrow is spent rather than
     * passed on: a page that scrolls with its arrows would otherwise scroll a field that
     * was only asking to move its caret. */
    hold(&ctx, CF_KEY_DOWN, false);

    CF_CHECK(text.caret == 5u && through == 0u, "an arrow past the last row stays there");

    cf_ctx_free(&ctx);

    memset(bytes, 0, sizeof bytes);
    text = (cf_text_edit){.chars = bytes, .length = 0u, .capacity = 32u};
    through = 0u;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts again");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_SINGLE, 0.0f, count_keys, &through, NULL, NULL, NULL,
             NULL);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold(&ctx, CF_KEY_UP, false);
    hold(&ctx, CF_KEY_DOWN, false);

    CF_CHECK(through == 2u && text.caret == 2u,
             "a field that names no row leaves both arrows to whoever above bound them");

    cf_ctx_free(&ctx);
}

/*
 * What a field shows of a run the host holds is the mode's answer too: a pasted newline
 * lands on a row of its own in a multiline field and is refused whole in a single-line one,
 * because half a refused break is a value that reads back differently than it was written.
 * A carriage return is refused in either, since it lays no row anywhere and would only hold
 * a place in the bytes.
 */
static void a_pasted_break_lands_where_the_mode_puts_rows(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    fake_board board = {0};
    const cf_clipboard clipboard = board_of(&board);
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_at(&ctx, &text, false, CF_FIELD_MULTILINE, 0.0f, NULL, NULL, NULL, NULL, count_change,
             &moved);
    press(&ctx, 10.0f, 10.0f);
    host_holds(&board, "a\nb");
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && bytes[1] == '\n',
             "a pasted break opens a row in a multiline field");
    CF_CHECK(moved.calls == 1u, "and the run that landed is one change");

    /* The device's own spelling of a break: it names no row here either, so the bytes
     * beside it would hold a place in the value while standing nowhere. */
    host_holds(&board, "x\ry");
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 3u && moved.calls == 1u,
             "a carriage return is refused whole in either mode");

    cf_ctx_free(&ctx);

    memset(bytes, 0, sizeof bytes);
    text = (cf_text_edit){.chars = bytes, .length = 0u, .capacity = 32u};
    board = (fake_board){0};
    moved = (moved_note){0};

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts again");
        return;
    }

    ctx.theme = cf_theme_default();
    ctx.clipboard = &clipboard;
    frame_at(&ctx, &text, false, CF_FIELD_SINGLE, 0.0f, NULL, NULL, NULL, NULL, count_change,
             &moved);
    press(&ctx, 10.0f, 10.0f);
    host_holds(&board, "a\nb");
    chord(&ctx, 'v', (uint32_t)CF_MOD_CONTROL);

    CF_CHECK(text.length == 0u && moved.calls == 0u,
             "the same run lands nowhere in a field that has no rows to put it in");

    cf_ctx_free(&ctx);
}

/*
 * The mode is a word the build says, not a reading of the box the word ended up in. A
 * multiline field pinned to one row's height still takes the break Enter writes - the value
 * is longer than the box shows, and the arrows and the click walk reach it all the same -
 * and a single-line field given two rows' height still refuses it. A mode inferred from a
 * rendered height would turn both of these over the moment a window changed width, which is
 * why the road asks for the mode to be stated.
 */
static void the_mode_survives_the_box_it_was_pinned_in(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    uint32_t asks = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_MULTILINE, 34.0f, NULL, NULL, count_keys, &asks, NULL,
             NULL);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold_with(&ctx, CF_KEY_ENTER, 0u);

    CF_CHECK(text.length == 3u && asks == 0u,
             "a box one row tall does not turn a multiline field into a single-line one");
    CF_CHECK(box_of(&ctx, child_of(&ctx, cf_frame_root(&ctx), 0u)).height == 34.0f,
             "and the box stayed the height it was pinned at rather than the rows it holds");

    hold(&ctx, CF_KEY_UP, false);

    CF_CHECK(text.caret == 2u, "the arrow still walks back onto the row the box cannot show");

    cf_ctx_free(&ctx);

    memset(bytes, 0, sizeof bytes);
    text = (cf_text_edit){.chars = bytes, .length = 0u, .capacity = 32u};
    asks = 0u;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts again");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_SINGLE, 54.0f, NULL, NULL, count_keys, &asks, NULL, NULL);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "ab", 2u);
    hold_with(&ctx, CF_KEY_ENTER, 0u);

    CF_CHECK(text.length == 2u && asks == 1u,
             "and height handed to a single-line field does not give it rows to break into");

    cf_ctx_free(&ctx);
}

/*
 * The rows one walk holds are a fixed count and a value can outgrow them: twelve forced
 * breaks lay twelve rows and the walk keeps the first handful. Past the rows it kept there is
 * no row an arrow can reach, so the caret stays on the last one the walk laid rather than
 * stepping for a line the walk never measured - reading one would take whatever bytes sat
 * where a line should have been and place the caret from them, and a caret placed from bytes
 * the field does not hold is a caret nobody can find again.
 */
static void an_arrow_past_the_rows_the_walk_holds_stays_on_the_last_one(void) {
    static const char value[] = "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl";
    char bytes[64] = {0};
    memcpy(bytes, value, sizeof(value) - 1u);
    cf_text_edit text = {.chars = bytes,
                         .length = (uint32_t)(sizeof(value) - 1u),
                         .caret = 0u,
                         .mark = 0u,
                         .capacity = 64u};
    uint32_t through = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_at(&ctx, &text, false, CF_FIELD_MULTILINE, 0.0f, count_keys, &through, NULL, NULL, NULL,
             NULL);
    press(&ctx, 10.0f, 10.0f);
    hold(&ctx, CF_KEY_HOME, false);

    /* One step per row the walk could hold, and four past them. Each row is two bytes, so the
     * eighth one starts where the fourteenth byte is. */
    for (uint32_t i = 0u; i < 12u; ++i) {
        hold(&ctx, CF_KEY_DOWN, false);
    }

    CF_CHECK(text.caret == 14u, "the walk stops at the last row it laid a caret into");
    CF_CHECK(through == 0u, "and a step that found nowhere to go is still a key the field used");

    hold(&ctx, CF_KEY_UP, false);

    CF_CHECK(text.caret == 12u, "the row above it is reached the same way as any other");

    cf_ctx_free(&ctx);
}

/*
 * The composition protocol, replayed headlessly: what a device stages, what the box does with
 * it while it stands, and which of the two endings changes the value. A person on a desktop
 * types these through the desk's own method; the same agreement arrives here as events, which
 * is the tier that says the wiring holds.
 */
static void a_run_being_decided_is_not_the_pages_value(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    moved_note moved = {0};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_doors(&ctx, &text, false, NULL, NULL, NULL, NULL, count_change, &moved);
    press(&ctx, 10.0f, 10.0f);
    compose(&ctx, "ni");

    CF_CHECK(text.length == 0u, "the bytes a method is deciding never reach the value");
    CF_CHECK(moved.calls == 0u, "and the change door hears nothing about them");
    CF_CHECK(text.refusals == 0u, "a run the field has room for is not a refusal");

    /* Two characters at ten points each, on a row one line height tall: the box the candidates
     * stand beside is the run's own, not a caret the run has pushed past it. */
    const cf_rect box = cf_frame_composition_rect(&ctx);
    CF_CHECK(box.width == 20.0f && box.height == 20.0f, "the box is where the run stands");

    compose(&ctx, "nihao");

    CF_CHECK(cf_frame_composition_rect(&ctx).width == 50.0f,
             "and it follows the run the method rewrote");

    CF_CHECK(moved.calls == 0u, "a rewrite is still not a change the page made");

    compose(&ctx, "");

    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "an empty run is the method finished with it: the box goes empty");
    CF_CHECK(text.length == 0u, "and the value is the one it was before any of it");

    cf_ctx_free(&ctx);
}

/*
 * The same two boxes with room booked above them, which is how a page moves a box without the
 * keyboard doing anything: scrolling the row a run stands in off the top is the same shift with a
 * different name. The boxes state their height so the shift is the number the answer moves by.
 */
static void frame_pair_below(cf_ctx *ctx, cf_text_edit *first, cf_text_edit *second, float above) {
    cf_frame_begin(ctx);

    const cf_text_stack stack = {.fonts = &mono, .count = 1u};
    cf_node a = cf_field(ctx, "first", first, &stack, CF_FIELD_SINGLE);
    cf_node b = cf_field(ctx, "second", second, &stack, CF_FIELD_SINGLE);
    a.style.height = CF_PX(20.0f);
    b.style.height = CF_PX(20.0f);

    cf_node gap = {.style = cf_style_default()};
    gap.style.height = CF_PX(above);

    const cf_node roots[] = {gap, a, b};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(200.0f);
    root.children = roots;
    root.child_count = 3u;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

/*
 * Where the answer comes from: the run's place inside its box is staged when the method speaks,
 * and the box is read from the frame that is being asked. A page that moved the box while the run
 * stood - the wheel, a resize, a build that booked room above it - moves the candidate window along
 * with the letters, rather than leaving it over the row the run was staged on.
 */
static void the_box_a_run_stands_in_follows_the_frame_that_lays_it(void) {
    char head[32] = {0};
    char tail[32] = {0};
    cf_text_edit first = {.chars = head, .length = 0u, .capacity = 32u};
    cf_text_edit second = {.chars = tail, .length = 0u, .capacity = 32u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_pair(&ctx, &first, &second);
    press(&ctx, 10.0f, 10.0f);
    lift(&ctx, 10.0f, 10.0f);
    compose(&ctx, "ni");

    const cf_rect stood = cf_frame_composition_rect(&ctx);

    CF_CHECK(stood.width == 20.0f && stood.height == 20.0f,
             "the run is decided on a row of its own, wherever that row stands");

    frame_pair_below(&ctx, &first, &second, 60.0f);

    const cf_rect moved = cf_frame_composition_rect(&ctx);

    /* The shift is the whole of the assertion: a box-relative place read against the rect this
     * frame laid moves by exactly what the box moved by, and an answer that had kept the point the
     * keystroke landed on would not move at all. */
    CF_CHECK(moved.y == stood.y + 60.0f && moved.x == stood.x,
             "and a frame that laid the box lower reports the run lower, with no key between");
    CF_CHECK(moved.width == stood.width && moved.height == stood.height,
             "the run itself is the same length the method staged");
    CF_CHECK(cf_frame_takes_text(&ctx), "the keyboard is still on a box that takes text");
    CF_CHECK(first.length == 0u, "and nothing reached the value while the page moved");

    /* The box is the claiming field's, so a frame that took the box away takes the answer with it
     * - and it is the claim's own going, not a rect left over from a layout nobody has. */
    compose(&ctx, "");

    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "and the finished run leaves no box standing anywhere on the page");

    cf_ctx_free(&ctx);
}

static void the_words_chosen_replace_the_bytes_the_run_covered(void) {
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

    /* Hold the first two bytes: the run a method stages over a selection replaces it, which is
     * how a person corrects a word they had already typed. */
    hold(&ctx, CF_KEY_HOME, false);
    hold(&ctx, CF_KEY_RIGHT, true);
    hold(&ctx, CF_KEY_RIGHT, true);
    compose(&ctx, "ni");

    CF_CHECK(text.length == 3u, "the run covers the selection without touching the value");
    CF_CHECK(text.caret == 2u && text.mark == 0u,
             "and the selection the commit needs is still the one the box held");

    /* The words the person settled on arrive as text, and they land across the bytes the
     * preedit was drawn over: two letters out, one ideograph in, the tail untouched. */
    type(&ctx, "\xE6\x9D\x8E", 3u);

    CF_CHECK(text.length == 4u, "the commit replaces exactly the range the run covered");
    CF_CHECK(memcmp(bytes,
                    "\xE6\x9D\x8E"
                    "c",
                    4u) == 0,
             "and the value reads the words chosen");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f, "the run went with its box");

    cf_ctx_free(&ctx);
}

static void a_key_the_method_owns_does_not_reach_the_value(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    uint32_t through = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_watched(&ctx, &text, false, count_keys, &through);
    press(&ctx, 10.0f, 10.0f);
    type(&ctx, "abc", 3u);
    compose(&ctx, "ni");

    /* The rebuild the run has to survive: it rides in the block the field's name keys, and a
     * build reads it back before the box lays anything, so the key below still lands on it. */
    frame_watched(&ctx, &text, false, count_keys, &through);
    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 3u, "a step back while a run stands takes no byte of the value");
    CF_CHECK(through == 0u, "and the key is the field's to stop, not the page's to hear");

    hold(&ctx, CF_KEY_BACKSPACE, false);

    CF_CHECK(text.length == 2u,
             "the run went with that key, so the next one is the box's own again");

    cf_ctx_free(&ctx);
}

static void the_tree_still_moves_the_keyboard_around_a_run(void) {
    char bytes[32] = {0};
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = 32u};
    uint32_t through = 0u;
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_watched(&ctx, &text, false, count_keys, &through);
    press(&ctx, 10.0f, 10.0f);
    compose(&ctx, "ni");
    hold(&ctx, CF_KEY_TAB, false);

    CF_CHECK(text.length == 0u, "walking away commits nothing of the run");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "and the box the candidates were told about goes with the keyboard");
    CF_CHECK(through == 1u, "the key still climbs, because moving is the tree's ask");

    cf_ctx_free(&ctx);
}

/*
 * The claim is what gates the answer, not the reach of the keyboard alone: a hand that moves from
 * one field to another leaves a page that still takes written text, so an empty box can only mean
 * the claim went back with the keyboard. A candidate window left standing over the box nobody is
 * typing into is the mistake this says the tree does not make.
 */
static void a_run_goes_back_when_the_keyboard_changes_hands(void) {
    char first_bytes[32] = {0};
    char second_bytes[32] = {0};
    cf_text_edit first = {.chars = first_bytes, .length = 0u, .capacity = 32u};
    cf_text_edit second = {.chars = second_bytes, .length = 0u, .capacity = 32u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_pair(&ctx, &first, &second);

    const cf_id a = cf_node_id(&ctx, child_of(&ctx, cf_frame_root(&ctx), 0u));
    const cf_id b = cf_node_id(&ctx, child_of(&ctx, cf_frame_root(&ctx), 1u));

    press(&ctx, 10.0f, 10.0f);
    lift(&ctx, 10.0f, 10.0f);
    compose(&ctx, "ni");

    CF_CHECK(cf_focused(&ctx, a), "the first press booked the keyboard into the box on top");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 20.0f, "and that box is deciding a run");

    frame_pair(&ctx, &first, &second);
    press(&ctx, 10.0f, 30.0f);
    lift(&ctx, 10.0f, 30.0f);

    CF_CHECK(cf_focused(&ctx, b), "the press below moves it to the other box");
    CF_CHECK(cf_frame_takes_text(&ctx), "and the page takes written text the whole way across");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "so the box going empty is the claim going back, not the keyboard going away");
    CF_CHECK(first.length == 0u && second.length == 0u,
             "and neither buffer was handed the run on the way out");

    cf_ctx_free(&ctx);
}

/*
 * A box that was deciding a run can leave the page between two frames - a dialog closes, a row is
 * culled - and then the repair that hands the keyboard back is the only writer left that can take
 * the run with it. The keyboard lands on another field, so the gate stays open the whole way: what
 * is being read here is that a candidate window never outlives the box it was laid beside.
 */
static void a_run_goes_back_when_the_box_that_held_it_leaves(void) {
    char first_bytes[32] = {0};
    char second_bytes[32] = {0};
    cf_text_edit first = {.chars = first_bytes, .length = 0u, .capacity = 32u};
    cf_text_edit second = {.chars = second_bytes, .length = 0u, .capacity = 32u};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();
    frame_pair(&ctx, &first, &second);

    const cf_id a = cf_node_id(&ctx, child_of(&ctx, cf_frame_root(&ctx), 0u));

    /* Where the keyboard came from is what the repair hands it back to, so the box below is
     * pressed first and the one that takes the run second. */
    press(&ctx, 10.0f, 30.0f);
    lift(&ctx, 10.0f, 30.0f);
    press(&ctx, 10.0f, 10.0f);
    lift(&ctx, 10.0f, 10.0f);
    compose(&ctx, "ni");

    CF_CHECK(cf_focused(&ctx, a), "the box on top holds the keyboard");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 20.0f, "and it is deciding a run");

    frame_after_the_first_leaves(&ctx, &second);

    CF_CHECK(!cf_focused(&ctx, a), "the box that held the run is out of the page");
    CF_CHECK(cf_frame_takes_text(&ctx), "and the keyboard went to a box that takes written text");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "so the run went with the box rather than staying standing beside another one");
    CF_CHECK(first.length == 0u && second.length == 0u,
             "and neither buffer was handed bytes the method never chose");

    cf_ctx_free(&ctx);
}

static void a_run_over_the_room_the_field_keeps_is_refused_whole(void) {
    static char bytes[256];
    /* A field keeps sixty-four bytes for a run. One more is refused whole rather than cut
     * between a letter and the mark written onto it, the way a paste too long for the buffer
     * is refused rather than half-inserted. */
    static char long_run[66];
    cf_text_edit text = {.chars = bytes, .length = 0u, .capacity = sizeof bytes};
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    memset(long_run, 'a', sizeof long_run - 1u);

    ctx.theme = cf_theme_default();
    frame_of(&ctx, &text, false);
    press(&ctx, 10.0f, 10.0f);
    compose(&ctx, long_run);

    CF_CHECK(text.refusals == 1u, "a run cut between a letter and its mark is refused whole");
    CF_CHECK(text.length == 0u, "and no part of it reaches the value");
    CF_CHECK(cf_frame_composition_rect(&ctx).width == 0.0f,
             "with nothing held there is no box to put candidates beside");

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
    CF_RUN(an_arrow_steps_a_whole_character);
    CF_RUN(an_accent_leaves_the_field_under_the_letter_it_sits_on);
    CF_RUN(a_joined_sequence_enters_and_leaves_the_field_as_one_character);
    CF_RUN(two_indicator_letters_are_one_character_too);
    CF_RUN(a_word_key_leaves_no_mark_behind_at_the_end_of_its_run);
    CF_RUN(a_caret_a_click_placed_inside_a_character_moves_out_of_it);
    CF_RUN(what_is_selected_gives_way_to_what_is_typed);
    CF_RUN(a_full_buffer_takes_what_the_selection_frees);
    CF_RUN(the_edit_keys_cut_the_selection_or_the_character_they_name);
    CF_RUN(a_press_places_the_caret_and_a_drag_selects_to_where_it_went);
    CF_RUN(a_press_below_the_rows_the_field_holds_stands_on_the_last_one);
    CF_RUN(a_press_lands_on_the_row_of_the_box_rather_than_the_row_of_the_page);
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
    CF_RUN(a_space_in_a_field_is_a_character_and_enter_is_the_ask);
    CF_RUN(the_change_door_hears_the_bytes_and_not_the_caret);
    CF_RUN(cutting_pasting_and_stepping_the_history_move_the_value);
    CF_RUN(the_change_door_answers_the_box_whose_value_moved);
    CF_RUN(a_multiline_field_asks_for_the_height_its_rows_need);
    CF_RUN(enter_writes_a_row_in_one_mode_and_asks_in_the_other);
    CF_RUN(the_vertical_arrows_belong_to_the_mode_and_not_the_page);
    CF_RUN(a_pasted_break_lands_where_the_mode_puts_rows);
    CF_RUN(the_mode_survives_the_box_it_was_pinned_in);
    CF_RUN(an_arrow_past_the_rows_the_walk_holds_stays_on_the_last_one);
    CF_RUN(a_run_being_decided_is_not_the_pages_value);
    CF_RUN(the_box_a_run_stands_in_follows_the_frame_that_lays_it);
    CF_RUN(the_words_chosen_replace_the_bytes_the_run_covered);
    CF_RUN(a_key_the_method_owns_does_not_reach_the_value);
    CF_RUN(the_tree_still_moves_the_keyboard_around_a_run);
    CF_RUN(a_run_goes_back_when_the_keyboard_changes_hands);
    CF_RUN(a_run_goes_back_when_the_box_that_held_it_leaves);
    CF_RUN(a_run_over_the_room_the_field_keeps_is_refused_whole);

    return CF_REPORT();
}
