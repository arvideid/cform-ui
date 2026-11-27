#include <math.h>
#include <stdio.h>
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
#include "document.h"

/*
 * The documentation site drawn through the real frame pipeline against the headless
 * recorder rather than a browser: every row of the navigation table, at three
 * surfaces, with the command stream judged as data on every submit.
 *
 * This is the only tier that can say the site works, because the site is a drawing.
 * A page that renders nothing on one route, a column that spills off a phone, a
 * heading that arrives as no text at all - none of those are visible to a build step
 * that only compiles, and a human opening the tab is not a check that runs on every
 * push. The invariants are the gallery tier's, because they are the ones that once
 * caught real defects: finite geometry, a clip inside the output, a radius inside its
 * own box, a sharp fill that arrived cut.
 *
 * What is added here is the site's own contract: that a row of the table renders a
 * page, that the page states its heading in the title face, that the prose fits the
 * window it was offered, that a route with no browser behind it still lands somewhere,
 * that every page is offered by a press somewhere on the first one - the list beside the
 * document, its bar, or its foot - that the list asks for the page a reader pressed it
 * for, that no two labels read in one glance say the same word, and that the document
 * moves under a wheel notch and under the Tab key - the two ways a reader has of asking
 * to see the rest of a page that does not fit.
 *
 * The frame is built by the same function the window runs, out of document.h, rather
 * than by a copy of it drawn here. A tier that measures a hand-made layout can pass
 * while the layout that ships is broken, and the two files have to be kept in step by
 * somebody remembering to do it.
 */

#define FRAME_BYTES (1024u * 1024u)
#define STORE_BYTES (256u * 1024u)
#define SEMANTIC_BYTES (128u * 1024u)

/* How many times a case may press Tab before it is told the keyboard is going nowhere.
 * A page holds a handful of controls, so this is several rounds of the ring: enough that
 * a walk which stops short is a page that moved, and not a walk that ran out. */
#define TAB_WALK 24u

/* Wide, medium, phone: the three windows the layout has to answer, with the narrow
 * one at 420 points because that is the minimum width the native window accepts. */
static const cf_size surfaces[] = {{1440.0f, 900.0f}, {960.0f, 640.0f}, {420.0f, 800.0f}};

/* The table's values, filled once with fonts that measure rather than rasterize: a
 * fixed rhythm and a fixed advance, so every number an assertion derives from text is
 * one the test states. Each face gets its own handle, which is what lets a command be
 * traced back to the face it was drawn in - a title is then provably a title rather
 * than the tallest thing on the page by luck. */
static cf_text_metrics fake_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float fake_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font, (void)codepoint, (void)previous;
    return 8.0f;
}

static bool fake_has_glyph(void *font, uint32_t codepoint) {
    (void)font, (void)codepoint;
    return true;
}

static const cf_text_ops fake_ops = {fake_metrics, fake_advance, fake_has_glyph};
static char face_marker[SITE_FACE_COUNT];
static site_view view;

static void bind_view(void) {
    cf_font fonts[SITE_FACE_COUNT];

    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        fonts[i] = (cf_font){.handle = &face_marker[i], .ops = &fake_ops};
    }

    site_view_bind(&view, fonts);

    /* No way out of the document: a page that offers a link still declares it, and
     * the headless build is exactly the case the seam was designed for - the address
     * is stated, the platform that would follow it is absent. */
    view.open_url = NULL;
}

/* The page under test, in the frame the site puts every page in: the masthead, the list
 * of pages beside the prose and the way on to the next one, all walked by a single
 * scroller. Driving a bare page would measure it against a window it never gets. */
static const site_entry *showing;

/* The address the reader is on, fragment and all. Left unset, a case is on the page's own route -
 * the bare address a press writes - and only the deep-link case names a section in it. */
static const char *address;

/* Where a press was aimed. This build has no address bar to write to, so the route the
 * row carries is recorded instead - which is the whole of what pressing a row is owed,
 * and the one thing about a press a tier with no browser can still read back. */
static char chosen[64];

static void go_to(cf_ctx *ctx, cf_id node, void *user_data) {
    (void)ctx;
    (void)node;

    const site_entry *entry = user_data;

    if (entry != NULL) {
        snprintf(chosen, sizeof chosen, "%s", entry->route);
    }
}

static cf_node build(cf_ctx *ctx, void *user_data) {
    (void)user_data;

    return site_document(ctx, &view, showing, showing->page(ctx, &view), go_to,
                         address != NULL ? address : showing->route);
}

/* ---------- the recorder, and what the last frame submitted ------------------ */

static cf_ctx session;
static bool session_open;
static cf_backend shell_backend;
static cf_arena store;
static cf_recorder recorder;
static cf_rect viewport;
static const cf_command *recorded;
static uint32_t recorded_count;

static bool finite_number(float value) {
    return isfinite(value) != 0;
}

static bool finite_rect(cf_rect rect) {
    return finite_number(rect.x) && finite_number(rect.y) && finite_number(rect.width) &&
           finite_number(rect.height);
}

static bool rect_inside(cf_rect inner, cf_rect outer) {
    return inner.x >= outer.x - 0.01f && inner.y >= outer.y - 0.01f &&
           inner.x + inner.width <= outer.x + outer.width + 0.01f &&
           inner.y + inner.height <= outer.y + outer.height + 0.01f;
}

/* Paint emits an empty clip as a sentinel meaning "no box to scissor to, the screen
 * itself", so those commands are judged against the viewport instead. */
static bool clip_is_none(cf_rect clip) {
    return clip.width == 0.0f && clip.height == 0.0f;
}

#define STREAM_FAIL(...)                                                                           \
    do {                                                                                           \
        printf("    FAIL stream at %.0fx%.0f: ", viewport.width, viewport.height);                 \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
        cf_failures++;                                                                             \
    } while (0)

static void check_stream(const cf_command *commands, uint32_t count, cf_rect output) {
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

        if (!clip_is_none(command.clip) && !rect_inside(command.clip, output)) {
            STREAM_FAIL("command %u kind %d scissors outside the output", i, (int)command.kind);
        }

        if ((command.clip.width == 0.0f) != (command.clip.height == 0.0f)) {
            STREAM_FAIL("command %u kind %d clips to a half-empty box", i, (int)command.kind);
        }

        const float shorter =
            command.rect.width < command.rect.height ? command.rect.width : command.rect.height;

        if (command.radius > shorter / 2.0f + 0.01f) {
            STREAM_FAIL("command %u kind %d rounds a %fx%f box by %f", i, (int)command.kind,
                        command.rect.width, command.rect.height, command.radius);
        }

        if (command.kind == CF_COMMAND_FILL && command.radius == 0.0f) {
            if (!clip_is_none(command.clip) && !rect_inside(command.rect, command.clip)) {
                STREAM_FAIL("fill %u arrives uncut against its clip", i);
            }

            if (!rect_inside(command.rect, output)) {
                STREAM_FAIL("fill %u sits outside the output", i);
            }
        }

        if (command.kind == CF_COMMAND_TEXT && command.text.length == 0u) {
            STREAM_FAIL("text command %u carries no bytes", i);
        }
    }
}

static void start(cf_size size) {
    if (session_open) {
        cf_ctx_free(&session);
        session_open = false;
        cf_arena_free(&store);
    }

    if (!cf_arena_init(&store, STORE_BYTES) || !cf_ctx_init(&session, FRAME_BYTES)) {
        CF_CHECK(false, "the session starts");
        return;
    }

    cf_use_theme(&session, cf_theme_default());
    recorder.arena = &store;
    recorder.commands = NULL;
    recorder.count = 0u;
    recorder.capacity = 0u;
    shell_backend = cf_backend_recorder(&recorder);

    viewport = (cf_rect){0.0f, 0.0f, size.width, size.height};

    cf_command_list list = {0};
    cf_frame_run(&session, build, NULL, size, 1.0f / 60.0f, &list);
    check_stream(list.commands, list.count, viewport);

    recorder.count = 0u;
    shell_backend.submit(shell_backend.user_data, list.commands, list.count);
    recorded = recorder.commands;
    recorded_count = recorder.count;
    session_open = true;
}

static void end(void) {
    if (session_open) {
        cf_ctx_free(&session);
        session_open = false;
    }

    cf_arena_free(&store);
}

/* One frame, submitted, with the harness reading the stream it produced. Every event a
 * case sends is followed by this, because a wheel and a key both write state that the
 * next build is the thing which turns into a moved page. */
static void frame(void) {
    cf_command_list list = {0};
    cf_frame_run(&session, build, NULL, (cf_size){viewport.width, viewport.height}, 1.0f / 60.0f,
                 &list);
    check_stream(list.commands, list.count, viewport);

    recorder.count = 0u;
    shell_backend.submit(shell_backend.user_data, list.commands, list.count);
    recorded = recorder.commands;
    recorded_count = recorder.count;
}

static void step(cf_event event) {
    cf_input(&session, event);
    frame();
}

/* The document's own offset, or NULL for a build that stopped carrying a scroller. */
static cf_scroll *document_scroll(void) {
    return cf_scroll_of(&session, cf_ctx_key(&session, SITE_PAGE_KEY));
}

/* ---------- reading the last frame back ------------------------------------- */

static bool command_reads(const cf_command *command, const char *needle) {
    const size_t length = strlen(needle);

    return command->kind == CF_COMMAND_TEXT && command->text.length >= length &&
           strncmp(command->text.chars, needle, length) == 0;
}

static bool shows(const char *needle) {
    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (command_reads(&recorded[i], needle)) {
            return true;
        }
    }

    return false;
}

/* The text a face drew, which is how a heading is told from a paragraph in a stream of
 * identical glyph metrics. */
static uint32_t lines_in_face(site_face face) {
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (recorded[i].kind == CF_COMMAND_TEXT && recorded[i].font.handle == &face_marker[face]) {
            seen++;
        }
    }

    return seen;
}

/* How many lines in one face read exactly these words: how a case tells a label that
 * was written once from one the drawing wrote twice. */
static uint32_t lines_reading(site_face face, const char *words) {
    const uint32_t length = (uint32_t)strlen(words);
    uint32_t seen = 0u;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        const cf_command command = recorded[i];

        if (command.kind == CF_COMMAND_TEXT && command.font.handle == &face_marker[face] &&
            command.text.length == length && strncmp(command.text.chars, words, length) == 0) {
            seen++;
        }
    }

    return seen;
}

/* The one axis a page cannot be excused on: width.
 *
 * A page lives in a scroller and is longer than any window, so a line below the fold is
 * the design working and not a defect - the vertical axis is already answered by the
 * clip every command in the stream is checked against. Reaching past the right edge is
 * the failure this tier exists to catch, because it is what a fixed column, an
 * unbreakable command or a panel that refuses to wrap looks like on a phone.
 */
static bool every_line_fits(cf_rect output) {
    bool spilled = false;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (recorded[i].kind != CF_COMMAND_TEXT) {
            continue;
        }

        if (recorded[i].rect.x < output.x - 0.01f ||
            recorded[i].rect.x + recorded[i].rect.width > output.x + output.width + 0.01f) {
            printf("    FAIL a line of prose reaches past the %.0f-point window: '%.*s' at "
                   "%.0f+%.0f\n",
                   output.width, (int)recorded[i].text.length, recorded[i].text.chars,
                   recorded[i].rect.x, recorded[i].rect.width);
            spilled = true;
        }
    }

    return !spilled;
}

/* Where a line was painted, which is how a case knows where to put its pointer. */
static cf_rect text_rect(const char *needle, bool *found) {
    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (command_reads(&recorded[i], needle)) {
            *found = true;
            return recorded[i].rect;
        }
    }

    *found = false;

    return (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
}

/* Whether a line is painted where a reader looking at the window can see it - inside the
 * viewport, rather than merely present in a stream that runs the length of the document. */
static bool text_in_view(const char *needle) {
    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (command_reads(&recorded[i], needle) && rect_inside(recorded[i].rect, viewport)) {
            return true;
        }
    }

    return false;
}

/* ---------- cases ----------------------------------------------------------- */

/* The table is the site's whole structure, so it is checked as data before anything is
 * drawn: a row that cannot be found by its own route is a page no address reaches. */
static void the_table_names_a_page_each_route_reaches(void) {
    CF_CHECK(SITE_NAV_COUNT > 0u, "the navigation table holds rows");

    for (uint32_t i = 0u; i < SITE_NAV_COUNT; ++i) {
        const site_entry *entry = site_entry_at(i);

        CF_CHECK(entry != NULL, "a row exists at every index");
        CF_CHECK(entry->page != NULL, "every row names a page to declare");
        CF_CHECK(entry->route != NULL && entry->route[0] == '/', "every route is an address");
        CF_CHECK(entry->label != NULL && entry->description != NULL,
                 "every row says what it is and what it holds");
        CF_CHECK(site_entry_for_route(entry->route) == entry, "its own address finds its row");
        CF_CHECK(site_index_of(entry) < SITE_NAV_COUNT, "the table can find the row it named");
    }

    CF_CHECK(site_entry_at(SITE_NAV_COUNT) == NULL, "the row past the end is nothing");
    CF_CHECK(site_entry_for_route("/nowhere") == NULL, "an unknown address names no row");
    /* The fallback the shell depends on: no browser, no route, still a page. */
    CF_CHECK(site_entry_or_first(NULL) == site_entry_at(0u), "no route lands on the first page");
    CF_CHECK(site_entry_or_first("/nowhere") == site_entry_at(0u),
             "a route the table does not hold still lands");
}

/* The other half of the same rule, from the platform side: a build with no address bar
 * reads no route, writes nothing and opens nothing, and the shell's fallback is what a
 * native window shows. This is the case that keeps `shell.c` free of a conditional. */
static void a_build_with_no_browser_answers_no_route(void) {
    char buffer[64] = "untouched";

    CF_CHECK(!cf_web_read_hash(buffer, sizeof buffer), "no browser reads no address");
    CF_CHECK(strcmp(buffer, "untouched") == 0, "a refused read leaves the caller's bytes alone");

    /* Neither call has anything to do. The assertion is that they return, and that a
     * route survives unwritten in the reader's memory rather than being half-filled. */
    cf_web_write_hash("/docs/api");
    cf_web_open_url("https://example.com/");
    CF_CHECK(true, "a write and an open with no browser are nothing to do");
}

/* The plan's claim about the site: every nav row resolves to a page, and the page says
 * something. Every heading block states its group as an eyebrow and its own title in
 * the title face, so those two are what "rendered" means as data. */
static void every_page_paints_at_every_surface(void) {
    for (uint32_t row = 0u; row < SITE_NAV_COUNT; ++row) {
        showing = site_entry_at(row);

        for (uint32_t s = 0u; s < SITE_COUNT(surfaces); ++s) {
            start(surfaces[s]);

            printf("    [%s at %.0fx%.0f: %u commands]\n", showing->route, viewport.width,
                   viewport.height, recorded_count);
            CF_CHECK(recorded_count > 0u, "the route paints a page");
            CF_CHECK(shows(showing->group), "the page states the group it is filed under");
            CF_CHECK(lines_in_face(SITE_FACE_TITLE) == 1u, "the page states one title");
            CF_CHECK(lines_in_face(SITE_FACE_EYEBROW) > 0u, "the page states an eyebrow");
            CF_CHECK(every_line_fits(viewport),
                     "no line of the page reaches past its window sideways");

            end();
        }
    }
}

/* A tab block keeps what the reader opened, in the state the context keys by the
 * block's name: the claim `blocks.h` makes about itself, proved by clicking. */
static void a_tab_the_reader_opened_survives_the_next_frame(void) {
    showing = site_entry_for_route(SITE_PREFIX "getting-started");
    CF_CHECK(showing != NULL, "the getting-started row exists");
    if (showing == NULL) {
        return;
    }

    start(surfaces[0]);

    CF_CHECK(shows("add_subdirectory(vendor/cform-ui)"), "the first panel shows its commands");
    CF_CHECK(!shows("find_package(cform-ui REQUIRED CONFIG)"),
             "the panel nobody opened paints nothing");

    bool tapped = false;

    for (uint32_t i = 0u; i < recorded_count; ++i) {
        if (!command_reads(&recorded[i], "Installed")) {
            continue;
        }

        const cf_rect tab = recorded[i].rect;
        const cf_point at = {tab.x + tab.width / 2.0f, tab.y + tab.height / 2.0f};

        /* A press is two events with a frame between them, which is what a click looks
         * like to a running shell. */
        step((cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at, .button = 1u});
        step((cf_event){.type = CF_EVENT_POINTER_UP, .position = at, .button = 1u});
        tapped = true;
        break;
    }

    CF_CHECK(tapped, "the second tab is on the page to press");
    CF_CHECK(shows("find_package(cform-ui REQUIRED CONFIG)"), "the panel the reader opened shows");
    CF_CHECK(!shows("add_subdirectory(vendor/cform-ui)"), "the panel they left stopped painting");

    /* A rebuild that is not a click: the choice has to survive it, which is the whole
     * reason it lives in keyed state rather than beside the node. */
    frame();

    CF_CHECK(shows("find_package(cform-ui REQUIRED CONFIG)"), "a frame after keeps the choice");

    end();
}

/*
 * A short list is only allowed to mean fewer words down the left, never a page nobody can
 * click to. The table is filed in three places - the list beside the prose, the bar, the
 * foot under the document - so this presses for every row of the table from the first page
 * and asks whether any of the lines that read the row's own words came back asking for its
 * route. A row dropped from every list fails here rather than quietly becoming an address
 * somebody has to type.
 */
static bool press_row(const site_entry *entry) {
    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        if (pass > 0u) {
            /* The foot is the last thing in the column. A notch is forty-eight points, so
             * this is a walk past the end of any page the table holds, and the offset stops
             * at the document's own limit instead of running off it. */
            step((cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = 200.0f}});
        }

        /* Every candidate is read out of one stream before any of them is pressed: a page's
         * own H1 reads "Getting started" as well as the row does, and a press rebuilds the
         * stream these indices came from. */
        cf_point spots[8];
        uint32_t found = 0u;

        for (uint32_t i = 0u; i < recorded_count && found < SITE_COUNT(spots); ++i) {
            const cf_command command = recorded[i];
            const size_t length = strlen(entry->label);

            /* Exactly the row's words, not a line that starts with them: the pager ends the
             * document with "previous: Concepts", which is a sentence about a page rather
             * than the row that is one. */
            if (command.kind != CF_COMMAND_TEXT || command.text.length != (uint32_t)length ||
                strncmp(command.text.chars, entry->label, length) != 0 ||
                !rect_inside(command.rect, viewport)) {
                continue;
            }

            spots[found++] = (cf_point){command.rect.x + command.rect.width / 2.0f,
                                        command.rect.y + command.rect.height / 2.0f};
        }

        for (uint32_t i = 0u; i < found; ++i) {
            chosen[0] = '\0';
            step((cf_event){.type = CF_EVENT_POINTER_DOWN, .position = spots[i], .button = 1u});
            step((cf_event){.type = CF_EVENT_POINTER_UP, .position = spots[i], .button = 1u});

            if (strcmp(chosen, entry->route) == 0) {
                return true;
            }
        }
    }

    return false;
}

static void every_page_is_offered_from_the_first_one(void) {
    showing = site_entry_at(0u);

    for (uint32_t row = 0u; row < SITE_NAV_COUNT; ++row) {
        const site_entry *entry = site_entry_at(row);
        CF_CHECK(entry != NULL, "the table has a row at every index");
        if (entry == NULL) {
            continue;
        }

        start(surfaces[0]);
        CF_CHECK(press_row(entry), "a press somewhere on the first page asks for this one");
        end();
    }
}

/*
 * The same rule seen from the other side: the list down the left is short on purpose, and
 * what makes it short is that it holds the documentation and nothing else. Judged where the
 * two columns are side by side, because that is the only window in which "down the left"
 * means something - on a phone the list is above the prose and every line starts near the
 * same edge.
 */
static void the_list_holds_only_the_pages_that_document_the_library(void) {
    showing = site_entry_at(0u);
    start(surfaces[0]);

    for (uint32_t row = 0u; row < SITE_NAV_COUNT; ++row) {
        const site_entry *entry = site_entry_at(row);
        const size_t length = strlen(entry->label);
        uint32_t listed = 0u;

        for (uint32_t i = 0u; i < recorded_count; ++i) {
            const cf_command command = recorded[i];

            if (command.kind == CF_COMMAND_TEXT && command.text.length == (uint32_t)length &&
                strncmp(command.text.chars, entry->label, length) == 0 &&
                command.rect.x <= SIDEBAR_WIDTH) {
                listed++;
            }
        }

        if (strcmp(entry->group, SITE_GROUP_DOCS) == 0) {
            CF_CHECK(listed == 1u, "a page that documents the library is in the list");
        } else {
            CF_CHECK(listed == 0u, "a page the list does not document is not in the list");
        }
    }

    end();
}

/*
 * The label over the list and the label over the page's heading are read in one glance,
 * and so is the label at the foot of the page under it. A word written twice where a
 * reader sees it twice is a drawing that repeated itself, and it is the kind of defect
 * only a picture shows - so the words themselves are counted here, in the face only
 * labels are drawn in.
 *
 * The two halves are counted in two frames, because what is below the fold is culled out
 * of the stream rather than painted and hidden: the top of the document answers the list
 * and the heading, the bottom answers the foot.
 *
 * The walk to the bottom runs until the wheel stops moving the page rather than for a fixed
 * distance, because pages grow as the library gains answers and a wheel given once would read a
 * foot that has since slid below the fold.
 */
static void no_label_is_written_twice_on_one_screen(void) {
    for (uint32_t row = 0u; row < SITE_NAV_COUNT; ++row) {
        showing = site_entry_at(row);
        start(surfaces[0]);

        CF_CHECK(lines_reading(SITE_FACE_EYEBROW, showing->group) == 1u,
                 "the page states its group once, above its own heading");
        CF_CHECK(lines_reading(SITE_FACE_EYEBROW, SITE_SIDEBAR_LABEL) == 1u,
                 "the list beside the prose is labelled once, and by a word of its own");
        CF_CHECK(lines_reading(SITE_FACE_EYEBROW, showing->label) == 0u,
                 "no label says the words the heading under it already says");

        /* A walk to the end of whatever the page grew to: the wheel is given until it stops
         * moving the document, which is the frame where the foot has come into view. The bound is
         * a guard against a page that never stops moving, not a distance anyone chose. */
        cf_scroll *page = document_scroll();
        float walked = page != NULL ? page->offset : 0.0f;

        for (uint32_t stride = 0u; stride < 200u; ++stride) {
            step((cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = 200.0f}});

            if (page == NULL || page->offset == walked) {
                break;
            }

            walked = page->offset;
        }

        CF_CHECK(lines_reading(SITE_FACE_EYEBROW, SITE_FOOT_LABEL) == 1u,
                 "the foot is labelled once, and by a word of its own");

        end();
    }
}

/*
 * The list beside the document is the site's contents, so a row of it has to ask for
 * the page it names. This is as much of a navigation as a build with no address bar can
 * check: the press reaches the driver's callback carrying the row it was aimed at, and
 * what the driver then writes - a hash, a window's route - is the shell's own business.
 */
static void a_row_in_the_list_names_the_page_it_holds(void) {
    const site_entry *target = site_entry_for_route(SITE_PREFIX "concepts");

    CF_CHECK(target != NULL, "the concepts row is in the table");

    if (target == NULL) {
        return;
    }

    showing = site_entry_at(0u);
    chosen[0] = '\0';
    start(surfaces[0]);

    bool tapped = false;

    for (uint32_t i = 0u; i < recorded_count && !tapped; ++i) {
        if (!command_reads(&recorded[i], target->label)) {
            continue;
        }

        /* The list owns the left of the document, so a line of prose that happened to
         * start with the same two words is not the row this case is aiming at. */
        const cf_rect row = recorded[i].rect;

        if (row.x > SIDEBAR_WIDTH) {
            continue;
        }

        const cf_point at = {row.x + row.width / 2.0f, row.y + row.height / 2.0f};
        step((cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at, .button = 1u});
        step((cf_event){.type = CF_EVENT_POINTER_UP, .position = at, .button = 1u});
        tapped = true;
    }

    CF_CHECK(tapped, "the list paints the row a reader can press");
    CF_CHECK(strcmp(chosen, target->route) == 0, "the press asks for the page the row names");

    end();
}

/*
 * The complaint the document's shape is the answer to: a wheel notch has to move the page,
 * and it has to move it wherever the hand happens to be. A notch that arrives with the
 * pointer at the window's origin is what a browser reports before anything has moved, and
 * a page built out of panes has to guess which of them the reader meant. One viewport has
 * nothing beside it to mean.
 */
static void a_wheel_walks_the_document_wherever_it_lands(void) {
    /* The longest page in the table, so a walk of two notches cannot run into its end and
     * answer zero for the wrong reason. */
    showing = site_entry_for_route(SITE_PREFIX "api");
    start(surfaces[0]);

    CF_CHECK(document_scroll() != NULL, "the document is in a scroller the state can find");

    /* A notch is forty-eight points, so two are worth ninety-six - asked for as two so
     * that a page which moved by a rounding does not pass for one that walked. */
    step((cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = 2.0f}});

    cf_scroll *held = document_scroll();
    CF_CHECK(held != NULL && held->offset >= 96.0f, "a notch the pointer never moved still walks");

    /* The same notch over prose, which is where a reader's hand actually rests. */
    bool found = false;
    const cf_rect lede = text_rect("Every exported function", &found);
    CF_CHECK(found, "the page's opening line is painted");

    if (found) {
        const cf_point at = {lede.x + lede.width / 2.0f, lede.y + lede.height / 2.0f};
        step((cf_event){.type = CF_EVENT_POINTER_MOVE, .position = at});
        step((cf_event){.type = CF_EVENT_SCROLL, .position = at, .wheel = {.x = 0.0f, .y = 2.0f}});

        held = document_scroll();
        CF_CHECK(held != NULL && held->offset >= 192.0f,
                 "and the notch walks the same document under the prose");
    }

    end();
}

/*
 * The keyboard's half of the same document: a Tab that lands on a control below the fold
 * has to bring the page with it, or the control is reachable and invisible - the worst kind
 * of accessible. The tree only moves a viewport it knows is one, so this case is also the
 * one that fails the day somebody strips the scroller's role to settle a colour.
 */
static void a_tab_past_the_fold_moves_the_page(void) {
    showing = site_entry_for_route(SITE_PREFIX "api");
    start(surfaces[0]);

    CF_CHECK(!text_in_view("The headers themselves"),
             "the page's last control starts below what the window shows");

    bool reached = false;

    for (uint32_t n = 0u; n < TAB_WALK && !reached; ++n) {
        step((cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
        reached = text_in_view("The headers themselves");
    }

    const cf_scroll *held = document_scroll();

    CF_CHECK(reached, "walking the ring brings the page to the control it reached");
    CF_CHECK(held != NULL && held->offset > 0.0f, "and the offset is the page that moved");

    end();
}

/*
 * The deep link, which is the one move a page makes for a reader who has touched nothing: a URL
 * copied out of the bar names a section by the slug its heading carries, and landing on the page
 * with the heading below the fold is the rule nobody questions in a browser and the rule this
 * site could only state in words while the tree had no way back from a name to a box.
 *
 * Three frames are the whole of it, and they are the framework's rather than the page's trick:
 * the first declares the headings without being able to find them, the second finds the box, asks
 * for it and is honoured by its own layout pass, and the third is the build that reads the moved
 * offset back and paints the heading where the reader asked for it.
 */
static void an_address_naming_a_section_walks_the_page_to_it(void) {
    /* The address decides both halves of where the reader is, so it is asked of the table whole:
     * a fragment that were read as part of the page would name no row at all, and the reader
     * would be sent to the first page in the table instead of the one they linked to. */
    showing = site_entry_for_route(SITE_PREFIX "api#backend");
    CF_CHECK(showing != NULL, "a section is a place in a page, not a page of its own");

    if (showing == NULL) {
        address = NULL;

        return;
    }

    address = SITE_PREFIX "api#backend";
    start(surfaces[0]);

    CF_CHECK(!text_in_view("Backends"), "the heading the address names starts below the fold");

    frame();

    CF_CHECK(cf_frame_requested(&session),
             "the frame that moved the page asked for the one that shows it");

    frame();

    const cf_scroll *held = document_scroll();

    CF_CHECK(held != NULL && held->offset > 0.0f,
             "the document walked to the section it was named");
    CF_CHECK(text_in_view("Backends"), "and the heading is where the window can read it");
    CF_CHECK(!cf_frame_requested(&session),
             "and it stops there: an address already walked asks for no more frames");

    /* The reader takes the wheel back. A page that re-walked its own address on every rebuild
     * would drag the heading under their hand again with the next frame, which is the difference
     * between a deep link and a page that will not let go of one. */
    const float walked_to = held != NULL ? held->offset : 0.0f;

    step((cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = -2.0f}});
    frame();

    held = document_scroll();

    CF_CHECK(held != NULL && held->offset < walked_to, "and the reader is allowed to walk away");

    end();
    address = NULL;
}

/*
 * The other half of the same address: a fragment the page holds no heading for is a section this
 * site does not have, and the page has to be left where it is. Chasing a name nothing answers
 * would be a build every frame forever - the idle rule is the whole point of the shell, and a
 * dead link is the one thing a reader can type into a bar.
 */
static void an_address_naming_no_section_leaves_the_page_alone(void) {
    showing = site_entry_for_route(SITE_PREFIX "api");
    address = SITE_PREFIX "api#nowhere";
    start(surfaces[0]);

    CF_CHECK(cf_frame_requested(&session), "a heading not yet declared is worth one more frame");

    frame();

    const cf_scroll *held = document_scroll();

    CF_CHECK(!cf_frame_requested(&session), "a heading the standing page does not have is not");
    CF_CHECK(held != NULL && held->offset == 0.0f, "and the document is left where it was");

    end();
    address = NULL;
}

/*
 * The address read as two halves: the page the table holds and the place inside it to stand at.
 * Only the bytes after the first `#` are the second, and a fragment that could not be a slug the
 * pages spell is no place at all - which is what keeps a mistyped link from being read as a
 * section nobody wrote.
 */
static void an_address_names_a_section_only_past_its_hash(void) {
    char slug[SITE_SLUG_BYTES];

    CF_CHECK(!site_section_of(NULL, slug, sizeof slug), "no address names no section");
    CF_CHECK(!site_section_of(SITE_PREFIX "api", slug, sizeof slug),
             "a bare route is a page rather than a place in one");
    CF_CHECK(!site_section_of(SITE_PREFIX "api#", slug, sizeof slug),
             "a hash with nothing after it names nothing");
    CF_CHECK(site_section_of(SITE_PREFIX "api#base", slug, sizeof slug) &&
                 strcmp(slug, "base") == 0,
             "the bytes after the hash are the section, in the words the page keyed it with");
    CF_CHECK(!site_section_of(
                 "#/too-long-to-be-any-slug-the-documentation-pages-spell-and-so-it-names-nothing",
                 slug, sizeof slug),
             "and a fragment no slug could be is no section either");
}

/* The accessible tree exists as data on every route, which is the mitigation the site
 * makes for being a drawing: whoever writes the ARIA host adapter has the page's own
 * headings to read out of the same build. */
static void the_semantic_tree_names_each_page(void) {
    char *out = cf_alloc_block(SEMANTIC_BYTES);

    if (out == NULL) {
        CF_CHECK(false, "a buffer for the tree");
        return;
    }

    for (uint32_t row = 0u; row < SITE_NAV_COUNT; ++row) {
        showing = site_entry_at(row);
        start(surfaces[0]);

        memset(out, 0, SEMANTIC_BYTES);
        const bool fits = cf_semantic_export(&session, out, SEMANTIC_BYTES);

        CF_CHECK(fits, "the page's tree fits the buffer");
        CF_CHECK(strstr(out, showing->group) != NULL, "the tree carries the heading block");
        /* The words the list beside the document reads and the words the page's own
         * H1 reads are the same words, so the table alone says what every page has to
         * be findable by: a page that renders a heading the tree does not name is a
         * heading nobody using a screen reader reaches. */
        CF_CHECK(strstr(out, showing->label) != NULL, "the tree names the page's title");
        CF_CHECK(strstr(out, "'") != NULL, "the tree quotes text rather than only boxes");
        /* The viewport is part of what a host adapter has to translate: a box a reader
         * walks is a region to expose, and only the role says so. */
        CF_CHECK(strstr(out, "scroll") != NULL, "the tree says the page scrolls");
    }

    /* That the tree says which boxes act is a claim about the site's own spelling: a
     * row of the list, a tab and a link all declare the list family - a hover well
     * rather than the button family's dark pill - so `list` is the word an actionable
     * row arrives as, and getting-started has tabs to prove it with. */
    showing = site_entry_for_route(SITE_PREFIX "getting-started");
    start(surfaces[0]);
    memset(out, 0, SEMANTIC_BYTES);
    CF_CHECK(cf_semantic_export(&session, out, SEMANTIC_BYTES) && strstr(out, "list") != NULL,
             "the tree says which boxes act");
    end();

    cf_free_block(out);
}

int main(void) {
    printf("site headless\n");
    bind_view();

    CF_RUN(the_table_names_a_page_each_route_reaches);
    CF_RUN(a_build_with_no_browser_answers_no_route);
    CF_RUN(every_page_paints_at_every_surface);
    CF_RUN(a_tab_the_reader_opened_survives_the_next_frame);
    CF_RUN(every_page_is_offered_from_the_first_one);
    CF_RUN(the_list_holds_only_the_pages_that_document_the_library);
    CF_RUN(no_label_is_written_twice_on_one_screen);
    CF_RUN(a_row_in_the_list_names_the_page_it_holds);
    CF_RUN(a_wheel_walks_the_document_wherever_it_lands);
    CF_RUN(a_tab_past_the_fold_moves_the_page);
    CF_RUN(an_address_naming_a_section_walks_the_page_to_it);
    CF_RUN(an_address_naming_no_section_leaves_the_page_alone);
    CF_RUN(an_address_names_a_section_only_past_its_hash);
    CF_RUN(the_semantic_tree_names_each_page);

    end();
    return CF_REPORT();
}
