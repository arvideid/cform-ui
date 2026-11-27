#ifndef CF_EXAMPLES_HOST_LOOP_H
#define CF_EXAMPLES_HOST_LOOP_H

/*
 * The route for a program that already has a main loop and already has a way to draw.
 * Nothing here calls cf_app_run: the host keeps the context, fills the cf_backend
 * struct with its own submit, present, surface and the three service asks, and decides
 * for itself when a frame is worth running. That is the whole agreement the shell is
 * built on, written out where a reader can set it beside loop.c and see that the shell
 * is this plus a window, a clock and the inspector.
 *
 * Header-only for the reason the gallery page is: host_loop.c runs it as a program and
 * the headless tier (tests/unit/host_loop.c) asserts over it. A driver only the demo
 * could reach would prove nothing about the route a third-party renderer takes.
 */

#include <stdio.h>
#include <string.h>

#include "cf/backend.h"
#include "cf/build.h"
#include "cf/widget.h"

/* The capacity every frame is cut from: the arena the view, the rects and the command
 * list live in, handed back whole at the start of the next frame. The shell asks for a
 * mebibyte because a gallery has a thousand boxes in a frame; this page has four, and a
 * host sizes this against its own deepest page rather than copying the number it saw. */
#define HOST_FRAME_BYTES (32u * 1024u)

/* The output this host claims to have. A real one reads the window's extent on every
 * surface call, which is what makes a resize arrive without a callback. */
#define HOST_WIDTH 320.0f
#define HOST_HEIGHT 200.0f

/* One face, answered by the host: a codepoint is this wide and a line is this tall.
 * Measuring never rasterizes, so the same three questions a real font answers are all a
 * host has to answer to lay text out - and the pixels it draws after that are its own. */
#define HOST_ADVANCE 8.0f
#define HOST_LINE 16.0f

/* The line under the button, written by the handler and read by the next build. */
#define HOST_SAID_BYTES 32u

/* What the field holds. The buffer is the run's, not the frame's: a host that keeps its own page
 * state keeps the bytes its controls edit, which is the whole of what the widget needs from it. */
#define HOST_TYPED_BYTES 24u

/* The row the field stands in. A single-line field keeps one row's height whatever it holds, so the
 * build states the extent rather than asking the frame to measure a value that is not written yet.
 */
#define HOST_FIELD_HEIGHT 24.0f

static cf_text_metrics host_face_metrics(void *font) {
    (void)font;

    return (cf_text_metrics){.line_height = HOST_LINE};
}

static float host_face_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font, (void)codepoint, (void)previous;

    return HOST_ADVANCE;
}

static bool host_face_glyph(void *font, uint32_t codepoint) {
    (void)font;

    /* A codepoint of zero is the absence of one, and nothing is drawn for it. */
    return codepoint != 0u;
}

static const cf_text_ops host_face_ops = {
    host_face_metrics,
    host_face_advance,
    host_face_glyph,
};

/*
 * What the host keeps between frames, and what it saw of the ones it drew. The counters
 * are not decoration: a loop that owns its frames has to be able to say what a frame
 * cost and what it produced, which is the question the shell answers with a clock and an
 * inspector panel this route has no use for.
 */
typedef struct host_app {
    cf_ctx ctx;
    /* The struct the framework reads. The host fills it once, before the first frame. */
    cf_backend out;
    const cf_theme *theme;
    cf_font face;
    cf_text_stack fonts;
    cf_size surface;
    /* The extent the last frame was laid against, which is what a turn compares the output's
     * current size to in order to notice a resize. */
    cf_size offered;

    /* The bytes the status label borrows, owned by the run rather than by a frame. */
    char said[HOST_SAID_BYTES];
    uint32_t presses;

    /* The buffer the field edits in place, and the run a method is holding inside it. */
    char typed_bytes[HOST_TYPED_BYTES];
    cf_text_edit typed;

    uint32_t builds;
    uint32_t presents;
    uint32_t frames;
    /* What the last submit carried, counted as it arrived, and where the array stood. A host that
     * draws inside the call needs the commands for the length of the call and nothing after it - a
     * renderer that defers its drawing copies the array out of the frame's scratch. */
    const cf_command *stream;
    uint32_t commands;
    uint32_t texts;
    uint32_t fills;

    /* The last answer the host was asked to act on, and how often it moved. A loop that
     * calls a platform every frame for a shape that has not changed is the reason the
     * shell dedupes these three asks; this route has to do the same for itself. */
    cf_cursor cursor;
    uint32_t cursor_asks;
    bool taking_text;
    uint32_t text_asks;
    cf_rect candidate;
    uint32_t candidate_asks;
    uint32_t destroys;

    /* The loop's own state: whether the last turn drew, and whether anything woke it. */
    bool painted;
    bool woke;
} host_app;

/* The commands are borrowed for the call alone, so a host that draws later copies them.
 * This one draws as it is handed them: it counts what arrived and remembers where the array stood
 * for the turn it is reading it back in, which is all a frame has to become for a host with its own
 * renderer - what to draw, in the order the engine said it. */
static void host_submit(void *user_data, const cf_command *commands, uint32_t count) {
    host_app *const host = user_data;

    host->frames++;
    host->stream = commands;
    host->commands = count;
    host->texts = 0u;
    host->fills = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        host->texts += commands[i].kind == CF_COMMAND_TEXT ? 1u : 0u;
        host->fills += commands[i].kind == CF_COMMAND_FILL ? 1u : 0u;
    }
}

static void host_present(void *user_data) {
    ((host_app *)user_data)->presents++;
}

static cf_size host_surface(void *user_data) {
    return ((host_app *)user_data)->surface;
}

static void host_set_cursor(void *user_data, cf_cursor cursor) {
    host_app *const host = user_data;

    host->cursor = cursor;
    host->cursor_asks++;
}

static void host_set_text_input(void *user_data, bool enabled) {
    host_app *const host = user_data;

    host->taking_text = enabled;
    host->text_asks++;
}

static void host_set_candidate_rect(void *user_data, cf_rect rect) {
    host_app *const host = user_data;

    host->candidate = rect;
    host->candidate_asks++;
}

/* The symmetric half of whatever the host took when it opened its output. This one took
 * nothing, so the call is the shape of a teardown rather than a free. */
static void host_destroy(void *user_data) {
    ((host_app *)user_data)->destroys++;
}

/* The ask the button fires, handed the same pointer the build was given. A click and the
 * key on a focused button are the same ask, and the count it moves is the host's own. */
static inline void host_count(cf_ctx *ctx, cf_id node, void *user_data) {
    (void)ctx, (void)node;

    host_app *const host = user_data;

    if (host == NULL) {
        return;
    }

    host->presses++;
    snprintf(host->said, sizeof host->said, "count %u", (unsigned)host->presses);
}

/*
 * The other way a run ends. A control inside the page has no event to send, so it asks the
 * context; the shell would notice before its next turn, and this route reads the ask itself
 * after the frame that answered the press - so the click the reader made is the one they see.
 */
static inline void host_finish(cf_ctx *ctx, cf_id node, void *user_data) {
    (void)node, (void)user_data;

    cf_request_quit(ctx);
}

/*
 * The page: a heading, two controls, a box to type in and a line that reports what the first did.
 * The build is handed the host, so the values a frame reads are the ones the run keeps - no file
 * scope a second window would have to share, and nothing the engine keeps on the side.
 */
static inline cf_node host_build(cf_ctx *ctx, void *user_data) {
    host_app *const host = user_data;

    if (host == NULL) {
        return (cf_node){0};
    }

    const cf_node inside =
        cf_label(ctx, cf_string_of("count me"), &host->fonts, host->theme->on_ink);
    const cf_node ending =
        cf_label(ctx, cf_string_of("that is enough"), &host->fonts, host->theme->on_ink);
    const cf_node heading =
        cf_label(ctx, cf_string_of("a host that owns the loop"), &host->fonts, host->theme->ink);
    const cf_node said = cf_label(ctx, cf_string_of(host->said), &host->fonts, host->theme->muted);
    const cf_node button = cf_button(ctx, "count", host_count, host, &inside, 1u);
    const cf_node done = cf_button(ctx, "done", host_finish, NULL, &ending, 1u);

    /* The box that takes the keyboard: its value is the host's buffer, so what a person typed is
     * readable after the frame that edited it, and the ask the host is given about written text
     * follows whatever the frame says is focused. */
    cf_node entry = cf_field(ctx, "type", &host->typed, &host->fonts, CF_FIELD_SINGLE);
    entry.style.width = CF_PERCENT(100.0f);
    entry.style.height = CF_PX(HOST_FIELD_HEIGHT);

    cf_style page = host->theme->page;
    page.width = CF_PERCENT(100.0f);
    page.height = CF_PERCENT(100.0f);
    page.direction = CF_DIRECTION_COLUMN;
    page.row_gap = CF_PT(8.0f);

    const cf_node rows[] = {heading, button, done, entry, said};

    return cf_col(ctx, page, rows, (uint32_t)(sizeof rows / sizeof rows[0]));
}

/* A context, a theme and a backend of the host's own making. Answers false when the
 * context cannot start, which is the one thing about this route a host cannot work around. */
static inline bool host_app_open(host_app *host, cf_size surface) {
    if (host == NULL) {
        return false;
    }

    memset(host, 0, sizeof *host);
    host->surface = surface;
    /* The line before the first press, so the page says what it does not have rather than
     * painting a row that carries no bytes. */
    snprintf(host->said, sizeof host->said, "nothing pressed yet");
    /* The field edits through this struct for as long as the build declares it; the memory and its
     * lifetime stay the host's, which is why it is filled here and not in a frame. */
    host->typed = (cf_text_edit){.chars = host->typed_bytes, .capacity = sizeof host->typed_bytes};

    if (!cf_ctx_init(&host->ctx, HOST_FRAME_BYTES)) {
        return false;
    }

    host->theme = cf_theme_default();
    cf_use_theme(&host->ctx, host->theme);

    /* No clipboard is named. The field's cut, copy and paste chords reach for it and get nothing
     * back, which is the shape a route that serves no clipboard has to take: the keys are refused,
     * the caret and the buffer keep working, and nothing falls over on the way. */
    host->face = (cf_font){.handle = host, .ops = &host_face_ops};
    host->fonts = (cf_text_stack){.fonts = &host->face, .count = 1u};

    host->out = (cf_backend){
        .abi_version = CF_BACKEND_ABI_VERSION,
        .size = (uint32_t)sizeof(cf_backend),
        .user_data = host,
        .submit = host_submit,
        .present = host_present,
        .surface = host_surface,
        .destroy = host_destroy,
        .set_cursor = host_set_cursor,
        .set_text_input = host_set_text_input,
        .set_candidate_rect = host_set_candidate_rect,
    };

    /* The slots a route with no textures and no device queue leaves NULL are the ones the
     * framework tests before it calls: create_texture, load_image, release_texture,
     * poll_event. A host whose events come from its own queue hands them to cf_input
     * itself, which is what host_app_send below does. */
    return true;
}

/* An event the host read from wherever its platform keeps them. Anything that moves the
 * tree wakes the next frame, so the loop's idle test is not the only thing that decides. */
static inline void host_app_send(host_app *host, cf_event event) {
    if (host == NULL) {
        return;
    }

    host->woke = true;
    cf_input(&host->ctx, event);
}

/* An output that changed shape. No event arrives for a resize, which is why the surface is
 * read every turn rather than remembered: the frame that lays the page wider is the one
 * that notices it was asked to. */
static inline void host_app_resize(host_app *host, cf_size surface) {
    if (host != NULL) {
        host->surface = surface;
    }
}

/*
 * What the device says was typed. A platform hands the framework letters rather than keys, because
 * the shift, the layout and the input method that turned a key into one are its business; a host
 * that owns its events hands the answer they settled on to the same door the pointer uses. A run
 * with no bytes behind it is sent as none, rather than measured.
 */
static inline void host_app_type(host_app *host, const char *chars) {
    if (host == NULL || chars == NULL) {
        return;
    }

    host_app_send(host, (cf_event){.type = CF_EVENT_TEXT,
                                   .text = (cf_string){chars, (uint32_t)strlen(chars)}});
}

/*
 * A run an input method is holding and has not decided. The bytes are staged in the focused box
 * rather than taken into its value, and the box they stand in is what the frame hands back through
 * cf_frame_composition_rect - the third of the three device asks, and the one a host with a
 * candidate window of its own has to answer. An empty run is the method giving up on the run.
 */
static inline void host_app_compose(host_app *host, const char *chars) {
    if (host == NULL || chars == NULL) {
        return;
    }

    host_app_send(host, (cf_event){.type = CF_EVENT_COMPOSITION,
                                   .text = (cf_string){chars, (uint32_t)strlen(chars)}});
}

/*
 * Aiming a click at the box a key names. A host with a pointer gets these coordinates from
 * its device; a program with no pointer finds the box by the name its build declared and
 * asks where the last layout put it - the same two doors a deep link and the Tab key use,
 * from the outside. Nothing is sent for a box the frame laid no rect for.
 */
static inline void host_app_click(host_app *host, const char *key) {
    if (host == NULL) {
        return;
    }

    const uint32_t slot = cf_slot_named(&host->ctx, cf_ctx_key(&host->ctx, key));
    const cf_rect *rect = cf_frame_rect(&host->ctx, slot);

    if (rect == NULL) {
        return;
    }

    const cf_point at = {rect->x + rect->width / 2.0f, rect->y + rect->height / 2.0f};

    host_app_send(host, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = at});
    host_app_send(host, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});
    host_app_send(host, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at});
}

/*
 * One turn of the loop. A page at rest - nothing arrived, the output did not move, and the
 * last frame asked for no more - costs a present and no build, which is the whole of the
 * idle shortcut; everything else runs the pipeline and hands the stream over. The interval
 * is the host's to measure: a page that steps a tween moves by what passed between frames,
 * and nothing above a backend reads a clock of its own.
 */
static inline bool host_app_frame(host_app *host, float seconds) {
    if (host == NULL) {
        return false;
    }

    const cf_size now = host->surface;

    /* The shortcut, and the three things that defeat it: something arrived, the page asked for
     * the next frame because its own state is moving, or the output changed shape under it. */
    if (host->painted && !host->woke && !cf_frame_requested(&host->ctx) &&
        now.width == host->offered.width && now.height == host->offered.height) {
        host->out.present(host->out.user_data);

        return false;
    }

    host->woke = false;
    host->offered = now;
    host->builds++;

    cf_command_list list = {0};
    cf_frame_run(&host->ctx, host_build, host, now, seconds, &list);
    host->painted = true;

    /* The three asks, sent only when they moved. A device that is told the same shape every
     * frame is a device that gets worked for nothing. */
    const cf_cursor asked = cf_frame_cursor(&host->ctx);

    if (asked != host->cursor) {
        host->out.set_cursor(host->out.user_data, asked);
    }

    const bool takes_text = cf_frame_takes_text(&host->ctx);

    if (takes_text != host->taking_text) {
        host->out.set_text_input(host->out.user_data, takes_text);
    }

    const cf_rect box = cf_frame_composition_rect(&host->ctx);

    if (box.width != host->candidate.width || box.height != host->candidate.height ||
        box.x != host->candidate.x || box.y != host->candidate.y) {
        host->out.set_candidate_rect(host->out.user_data, box);
    }

    host->out.submit(host->out.user_data, list.commands, list.count);
    host->out.present(host->out.user_data);

    return true;
}

/*
 * Whether the page asked to be finished. The shell stops on this after the present, and a host
 * with its own loop checks it in the same place: the ask is the frame's, and quitting before the
 * stream went out would lose the press that asked.
 */
static inline bool host_app_should_quit(const host_app *host) {
    return host != NULL && cf_quit_requested(&host->ctx);
}

/*
 * The way this route ends. The context is the host's, so the host frees it; the backend's
 * destroy is called after the last frame it was handed, because a device that is gone
 * before the last submit leaves that submit drawing nowhere. A page that asked to quit
 * through cf_request_quit is the other way out, and reading cf_quit_requested is the
 * host's decision to make - this one runs a script and stops when the script is done.
 */
static inline void host_app_close(host_app *host) {
    if (host == NULL) {
        return;
    }

    cf_ctx_free(&host->ctx);
    host->out.destroy(host->out.user_data);
}

#endif
