/*
 * The documentation site: one cform program whose pages are the library's own.
 *
 * What the reader sees is one document - the site's name, the pages to choose from,
 * the prose the address names, and the way on to the next one - in a column that
 * stops growing at a readable measure and walks as a whole. That shape is the
 * gallery's, for the gallery's reason: a document is a column a reader moves down,
 * not a frame of panels a reader steers between, and one viewport costs nothing to
 * keep in step where two panes have to agree about every edge.
 *
 * Nothing here is a template language, a static generator or a copy of someone
 * else's stylesheet: the pages are declaration functions in C, the values they paint
 * with are one table in tokens.h, and the navigation is one array in nav.h. The
 * route lives in the address bar because a page a reader cannot link to is not a
 * page.
 *
 * This file is the driver only. It owns the fonts, the window, the current route and
 * the page's neighbours, and it hands a page a context and the table - the whole of
 * what a page is given, so a page cannot reach the window, the route or the platform
 * behind it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cf/app.h"
#include "cf/version.h"

#include "nav.h"

/* The longest address this shell can hold. A route the table does not name falls
 * back to its first page, so an over-long hash costs the reader a page they did not
 * ask for rather than a crash. */
#define ROUTE_BYTES 96

/* The addresses the masthead points at are in pages.h, beside the pages that point at
 * the same places: leaving this document is the driver's business, but the address is
 * not the driver's to invent twice. */

/* The longest a pager's words can be: the two ends of the site name a page beside
 * them, and a row of the table is what the table has already agreed to hold. */
#define SITE_STEP_BYTES 96u

/* The two faces, as the build named them. On a browser tab both are embedded in the
 * module and read from a virtual path; on a desktop they are the files the fetch
 * left in the build tree, and where the fetch got nothing they open as nothing. */
#ifndef CFORM_SITE_FONT
#define CFORM_SITE_FONT "/Lato-Regular.ttf"
#endif
#ifndef CFORM_SITE_FONT_MONO
#define CFORM_SITE_FONT_MONO "/IBMPlexMono-Regular.ttf"
#endif

static site_view view;
static char route[ROUTE_BYTES];

/* The row the address bar names, which is the whole of the site's state: a route
 * with nothing behind it is the first page, and a page is a row of the table. */
static const site_entry *current(void) {
    return site_entry_or_first(route);
}

/*
 * Going to a page.
 *
 * The route is the site's only state and the address bar is where a reader keeps it:
 * the write follows an event the shell already saw, so the frame that paints the new
 * page is the one that event wakes and nothing has to ask for a second one.
 */
static void follow(const site_entry *entry) {
    snprintf(route, sizeof route, "%s", entry->route);
    cf_web_write_hash(route);
}

static void go_to(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)ctx;
    (void)slot;

    if (user_data != NULL) {
        follow(user_data);
    }
}

/* The mark: a rounded square in the accent tone with two columns of the tone a
 * button's label carries inside it. Drawn, not loaded - the repository carries no
 * binary it can regenerate, and an image would mean a decoder, which is a
 * backend-ABI conversation rather than a logo. */
static cf_node make_mark(cf_ctx *ctx) {
    cf_style column = cf_style_default();
    column.width = CF_PT(MARK_STROKE);
    column.height = CF_PERCENT(56.0f);
    column.background = (cf_opt_color){.present = true, .value = view.on_accent};

    cf_style pair = cf_style_default();
    pair.column_gap = CF_PT(MARK_STROKE);
    pair.align_items = CF_ALIGN_CENTER;
    pair.justify_content = CF_JUSTIFY_CENTER;

    const cf_node columns[] = {{.style = column}, {.style = column}};

    cf_style square = view.accent;
    square.width = CF_PT(MARK_SIZE);
    square.height = CF_PT(MARK_SIZE);
    square.align_items = CF_ALIGN_CENTER;
    square.justify_content = CF_JUSTIFY_CENTER;

    return cf_col(ctx, square, (cf_node[]){cf_row(ctx, pair, columns, 2u)}, 1u);
}

/*
 * The top of the document: what the site is, what the build was made from, and where
 * the source lives.
 *
 * The version is a link rather than a label, because the number a reader cannot look
 * up is a number they cannot act on, and the number is the header's own - a masthead
 * that lies about it is a build nobody shipped. Everything else a page can be reached
 * by is in the row under this one, so the two ways out of the document are the only
 * things standing beside the name.
 */
static cf_node make_masthead(cf_ctx *ctx) {
    const cf_node wordmark = site_text(ctx, &view, SITE_FACE_HEAD, view.prose, "cform");

    cf_style pair = cf_style_default();
    pair.column_gap = CF_PT(CHIP_GAP);
    pair.align_items = CF_ALIGN_CENTER;

    cf_style line = cf_style_default();
    line.width = CF_PERCENT(100.0f);
    line.align_items = CF_ALIGN_CENTER;
    line.justify_content = CF_JUSTIFY_SPACE_BETWEEN;

    const cf_node leading[] = {
        make_mark(ctx),
        wordmark,
        site_link(ctx, &view, "version", "v" CF_VERSION_STRING, SITE_RELEASES),
    };

    return cf_row(
        ctx, line,
        (cf_node[]){cf_row(ctx, pair, leading, SITE_COUNT(leading)),
                    cf_row(ctx, pair,
                           (cf_node[]){site_link(ctx, &view, "github", "GitHub", SITE_REPO)}, 1u)},
        2u);
}

/*
 * Every page the site holds, in one row of chips.
 *
 * The table is short enough that grouping it would be furniture: the group a page
 * belongs to is already the eyebrow over its title, and a reader choosing between
 * seven words needs the words. The row wraps, so the same chips answer a wide tab and
 * a phone without a second navigation to keep in step - and every chip is a button the
 * keyboard walks in the order the table reads, which is what makes the row below the
 * masthead the page's contents rather than a picture of them.
 */
static cf_node make_nav(cf_ctx *ctx) {
    cf_node chips[SITE_NAV_COUNT];

    for (uint32_t i = 0u; i < SITE_NAV_COUNT; ++i) {
        const site_entry *entry = site_entry_at(i);

        chips[i] = site_row_button(ctx, &view, entry->route, entry->label, entry == current(),
                                   false, go_to, (void *)entry);
    }

    cf_style sheet = cf_style_default();
    sheet.column_gap = CF_PT(CHIP_GAP);
    sheet.row_gap = CF_PT(CHIP_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;
    sheet.wrap = CF_WRAP;

    return cf_row(ctx, sheet, chips, SITE_NAV_COUNT);
}

/*
 * One end of the pager, in words.
 *
 * "Previous: Concepts" is what the box says and what a reader who never sees the
 * drawing hears. An arrow would be shorter, and it would be a glyph neither of the two
 * faces here is obliged to carry and a label no semantic tree can name.
 */
static cf_node make_step(cf_ctx *ctx, const char *word, const site_entry *entry) {
    char *label = cf_alloc(&ctx->frame, SITE_STEP_BYTES);

    if (label == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    snprintf(label, SITE_STEP_BYTES, "%s: %s", word, entry->label);

    return site_row_button(ctx, &view, word, label, false, false, go_to, (void *)entry);
}

/* The two pages beside this one, at the two ends of the line the document ends on. */
static cf_node make_pager(cf_ctx *ctx, const site_entry *entry) {
    const uint32_t at = site_index_of(entry);
    cf_node steps[2];
    uint32_t count = 0u;

    if (at > 0u) {
        steps[count++] = make_step(ctx, "previous", site_entry_at(at - 1u));
    }

    if (at + 1u < SITE_NAV_COUNT) {
        steps[count++] = make_step(ctx, "next", site_entry_at(at + 1u));
    }

    cf_style sheet = cf_style_default();
    sheet.column_gap = CF_PT(CHIP_GAP);
    sheet.row_gap = CF_PT(CHIP_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_CENTER;
    sheet.wrap = CF_WRAP;
    /* Both ends of the document keep their own end of the line; with one there is
     * nothing to space between, and pushing it to a corner would be the layout
     * inventing a distance no block asked for. */
    sheet.justify_content = count == 2u ? CF_JUSTIFY_SPACE_BETWEEN : CF_JUSTIFY_START;

    return cf_row(ctx, sheet, steps, count);
}

/*
 * The native route.
 *
 * A window has no address bar to type into, so the keys walk the table instead - the
 * same entries and the same write a press makes, one line that decides where a page's
 * route came from. The handler rides the root and runs when the keyboard's chain
 * climbs to it, so a chip the Tab key reached is a chip the arrows can walk off.
 */
static void route_key(cf_event *event, void *user_data) {
    (void)user_data;

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    const uint32_t at = site_index_of(current());

    if (event->key == CF_KEY_RIGHT && at + 1u < SITE_NAV_COUNT) {
        follow(site_entry_at(at + 1u));
    } else if (event->key == CF_KEY_LEFT && at > 0u) {
        follow(site_entry_at(at - 1u));
    }

    /* The address bar follows the page here as much as it does after a press: a reader on
     * a tab who walks the pages with the keys and then copies the address is owed the page
     * they are looking at, and on a desktop the write is nothing. */
}

/*
 * Where the address bar says the reader is.
 *
 * A press writes a route and this reads one back, so the bar is the record rather than
 * the receipt: typing an address, pasting a link, and the back and forward buttons all
 * arrive here instead of needing a listener the frame driver knows nothing about. The
 * test is against what this shell already holds, which is what stops a write and a read
 * of the same route from turning one navigation into two rebuilds.
 *
 * A build with no browser reads nothing and keeps what it has, so a native window stays
 * on the keyboard route and this line costs it a call that answers false.
 */
static void follow_address_bar(void) {
    char named[ROUTE_BYTES];

    if (!cf_web_read_hash(named, sizeof named) || strcmp(named, route) == 0) {
        return;
    }

    snprintf(route, sizeof route, "%s", named);
}

/*
 * The whole frame: one document, in one viewport that walks it.
 *
 * The column is capped rather than filled, because a line of prose a reader cannot
 * track back along is not what a wide tab asked for, and it is centred by the viewport
 * rather than by margins of its own.
 *
 * The scroller keeps the role the widget asked for. The role is what lets a Tab the
 * reader has not finished scroll the page to whatever it moved focus onto - a control
 * below the fold is reachable but invisible without it - and the theme's scroll family
 * states a resting box and no marks for the pointer's states, so restating the style
 * whole takes the page's tone and leaves nothing to paint over it.
 *
 * One viewport, wherever the pointer rests: a wheel notch that arrives with no move
 * before it is delivered against the position it carries, and a page made of panes has
 * to guess which of them the reader meant. The document has nothing beside it to mean.
 */
static cf_node build(cf_ctx *ctx) {
    /* Read before the page is chosen: a route the bar named has to be the one this
     * frame describes, or the reader sees the page they left for one more frame. */
    follow_address_bar();

    const site_entry *entry = current();
    const cf_node page = entry->page(ctx, &view);

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(DOC_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.max_width = CF_PT(DOC_WIDTH);
    sheet.padding = site_pad(DOC_PADDING);

    const cf_node document =
        cf_col(ctx, sheet,
               (cf_node[]){make_masthead(ctx), make_nav(ctx), page, make_pager(ctx, entry)}, 4u);

    /* Restated whole, the way a scroller's viewport is always restated: the widget
     * asked for the clip and the direction, and a style handed back over it keeps the
     * wheel handler it installed on the node. */
    cf_node root = cf_scroller(ctx, "page", &document, 1u);
    root.style = view.surface;
    root.style.width = CF_PERCENT(100.0f);
    root.style.height = CF_PERCENT(100.0f);
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.align_items = CF_ALIGN_CENTER;
    root.style.clips_content = true;
    root.handlers.key = route_key;

    return root;
}

/* The faces, one per job, from the two files the configure step fetched. A font
 * carries its size and no layer above the backend resizes one, so the table of
 * sizes in tokens.h is what opens them. */
static void bind_view(void) {
    cf_font fonts[SITE_FACE_COUNT];

    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        const site_face face = (site_face)i;

        fonts[i] = cf_font_sdl(site_face_mono(face) ? CFORM_SITE_FONT_MONO : CFORM_SITE_FONT,
                               site_face_point(face));
    }

    site_view_bind(&view, fonts);

    /* The one platform call a page is allowed to reach, handed over as a
     * function the driver picked rather than as a call the page makes. */
    view.open_url = cf_web_open_url;
}

static void close_view(void) {
    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        cf_font_sdl_close(&view.fonts[i]);
    }
}

/*
 * One frame, no window, the semantic tree on stdout.
 *
 * A drawing is not text a machine can read, and that is the cost of writing a
 * documentation site in a UI framework. The tree the framework already keeps -
 * role, text, rect, disabled, one line deep per node - is the same information in
 * the form a tool can use, so printing it is a flag rather than a feature: whoever
 * writes the ARIA host adapter has the accessible page to read from the same build
 * this one shows.
 */
static int dump_semantics(const char *want) {
    snprintf(route, sizeof route, "%s", want);
    bind_view();

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 1024u * 1024u)) {
        fprintf(stderr, "site: no context\n");
        close_view();
        return 1;
    }

    ctx.theme = cf_theme_default();
    cf_command_list list = {0};
    cf_frame_run(&ctx, build, (cf_size){1440.0f, 900.0f}, 0.016f, &list);

    char *out = cf_alloc_block(256u * 1024u);
    int failed = 0;

    if (out == NULL) {
        fprintf(stderr, "site: no buffer for the tree\n");
        failed = 1;
    } else {
        /* False is the answer for a tree that does not fit, and the lines that did
         * are still what the page said: print them and let the reader count. */
        cf_semantic_export(&ctx, out, 256u * 1024u);
        fputs(out, stdout);
        cf_free_block(out);
    }

    cf_ctx_free(&ctx);
    close_view();

    return failed;
}

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--dump-semantics") == 0) {
        return dump_semantics(argv[2]);
    }

    cf_sdl_install_log_sink();

    /* The address the reader arrived on, if there was one. A native build reads
     * nothing at all and the table answers its first page; the keys are the route
     * from there. */
    if (!cf_web_read_hash(route, sizeof route)) {
        route[0] = '\0';
    }

    bind_view();

    /* The name and nothing else, because on a browser tab SDL writes this into
     * document.title after the page's own head has been read - a longer window title
     * would therefore overwrite the per-route title the shell stamps from the address,
     * which is the one a tab strip and a link preview need. The route names the page;
     * this names the program, and it reads the same in a desktop title bar. */
    const cf_backend backend = cf_backend_sdl(&(cf_sdl_config){.title = "cform-ui",
                                                               .width = 1440u,
                                                               .height = 900u,
                                                               .min_width = 420u,
                                                               .min_height = 320u});
    if (backend.submit == NULL) {
        printf("site: no window\n");
        close_view();
        return 1;
    }

    const cf_clipboard clipboard = cf_clipboard_sdl();

    cf_app_run(&(cf_app_config){.build = build,
                                .backend = backend,
                                .theme = cf_theme_default(),
                                .fonts = &view.faces[SITE_FACE_PROSE],
                                .clipboard = &clipboard});

    /* The run is over, so the faces the driver opened are its own to hand back, and
     * they go before the window: a font belongs to the platform, and the teardown
     * may be the thing that stops it. */
    close_view();

    if (backend.destroy != NULL) {
        backend.destroy(backend.user_data);
    }

    return 0;
}
