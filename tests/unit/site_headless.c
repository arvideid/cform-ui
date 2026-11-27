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
#include "nav.h"

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
 * window it was offered, and that a route with no browser behind it still lands
 * somewhere.
 */

#define FRAME_BYTES (1024u * 1024u)
#define STORE_BYTES (256u * 1024u)
#define SEMANTIC_BYTES (128u * 1024u)

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

/* The page under test, in the column the shell puts a page in: capped, centred,
 * clipped. Driving a bare page would measure it against a window it never gets. */
static const site_entry *showing;

static cf_node build(cf_ctx *ctx) {
    const cf_node page = showing->page(ctx, &view);

    cf_node column = page;
    column.style.max_width = CF_PT(CONTENT_WIDTH);
    column.style.padding = site_pad(CONTENT_PADDING);
    column.style.margin = (cf_box){CF_AUTO, CF_PT(0.0f), CF_AUTO, CF_PT(0.0f)};

    cf_node content = cf_scroller(ctx, "content", &column, 1u);

    /* The same reset the shell makes, because this harness exists to measure what
     * ships: a viewport is restated to the page tone and carries no visual role. */
    content.role = CF_ROLE_NONE;
    content.style = view.surface;
    content.style.width = CF_PERCENT(100.0f);
    content.style.height = CF_PERCENT(100.0f);
    content.style.direction = CF_DIRECTION_COLUMN;
    content.style.align_items = CF_ALIGN_CENTER;
    content.style.clips_content = true;

    return content;
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

    session.theme = cf_theme_default();
    recorder.arena = &store;
    recorder.commands = NULL;
    recorder.count = 0u;
    recorder.capacity = 0u;
    shell_backend = cf_backend_recorder(&recorder);

    viewport = (cf_rect){0.0f, 0.0f, size.width, size.height};

    cf_command_list list = {0};
    cf_frame_run(&session, build, size, 1.0f / 60.0f, &list);
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
        CF_CHECK(site_group_first(entry->group) < SITE_NAV_COUNT, "its group files it somewhere");
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

        cf_input(&session, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at, .button = 1u});
        cf_command_list down = {0};
        cf_frame_run(&session, build, (cf_size){viewport.width, viewport.height}, 1.0f / 60.0f,
                     &down);
        check_stream(down.commands, down.count, viewport);

        cf_input(&session, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at, .button = 1u});
        cf_command_list up = {0};
        cf_frame_run(&session, build, (cf_size){viewport.width, viewport.height}, 1.0f / 60.0f,
                     &up);
        check_stream(up.commands, up.count, viewport);

        recorder.count = 0u;
        shell_backend.submit(shell_backend.user_data, up.commands, up.count);
        recorded = recorder.commands;
        recorded_count = recorder.count;
        tapped = true;
        break;
    }

    CF_CHECK(tapped, "the second tab is on the page to press");
    CF_CHECK(shows("find_package(cform-ui REQUIRED CONFIG)"), "the panel the reader opened shows");
    CF_CHECK(!shows("add_subdirectory(vendor/cform-ui)"), "the panel they left stopped painting");

    /* A rebuild that is not a click: the choice has to survive it, which is the whole
     * reason it lives in keyed state rather than beside the node. */
    cf_command_list again = {0};
    cf_frame_run(&session, build, (cf_size){viewport.width, viewport.height}, 1.0f / 60.0f, &again);
    check_stream(again.commands, again.count, viewport);
    recorder.count = 0u;
    shell_backend.submit(shell_backend.user_data, again.commands, again.count);
    recorded = recorder.commands;
    recorded_count = recorder.count;

    CF_CHECK(shows("find_package(cform-ui REQUIRED CONFIG)"), "a frame after keeps the choice");

    end();
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
        /* The words the sidebar row reads and the words the page's own H1 reads are
         * the same words, so the table alone says what every page has to be
         * findable by: a page that renders a heading the tree does not name is a
         * heading nobody using a screen reader reaches. */
        CF_CHECK(strstr(out, showing->label) != NULL, "the tree names the page's title");
        CF_CHECK(strstr(out, "'") != NULL, "the tree quotes text rather than only boxes");
    }

    /* That the tree says which boxes act is a claim about the site's own spelling: a
     * nav row, a tab and a link all declare the list family - a hover well rather
     * than the button family's dark pill - so `list` is the word an actionable row
     * arrives as, and getting-started has tabs to prove it with. */
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
    CF_RUN(the_semantic_tree_names_each_page);

    end();
    return CF_REPORT();
}
