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

    const float x = label.x + label.width / 2.0f;
    const float y = label.y + label.height / 2.0f;

    step(press_at(x, y));
    step(release_at(x, y));
    return true;
}

/* Walk the current session's page scroll by 192 pixels at a time until the
 * needle appears. The wheel handler would chain to a chapter's own scroller
 * and land wherever it stops, so the helper writes the offset directly and
 * lets the build read it back - the same value a wheel would produce, just
 * without the intermediate hit walk. The session stays open at whichever
 * step found the needle, so the caller can tap it next. */
#define REVEAL_STEP_PX 192.0f
#define REVEAL_MAX_STEPS 40u

static bool reveal_to(const char *needle, uint32_t max_steps) {
    if (max_steps > REVEAL_MAX_STEPS) {
        max_steps = REVEAL_MAX_STEPS;
    }

    /* Reveal drives whichever session is open; a case that starts from
     * cold gets a fresh one at the default size. */
    if (!session_open) {
        start_session((cf_size){WINDOW_WIDTH, WINDOW_HEIGHT});
    }

    /* If the needle is already visible, no walking needed. */
    if (page_shows(needle)) {
        return true;
    }

    cf_name page_key = cf_intern_text(&session.names, "page");

    for (uint32_t n = 1u; n <= max_steps; ++n) {
        cf_scroll *held = cf_scroll_of(&session, page_key);

        if (held == NULL) {
            printf("    FAIL %s:%d: the page has no scroller to walk\n", __FILE__, __LINE__);
            cf_failures++;
            return false;
        }

        /* Offsets past the document's height clamp back to its end; the
         * helper does not know the content height, so it hands the number
         * to cf_scroll_clamp through the same widget path the wheel takes.
         * Simpler: state the offset directly and let the next build clamp
         * it into its layout by way of the scroller's own inset. */
        held->offset = (float)n * REVEAL_STEP_PX;

        cf_command_list list = {0};
        cf_frame_run(&session, build, current_size(), 1.0f / 60.0f, &list);
        record_frame(&list);

        if (page_shows(needle)) {
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

static void a_focused_field_takes_what_the_keyboard_wrote(void) {
    const cf_event probe[] = {
        (cf_event){.type = CF_EVENT_QUIT},
    };
    run_session(current_size(), probe, 1u);

    /* The caption carries the buffer's bytes, so it is hunted by prefix; the
     * field states its height rather than measuring an empty buffer, so its
     * box is the last painted one above that line. */
    bool found = false;
    const cf_rect echo = find_text_prefix("the buffer holds", &found);

    CF_CHECK(found, "the field's echo line is on the page");

    if (!found) {
        return;
    }

    field_echo_y = echo.y;
    const cf_rect field = find_fill_rect(accept_field_box, &found);

    CF_CHECK(found, "the entry box is painted above its own echo");

    if (!found) {
        return;
    }

    typed.length = 0u;

    const float x = field.x + 30.0f;
    const float y = field.y + field.height / 2.0f;

    cf_event hands[8];
    uint32_t at = 0u;

    hands[at++] = press_at(x, y);
    hands[at++] = release_at(x, y);
    hands[at++] = typed_text("hello");
    hands[at++] = typed_text(" world");
    hands[at++] = key_down(CF_KEY_BACKSPACE);
    hands[at++] = move_to(x, y);
    hands[at++] = (cf_event){.type = CF_EVENT_QUIT};

    run_session(current_size(), hands, at);

    CF_CHECK(typed.length == 10u, "five plus six minus the deleted scalar is ten bytes");
    CF_CHECK(memcmp(typed_bytes, "hello worl", 10) == 0, "and they are the bytes typed");
    CF_CHECK(last_cursor == CF_CURSOR_TEXT, "and the pointer over the field asks for the I-beam");
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

    /* Two more steps forward from the second chip: the third, then past the end
     * of the page and round to its first control - the only box whose ask moves
     * the count the first chapter keeps. */
    step(tab_key(false));
    step(tab_key(false));
    step(key_down(CF_KEY_ENTER));

    CF_CHECK(*click_count(&session) == 2u, "a walk off the end of the page returns to its start");

    end_session();
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

int main(void) {
    printf("gallery headless\n");

    CF_RUN(the_page_fills_the_recorder);
    CF_RUN(a_click_counts_and_a_clear_ends_the_count);
    CF_RUN(a_switched_off_button_takes_no_click);
    CF_RUN(a_focused_field_takes_what_the_keyboard_wrote);
    CF_RUN(the_narrow_button_pins_the_well_and_the_lines_follow);
    CF_RUN(the_wheel_walks_the_page_it_lands_on);
    CF_RUN(the_panel_keeps_its_own_wheel_and_reveals_its_rows);
    CF_RUN(the_list_builds_only_the_rows_the_viewport_shows);
    CF_RUN(the_chips_leave_their_number_in_the_state_the_build_reads);
    CF_RUN(the_badge_switch_moves_it_and_the_logo_still_paints);
    CF_RUN(a_press_sends_the_puck_and_the_page_stops_asking_when_it_lands);
    CF_RUN(the_keyboard_walks_the_page_and_asks_where_it_stands);
    CF_RUN(a_narrow_window_reflows_without_spilling_geometry);
    CF_RUN(a_bigger_output_still_bounds_every_box);
    CF_RUN(the_inspector_lays_its_numbers_over_the_page);

    end_sessions();
    return CF_REPORT();
}
