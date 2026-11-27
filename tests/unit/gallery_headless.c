#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cf/app.h"
#include "cf/backend.h"
#include "cf/base.h"
#include "cf/cf.h"
#include "cf/event.h"
#include "cf/node.h"
#include "cf/paint.h"
#include "cf/style.h"
#include "cf/text.h"
#include "cf/types.h"
#include "cf/widget.h"

#include "cform_test.h"
#include "gallery_page.h"

/*
 * The gallery page drawn through the real frame pipeline against the headless
 * recorder instead of a window: the same chapter builders gallery.c ships,
 * driven across surfaces and scripted interactions, with the command stream
 * checked as data on every submit. The three defects the gallery once hid - a
 * track array on the stack, a missing shrink, a rounded fill cut to a clip -
 * all showed up as geometry the stream carried wrongly, so the checks are on
 * the stream: finite numbers, clips inside the output, radii within their own
 * box, and a sharp fill, which arrives pre-cut, never outside its clip or the
 * viewport. A preserved rounded, image, border or text rect may legitimately
 * cross a clip, so those are judged by the scissor they name, not by their own
 * edges.
 */

#define SCENARIO_ARENA_BYTES (256u * 1024u)
#define SESSION_FRAME_BYTES (1024u * 1024u)

/* What the last frame's submit carried: read by find_* helpers so a case
 * can see what the page paints after each step. */
static const cf_command *recorded;
static uint32_t recorded_count;
static cf_ctx session;
static bool session_open;
static cf_cursor last_cursor;
/* The frame's other device answer, read where the cursor's is: whether the box holding
 * the keyboard takes written text. */
static bool last_text;
static cf_backend shell_backend;
/* The last rendered viewport, so helpers and stream checks can judge
 * geometry against the same offer the layout was measured in. */
static cf_rect current_viewport;

/* The fake font the page measures and paints through: a fixed rhythm and a
 * fixed advance, the same table shape a real font backend answers, so every
 * position an assertion derives from text is a number the test knows. */
static cf_text_metrics mono_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float mono_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font, (void)codepoint, (void)previous;
    return 10.0f;
}

static bool mono_has_glyph(void *font, uint32_t codepoint) {
    (void)font, (void)codepoint;
    return true;
}

static const cf_text_ops mono_ops = {mono_metrics, mono_advance, mono_has_glyph};
/* The inspector's line height asks the font for metrics through its handle,
 * and treats a NULL handle as "no font here". The mono helpers ignore the
 * pointer, so any stable non-null address works; a sentinel static keeps
 * that rule off the code path. */
static int mono_handle_sentinel;
static cf_font mono_font = {.handle = &mono_handle_sentinel, .ops = &mono_ops};

/* The recorder leaves create_texture NULL; the page still needs a non-zero
 * id so an image command reads as an uploaded texture rather than the one
 * nobody filled. A monotonic counter is enough - the stream is judged on
 * the id being non-zero, not on the pixels it names. */
static uint32_t next_texture;

static uint32_t fake_create_texture(void *user_data, uint32_t width, uint32_t height,
                                    const void *pixels) {
    (void)user_data, (void)width, (void)height, (void)pixels;
    return ++next_texture;
}

static bool finite_number(float value) {
    return isfinite(value) != 0;
}

static bool finite_rect(cf_rect rect) {
    return finite_number(rect.x) && finite_number(rect.y) && finite_number(rect.width) &&
           finite_number(rect.height);
}

static bool rects_overlap(cf_rect a, cf_rect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

static bool rect_inside(cf_rect inner, cf_rect outer) {
    return inner.x >= outer.x - 0.01f && inner.y >= outer.y - 0.01f &&
           inner.x + inner.width <= outer.x + outer.width + 0.01f &&
           inner.y + inner.height <= outer.y + outer.height + 0.01f;
}

#define STREAM_FAIL(...)                                                                           \
    do {                                                                                           \
        printf("    FAIL stream at %.0fx%.0f: ", current_viewport.width, current_viewport.height); \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
        cf_failures++;                                                                             \
    } while (0)

/* The invariants every submitted frame must hold, judged against the
 * viewport that offer laid the page out in. Paint emits an empty clip
 * (0,0,0,0) as a sentinel meaning "outside any clipping box; the screen
 * bounds itself", so those commands are judged against the viewport. */
static bool clip_is_none(cf_rect clip) {
    return clip.width == 0.0f && clip.height == 0.0f;
}

static void check_stream(const cf_command *commands, uint32_t count, cf_rect viewport) {
    for (uint32_t i = 0u; i < count; ++i) {
        const cf_command command = commands[i];

        if (!finite_rect(command.rect) || !finite_rect(command.clip)) {
            STREAM_FAIL("command %u kind %d carries a rect that is not a number", i,
                        (int)command.kind);
            continue;
        }

        if (!finite_number(command.radius) || command.radius < 0.0f) {
            STREAM_FAIL("command %u names a radius of %f", i, command.radius);
        }

        /* A named clip must fit inside the viewport; the (0,0,0,0) sentinel
         * says "nothing to scissor" and is not a violation. */
        if (!clip_is_none(command.clip) && !rect_inside(command.clip, viewport)) {
            STREAM_FAIL("command %u kind %d scissors outside the output", i, (int)command.kind);
        }

        /* A clip with extent in one axis but not the other cannot come from
         * a walk - carried_clip returns either the sentinel or a real rect
         * from a box that clips. */
        if ((command.clip.width == 0.0f) != (command.clip.height == 0.0f)) {
            STREAM_FAIL("command %u kind %d clips to a half-empty box", i, (int)command.kind);
        }

        /* Half the shorter side is the most a corner can be round before it
         * is a circle's whole quarter; past it the arc eats its own border. */
        const float shorter =
            command.rect.width < command.rect.height ? command.rect.width : command.rect.height;

        if (command.radius > shorter / 2.0f + 0.01f) {
            STREAM_FAIL("command %u kind %d rounds a %fx%f box by %f", i, (int)command.kind,
                        command.rect.width, command.rect.height, command.radius);
        }

        if (command.kind == CF_COMMAND_FILL && command.radius == 0.0f) {
            /* A sharp fill inside a clip is cut before it is emitted;
             * spilling its own clip means the cut was skipped. A sharp fill
             * outside any clip carries the sentinel and is not cut, so the
             * only judgement left is that its rect still reaches the screen. */
            if (!clip_is_none(command.clip) && !rect_inside(command.rect, command.clip)) {
                STREAM_FAIL("fill %u arrives uncut against its clip", i);
            }

            if (!rect_inside(command.rect, viewport)) {
                STREAM_FAIL("fill %u sits outside the output", i);
            }
        }

        if (command.kind == CF_COMMAND_TEXT && command.text.length == 0u) {
            STREAM_FAIL("text command %u carries no bytes", i);
        }

        if (command.kind == CF_COMMAND_IMAGE && command.texture == 0u) {
            STREAM_FAIL("image command %u names the texture nobody uploaded", i);
        }
    }
}

/* ---------- predicates over the recorded stream, captured via file-scope ---- */

static cf_size current_size(void) {
    return (cf_size){current_viewport.width, current_viewport.height};
}

/* page_box's shape: the fill that spans the current output. */
static bool accept_page_fill(const cf_command *command) {
    return fabsf(command->rect.width - current_viewport.width) < 0.5f &&
           fabsf(command->rect.height - current_viewport.height) < 0.5f;
}

/* The bigger-output case checks the same shape against 1600x1200 rather than
 * whatever surface currently is; a distinct predicate keeps page_box honest. */
static bool accept_bigger_fill(const cf_command *command) {
    return fabsf(command->rect.width - 1600.0f) < 0.5f &&
           fabsf(command->rect.height - 1200.0f) < 0.5f;
}

/* The field-box hunt needs the echo's y to sit above it. */
static float field_echo_y;

/* The memo's caption sits below the box the other mode declared, so the hunt for that box
 * needs the caption's y the same way the entry's hunt needs its echo. */
static float memo_caption_y;

static cf_point memo_press;

/* Where the press that focused the field landed, for the case that moves from there. */
static cf_point field_press;

static bool accept_field_box(const cf_command *command) {
    return command->rect.y + command->rect.height <= field_echo_y + 1.0f &&
           command->rect.width > 100.0f && command->rect.height > FIELD_HEIGHT - 1.0f &&
           command->rect.height < 2.0f * FIELD_HEIGHT;
}

/* ---------- find helpers ---------- */

/* The last command whose text is exactly the needle. A wrapped paragraph
 * arrives as one command per line, so a full sentence is one line and a
 * prefix hunt is not needed for it. */
static cf_rect find_text_rect(const char *needle, bool *found) {
    const cf_string want = cf_string_of(needle);
    cf_rect out = {0};

    *found = false;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        const cf_command command = recorded[i];

        if (command.kind == CF_COMMAND_TEXT && command.text.length == want.length &&
            command.text.chars != NULL && memcmp(command.text.chars, needle, want.length) == 0) {
            *found = true;
            out = command.rect;
        }
    }

    if (getenv("GALLERY_DEBUG") != NULL) {
        printf("      [find_text_rect '%s' -> found=%s rect=(%.0f,%.0f %.0fx%.0f) total=%u]\n",
               needle, *found ? "yes" : "no", out.x, out.y, out.width, out.height, recorded_count);
    }

    return out;
}

/*
 * The memo is the same widget with the other word said about it: two rows of its own, and a
 * box taller than the entry's. The height window is what separates the two field fills, so a
 * page that gave the memo a different height would report the hunt as a precondition that
 * was not met rather than as a wrong assertion.
 */
static bool accept_memo_box(const cf_command *command) {
    return command->rect.y + command->rect.height <= memo_caption_y + 1.0f &&
           command->rect.width > 100.0f && command->rect.height > 2.0f * FIELD_HEIGHT - 1.0f &&
           command->rect.height < 3.0f * FIELD_HEIGHT;
}

static cf_rect find_fill_rect(bool (*accept)(const cf_command *), bool *found) {
    cf_rect out = {0};

    *found = false;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        const cf_command command = recorded[i];

        if (command.kind == CF_COMMAND_FILL && accept(&command)) {
            *found = true;
            out = command.rect;
        }
    }

    return out;
}

static cf_rect find_text_prefix(const char *prefix, bool *found) {
    const size_t want = strlen(prefix);
    cf_rect out = {0};

    *found = false;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        const cf_command command = recorded[i];

        if (command.kind == CF_COMMAND_TEXT && command.text.chars != NULL &&
            command.text.length >= (uint32_t)want &&
            memcmp(command.text.chars, prefix, want) == 0) {
            *found = true;
            out = command.rect;
        }
    }

    return out;
}

/* The box the output itself: the page scroller is the one painted box as
 * wide and as tall as the offer, and it is where a wheel asks for the walk. */
static cf_rect page_box(void) {
    bool found = false;
    const cf_rect page = find_fill_rect(accept_page_fill, &found);

    CF_CHECK(found, "the page itself is a painted box");
    return page;
}

static bool page_shows(const char *needle) {
    bool found = false;
    (void)find_text_rect(needle, &found);
    return found;
}

/* The same hunt for a line the page writes out of its state: the report line
 * changes with what happened, so it is named by what it starts with. */
static bool page_reads(const char *prefix) {
    bool found = false;
    (void)find_text_prefix(prefix, &found);
    return found;
}

/*
 * Whether a pointer can be put on a painted box at all. A row that the scroller
 * clips at its edge is still recorded, so "the page shows it" is not "the page can
 * be clicked there": the centre of a box hanging over the bottom edge lands on
 * nothing, and a tap that fires no handler reads as a dead control.
 */
static bool reaches_pointer(cf_rect rect) {
    return rect.y >= current_viewport.y - 0.5f &&
           rect.y + rect.height <= current_viewport.y + current_viewport.height - 0.5f &&
           rect.x >= current_viewport.x - 0.5f &&
           rect.x + rect.width <= current_viewport.x + current_viewport.width - 0.5f;
}

/* The needle is on the page and within reach of a pointer. */
static bool page_reveals(const char *needle) {
    bool found = false;
    const cf_rect rect = find_text_rect(needle, &found);

    if (found) {
        return reaches_pointer(rect);
    }

    /* A line whose text is written from the page's own state - the field's echo, say -
     * is named by what it always starts with rather than by a string that changes with
     * every keystroke. */
    const cf_rect prefix = find_text_prefix(needle, &found);

    return found && reaches_pointer(prefix);
}

/* ---------- events ---------- */

static cf_event move_to(float x, float y) {
    return (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {x, y}};
}

static cf_event press_at(float x, float y) {
    return (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {x, y}, .button = 1u};
}

static cf_event release_at(float x, float y) {
    return (cf_event){.type = CF_EVENT_POINTER_UP, .position = {x, y}, .button = 1u};
}

static cf_event wheel_at(float x, float y, float dy) {
    return (cf_event){.type = CF_EVENT_SCROLL, .position = {x, y}, .wheel = {0.0f, dy}};
}

static cf_event typed_text(const char *chars) {
    cf_event event = {.type = CF_EVENT_TEXT};
    event.text = cf_string_of(chars);
    return event;
}

static cf_event key_down(cf_key k) {
    return (cf_event){.type = CF_EVENT_KEY_DOWN, .key = k};
}

/* Tab, forward or with shift held: the two directions one key carries. */
static cf_event tab_key(bool backward) {
    return (cf_event){.type = CF_EVENT_KEY_DOWN,
                      .key = CF_KEY_TAB,
                      .mods = backward ? (uint32_t)CF_MOD_SHIFT : 0u};
}

/* An arrow with shift held, which is how a hand makes a selection. */
static cf_event shift_arrow(cf_key key) {
    return (cf_event){
        .type = CF_EVENT_KEY_DOWN, .key = (uint32_t)key, .mods = (uint32_t)CF_MOD_SHIFT};
}

/* A letter key with the modifiers the desk held down with it. */
static cf_event chord(char letter, uint32_t mods) {
    return (cf_event){.type = CF_EVENT_KEY_DOWN, .key = (uint32_t)letter, .mods = mods};
}

/* A scripted schedule: an array of events, the last one a QUIT so the loop
 * knows to stop after submitting what the last event produced. */

/* One session: the page driven through cf_frame_run against the recorder,
 * with the scripted events handed one at a time. The initial frame paints
 * before any event so cf_input's hit-test has rects to work against; every
 * event then runs a frame, and a frame that asked for another runs one
 * more - the same rule the shell keeps. The ctx lives across the call so
 * state accessors can read what the build wrote. */
/* The last rendered viewport, so flush_frames can run its invariant checks
 * against the same offer the layout was measured in. */

static void record_frame(cf_command_list *list) {
    check_stream(list->commands, list->count, current_viewport);

    /* The recorder keeps only what the last frame submitted: a case that
     * asks whether a chapter is on screen after a wheel means what the
     * eye sees now, not a history of every frame the session drew. The
     * commands array grows once and stays; count resets, capacity holds. */
    cf_recorder *recorder = shell_backend.user_data;
    recorder->count = 0u;
    shell_backend.submit(shell_backend.user_data, list->commands, list->count);

    recorded = recorder->commands;
    recorded_count = recorder->count;
}

static void flush_frames(void) {
    const float step = 1.0f / 60.0f;
    cf_size size = {current_viewport.width, current_viewport.height};

    /* cf_app_run's rule: a frame that asked for another gets one. The bound
     * is a safety net - the page never asks forever, but a case that broke
     * the ask-when-moving contract would loop here rather than hang. */
    uint32_t guard = 0u;

    while (cf_frame_requested(&session) && guard < 1000u) {
        cf_command_list list = {0};
        cf_frame_run(&session, build, size, step, &list);
        record_frame(&list);
        ++guard;
    }
}

/* One scripted event: cf_input, then any frames the event asked for.
 * Between DOWN and UP the button's slot id persists (cf_store recycles only
 * when a key leaves the tree), so the release still lands on the press. */
static void step(cf_event event) {
    if (event.type == CF_EVENT_QUIT) {
        return;
    }

    cf_input(&session, event);

    /* One frame after every event, so a build that reads the new state
     * rebinds rects before the next cf_input. Without this, a switch that
     * disabled a button would leave it still enabled to the very last
     * event of the batch. */
    cf_command_list list = {0};
    cf_frame_run(&session, build, (cf_size){current_viewport.width, current_viewport.height},
                 1.0f / 60.0f, &list);
    record_frame(&list);
    flush_frames();
}

/* Open a session: a fresh ctx with the theme, fonts and assets a shell
 * installs before the first frame, then one paint that gives hit-testing
 * its rects. The caller then steers with step() and closes with end_session. */
static void start_session(cf_size size) {
    static cf_arena store;
    static cf_recorder recorder;

    if (session_open) {
        cf_ctx_free(&session);
        session_open = false;
        cf_arena_free(&store);
    }

    if (!cf_arena_init(&store, SCENARIO_ARENA_BYTES)) {
        CF_CHECK(false, "the recorder's store starts");
        return;
    }

    if (!cf_ctx_init(&session, SESSION_FRAME_BYTES)) {
        CF_CHECK(false, "the session's context starts");
        cf_arena_free(&store);
        return;
    }

    session.theme = cf_theme_default();
    recorder.arena = &store;
    recorder.count = 0u;
    recorder.capacity = 0u;
    recorder.commands = NULL;

    shell_backend = cf_backend_recorder(&recorder);
    shell_backend.create_texture = fake_create_texture;

    theme = cf_theme_default();
    body_font = mono_font;
    title_font = mono_font;
    body = (cf_text_stack){.fonts = &body_font, .count = 1u};
    title = (cf_text_stack){.fonts = &title_font, .count = 1u};
    gallery_init_assets();
    logo = (cf_image_content){
        .texture = shell_backend.create_texture(shell_backend.user_data, 8u, 8u, checker),
        .width = 32.0f,
        .height = 32.0f};

    current_viewport = (cf_rect){0.0f, 0.0f, size.width, size.height};

    cf_command_list list = {0};
    cf_frame_run(&session, build, size, 1.0f / 60.0f, &list);
    record_frame(&list);
    session_open = true;
}

static void end_session(void) {
    last_cursor = cf_frame_cursor(&session);
    last_text = cf_frame_takes_text(&session);

    if (getenv("GALLERY_DEBUG") != NULL) {
        cf_recorder *r = shell_backend.user_data;
        printf("      [session end: %u commands]\n", r->count);
    }

    CF_CHECK(recorded_count > 0u, "the session submitted a frame");
}

/* The one-shot form a scripted schedule uses: open, feed every event, close.
 * Cases that need to inspect what the page shows between events call the
 * helpers directly instead. */
static void run_session(cf_size size, const cf_event *events, uint32_t count) {
    start_session(size);

    for (uint32_t i = 0u; i < count; ++i) {
        if (events[i].type == CF_EVENT_QUIT) {
            break;
        }

        step(events[i]);
    }

    end_session();
}

static void end_sessions(void) {
    if (session_open) {
        cf_ctx_free(&session);
        session_open = false;
    }

    /* store is a static inside run_session; the process is exiting and the
     * OS will reclaim. A long-lived host would want the arena freed too. */
}

/* The TAP macro: the label sits inside the button's rect, so pressing at
 * its centre is guaranteed to hit the label, climb to the button via
 * interactive_of and land on release inside the button's rect - the same
 * shape the widget_button tests drive. `at` must be a uint32_t lvalue. */
#define TAP(events, at, name)                                                                      \
    do {                                                                                           \
        bool tap_found_ = false;                                                                   \
        const cf_rect label_ = find_text_rect((name), &tap_found_);                                \
        if (!tap_found_) {                                                                         \
            printf("    FAIL %s:%d: the page does not show '%s'\n", __FILE__, __LINE__, (name));   \
            cf_failures++;                                                                         \
        }                                                                                          \
        if (tap_found_) {                                                                          \
            const float tap_x_ = label_.x + label_.width / 2.0f;                                   \
            const float tap_y_ = label_.y + label_.height / 2.0f;                                  \
            (events)[(*at)++] = press_at(tap_x_, tap_y_);                                          \
            (events)[(*at)++] = release_at(tap_x_, tap_y_);                                        \
        }                                                                                          \
    } while (0)

/* A test that walks to a chapter and then taps needs to keep the session
 * open across the walk; the reveal_to helper does that, and the tap helper
 * runs DOWN + a frame + UP inside it. */

/* The TAP helper: the label sits inside the button's rect, so pressing at
 * its centre is guaranteed to hit the label, climb to the button via
 * interactive_of and land on release inside the button's rect - the same
 * shape the widget_button tests drive. DOWN and UP are two steps with a
 * frame in between, which is what a click looks like to a running shell. */
static bool tap(const char *name) {
    bool found = false;
    const cf_rect label = find_text_rect(name, &found);

    if (!found) {
        printf("    FAIL %s:%d: the page does not show '%s'\n", __FILE__, __LINE__, name);
        cf_failures++;
        return false;
    }

    if (!reaches_pointer(label)) {
        printf("    FAIL %s:%d: '%s' is painted past the edge a pointer can reach\n", __FILE__,
               __LINE__, name);
        cf_failures++;
        return false;
    }

    const float x = label.x + label.width / 2.0f;
    const float y = label.y + label.height / 2.0f;

    step(press_at(x, y));
    step(release_at(x, y));
    return true;
}

/* Walk the current session's page scroll by 192 pixels at a time until the
 * needle is somewhere a pointer can be put on it. The wheel handler would chain
 * to a chapter's own scroller and land wherever it stops, so the helper writes
 * the offset directly and lets the build read it back - the same value a wheel
 * would produce, just without the intermediate hit walk. The session stays open
 * at whichever step reached the needle, so the caller can tap it next. */
#define REVEAL_STEP_PX 192.0f
#define REVEAL_MAX_STEPS 40u

/* Move the page to a stated offset and repaint. Offsets past the document's
 * height clamp back to its end: the helper does not know the content height, so
 * it states the number and lets the next build put it through the scroller's own
 * inset. */
static bool page_scroll_to(float offset) {
    cf_scroll *held = cf_scroll_of(&session, cf_intern_text(&session.names, "page"));

    if (held == NULL) {
        printf("    FAIL %s:%d: the page has no scroller to walk\n", __FILE__, __LINE__);
        cf_failures++;
        return false;
    }

    held->offset = offset;

    cf_command_list list = {0};
    cf_frame_run(&session, build, current_size(), 1.0f / 60.0f, &list);
    record_frame(&list);
    return true;
}

static bool reveal_to(const char *needle, uint32_t max_steps) {
    if (max_steps > REVEAL_MAX_STEPS) {
        max_steps = REVEAL_MAX_STEPS;
    }

    /* Reveal drives whichever session is open; a case that starts from
     * cold gets a fresh one at the default size. */
    if (!session_open) {
        start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});
    }

    /* If the needle is already reachable, no walking needed. */
    if (page_reveals(needle)) {
        return true;
    }

    for (uint32_t n = 1u; n <= max_steps; ++n) {
        if (!page_scroll_to((float)n * REVEAL_STEP_PX)) {
            return false;
        }

        if (page_reveals(needle)) {
            return true;
        }
    }

    printf("    FAIL %s:%d: no step reaches '%s'\n", __FILE__, __LINE__, needle);
    cf_failures++;
    return false;
}

/* A session that opens at the current size, runs the reveal walk, and hands
 * the caller an open ctx to tap in. run_session remains the standalone form
 * a schedule can drive without an intermediate inspection. */

/* ---------- cases ---------- */

static void the_page_fills_the_recorder(void) {
    const cf_event hands[] = {
        (cf_event){.type = CF_EVENT_QUIT},
    };
    run_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT}, hands, 1u);

    /* Chapter 1 alone is a full page's worth: header, prose, three buttons,
     * the switch, the status line, and every card around them. */
    CF_CHECK(recorded_count > 20u, "chapter one paints a full frame's worth");

    bool found = false;
    (void)find_text_rect("cform gallery", &found);
    CF_CHECK(found, "the header's own words arrive");

    (void)find_text_rect("count", &found);
    CF_CHECK(found, "the counter's button reads back");
}

static void a_click_counts_and_a_clear_ends_the_count(void) {
    cf_event hands[8];
    uint32_t at = 0u;

    TAP(hands, &at, "count");
    TAP(hands, &at, "count");
    TAP(hands, &at, "count");
    hands[at++] = (cf_event){.type = CF_EVENT_QUIT};

    run_session(current_size(), hands, at);
    if (getenv("GALLERY_DEBUG") != NULL) {
        uint32_t *c = click_count(&session);
        printf("      [clicks ptr=%p value=%u]\n", (void *)c, c != NULL ? *c : 0u);
    }
    CF_CHECK(*click_count(&session) == 3u, "three clicks, three counted");

    at = 0u;
    TAP(hands, &at, "clear");
    hands[at++] = (cf_event){.type = CF_EVENT_QUIT};

    run_session(current_size(), hands, at);
    CF_CHECK(*click_count(&session) == 0u, "the clear empties the block the name keys");
}

static void a_switched_off_button_takes_no_click(void) {
    cf_event hands[6];
    uint32_t at = 0u;

    TAP(hands, &at, "switch off");
    TAP(hands, &at, "count");
    hands[at++] = (cf_event){.type = CF_EVENT_QUIT};

    run_session(current_size(), hands, at);

    CF_CHECK(*switch_off(&session), "the switch flipped");
    CF_CHECK(*click_count(&session) == 0u, "the button it disabled fired nothing");
}

/* An arrow with the modifier that turns it into a word key. */
static cf_event word_arrow(cf_key key, uint32_t extra) {
    return (cf_event){
        .type = CF_EVENT_KEY_DOWN, .key = (uint32_t)key, .mods = (uint32_t)CF_MOD_CONTROL | extra};
}

/*
 * Chapter two's field, reached and focused. The caption carries the buffer's bytes, so
 * it is hunted by prefix; the field states its height rather than measuring an empty
 * buffer, so its box is the painted one above that line. Every step is its own frame,
 * because the reveal moves what the page shows and the box hunted has to come from the
 * frame the press lands in. False leaves the precondition reported by the caller.
 */
static bool focus_the_field(void) {
    if (!reveal_to("the buffer holds", REVEAL_MAX_STEPS)) {
        CF_CHECK(false, "the field's echo line can be reached on the page");
        return false;
    }

    bool found = false;
    const cf_rect echo = find_text_prefix("the buffer holds", &found);

    CF_CHECK(found, "the field's echo line is on the page");

    if (!found) {
        return false;
    }

    field_echo_y = echo.y;
    const cf_rect field = find_fill_rect(accept_field_box, &found);

    CF_CHECK(found, "the entry box is painted above its own echo");

    if (!found) {
        return false;
    }

    typed.length = 0u;
    /* The refusal count is the buffer's own statement about the keys it turned away, so
     * a case that means to read it starts from none. */
    typed.refusals = 0u;
    field_press = (cf_point){.x = field.x + 30.0f, .y = field.y + field.height / 2.0f};
    step(press_at(field_press.x, field_press.y));
    step(release_at(field_press.x, field_press.y));

    return true;
}

/*
 * Chapter two's other box, reached and focused, the way focus_the_field reaches the entry:
 * the caption is hunted by prefix, the box above it by shape, and the press that books focus
 * lands in the middle of that box. The buffer is wiped first, because the rows the caption
 * counts are read out of the bytes the page keeps.
 */
static bool focus_the_memo(void) {
    if (!reveal_to("the memo holds", REVEAL_MAX_STEPS)) {
        CF_CHECK(false, "the memo's caption can be reached on the page");
        return false;
    }

    bool found = false;
    const cf_rect caption = find_text_prefix("the memo holds", &found);

    CF_CHECK(found, "the memo's caption is on the page");

    if (!found) {
        return false;
    }

    memo_caption_y = caption.y;
    const cf_rect box = find_fill_rect(accept_memo_box, &found);

    CF_CHECK(found, "the memo box is painted above its own caption");

    if (!found) {
        return false;
    }

    CF_CHECK(box.height > 2.0f * FIELD_HEIGHT - 1.0f,
             "and the box it was painted in is the two rows the build stated");

    memo.length = 0u;
    memo.refusals = 0u;
    memo.caret = 0u;
    memo.mark = 0u;
    memo_press = (cf_point){.x = box.x + 30.0f, .y = box.y + box.height / 2.0f};
    step(press_at(memo_press.x, memo_press.y));
    step(release_at(memo_press.x, memo_press.y));

    return true;
}

static void a_focused_field_takes_what_the_keyboard_wrote(void) {
    /* One session for the whole case: the reveal moves what the page shows, so the box
     * hunted and the point pressed have to come from the same scrolled frame. */
    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("hello"));
    step(typed_text(" world"));
    step(key_down(CF_KEY_BACKSPACE));
    step(move_to(field_press.x, field_press.y));

    /* The shell's two answers are read as the session closes, so the close comes first:
     * what the last frame asked for is what the case then checks. */
    end_session();

    CF_CHECK(typed.length == 10u, "five plus six minus the deleted scalar is ten bytes");
    CF_CHECK(memcmp(typed_bytes, "hello worl", 10) == 0, "and they are the bytes typed");
    CF_CHECK(last_cursor == CF_CURSOR_TEXT, "and the pointer over the field asks for the I-beam");
    CF_CHECK(last_text, "and the field the press focused asks the device for written text");
}

/*
 * The clipboard the page is handed, scripted: the bytes the field's chords write
 * and read, so the case checks against a desk it owns rather than against whatever
 * the machine running it happens to hold.
 */
typedef struct scripted_board {
    char text[32];
    uint32_t length;
} scripted_board;

static cf_string script_read(void *user_data, char *out, uint32_t capacity) {
    scripted_board *board = user_data;
    cf_string answer = {.chars = NULL, .length = board->length};

    if (out != NULL && capacity > 0u) {
        const uint32_t taken = board->length < capacity ? board->length : capacity;
        memcpy(out, board->text, taken);
        answer = (cf_string){.chars = out, .length = taken};
    }

    return answer;
}

static bool script_write(void *user_data, cf_string utf8) {
    scripted_board *board = user_data;

    if (utf8.chars == NULL || utf8.length > sizeof board->text) {
        return false;
    }

    memcpy(board->text, utf8.chars, utf8.length);
    board->length = utf8.length;

    return true;
}

static void script_holds(scripted_board *board, const char *run) {
    board->length = (uint32_t)strlen(run);
    memcpy(board->text, run, board->length);
}

/*
 * The field in the page answers the three chords through the clipboard its host
 * named: the wiring the SDL shell runs on, checked here because a page built with a
 * context that never got the pair would type fine and move nothing.
 */
static void the_gallery_field_trades_bytes_with_the_host_it_names(void) {
    scripted_board board = {0};
    const cf_clipboard clipboard = {
        .read = script_read, .write = script_write, .user_data = &board};

    start_session(current_size());
    session.clipboard = &clipboard;

    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("hello"));
    step(shift_arrow(CF_KEY_LEFT));
    step(shift_arrow(CF_KEY_LEFT));
    step(chord('c', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(board.length == 2u && board.text[0] == 'l' && board.text[1] == 'o',
             "the chord puts the field's selection on the desk the page was given");
    CF_CHECK(typed.length == 5u, "and copy takes no byte out of the buffer");

    step(chord('x', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.length == 3u, "cut drops the bytes the host agreed to hold");

    script_holds(&board, " world");
    step(chord('v', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.length == 9u && memcmp(typed_bytes, "hel world", 9) == 0,
             "and paste writes what the host holds over what the field had");

    end_session();
}

/*
 * The same field walked back. Every step is its own frame, so this is the case that
 * says the history rides the keyed block rather than the arena a frame ends in: a page
 * that rebuilt the field and lost the ring would type fine and undo nothing.
 */
static void the_gallery_field_walks_its_own_typing_back(void) {
    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("hello"));
    step(typed_text(" world"));

    CF_CHECK(typed.length == 11u, "the page holds what the two runs typed");

    step(chord('z', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.length == 5u && memcmp(typed_bytes, "hello", 5) == 0,
             "a chord in a later frame walks the earlier run back out");

    step(chord('z', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.length == 0u, "and one more reaches where the typing started");

    step(chord('y', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.length == 5u, "and the key beside it comes forward to where the walk stood");

    end_session();
}

/*
 * The word keys and select-all over the page's own build. `widget_field` checks the same
 * rules against a context it assembles; this is the case that says the chords reach the
 * field through the frame run, since a page that lost the handler would still type and
 * still undo nothing through a path no key takes.
 */
static void the_gallery_field_steps_a_word_and_holds_the_value(void) {
    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("let me in"));
    step(key_down(CF_KEY_HOME));

    CF_CHECK(typed.caret == 0u && typed.mark == 0u, "the caret starts at the front of the value");

    step(word_arrow(CF_KEY_RIGHT, 0u));

    CF_CHECK(typed.caret == 3u && typed.mark == 3u, "one word key spends the word it stands in");

    step(word_arrow(CF_KEY_RIGHT, (uint32_t)CF_MOD_SHIFT));

    CF_CHECK(typed.caret == 6u && typed.mark == 3u,
             "with shift it crosses the blank and selects the run beyond it");

    step(chord('a', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(typed.caret == 9u && typed.mark == 0u, "and the chord holds the whole value");

    step(typed_text("x"));

    CF_CHECK(typed.length == 1u && typed_bytes[0] == 'x',
             "so the next run typed goes over all of it");

    end_session();
}

/*
 * The page's field over a sixty-four byte buffer: the run that would not fit whole
 * lands nowhere and is counted, which is the difference the reader sees between a field
 * that is full and a field that stopped answering.
 */
static void the_gallery_field_says_what_it_had_no_room_for(void) {
    static const char wide[] = "abcdefghijklmnopqrstuvwxyz0123456789ABCDE";
    static const char short_run[] = "twenty-five bytes here!!!";

    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text(wide));

    CF_CHECK(typed.length == 41u && typed.refusals == 0u, "the first run fits the page's buffer");

    step(typed_text(wide));

    CF_CHECK(typed.length == 41u && typed.refusals == 1u,
             "the one that would overflow it lands nowhere and is said");

    step(chord('a', (uint32_t)CF_MOD_CONTROL));
    step(typed_text(short_run));

    CF_CHECK(typed.length == 25u && typed.refusals == 1u,
             "and a run the selection freed is no refusal at all");

    end_session();
}

/*
 * The two asks chapter two's field can be handed and answers the other way round: the
 * space bar is the character the device sends beside it, so the box keeps it and the
 * page hears nothing, while Enter runs the handler the page put on the field itself.
 */
static void the_gallery_field_saves_on_enter_and_types_a_space(void) {
    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    CF_CHECK(page_reads("asked to save: nothing since the page opened"),
             "the page says no value has been asked for");

    step(typed_text("save me"));
    step(key_down(CF_KEY_SPACE));

    CF_CHECK(typed.length == 7u, "a key down with no text alongside it writes no byte");
    CF_CHECK(page_reads("asked to save: nothing since the page opened"),
             "and the space a person is typing asks the page to save nothing");

    step(typed_text(" "));

    CF_CHECK(typed.length == 8u, "the character itself comes from the device as text");

    step(key_down(CF_KEY_ENTER));

    CF_CHECK(page_reads("asked to save: save me"),
             "Enter hands the whole value to the door the page put on the field");

    end_session();
}

/*
 * The other door chapter two's field reports through. The page counts the keys that moved
 * the bytes it owns and paints the count, so a reader can see the rule the door keeps: a run
 * is one change however many letters it carried, and the keys that move the caret or hold the
 * selection leave the number where it stands.
 */
static void the_gallery_page_hears_the_value_move_and_not_the_caret(void) {
    start_session(current_size());

    if (!focus_the_field()) {
        end_session();
        return;
    }

    CF_CHECK(page_reads("changes the page heard: 0"), "an untouched buffer has moved nowhere");

    step(typed_text("save me"));

    CF_CHECK(typed.length == 7u && page_reads("changes the page heard: 1"),
             "one run of bytes is one report, however many it wrote");

    step(key_down(CF_KEY_LEFT));
    step(key_down(CF_KEY_HOME));
    step(chord('a', (uint32_t)CF_MOD_CONTROL));

    CF_CHECK(page_reads("changes the page heard: 1"),
             "a caret that walks and a value that is held are no change to it");

    step(key_down(CF_KEY_BACKSPACE));

    CF_CHECK(typed.length == 0u && page_reads("changes the page heard: 2"),
             "the key that took the bytes is the next report");

    end_session();
}

/*
 * The two modes of the same widget, side by side on one page. The memo was declared for rows,
 * so Enter writes one and the page is asked to save nothing, and its arrows walk the caret
 * between the rows the box paints; the entry above it keeps the key as the page's ask and
 * refuses the break altogether. The memo's third row is the other half of the rule: the box
 * stays the two rows the build stated while the value lays three, because the mode is a word
 * the build said and not a reading of what the box turned out to be.
 */
static void the_two_modes_answer_the_same_keys_differently(void) {
    start_session(current_size());

    if (!focus_the_memo()) {
        end_session();
        return;
    }

    step(typed_text("first line"));
    step(key_down(CF_KEY_ENTER));
    step(typed_text("second"));

    CF_CHECK(memo.length == 17u && memo_chars[10] == '\n',
             "Enter writes the break a memo is declared to hold");
    CF_CHECK(page_reads("the memo holds 17 bytes and 1 break"),
             "and the caption says the break is in the value the page reads");
    CF_CHECK(page_reads("asked to save: nothing since the page opened"),
             "the key that broke a line asks the page to save nothing");

    step(key_down(CF_KEY_UP));

    CF_CHECK(memo.caret == 6u, "one arrow steps onto the row above, six scalars in");

    step(shift_arrow(CF_KEY_DOWN));

    CF_CHECK(memo.caret == 17u && memo.mark == 6u,
             "and holding shift reaches down over the row it came back from");

    /* The shift above left a range held, and a key that writes takes what is selected: end
     * the walk at the value's own edge before adding to it. */
    step(key_down(CF_KEY_END));
    step(key_down(CF_KEY_ENTER));
    step(typed_text("third"));

    CF_CHECK(memo.length == 23u && page_reads("the memo holds 23 bytes and 2 break"),
             "a third row lands in the value the same way");

    step(key_down(CF_KEY_UP));

    CF_CHECK(memo.caret == 16u,
             "and the arrow finds the row above it, which the box slides to show");

    /* The other box on the same page, with the other word said about it. */
    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("done memo"));
    step(key_down(CF_KEY_ENTER));

    CF_CHECK(page_reads("asked to save: done memo"),
             "the single-line box hands the same key to the door the page put on it");
    CF_CHECK(memo.length == 23u, "and adds no row to the value the other box is keeping");

    end_session();
}

static void the_narrow_button_pins_the_well_and_the_lines_follow(void) {
    /* The chip and the well live in chapter three; a walk puts it on screen. */
    if (!reveal_to("narrow it", 12u)) {
        return;
    }

    tap("narrow it");
    end_session();

    CF_CHECK(*narrow_text(&session), "the press flipped the pinch");
}

static void the_wheel_walks_the_page_it_lands_on(void) {
    const cf_event probe[] = {
        (cf_event){.type = CF_EVENT_QUIT},
    };
    run_session(current_size(), probe, 1u);
    const cf_rect page = page_box();

    cf_event hands[4];
    uint32_t at = 0u;

    hands[at++] = wheel_at(page.x + 50.0f, page.y + 50.0f, 100.0f);
    hands[at++] = wheel_at(page.x + 50.0f, page.y + 50.0f, 100.0f);
    hands[at++] = (cf_event){.type = CF_EVENT_QUIT};

    run_session(current_size(), hands, at);

    const cf_scroll *held = cf_scroll_of(&session, cf_intern_text(&session.names, "page"));

    CF_CHECK(held != NULL && held->offset >= 150.0f,
             "two notches walk the document toward its end");
}

static void the_panel_keeps_its_own_wheel_and_reveals_its_rows(void) {
    if (!reveal_to("box 0", 12u)) {
        return;
    }

    /* Wheel inside the pane's own content, so the walk climbs through the
     * row into the panel's wheel handler and stops there: the page that
     * carries it holds its place. Aiming at the pane's title bar would land
     * on the chapter card instead, and the page would take the wheel. */
    bool found = false;
    const cf_rect first = find_text_rect("box 0", &found);

    if (!found) {
        CF_CHECK(false, "the pane's first row is on screen before the wheel");
        end_session();
        return;
    }

    const cf_scroll *page_before = cf_scroll_of(&session, cf_intern_text(&session.names, "page"));
    const float page_offset = page_before != NULL ? page_before->offset : 0.0f;

    /* One wheel unit is forty-eight pixels; the pane's eight rows of sixty
     * want more than one notch to visibly move. */
    step(wheel_at(first.x + first.width / 2.0f, first.y + first.height / 2.0f, 4.0f));
    end_session();

    const cf_scroll *panel = cf_scroll_of(&session, cf_intern_text(&session.names, "panel"));
    const cf_scroll *page = cf_scroll_of(&session, cf_intern_text(&session.names, "page"));

    CF_CHECK(panel != NULL && panel->offset > 0.0f, "the pane walked its own content");
    CF_CHECK(page != NULL && fabsf(page->offset - page_offset) < 1.0f,
             "and the page it sits on did not move");

    /* After the walk the pane's row 0 has scrolled past its top, so the
     * hunt is a prefix - any row that still paints proves the pane kept
     * drawing content rather than falling off the stream. */
    bool row_shown = false;
    (void)find_text_prefix("box", &row_shown);
    CF_CHECK(row_shown, "the pane still paints a row after its wheel");
}

/* The pane's scrollbar track: the one painted box exactly as wide and as tall as
 * the bar the thumb rides, which is what turns the thumb's own numbers into a
 * point on the page. */
static bool accept_track(const cf_command *command) {
    return fabsf(command->rect.width - PANEL_TRACK) < 0.5f &&
           fabsf(command->rect.height - PANEL_VIEWPORT) < 0.5f;
}

static float pane_offset(void) {
    const cf_scroll *held = panel_scroll(&session);

    return held != NULL ? held->offset : -1.0f;
}

/* The bar's track as the stream paints it. */
static bool track_rect(cf_rect *out) {
    bool found = false;
    const cf_rect track = find_fill_rect(accept_track, &found);

    if (found) {
        *out = track;
    }

    return found;
}

/* The thumb as it stands this frame, in the page's coordinates: the bar's own
 * numbers, laid on the track the stream shows. */
static bool thumb_rect(cf_rect *out) {
    cf_rect track;

    if (!track_rect(&track)) {
        return false;
    }

    const cf_rect thumb = cf_scrollbar_thumb(panel_bar(), pane_offset());

    *out = (cf_rect){track.x, track.y + thumb.y, thumb.width, thumb.height};
    return true;
}

/* The point a press has to land on for the thumb to take it. */
static cf_point thumb_grip(cf_rect thumb) {
    return (cf_point){thumb.x + thumb.width / 2.0f, thumb.y + thumb.height / 2.0f};
}

/* Walk the page until the whole bar lies inside the window, and hand back the
 * thumb as that frame paints it. A row being reachable is not enough: the thumb
 * rides below it and slides as the pane moves, and cf_hit_test honours the page's
 * clip, so a press on the part the fold cuts off lands on nothing. */
static bool reveal_thumb(cf_rect *out) {
    for (uint32_t n = 0u; n <= REVEAL_MAX_STEPS; ++n) {
        cf_rect track;

        if (track_rect(&track) && reaches_pointer(track) && thumb_rect(out)) {
            return true;
        }

        if (!page_scroll_to((float)(n + 1u) * REVEAL_STEP_PX)) {
            return false;
        }
    }

    CF_CHECK(false, "no step brings the whole bar inside the window");
    return false;
}

/* The thumb is the one box on the page that keeps a drag of its own between the
 * press and the release, so it is the box a cancellation has to reach: the pane
 * stops following a pointer the window lost, and takes the next one it is given. */
static void a_lost_pointer_ends_the_drag_the_thumb_held(void) {
    /* A session of its own: the pane's offset is state the previous case walked,
     * and a thumb dragged to the bottom of its track is not the one this case
     * presses. */
    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    /* The walk stops on the bar rather than on a row, so that both drags this case
     * runs press a thumb the window still shows. */
    cf_rect thumb;

    if (!reveal_thumb(&thumb)) {
        end_session();
        return;
    }

    const cf_point grip = thumb_grip(thumb);
    const float start = pane_offset();

    step(press_at(grip.x, grip.y));
    step(move_to(grip.x, grip.y + 40.0f));

    const float dragged = pane_offset();

    CF_CHECK(dragged > start, "the drag walked the pane");

    step((cf_event){.type = CF_EVENT_POINTER_CANCEL, .position = {grip.x, grip.y + 40.0f}});

    /* The pointer travelling back over the thumb is not a hand still holding it.
     * The drag ran from `grip`, so one that had outlived the cancellation would
     * push the pane by the whole distance from that first point rather than by
     * nothing. The thumb slid down with the offset, so its lower edge is one
     * half-height below where the drag left its middle. */
    step(move_to(grip.x, grip.y + 40.0f + thumb.height / 2.0f - 4.0f));

    CF_CHECK(fabsf(pane_offset() - dragged) < 1.0f,
             "a pointer the window lost leaves the pane where it was");

    /* The widget is not wedged by the cancellation: a press that starts after it
     * drags again. The thumb slid with the offset, so it is gripped afresh. */
    if (!thumb_rect(&thumb)) {
        CF_CHECK(false, "the thumb is still painted after the cancellation");
        end_session();
        return;
    }

    const cf_point again = thumb_grip(thumb);

    step(press_at(again.x, again.y));
    step(move_to(again.x, again.y + 40.0f));

    CF_CHECK(pane_offset() > dragged, "and the thumb answers a drag that starts after it");

    /* Letting go ends the second drag the ordinary way, so the session hands on no
     * hold the next case would have to fight. */
    step(release_at(again.x, again.y + 40.0f));

    end_session();
}

static void the_list_builds_only_the_rows_the_viewport_shows(void) {
    /* The list is chapter six, well under the fold at the default height. */
    if (!reveal_to("row 0", 30u)) {
        return;
    }

    /* The list keeps only the rows the viewport can show, so the frame
     * that reveals it declared fewer than half the forty total. */
    CF_CHECK(*rows_built(&session) < LIST_ROWS / 2u,
             "the frame declared fewer than half the forty rows");

    bool found = false;
    const cf_rect first = find_text_rect("row 0", &found);

    CF_CHECK(found, "the list's first row reads back");

    if (!found) {
        end_session();
        return;
    }

    const cf_rect viewport = {0.0f, 0.0f, current_viewport.width, current_viewport.height};

    CF_CHECK(rects_overlap(first, viewport), "and it sits where the window can show it");

    /* The hover tone is the list widget's own ask: the move lands on the
     * row's text, and the widget's handler reads what is under it. */
    step(move_to(first.x + 5.0f, first.y + first.height / 2.0f));
    end_session();

    CF_CHECK(!cf_id_is_none(session.hover_id), "the pointer booked a hover on the page");
}

static void the_chips_leave_their_number_in_the_state_the_build_reads(void) {
    if (!reveal_to("wrap", 20u)) {
        return;
    }

    tap("wrap");
    end_session();
    CF_CHECK(*picked_rule(&session) == RULE_WRAP, "the rule chip wrote its index");

    if (!reveal_to("repeated pair", 30u)) {
        return;
    }

    tap("repeated pair");
    end_session();
    CF_CHECK(*picked_plan(&session) == PLAN_REPEAT, "and the plan chip wrote its own");
}

static void the_badge_switch_moves_it_and_the_logo_still_paints(void) {
    if (!reveal_to("put it back in the flow", 30u)) {
        return;
    }

    tap("put it back in the flow");
    end_session();

    CF_CHECK(*badge_in_flow(&session), "the badge came back into the flow");
    CF_CHECK(page_shows("v1"), "and the badge paints where the flow put it");
}

static void a_press_sends_the_puck_and_the_page_stops_asking_when_it_lands(void) {
    if (!reveal_to("send it", 40u)) {
        return;
    }

    /* The step helper keeps the session alive so the tween runs to its end
     * inside the frames the press asks for; the flush after the release
     * keeps building while cf_request_frame says the page is mid-motion. */
    tap("send it");
    end_session();

    const cf_tween *tween = slide_tween(&session);

    CF_CHECK(tween != NULL && tween->to >= SLIDE_TRAVEL - 0.01f,
             "the press aimed the tween at the far end");
    CF_CHECK(page_shows("bring it back"), "the button now offers the way home");
}

/* Chapter ten, and the ring the whole page is in: from a cold start the first
 * Tab stands the keyboard on the page's first control, the walk steps past the
 * chip that is switched off, reverse Tab walks back, and both the keys that ask
 * reach the chip the walk stopped on. One pointer press starts the chapter - the
 * rest of the session never touches the mouse. */
static void the_keyboard_walks_the_page_and_asks_where_it_stands(void) {
    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    step(tab_key(false));
    step(key_down(CF_KEY_ENTER));

    CF_CHECK(*click_count(&session) == 1u,
             "a Tab from no focus stands on the first control the build wrote");

    if (!reveal_to("chip one", 40u)) {
        return;
    }

    tap("chip two");
    CF_CHECK(page_reads("the keyboard last asked: chip two"), "the click answered through the row");

    step(tab_key(false));
    step(key_down(CF_KEY_ENTER));

    CF_CHECK(page_reads("the keyboard last asked: chip three"),
             "the walk stepped past the switched-off chip and the key asked where it landed");

    step(tab_key(true));
    step(key_down(CF_KEY_SPACE));

    CF_CHECK(page_reads("the keyboard last asked: chip two"),
             "reverse Tab walks back and the space bar asks the same way Enter does");

    /* Onward from the second chip the ring runs out the rest of the page and comes back round to
     * its start. Stepping until the count moves is what keeps this about the wrap rather than about
     * how many controls happen to sit below the chips - a chapter added at the end of the document
     * lengthens the run without changing the rule. */
    uint32_t around = 0u;

    while (*click_count(&session) == 1u && around < 24u) {
        step(tab_key(false));
        step(key_down(CF_KEY_ENTER));
        around++;
    }

    CF_CHECK(around < 24u, "the walk reached a control whose ask the first chapter counts");
    CF_CHECK(*click_count(&session) == 2u, "a walk off the end of the page returns to its start");
    CF_CHECK(!cf_frame_takes_text(&session),
             "and the box it stopped on takes the keyboard without asking for one to show");

    end_session();
}

/* The other half of chapter ten: the box the keyboard stands on can stop being
 * declared, and the keyboard has to come back from that rather than be left holding a
 * slot no build answers. Only the first press is made by hand; after it the state can
 * move again solely because a key reached a box that takes it. */
static void the_box_that_closes_hands_the_keyboard_back(void) {
    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    if (!reveal_to("open the box", REVEAL_MAX_STEPS)) {
        end_session();
        return;
    }

    tap("open the box");
    CF_CHECK(*dialog_box(&session), "the press declared the second control");
    CF_CHECK(page_shows("close the box"), "and the row reads it back");

    step(tab_key(false));
    step(key_down(CF_KEY_ENTER));

    CF_CHECK(!*dialog_box(&session),
             "Tab walked from the opener onto the new control and the plain Enter asked it to go");
    CF_CHECK(!page_shows("close the box"), "the build stops declaring the box that answered");

    step(key_down(CF_KEY_ENTER));

    CF_CHECK(*dialog_box(&session),
             "with no hand on the mouse the control comes back, so the keyboard is standing on the "
             "box it left");
    CF_CHECK(page_shows("close the box"), "and the row paints the control again");

    end_session();
}

/* The last chapter's button is the page's own way out. This harness is not a shell -
 * it runs the pipeline itself and never leaves on the ask - which is exactly why the
 * ask is readable here: it belongs to the context, not to whoever owns the window. */
static void the_page_can_ask_the_shell_to_leave(void) {
    start_session(current_size());

    /* Revealed by the line rather than the button: the line is under it, so a step
     * that reaches the line has the button well inside what a pointer can hit. */
    if (!reveal_to("nothing asked yet", REVEAL_MAX_STEPS)) {
        end_sessions();
        return;
    }

    CF_CHECK(!cf_quit_requested(&session), "the chapters above the last one ask for nothing");

    if (tap("end the run")) {
        CF_CHECK(cf_quit_requested(&session), "the press left the ask on the context");
        CF_CHECK(page_reads("the page has asked to be done"),
                 "and the chapter reads the ask back in its own line");
    }

    end_sessions();
}

static void a_narrow_window_reflows_without_spilling_geometry(void) {
    const cf_event hands[] = {
        (cf_event){.type = CF_EVENT_QUIT},
    };
    run_session((cf_size){WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT}, hands, 1u);

    /* At 360x240 the page viewport shows the header, the first card's edge
     * and the row gaps between them: enough commands to prove the paint
     * walk did not stop after the header, and no assertion here needs a
     * count the layout could not reasonably produce. */
    CF_CHECK(recorded_count > 5u, "the page still paints at the smallest window");

    /* The header's own words wrap into per-line text commands at a narrow
     * offer, so the hunt is a prefix - the first word arriving on a line
     * that could not hold the whole phrase is still the header painting
     * its name rather than a card or a chapter. */
    bool found = false;
    (void)find_text_prefix("cform", &found);
    CF_CHECK(found, "the header holds its words at 360 points");
}

static void a_bigger_output_still_bounds_every_box(void) {
    const cf_event hands[] = {
        (cf_event){.type = CF_EVENT_QUIT},
    };
    run_session((cf_size){1600.0f, 1200.0f}, hands, 1u);

    bool found = false;
    (void)find_fill_rect(accept_bigger_fill, &found);

    CF_CHECK(found, "the scroller fills the bigger output");
}

/* The inspector overlay is added inside cf_app_run, not cf_frame_run, so
 * this case drives the shell twice - once plain, once with F12 down - and
 * compares the two submits. The recorder keeps only what the last submit
 * handed it, so each run gets a fresh arena clear.
 *
 * cf_app_run drains its poll queue before every paint, so a script that
 * quits at position zero would exit without a frame. The helper holds
 * QUIT back until the poll has been asked a few times past the drained
 * queue - enough for at least one paint to happen before the shell reads
 * the ask. */
static uint32_t shell_at;
static const cf_event *shell_events;
static uint32_t shell_len;
static uint32_t shell_idle;
static cf_size shell_offer;

#define SHELL_PAINTS_BEFORE_QUIT 4u

static bool shell_poll(void *user_data, cf_event *out) {
    (void)user_data;

    if (shell_at < shell_len) {
        *out = shell_events[shell_at++];
        return true;
    }

    if (shell_idle >= SHELL_PAINTS_BEFORE_QUIT) {
        *out = (cf_event){.type = CF_EVENT_QUIT};
        return true;
    }

    ++shell_idle;
    return false;
}

static cf_size shell_surface_cb(void *user_data) {
    (void)user_data;
    return shell_offer;
}

static uint32_t shell_run(const cf_event *events, uint32_t count, cf_recorder *recorder,
                          cf_arena *store) {
    cf_arena_clear(store);
    recorder->arena = store;
    recorder->count = 0u;
    recorder->capacity = 0u;
    recorder->commands = NULL;

    cf_backend backend = cf_backend_recorder(recorder);
    backend.create_texture = fake_create_texture;
    backend.poll_event = shell_poll;
    backend.surface = shell_surface_cb;

    shell_events = events;
    shell_len = count;
    shell_at = 0u;
    shell_idle = 0u;
    shell_offer = current_size();

    cf_app_run(
        &(cf_app_config){.build = build, .backend = backend, .theme = theme, .fonts = &body});

    return recorder->count;
}

static void the_inspector_lays_its_numbers_over_the_page(void) {
    static cf_arena store;
    static cf_recorder plain_rec;
    static cf_recorder shown_rec;

    if (!cf_arena_init(&store, SCENARIO_ARENA_BYTES)) {
        CF_CHECK(false, "the recorder's store starts");
        return;
    }

    theme = cf_theme_default();
    body_font = mono_font;
    title_font = mono_font;
    body = (cf_text_stack){.fonts = &body_font, .count = 1u};
    title = (cf_text_stack){.fonts = &title_font, .count = 1u};
    gallery_init_assets();
    current_viewport = (cf_rect){0.0f, 0.0f, WINDOW_WIDTH, WINDOW_HEIGHT};

    /* QUIT is never in the event script; shell_poll synthesises it once
     * the queue drains past the paint count the shell needs to see. */
    const cf_event plain[] = {
        (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {200.0f, 200.0f}},
    };
    const cf_event shown[] = {
        (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_F12},
    };

    const uint32_t without = shell_run(plain, 1u, &plain_rec, &store);
    const uint32_t with = shell_run(shown, 1u, &shown_rec, &store);

    /* The recorder's count sums every frame cf_app_run drew; with the
     * inspector on, the same frames emit more commands per frame because
     * the panel adds its own text lines. */
    CF_CHECK(without > 0u, "the plain shell paints a frame");
    CF_CHECK(with > without, "the panel adds its lines to the frame");

    bool numbers_seen = false;

    for (uint32_t i = 0u; i < shown_rec.count && !numbers_seen; ++i) {
        const cf_command command = shown_rec.commands[i];

        numbers_seen = command.kind == CF_COMMAND_TEXT && command.text.chars != NULL &&
                       command.text.length >= 5u && memcmp(command.text.chars, "nodes", 5) == 0;
    }

    CF_CHECK(numbers_seen, "the numbers name themselves in the stream");

    cf_arena_free(&store);
}

/*
 * The bridge a screen reader would be.
 *
 * Everything below is what a host outside the framework could do with the walk alone: ask after a
 * frame, copy the announcements out into its own table, and work out what moved by comparing two
 * asks by identity. Nothing here reads the painted stream, so a page that moved words from one box
 * to another would show up as a row whose name changed underneath an id the bridge was holding.
 */

#define SPOKEN_MAX 512u
#define SPOKEN_BYTES 48u

typedef struct announced {
    cf_id id;
    uint32_t slot;
    cf_access access;
    cf_announce announce;
    char name[SPOKEN_BYTES];
    char value[SPOKEN_BYTES];
    cf_id labelled_by;
    cf_id described_by;
    cf_id controls;
    cf_opt_u32 selection_start;
    cf_opt_u32 selection_end;
    uint32_t states;
    uint32_t actions;
} announced;

static announced spoken[SPOKEN_MAX];
static uint32_t spoken_count;

/* Copied out because the visit borrows: a bridge that means to read a name after the frame has gone
 * has to keep its own bytes, exactly as a build keeps its text in a buffer it owns. */
static void take_words(char *out, cf_string text) {
    const uint32_t length = text.chars != NULL && text.length < SPOKEN_BYTES - 1u
                                ? text.length
                                : (text.chars != NULL ? SPOKEN_BYTES - 1u : 0u);

    if (length != 0u) {
        memcpy(out, text.chars, length);
    }

    out[length] = '\0';
}

static void keep_the_visit(const cf_semantic *node, void *user_data) {
    (void)user_data;

    if (spoken_count == SPOKEN_MAX) {
        return;
    }

    announced *row = &spoken[spoken_count++];
    *row = (announced){.id = node->id,
                       .slot = node->slot,
                       .access = node->access,
                       .announce = node->announce,
                       .labelled_by = node->labelled_by,
                       .described_by = node->described_by,
                       .controls = node->controls,
                       .selection_start = node->selection_start,
                       .selection_end = node->selection_end,
                       .states = node->states,
                       .actions = node->actions};
    take_words(row->name, node->name);
    take_words(row->value, node->value);
}

/* Ask the walk what the frame a person is looking at announces. The page has been laid out and
 * painted before this runs, so the rects come from the same frame the paint read. */
static void ask_the_page(void) {
    spoken_count = 0u;
    cf_semantic_visit(&session, keep_the_visit, NULL);
}

static const announced *row_with(const announced *rows, uint32_t count, cf_access access,
                                 const char *name) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (rows[i].access == access && strcmp(rows[i].name, name) == 0) {
            return &rows[i];
        }
    }

    return NULL;
}

static const announced *row_by_id(const announced *rows, uint32_t count, cf_id id) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (cf_id_equal(rows[i].id, id)) {
            return &rows[i];
        }
    }

    return NULL;
}

static const announced *heard_as(cf_access access, const char *name) {
    return row_with(spoken, spoken_count, access, name);
}

static uint32_t count_of_access(const announced *rows, uint32_t count, cf_access access) {
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        seen += rows[i].access == access ? 1u : 0u;
    }

    return seen;
}

/* A row found by the beginning of its words, for the lines a page writes into a buffer of its own:
 * the copy is bounded, so an exact match would ask the case to know how long the sentence was
 * allowed to be. */
static const announced *heard_starting(const char *prefix) {
    const size_t length = strlen(prefix);

    for (uint32_t i = 0u; i < spoken_count; ++i) {
        if (strncmp(spoken[i].name, prefix, length) == 0) {
            return &spoken[i];
        }
    }

    return NULL;
}

static uint32_t count_of_announce(const announced *rows, uint32_t count, cf_announce level) {
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        seen += rows[i].announce == level ? 1u : 0u;
    }

    return seen;
}

/*
 * What the page says about itself, read the way an outside reader would read it. Every claim here
 * is the acceptance the roadmap sets for the bridge: a button's name comes from the label painted
 * on it, a field exports the bytes it holds along with the run a hand is holding, a box that
 * declared nothing is passed through rather than announced, and a control nobody named is announced
 * with nothing to say - which is the bug a person who cannot see the page cannot tell apart from a
 * control that is not there at all.
 */
static void a_bridge_hears_the_page_the_way_a_person_reads_it(void) {
    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});
    ask_the_page();

    CF_CHECK(spoken_count > 20u, "a whole page of controls is announced");
    CF_CHECK(count_of_access(spoken, spoken_count, CF_ACCESS_NONE) == 0u,
             "and no row of the reading is a box that declared nothing");

    const cf_tree *tree = cf_frame_tree(&session);

    CF_CHECK(tree != NULL && spoken_count < tree->node_count,
             "the wrappers a layout needed stay out of the reading");

    const announced *count = heard_as(CF_ACCESS_BUTTON, "count");
    const announced *clear = heard_as(CF_ACCESS_BUTTON, "clear");

    CF_CHECK(count != NULL, "a button is announced by the words painted on it");
    CF_CHECK(clear != NULL, "and so is the one beside it");

    const announced *labelled =
        count != NULL ? row_by_id(spoken, spoken_count, count->labelled_by) : NULL;

    CF_CHECK(labelled != NULL && labelled->access == CF_ACCESS_LABEL,
             "the name came from a label the same reading carries, reachable by its id");
    CF_CHECK(labelled != NULL && !labelled->selection_start.present &&
                 !labelled->selection_end.present,
             "and text nobody types into is announced with no selection at all, rather than one "
             "sitting at offset zero");
    CF_CHECK(count != NULL && (count->actions & (uint32_t)CF_SEMANTIC_ACTIVATE) != 0u,
             "and the ask the frame really has is the one offered");

    bool painted = false;
    (void)find_text_rect(count != NULL ? count->name : "count", &painted);
    CF_CHECK(painted, "the words handed to a bridge are the words the page shows");

    CF_CHECK(heard_as(CF_ACCESS_IMAGE, "The cform mark") != NULL,
             "a picture is announced by the sentence its build wrote for it");
    CF_CHECK(count_of_access(spoken, spoken_count, CF_ACCESS_SCROLL_AREA) == 2u,
             "both boxes that scroll their content say so - the page and the pane in it");
    CF_CHECK(count_of_access(spoken, spoken_count, CF_ACCESS_LIST) == 1u,
             "and the list that holds the rows is announced as the run, above its items");

    /* Which words the page means to say again. Two boxes of the build carry a level and the walk
     * hands it to the lines that move, so a bridge knows what to watch for news without watching
     * the whole page for it. */
    const announced *refusal = heard_starting("the buffer holds");
    const announced *news = heard_starting("the page has made news");
    const announced *beside = heard_starting("and 0 of them");
    CF_CHECK(refusal != NULL && refusal->announce == CF_ANNOUNCE_ASSERTIVE,
             "the line that reports a key refused is the one worth interrupting for");
    CF_CHECK(count_of_announce(spoken, spoken_count, CF_ANNOUNCE_ASSERTIVE) == 1u,
             "and it is the only line on the page that says so");
    CF_CHECK(news != NULL && news->announce == CF_ANNOUNCE_POLITE,
             "the status line is covered by the box that holds it, having stated nothing itself");
    CF_CHECK(beside != NULL && beside->announce == CF_ANNOUNCE_POLITE,
             "and so is the line written beside it, which is what marking an area buys");
    CF_CHECK(count_of_announce(spoken, spoken_count, CF_ANNOUNCE_POLITE) == 2u,
             "only the two lines inside that region are watched, not the page around them");

    const announced *mute = NULL;

    for (uint32_t i = 0u; i < spoken_count && mute == NULL; ++i) {
        if (spoken[i].access == CF_ACCESS_BUTTON && spoken[i].name[0] == '\0') {
            mute = &spoken[i];
        }
    }

    CF_CHECK(mute != NULL && cf_id_is_none(mute->labelled_by),
             "the control nobody named is announced with nothing to say and no label behind it");

    /* The rows the list built rather than the rows the list holds: what a bridge is told about a
     * virtualized run is what this frame declared. */
    const uint32_t items = count_of_access(spoken, spoken_count, CF_ACCESS_ITEM);

    CF_CHECK(items > 0u && items < LIST_ROWS,
             "only the rows the frame built are announced as items of the list");

    /* The field the keyboard is standing on: its stated name, the bytes typed into it, the run the
     * caret marks, and the two states a reader needs to know where they are. */
    if (!focus_the_field()) {
        end_session();
        return;
    }

    step(typed_text("hi"));
    step(key_down(CF_KEY_LEFT));
    ask_the_page();

    const announced *entry = heard_as(CF_ACCESS_TEXT_BOX, "Display name");

    CF_CHECK(entry != NULL, "the field is announced by the name its build stated");
    CF_CHECK(entry != NULL && strcmp(entry->value, "hi") == 0, "and by the bytes it holds");
    CF_CHECK(entry != NULL && (entry->states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u,
             "with the keyboard standing on it");
    CF_CHECK(entry != NULL && (entry->states & (uint32_t)CF_SEMANTIC_EDITABLE) != 0u,
             "and the fact that a person can type into it");
    CF_CHECK(entry != NULL && (entry->actions & (uint32_t)CF_SEMANTIC_SET_VALUE) != 0u,
             "a bridge is offered the ask a field actually takes");
    CF_CHECK(entry != NULL && entry->selection_start.present && entry->selection_end.present &&
                 entry->selection_start.value == 1u && entry->selection_end.value == 1u,
             "and where the caret is, as offsets into the bytes");

    CF_CHECK(count_of_access(spoken, spoken_count, CF_ACCESS_TEXT_BOX) == 3u,
             "every field the page builds answers, and no other box claims to take typed text");
    CF_CHECK(entry != NULL && entry->announce == CF_ANNOUNCE_NONE,
             "a field is not a news line: no region covers what a person is typing into");

    uint32_t focused = 0u;

    for (uint32_t i = 0u; i < spoken_count; ++i) {
        focused += (spoken[i].states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u ? 1u : 0u;
    }

    CF_CHECK(focused == 1u, "exactly one box holds the keyboard in the reading");

    end_session();
}

/*
 * The other half of the agreement: a bridge does not rebuild its world each frame, it asks twice
 * and paints the difference. Identity is the store's generational id, so the box that stayed is the
 * same id saying the same words, the box that came is an id nothing was held for, and the box that
 * left is simply not answered - which is also why a row that later took the same slot cannot
 * inherit what the bridge knew about the one that went.
 */
static void a_bridge_asking_twice_sees_which_boxes_moved(void) {
    static announced before[SPOKEN_MAX];
    uint32_t before_count = 0u;

    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    if (!reveal_to("open the box", REVEAL_MAX_STEPS)) {
        end_session();
        return;
    }

    ask_the_page();
    CF_CHECK(spoken_count < SPOKEN_MAX, "the page fits the table a bridge would keep");
    memcpy(before, spoken, (size_t)spoken_count * sizeof before[0]);
    before_count = spoken_count;

    const announced *opener = row_with(before, before_count, CF_ACCESS_BUTTON, "open the box");

    CF_CHECK(opener != NULL, "the opener is announced before the box it adds exists");

    if (!tap("open the box")) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *added = heard_as(CF_ACCESS_BUTTON, "close the box");

    CF_CHECK(added != NULL, "the control the page added is announced");
    CF_CHECK(added != NULL && row_by_id(before, before_count, added->id) == NULL,
             "under an id the earlier ask never heard, which is the whole of the notification");

    uint32_t retired = 0u;
    uint32_t reworded = 0u;

    for (uint32_t i = 0u; i < before_count; ++i) {
        const announced *still = row_by_id(spoken, spoken_count, before[i].id);

        if (still == NULL) {
            retired++;
        } else if (still->access != before[i].access || strcmp(still->name, before[i].name) != 0) {
            reworded++;
        }
    }

    CF_CHECK(retired == 0u, "nothing the page kept stopped being announced");
    CF_CHECK(reworded == 0u, "and every box that stayed says the words it said before");

    const cf_id gone = added != NULL ? added->id : CF_NO_ID;

    if (!tap("close the box")) {
        end_session();
        return;
    }

    ask_the_page();

    CF_CHECK(row_by_id(spoken, spoken_count, gone) == NULL,
             "the box that left is not answered at all rather than answered empty");

    const announced *kept = heard_as(CF_ACCESS_BUTTON, "open the box");

    CF_CHECK(kept != NULL && cf_id_equal(kept->id, opener != NULL ? opener->id : CF_NO_ID),
             "and the box that stayed through it all is the same box to the bridge");

    end_session();
}

/*
 * The live region read the way a bridge has to read it: ask, keep the identities, let the page move
 * the words inside the marked area, ask again. The lines that were watched are still there under
 * the ids they were watched at, answering differently, and the urgency the region was marked with
 * does not move with the news. That is a notification, handed over by a bridge that never looked at
 * the painted stream.
 */
static void the_boxes_inside_a_live_region_are_reheard_under_their_own_ids(void) {
    char news_words[SPOKEN_BYTES];
    char beside_words[SPOKEN_BYTES];
    char urgent_words[SPOKEN_BYTES];

    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    if (!reveal_to("make news", REVEAL_MAX_STEPS)) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *news = heard_starting("the page has made news");
    const announced *beside = heard_starting("and 0 of them");
    const announced *urgent = heard_starting("the buffer holds");
    CF_CHECK(news != NULL && beside != NULL && urgent != NULL,
             "both lines of the region and the urgent line outside it are in the first reading");

    /* Kept by value: the next ask overwrites the table, and a bridge that meant to compare two
     * frames had to remember what it was told rather than point at it. */
    const cf_id news_id = news != NULL ? news->id : CF_NO_ID;
    const cf_id beside_id = beside != NULL ? beside->id : CF_NO_ID;
    const cf_id urgent_id = urgent != NULL ? urgent->id : CF_NO_ID;
    snprintf(news_words, sizeof news_words, "%s", news != NULL ? news->name : "");
    snprintf(beside_words, sizeof beside_words, "%s", beside != NULL ? beside->name : "");
    snprintf(urgent_words, sizeof urgent_words, "%s", urgent != NULL ? urgent->name : "");

    if (!tap("make news")) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *again = row_by_id(spoken, spoken_count, news_id);
    const announced *mate = row_by_id(spoken, spoken_count, beside_id);
    const announced *steady = row_by_id(spoken, spoken_count, urgent_id);

    CF_CHECK(again != NULL && mate != NULL,
             "the page moved the words without retiring the boxes that carry them");
    CF_CHECK(again != NULL && again->announce == CF_ANNOUNCE_POLITE && mate != NULL &&
                 mate->announce == CF_ANNOUNCE_POLITE,
             "and the region still covers both of them at the level it was marked with");
    CF_CHECK(again != NULL && strcmp(again->name, news_words) != 0 && mate != NULL &&
                 strcmp(mate->name, beside_words) != 0,
             "both watched lines answer differently now, which is the delta asked for");
    CF_CHECK(again != NULL && strcmp(again->name, "the page has made news 1 times") == 0,
             "and they answer with what happened, not with that something happened");
    CF_CHECK(steady != NULL && steady->announce == CF_ANNOUNCE_ASSERTIVE &&
                 strcmp(steady->name, urgent_words) == 0,
             "the urgent line outside the region kept its words and its urgency alike");

    end_session();
}

/*
 * The relation as a bridge reads it: a name the build booked rather than a box it kept a pointer
 * to. The field points at the caption beside it, which no walk pairing a control with its own label
 * could find, and the button points at a note the page only declares once the press has asked for
 * it. Between the two readings the caption rewrote its own words and stayed the box the field names
 * - which is the whole reason the relation is a key and not an identity the build would have to
 * remember.
 */
static void a_relation_names_the_box_the_page_carries(void) {
    char caption_words[SPOKEN_BYTES];

    start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});

    if (!reveal_to("toggle the note", REVEAL_MAX_STEPS)) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *opener = heard_as(CF_ACCESS_BUTTON, "toggle the note");
    const announced *field = heard_as(CF_ACCESS_TEXT_BOX, "Signature");
    const announced *caption = heard_starting("Letters only");
    CF_CHECK(opener != NULL && field != NULL && caption != NULL,
             "the field, its caption and the button that has a note are all in the reading");
    CF_CHECK(field != NULL && caption != NULL && cf_id_equal(field->described_by, caption->id),
             "the field is read with the caption standing beside it, not inside it");
    CF_CHECK(opener != NULL && cf_id_is_none(opener->controls),
             "and the button whose note the page does not declare answers no box at all");

    /* Kept by value, because the next ask overwrites the table the first one filled. */
    const cf_id opener_id = opener != NULL ? opener->id : CF_NO_ID;
    const cf_id field_id = field != NULL ? field->id : CF_NO_ID;
    const cf_id caption_id = caption != NULL ? caption->id : CF_NO_ID;
    snprintf(caption_words, sizeof caption_words, "%s", caption != NULL ? caption->name : "");

    if (!tap("toggle the note")) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *again = row_by_id(spoken, spoken_count, opener_id);
    const announced *note = heard_starting("the note the press");
    CF_CHECK(again != NULL && note != NULL, "the note joined the reading under an id of its own");
    CF_CHECK(again != NULL && note != NULL && cf_id_equal(again->controls, note->id),
             "and the relation the button stated a frame ago now answers that box");

    const announced *steady = row_by_id(spoken, spoken_count, field_id);
    const announced *moved = row_by_id(spoken, spoken_count, caption_id);
    CF_CHECK(steady != NULL && moved != NULL && cf_id_equal(steady->described_by, moved->id),
             "the field still names the same caption, whose words moved with the flag");
    CF_CHECK(moved != NULL && strcmp(moved->name, caption_words) != 0,
             "and it is the same box saying something different, not another box saying it");

    if (!tap("toggle the note")) {
        end_session();
        return;
    }

    ask_the_page();

    const announced *closed = row_by_id(spoken, spoken_count, opener_id);
    CF_CHECK(closed != NULL && cf_id_is_none(closed->controls),
             "putting the note away takes the relation back to no box as well");
    CF_CHECK(heard_starting("the note the press") == NULL,
             "and the note is not answered at all, rather than answered empty");

    end_session();
}

int main(void) {
    printf("gallery headless\n");

    CF_RUN(the_page_fills_the_recorder);
    CF_RUN(a_click_counts_and_a_clear_ends_the_count);
    CF_RUN(a_switched_off_button_takes_no_click);
    CF_RUN(a_focused_field_takes_what_the_keyboard_wrote);
    CF_RUN(the_gallery_field_trades_bytes_with_the_host_it_names);
    CF_RUN(the_gallery_field_walks_its_own_typing_back);
    CF_RUN(the_gallery_field_steps_a_word_and_holds_the_value);
    CF_RUN(the_gallery_field_says_what_it_had_no_room_for);
    CF_RUN(the_gallery_field_saves_on_enter_and_types_a_space);
    CF_RUN(the_gallery_page_hears_the_value_move_and_not_the_caret);
    CF_RUN(the_two_modes_answer_the_same_keys_differently);
    CF_RUN(the_narrow_button_pins_the_well_and_the_lines_follow);
    CF_RUN(the_wheel_walks_the_page_it_lands_on);
    CF_RUN(the_panel_keeps_its_own_wheel_and_reveals_its_rows);
    CF_RUN(a_lost_pointer_ends_the_drag_the_thumb_held);
    CF_RUN(the_list_builds_only_the_rows_the_viewport_shows);
    CF_RUN(the_chips_leave_their_number_in_the_state_the_build_reads);
    CF_RUN(the_badge_switch_moves_it_and_the_logo_still_paints);
    CF_RUN(a_press_sends_the_puck_and_the_page_stops_asking_when_it_lands);
    CF_RUN(the_keyboard_walks_the_page_and_asks_where_it_stands);
    CF_RUN(the_box_that_closes_hands_the_keyboard_back);
    CF_RUN(the_page_can_ask_the_shell_to_leave);
    CF_RUN(a_narrow_window_reflows_without_spilling_geometry);
    CF_RUN(a_bigger_output_still_bounds_every_box);
    CF_RUN(the_inspector_lays_its_numbers_over_the_page);
    CF_RUN(a_bridge_hears_the_page_the_way_a_person_reads_it);
    CF_RUN(a_bridge_asking_twice_sees_which_boxes_moved);
    CF_RUN(the_boxes_inside_a_live_region_are_reheard_under_their_own_ids);
    CF_RUN(a_relation_names_the_box_the_page_carries);

    end_sessions();
    return CF_REPORT();
}
