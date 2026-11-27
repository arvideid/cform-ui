#include <string.h>

#include "cform_test.h"
#include "host_loop.h"

/*
 * The second adoption route exercised as data: a program that keeps its own main loop and its own
 * renderer takes frames from cf_frame_run and fills the cf_backend struct itself, so everything the
 * shell guarantees has to be true of this path on its own. The cases drive host_loop.h - the same
 * page and loop the example program runs - and read what the host counted in the stream its own
 * submit was handed. Nothing here needs a window, a font file or a display.
 */

/* The interval a turn reports. A script has no gap worth timing; the page under test never reads
 * it, because nothing on it steps a tween. */
#define STEP_SECONDS (1.0f / 60.0f)

/* One session, laid once. The route's interesting behaviour is what happens to a page that already
 * stands, so every case begins after the frame that built it. */
static void session_starts(host_app *host) {
    CF_CHECK(host_app_open(host, (cf_size){HOST_WIDTH, HOST_HEIGHT}), "the host starts a context");
    CF_CHECK(host_app_frame(host, STEP_SECONDS), "the first turn has no previous frame to skip");
}

/* The stream the host was handed, read the way a renderer that draws inside the call reads it. The
 * cases look at the commands behind the counters so a count that agreed with nothing real would be
 * caught, and so the claim a command renderer actually cares about is checked: that the bytes and
 * the face the build stated arrive in the array the engine hands over. */
static uint32_t stream_texts(const host_app *host) {
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < host->commands; ++i) {
        seen += host->stream[i].kind == CF_COMMAND_TEXT ? 1u : 0u;
    }

    return seen;
}

static uint32_t stream_fills(const host_app *host) {
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < host->commands; ++i) {
        seen += host->stream[i].kind == CF_COMMAND_FILL ? 1u : 0u;
    }

    return seen;
}

static bool stream_says(const host_app *host, const char *words) {
    const uint32_t length = (uint32_t)strlen(words);

    for (uint32_t i = 0u; i < host->commands; ++i) {
        const cf_command *const command = &host->stream[i];

        if (command->kind == CF_COMMAND_TEXT && command->text.length == length &&
            memcmp(command->text.chars, words, length) == 0) {
            return true;
        }
    }

    return false;
}

/* Every run carries the face its stack named. A host that swaps fonts per command cannot tell
 * fallback from preference without being told, so the run it is handed has to say. */
static bool every_run_in_the_hosts_face(const host_app *host) {
    for (uint32_t i = 0u; i < host->commands; ++i) {
        if (host->stream[i].kind == CF_COMMAND_TEXT &&
            host->stream[i].font.handle != host->face.handle) {
            return false;
        }
    }

    return true;
}

static void the_first_frame_lays_the_page_and_hands_the_stream_over(void) {
    host_app host = {0};

    session_starts(&host);

    CF_CHECK(host.builds == 1u, "and it built the page once");
    CF_CHECK(host.frames == 1u, "once it handed a stream over");
    CF_CHECK(host.presents == 1u, "once it showed what it was handed");
    /* Five boxes carrying a string - heading, two buttons, the field, the status line - and a fill
     * for the page, each control and the field. The host counts them as they arrive, so the numbers
     * are the stream's rather than the test's. */
    CF_CHECK(host.texts == 4u,
             "four of the five are painted as text: the field holds no bytes yet");
    CF_CHECK(host.fills == 4u, "and a fill for the page, both controls and the field");
    CF_CHECK(stream_texts(&host) == host.texts, "the text count is the stream's own");
    CF_CHECK(stream_fills(&host) == host.fills, "and so is the fill count");
    CF_CHECK(stream_says(&host, "a host that owns the loop"), "the heading reaches the renderer");
    CF_CHECK(every_run_in_the_hosts_face(&host), "and names the face the build handed it");
    CF_CHECK(strcmp(host.said, "nothing pressed yet") == 0, "the line before the first press");

    host_app_close(&host);
}

static void a_page_at_rest_builds_nothing_and_still_presents(void) {
    host_app host = {0};

    session_starts(&host);

    const uint32_t built = host.builds;
    const uint32_t submitted = host.frames;
    const uint32_t shown = host.presents;

    host_app_frame(&host, STEP_SECONDS);
    host_app_frame(&host, STEP_SECONDS);

    /* The idle rule belongs to the host on this route: nobody else knows whether its events moved
     * the tree, so nobody else can decide a turn is free. */
    CF_CHECK(host.builds == built, "two empty turns laid nothing out");
    CF_CHECK(host.frames == submitted, "and produced no stream");
    CF_CHECK(host.presents == shown + 2u, "but still showed the frame it had");

    host_app_close(&host);
}

static void the_key_on_a_focused_button_asks_the_same_ask_as_a_click(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_frame(&host, STEP_SECONDS);
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.presses == 1u, "Tab walked to the control and Enter pressed it");
    CF_CHECK(strcmp(host.said, "count 1") == 0, "the handler wrote the line the next build reads");
    CF_CHECK(stream_says(&host, "count 1"), "and the frame after it drew that line");

    host_app_close(&host);
}

static void a_page_reached_by_key_never_asks_for_a_pointer_shape(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.presses == 1u, "the keyboard route answers both events in the frame it runs");
    CF_CHECK(host.cursor_asks == 0u, "and asks the host for no cursor at all");
    CF_CHECK(host.cursor == CF_CURSOR_DEFAULT, "so the shape stands where the host left it");

    host_app_close(&host);
}

static void a_pointer_that_stands_still_is_asked_about_once(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "count");
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.cursor_asks == 1u, "the press over a control asks for its shape once");
    CF_CHECK(host.cursor == CF_CURSOR_POINTER, "and the shape is the one a control states");

    /* The turn the event woke is the only one it costs: a wake that outlived its own frame would
     * have the host laying the page out for as long as the window stayed open. */
    const uint32_t settled = host.builds;

    host_app_frame(&host, STEP_SECONDS);
    CF_CHECK(host.builds == settled, "the press woke one turn, and no more");
    CF_CHECK(host.cursor_asks == 1u, "an empty turn asks for nothing");

    const cf_point off = {2.0f, 2.0f};

    host_app_send(&host, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = off});
    host_app_frame(&host, STEP_SECONDS);
    CF_CHECK(host.cursor_asks == 2u, "leaving the control is a change worth one call");
    CF_CHECK(host.cursor == CF_CURSOR_DEFAULT, "back to the shape the page asks for by default");

    host_app_send(&host, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = off});
    host_app_frame(&host, STEP_SECONDS);
    CF_CHECK(host.cursor_asks == 2u, "and standing there asks for nothing more");

    host_app_close(&host);
}

static void a_keyboard_that_was_not_asked_for_stays_unasked(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "count");
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_resize(&host, (cf_size){HOST_WIDTH * 2.0f, HOST_HEIGHT});
    host_app_frame(&host, STEP_SECONDS);

    /* Nothing in this stretch focused the field, so the host is never told to take written text and
     * never handed a candidate box: both doors open for a box that is holding the keyboard. */
    CF_CHECK(host.text_asks == 0u, "no keyboard was ever asked for");
    CF_CHECK(!host.taking_text, "and none is being taken");
    CF_CHECK(host.candidate_asks == 0u, "and no candidate box was ever stated");

    host_app_close(&host);
}

static void the_box_a_press_focuses_asks_the_host_for_its_keyboard(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "type");
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.taking_text, "the field took the keyboard off the press");
    CF_CHECK(host.text_asks == 1u, "and the host was told once");
    CF_CHECK(host.cursor == CF_CURSOR_TEXT, "with the shape a text box states");

    /* The dedupe again, on a different door: a turn that changes nothing repeats nothing. */
    host_app_frame(&host, STEP_SECONDS);
    CF_CHECK(host.text_asks == 1u, "an empty turn asks for no keyboard again");

    /* Two more Tabs walk past both buttons and back to the field's own end of the ring, so the
     * keyboard leaves the box that called for it and the host is told it can stop listening. */
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.text_asks == 2u, "handing the keyboard over is one more call");
    CF_CHECK(!host.taking_text, "and the answer the host was given is no");

    host_app_close(&host);
}

static void what_the_device_typed_lands_in_the_buffer_the_host_keeps(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "type");
    host_app_type(&host, "hi");
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.typed.length == 2u, "the edit reports the two bytes it took");
    CF_CHECK(memcmp(host.typed_bytes, "hi", 2u) == 0, "in the buffer the run owns");
    CF_CHECK(host.typed.caret == 2u, "with the caret after them");
    CF_CHECK(host.typed.refusals == 0u, "and nothing it had to give back");

    /* A run longer than the buffer is refused whole rather than cut: the extent is the host's, and
     * a field that wrote half a sequence would leave it holding bytes nobody asked for. */
    host_app_type(&host, "a run the buffer has no room for whole");
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.typed.length == 2u, "the long run changed no byte");
    CF_CHECK(host.typed.refusals == 1u, "and is counted as a refusal the page can say out loud");
    CF_CHECK(stream_says(&host, "hi"), "and the two bytes it did take are in the stream");

    /* A platform that hands over a run with no bytes behind it is answered by a page that keeps
     * what it had: the helper measures nothing in that case, so nothing is written and nothing is
     * refused - the refusal above is the buffer's answer, and this is not one. */
    host_app_type(&host, NULL);
    host_app_compose(&host, NULL);
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.typed.length == 2u, "a run with no bytes wrote nothing");
    CF_CHECK(host.typed.refusals == 1u, "and was not counted as a refusal either");

    /* The caret is a box the field draws only while it holds the keyboard, so handing the keyboard
     * over leaves a frame whose two command kinds are different numbers - the place a host that
     * mixed the two up in its own counting would show. */
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(stream_texts(&host) == host.texts,
             "the text count is the array's own after the field lets go");
    CF_CHECK(stream_fills(&host) == host.fills, "and so is the fill count on that frame");
    CF_CHECK(host.texts != host.fills,
             "which is a frame where the two kinds are different numbers");

    host_app_close(&host);
}

static void a_run_a_method_holds_moves_the_candidate_box(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "type");
    host_app_frame(&host, STEP_SECONDS);

    host_app_compose(&host, "ni");
    host_app_frame(&host, STEP_SECONDS);

    /* The staged run is not the value: the bytes a method has not settled on stay out of the buffer
     * the host owns, and only the box they stand in is handed over. */
    CF_CHECK(host.typed.length == 0u, "the run is not in the value");
    CF_CHECK(stream_says(&host, "ni"), "but it is drawn, which is all the host can show of it");
    CF_CHECK(host.candidate_asks == 1u, "the host was told where it stands, once");
    CF_CHECK(host.candidate.width > 0.0f && host.candidate.height > 0.0f,
             "and the box it was given has room in it");

    /* The method gives the run up, and the box goes empty in the same frame that hears it - a
     * candidate window left standing over a field with nothing under it is the failure this door
     * exists to prevent. */
    host_app_compose(&host, "");
    host_app_type(&host, "ni");
    host_app_frame(&host, STEP_SECONDS);

    CF_CHECK(host.candidate_asks == 2u, "giving it up is the second and last call");
    CF_CHECK(host.candidate.width == 0.0f && host.candidate.height == 0.0f,
             "and the box the host was left holding is empty");
    CF_CHECK(host.typed.length == 2u, "while the committed bytes did reach the buffer");

    host_app_close(&host);
}

static void an_output_that_changed_shape_rebuilds_without_an_event(void) {
    host_app host = {0};

    session_starts(&host);

    const uint32_t built = host.builds;

    host_app_resize(&host, (cf_size){HOST_WIDTH * 2.0f, HOST_HEIGHT});
    CF_CHECK(host_app_frame(&host, STEP_SECONDS), "a resize the host read builds on its own");

    host_app_resize(&host, (cf_size){HOST_WIDTH * 2.0f, HOST_HEIGHT});
    CF_CHECK(!host_app_frame(&host, STEP_SECONDS), "the same size read twice is not a resize");
    CF_CHECK(host.builds == built + 1u, "so only the turn that saw the change laid the page out");

    host_app_close(&host);
}

static void the_frame_the_host_laid_reflects_the_offer_it_was_handed(void) {
    host_app host = {0};

    session_starts(&host);

    const uint32_t slot = cf_slot_named(&host.ctx, cf_ctx_key(&host.ctx, "count"));
    const cf_rect *before = cf_frame_rect(&host.ctx, slot);

    CF_CHECK(before != NULL, "the box the build keyed is one the frame laid a rect for");
    CF_CHECK(before->width > 0.0f && before->height > 0.0f, "and the rect has room in it");

    const float width = before->width;
    /* The rect lives in the arena the next frame re-cuts, so what the case compares against is read
     * here, before the turn that overwrites it. */
    const float height = before->height;

    host_app_resize(&host, (cf_size){HOST_WIDTH * 2.0f, HOST_HEIGHT});
    host_app_frame(&host, STEP_SECONDS);

    const cf_rect *after = cf_frame_rect(&host.ctx, slot);

    /* The control stretches across the column, so the wider offer has to reach the rect the host
     * reads back - the numbers it draws from are the ones this frame computed. */
    CF_NEAR(after->width, width * 2.0);
    CF_NEAR(after->height, height);

    host_app_close(&host);
}

static void nothing_reaches_a_box_the_page_never_keyed(void) {
    host_app host = {0};

    session_starts(&host);

    host_app_click(&host, "a name the build never declared");

    CF_CHECK(host.presses == 0u, "a key with no box behind it reaches no handler");
    CF_CHECK(!host_app_frame(&host, STEP_SECONDS), "and the turn that answered nothing stays idle");

    host_app_close(&host);
}

static void the_press_shows_before_the_ask_to_leave_is_read(void) {
    host_app host = {0};

    session_starts(&host);

    const uint32_t shown = host.presents;

    host_app_click(&host, "done");

    const bool asked_at_the_press = host_app_should_quit(&host);

    host_app_frame(&host, STEP_SECONDS);

    /* The ask arrives with the dispatch and stands until the run ends; what the host controls is
     * where it reads it. Reading it after the frame that carried the press is what lets the reader
     * see the press answered before the window goes. */
    CF_CHECK(asked_at_the_press, "the control asked as its handler ran");
    CF_CHECK(host_app_should_quit(&host), "and the ask is sticky across the frame");
    CF_CHECK(host.presents == shown + 1u, "the frame went out before the loop was asked to stop");
    CF_CHECK(host.frames == host.builds, "every frame the host built it also submitted");

    host_app_close(&host);
}

static void teardown_is_symmetric_and_a_second_session_starts_clean(void) {
    host_app host = {0};

    session_starts(&host);
    host_app_click(&host, "count");
    host_app_frame(&host, STEP_SECONDS);
    host_app_close(&host);

    CF_CHECK(host.destroys == 1u, "the output the host built is closed once");
    CF_CHECK(host.presses == 1u, "after the work it was asked to do");

    /* The same block again: a host that opens twice has to start from an empty tree, not from the
     * counters and the state of the session that ended. */
    session_starts(&host);

    CF_CHECK(host.builds == 1u, "the second session builds once");
    CF_CHECK(host.presses == 0u, "remembers no press");
    CF_CHECK(strcmp(host.said, "nothing pressed yet") == 0, "and states the line it starts with");
    CF_CHECK(host.texts == 4u, "and paints the same page");

    host_app_close(&host);
    /* One, not two: the counters the run kept are the session's, and an open wipes the block rather
     * than resuming the state a previous window left in it. */
    CF_CHECK(host.destroys == 1u, "and closes only the output its own session built");
}

static void a_host_handed_nothing_answers_for_the_missing_page(void) {
    /* A route a caller may hold a handle to before it opened. Nothing here may dereference it: an
     * early return is the whole of what a host that is not there can be given. */
    CF_CHECK(!host_app_open(NULL, (cf_size){HOST_WIDTH, HOST_HEIGHT}),
             "an open has nowhere to write");
    CF_CHECK(!host_app_frame(NULL, STEP_SECONDS), "a turn builds nothing");
    CF_CHECK(!host_app_should_quit(NULL), "and has not been asked to stop");

    host_app_send(NULL, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});
    host_app_click(NULL, "count");
    host_app_type(NULL, "hi");
    host_app_compose(NULL, "ni");
    host_app_resize(NULL, (cf_size){HOST_WIDTH, HOST_HEIGHT});
    host_app_close(NULL);

    CF_CHECK(true, "the six calls that answer with nothing survive a missing host");
}

int main(void) {
    printf("host loop\n");

    CF_RUN(the_first_frame_lays_the_page_and_hands_the_stream_over);
    CF_RUN(a_page_at_rest_builds_nothing_and_still_presents);
    CF_RUN(the_key_on_a_focused_button_asks_the_same_ask_as_a_click);
    CF_RUN(a_page_reached_by_key_never_asks_for_a_pointer_shape);
    CF_RUN(a_pointer_that_stands_still_is_asked_about_once);
    CF_RUN(a_keyboard_that_was_not_asked_for_stays_unasked);
    CF_RUN(the_box_a_press_focuses_asks_the_host_for_its_keyboard);
    CF_RUN(what_the_device_typed_lands_in_the_buffer_the_host_keeps);
    CF_RUN(a_run_a_method_holds_moves_the_candidate_box);
    CF_RUN(an_output_that_changed_shape_rebuilds_without_an_event);
    CF_RUN(the_frame_the_host_laid_reflects_the_offer_it_was_handed);
    CF_RUN(nothing_reaches_a_box_the_page_never_keyed);
    CF_RUN(the_press_shows_before_the_ask_to_leave_is_read);
    CF_RUN(teardown_is_symmetric_and_a_second_session_starts_clean);
    CF_RUN(a_host_handed_nothing_answers_for_the_missing_page);

    return CF_REPORT();
}
