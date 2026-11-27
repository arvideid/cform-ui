/*
 * The documentation site: one cform program whose pages are the library's own.
 *
 * What the reader sees is a bar, a sidebar and a column of prose - the structure a
 * documentation site has on every project, drawn with the framework the site is
 * about. Nothing here is a template language, a static generator or a copy of
 * someone else's stylesheet: the pages are declaration functions in C, the values
 * they paint with are one table in tokens.h, and the navigation is one array in
 * nav.h. The route lives in the address bar because a page a reader cannot link to
 * is not a page.
 *
 * This file is the driver only. It owns the fonts, the window, the current route
 * and the chrome around a page, and it hands a page a context and the table - the
 * whole of what a page is given, so a page cannot reach the window, the route or
 * the platform behind it.
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

/* The addresses the bar and the chip point at are in pages.h, beside the pages that
 * point at the same places: leaving this document is the driver's business, but the
 * address is not the driver's to invent twice. */

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

/* Goes to a page by asking the browser for its address. The write follows an event
 * the shell already saw, so the frame that paints the new page is the one that
 * event wakes and nothing has to ask for a second one. */
static void go_to(cf_ctx *ctx, uint32_t slot, void *user_data) {
    const site_entry *entry = user_data;

    (void)ctx;
    (void)slot;

    if (entry == NULL) {
        return;
    }

    snprintf(route, sizeof route, "%s", entry->route);
    cf_web_write_hash(route);
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

/* The version chip: what the build was made from, and the way to the page that
 * says what it cost. The number is the header's, so a chip that lies is a build
 * nobody shipped. */
static cf_node make_chip(cf_ctx *ctx) {
    cf_node chip = site_link(ctx, &view, "version", "v" CF_VERSION_STRING, SITE_RELEASES);

    /* A bordered pill rather than a hover well, in the hairline the theme already
     * states for a card: a chip states a fact about the build, and the page's own
     * edge tone is the only rule on it that is not the page's. */
    chip.style.border = site_pad(HAIRLINE);
    chip.style.border_color = view.card.border_color;
    chip.style.radius = view.card.radius;

    return chip;
}

/* The bar: the mark, the word and the chip on the leading edge, the three ways out
 * on the trailing one. */
static cf_node make_bar(cf_ctx *ctx) {
    const cf_node wordmark = site_text(ctx, &view, SITE_FACE_HEAD, view.prose, "cform");

    cf_style left = cf_style_default();
    left.column_gap = CF_PT(BAR_ITEM_GAP);
    left.align_items = CF_ALIGN_CENTER;

    cf_style right = cf_style_default();
    right.column_gap = CF_PT(BAR_ITEM_GAP);
    right.align_items = CF_ALIGN_CENTER;

    const cf_node leading[] = {
        site_row_button(ctx, &view, "docs", "Docs", false, false, go_to,
                        (void *)site_entry_or_first(SITE_PREFIX "getting-started")),
        site_row_button(ctx, &view, "showcase", "Showcase", false, false, go_to,
                        (void *)site_entry_for_route("/showcase")),
        site_link(ctx, &view, "github", "GitHub", SITE_REPO),
    };

    cf_style bar = view.chrome;
    bar.width = CF_PERCENT(100.0f);
    bar.height = CF_PT(BAR_HEIGHT);
    bar.padding = (cf_box){CF_PT(BAR_PADDING), CF_PT(0.0f), CF_PT(BAR_PADDING), CF_PT(0.0f)};
    bar.align_items = CF_ALIGN_CENTER;
    bar.justify_content = CF_JUSTIFY_SPACE_BETWEEN;
    /* The rule under the bar is the hairline the theme states for a card, turned
     * to one edge: a new tone for a line would be a new colour on the page. */
    bar.border = (cf_box){CF_PT(0.0f), CF_PT(0.0f), CF_PT(0.0f), CF_PT(HAIRLINE)};
    bar.border_color = view.card.border_color;
    bar.radius = 0.0f;

    return cf_row(
        ctx, bar,
        (cf_node[]){cf_row(ctx, left, (cf_node[]){make_mark(ctx), wordmark, make_chip(ctx)}, 3u),
                    cf_row(ctx, right, leading, 3u)},
        2u);
}

/*
 * The rows under one heading.
 *
 * Only the group the reader is in opens: three headings over three rows in a
 * column 260 points wide is furniture, and on the narrow window it is furniture
 * on top of the page. The closed groups leave the layout rather than painting
 * nothing where they stood, which is what makes the row that is open sit at the
 * top of the sidebar instead of under two blank headings.
 */
static cf_node make_group(cf_ctx *ctx, const char *group) {
    cf_node rows[SITE_GROUP_ROWS + 1u];
    uint32_t count = 0u;

    rows[count++] = site_eyebrow(ctx, &view, group);

    for (uint32_t i = 0u; i < SITE_NAV_COUNT && count <= SITE_GROUP_ROWS; ++i) {
        const site_entry *entry = site_entry_at(i);

        if (entry == NULL || !site_entry_in_group(entry, group)) {
            continue;
        }

        rows[count++] = site_row_button(ctx, &view, entry->route, entry->label,
                                        strcmp(current()->route, entry->route) == 0, true, go_to,
                                        (void *)entry);
    }

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(2.0f);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;
    sheet.display = site_entry_in_group(current(), group) ? CF_DISPLAY_FLEX : CF_DISPLAY_NONE;

    return cf_col(ctx, sheet, rows, count);
}

static cf_node make_sidebar(cf_ctx *ctx) {
    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(SIDEBAR_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;

    const cf_node document =
        cf_col(ctx, sheet,
               (cf_node[]){make_group(ctx, SITE_GROUP_DOCS), make_group(ctx, SITE_GROUP_PROJECT),
                           make_group(ctx, SITE_GROUP_SHOWCASE)},
               3u);

    cf_node side = cf_scroller(ctx, "sidebar", &document, 1u);

    /* Restated whole, the way a scroller's viewport is always restated: the widget
     * asked for the clip and the direction, and a style handed back over it keeps
     * the wheel handler it installed on the node.
     *
     * The visual role comes off with the restatement. A role is a request for the
     * theme's variant set over the declared style, and the scroll family's paint is a
     * scrollbar thumb's greys - so a sidebar that kept the role would answer the whole
     * panel in thumb grey however the page is themed, and the content column would do
     * it across the entire reading area. The wheel handler rides the node's handler
     * rather than its role, so dropping the role takes the paint and not the scroll. */
    side.role = CF_ROLE_NONE;
    side.style = view.chrome;
    side.style.width = CF_PT(SIDEBAR_WIDTH);
    side.style.height = CF_PERCENT(100.0f);
    side.style.direction = CF_DIRECTION_COLUMN;
    side.style.padding = site_pad(SIDEBAR_PADDING);
    side.style.align_items = CF_ALIGN_START;
    side.style.radius = 0.0f;
    side.style.border = (cf_box){CF_PT(0.0f), CF_PT(0.0f), CF_PT(HAIRLINE), CF_PT(0.0f)};
    side.style.border_color = view.card.border_color;
    side.style.clips_content = true;

    return side;
}

/* The page in the column it is capped to, inside a viewport that walks. Capped
 * rather than filled: a line of prose a reader cannot track back along is not what
 * a wide tab asked for. */
static cf_node make_content(cf_ctx *ctx, const cf_node *page) {
    cf_node column = *page;
    column.style.max_width = CF_PT(CONTENT_WIDTH);
    column.style.padding = site_pad(CONTENT_PADDING);
    column.style.margin = (cf_box){CF_AUTO, CF_PT(0.0f), CF_AUTO, CF_PT(0.0f)};

    cf_node content = cf_scroller(ctx, "content", &column, 1u);

    /* The reading area is the wider of the two panels a thumb grey would ruin, and it
     * is the one that carries the page's own tone: the same reason as the sidebar and
     * the same one-line answer. */
    content.role = CF_ROLE_NONE;
    content.style = view.surface;
    content.style.flex_grow = 1.0f;
    content.style.height = CF_PERCENT(100.0f);
    content.style.direction = CF_DIRECTION_COLUMN;
    content.style.align_items = CF_ALIGN_CENTER;
    content.style.clips_content = true;

    return content;
}

/*
 * The native route.
 *
 * A window has no address bar to type into, so the keys walk the table instead -
 * the same entries, the same handler, one line that decides where a page's route
 * came from. The handler rides the root and runs when the keyboard's chain climbs
 * to it, so a row the Tab key reached is a row the arrows can walk off.
 */
static void route_key(cf_event *event, void *user_data) {
    (void)user_data;

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    uint32_t index = 0u;

    for (uint32_t i = 0u; i < SITE_NAV_COUNT; ++i) {
        const site_entry *entry = site_entry_at(i);

        if (entry != NULL && strcmp(entry->route, current()->route) == 0) {
            index = i;
            break;
        }
    }

    if (event->key == CF_KEY_RIGHT && index + 1u < SITE_NAV_COUNT) {
        index++;
    } else if (event->key == CF_KEY_LEFT && index > 0u) {
        index--;
    } else {
        return;
    }

    const site_entry *next = site_entry_at(index);

    if (next != NULL) {
        snprintf(route, sizeof route, "%s", next->route);
        /* The bar follows the page here as much as it does after a press: a reader on a
         * tab who walks the pages with the keys and then copies the address is owed the
         * page they are looking at, and on a desktop the write is nothing. */
        cf_web_write_hash(route);
    }
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

static cf_node build(cf_ctx *ctx) {
    /* Read before the page is chosen: a route the bar named has to be the one this
     * frame describes, or the reader sees the page they left for one more frame. */
    follow_address_bar();

    const site_entry *entry = current();
    const cf_node page = entry->page(ctx, &view);

    cf_style sheet = view.surface;
    sheet.width = CF_PERCENT(100.0f);
    sheet.height = CF_PERCENT(100.0f);
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.clips_content = true;

    cf_style columns = cf_style_default();
    columns.width = CF_PERCENT(100.0f);
    columns.flex_grow = 1.0f;
    columns.align_items = CF_ALIGN_START;
    /* The two columns fill what the bar left: without this a row hugs its content
     * and the page gets the height the sidebar asked for. */
    columns.height = CF_AUTO;

    const cf_node body =
        cf_row(ctx, columns, (cf_node[]){make_sidebar(ctx), make_content(ctx, &page)}, 2u);

    cf_node root = cf_col(ctx, sheet, (cf_node[]){make_bar(ctx), body}, 2u);
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
