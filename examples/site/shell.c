/*
 * The documentation site: one cform program whose pages are the library's own.
 *
 * What the reader sees is one document - the site's name, the pages to choose from
 * down the left, the prose the address names, and the way on to the next one - in a
 * column that stops growing at a readable measure and walks as a whole. That shape is
 * the gallery's, for the gallery's reason: a document is a column a reader moves down,
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
 * the address bar. The document a frame is made of is in document.h, which the headless
 * tier builds as well - so what that tier measures is what ships, rather than a copy of
 * it someone has to keep in step. A page is handed a context, the table of values and the
 * faces the driver opened: the whole of what a page is given, so a page cannot reach the
 * window, the route or the platform behind it. The driver keeps none of its own state in
 * a file-scope variable - the faces are a local main hands to every frame, and the
 * address is the keyed block a page reads its own state from.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cf/app.h"

#include "document.h"

/* The longest address this shell can hold. A route the table does not name falls
 * back to its first page, so an over-long hash costs the reader a page they did not
 * ask for rather than a crash. */
#define ROUTE_BYTES 96

/* The addresses the masthead points at are in pages.h, beside the pages that point at
 * the same places: leaving this document is the driver's business, but the address is
 * not the driver's to invent twice. */

/* The two faces, as the build named them. On a browser tab both are embedded in the
 * module and read from a virtual path; on a desktop they are the files the fetch
 * left in the build tree, and where the fetch got nothing they open as nothing. */
#ifndef CFORM_SITE_FONT
#define CFORM_SITE_FONT "/Lato-Regular.ttf"
#endif
#ifndef CFORM_SITE_FONT_MONO
#define CFORM_SITE_FONT_MONO "/IBMPlexMono-Regular.ttf"
#endif

/*
 * The address the reader is on, in the keyed block a page keeps its own state in.
 *
 * A press writes it through the context its handler was handed; the arrow keys reach the
 * same block through the context the build stored as the root's handler data, because an
 * event carries no context of its own. Nothing else holds a route, so a second window in
 * one process is a second address rather than one shared.
 *
 * cf_state hands a fresh block zeroed, and an empty route is the table's first page, so
 * the block needs no seeding to be readable.
 */
#define ROUTE_KEY "cf/site/route"

static char *route_of(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, ROUTE_KEY), ROUTE_BYTES);
}

/* The one write a navigation makes: the shell's record, then the bar that is the
 * reader's copy of it. */
static void write_route(char *held, const site_entry *entry) {
    snprintf(held, ROUTE_BYTES, "%s", entry->route);
    cf_web_write_hash(held);
}

/*
 * Going to a page.
 *
 * The route is the site's only state and the address bar is where a reader keeps it:
 * the write follows an event the shell already saw, so the frame that paints the new
 * page is the one that event wakes and nothing has to ask for a second one.
 */
static void follow(cf_ctx *ctx, const site_entry *entry) {
    char *held = route_of(ctx);

    if (held != NULL) {
        write_route(held, entry);
    }
}

static void go_to(cf_ctx *ctx, cf_id node, void *user_data) {
    (void)node;

    const site_entry *entry = user_data;

    if (entry != NULL) {
        follow(ctx, entry);
    }
}

/*
 * The native route.
 *
 * A window has no address bar to type into, so the keys walk the table instead - the
 * same entries and the same write a press makes, one line that decides where a page's
 * route came from. The handler rides the root and runs when the keyboard's chain
 * climbs to it, so a row the Tab key reached is a row the arrows can walk off.
 */
static void route_key(cf_event *event, void *user_data) {
    cf_ctx *ctx = user_data;

    if (event->type != CF_EVENT_KEY_DOWN || ctx == NULL) {
        return;
    }

    char *held = route_of(ctx);

    if (held == NULL) {
        return;
    }

    const uint32_t at = site_index_of(site_entry_or_first(held));

    if (event->key == CF_KEY_RIGHT && at + 1u < SITE_NAV_COUNT) {
        write_route(held, site_entry_at(at + 1u));
    } else if (event->key == CF_KEY_LEFT && at > 0u) {
        write_route(held, site_entry_at(at - 1u));
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
 * on the keyboard route and this line costs it a call that answers false. It is handed
 * the block rather than looking it up again: one frame holds one route pointer, and the
 * shell never asks the state store to grow that block after its first frame.
 */
static void follow_address_bar(char *held) {
    char named[ROUTE_BYTES];

    if (!cf_web_read_hash(named, sizeof named) || strcmp(named, held) == 0) {
        return;
    }

    snprintf(held, ROUTE_BYTES, "%s", named);
}

/*
 * The frame the page is drawn in, and the one thing the driver adds to it: the keys
 * that walk the table, which are this shell's route and not something a headless tier
 * building the same document has any use for.
 *
 * The faces come from the host's pointer and the address from the keyed block: with
 * neither in hand there is no page to state, and a build that declares nothing is the
 * honest answer rather than a document drawn with nobody's fonts.
 */
static cf_node build(cf_ctx *ctx, void *user_data) {
    site_view *const view = user_data;
    char *held = route_of(ctx);

    if (view == NULL || held == NULL) {
        return (cf_node){0};
    }

    /* Read before the page is chosen: a route the bar named has to be the one this
     * frame describes, or the reader sees the page they left for one more frame. */
    follow_address_bar(held);

    const site_entry *entry = site_entry_or_first(held);
    cf_node root = site_document(ctx, view, entry, entry->page(ctx, view), go_to);
    root.handlers.key = route_key;
    root.handlers.key_data = ctx;

    return root;
}

/* The faces, one per job, from the two files the configure step fetched. A font
 * carries its size and no layer above the backend resizes one, so the table of
 * sizes in tokens.h is what opens them. */
static void bind_view(site_view *view) {
    cf_font fonts[SITE_FACE_COUNT];

    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        const site_face face = (site_face)i;

        fonts[i] = cf_font_sdl(site_face_mono(face) ? CFORM_SITE_FONT_MONO : CFORM_SITE_FONT,
                               site_face_point(face));
    }

    site_view_bind(view, fonts);

    /* The one platform call a page is allowed to reach, handed over as a
     * function the driver picked rather than as a call the page makes. */
    view->open_url = cf_web_open_url;
}

static void close_view(site_view *view) {
    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        cf_font_sdl_close(&view->fonts[i]);
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
    site_view view;
    bind_view(&view);

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 1024u * 1024u)) {
        fprintf(stderr, "site: no context\n");
        close_view(&view);
        return 1;
    }

    /* The address the caller asked for, written into the block the build reads it
     * back from: this run has no window to leave it in, and no bar to be read
     * from, so the route is stated here rather than discovered there. */
    char *held = route_of(&ctx);

    if (held != NULL) {
        snprintf(held, ROUTE_BYTES, "%s", want);
    }

    ctx.theme = cf_theme_default();
    cf_command_list list = {0};
    cf_frame_run(&ctx, build, &view, (cf_size){1440.0f, 900.0f}, 0.016f, &list);

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
    close_view(&view);

    return failed;
}

int main(int argc, char **argv) {
    if (argc > 2 && strcmp(argv[1], "--dump-semantics") == 0) {
        return dump_semantics(argv[2]);
    }

    cf_sdl_install_log_sink();

    /* The faces live here, on the run's own stack, and reach every frame through the
     * build's pointer: the driver keeps no file-scope copy a second window in the same
     * process would have to share. The address is not seeded here at all - the first
     * build reads it from the bar the way every later one does, a native build reads
     * nothing from that bar, and the table answers its first page, which is where the
     * keys are the route from. */
    site_view view;
    bind_view(&view);

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
        close_view(&view);
        return 1;
    }

    const cf_clipboard clipboard = cf_clipboard_sdl();

    cf_app_run(&(cf_app_config){.build = build,
                                .build_user_data = &view,
                                .backend = backend,
                                .theme = cf_theme_default(),
                                .fonts = &view.faces[SITE_FACE_PROSE],
                                .clipboard = &clipboard});

    /* The run is over, so the faces the driver opened are its own to hand back, and
     * they go before the window: a font belongs to the platform, and the teardown
     * may be the thing that stops it. */
    close_view(&view);

    if (backend.destroy != NULL) {
        backend.destroy(backend.user_data);
    }

    return 0;
}
