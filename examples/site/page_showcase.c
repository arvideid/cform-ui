/*
 * Showcase: what the library has already drawn, and the shortest way to see it.
 *
 * Every preview on this page is a cform declaration - the same fills, borders and
 * radii the rest of the site paints with, at a smaller scale. Nothing here is a
 * screenshot, which would be a bitmap committed to the repository that the code
 * cannot read, a screen reader cannot reach and a change to the theme would leave
 * behind. A drawing that demonstrates a drawing library is the whole point of the
 * page; a photograph of one is not.
 */
#include "pages.h"

/* The chapters, in the order the gallery runs them and in the order a reader should
 * walk them: state, text, layout, motion, input, leaving. */
static const char *const chapters[][3] = {
    {"01", "Buttons, and the state they keep",
     "A click that survives a rebuild because the count lives under the button's name."},
    {"02", "A field that edits a buffer you own",
     "Typed text, caret, selection and the three clipboard chords, with no copy anywhere."},
    {"03", "Text that measures itself",
     "The paragraph asks its font how tall it is and the layout books the answer."},
    {"04", "Eight rules, one row",
     "Wrap, gap, radius, clamp, align and overlay - each one a field of one struct."},
    {"05", "A pane that scrolls itself",
     "An offset, a content box and a thumb that is the same number drawn a second way."},
    {"06", "A list that builds only what shows",
     "Forty rows, the handful the viewport can hold, and a counter that proves it."},
    {"07", "An image, and a badge that leaves the flow",
     "A texture in a box the measure sized, and an absolute child placed by insets."},
    {"08", "Tracks a container declares",
     "The one layout whose sizes come from the parent instead of the children."},
    {"09", "A value that moves between frames",
     "A tween, the seconds a frame covers, and the page asking for the next one."},
    {"10", "The box the keyboard stands on",
     "Tab, shift-tab, Enter and the space bar, walked through the same handlers."},
    {"11", "The end the page asks for",
     "A build that can say it is done, and a shell that finishes the frame first."},
};

/* The preview each chapter is drawn by: the chapter's own shape, at one scale. */
static const site_preview previews[] = {
    SITE_PREVIEW_PILL,  SITE_PREVIEW_LINES, SITE_PREVIEW_LINES, SITE_PREVIEW_BOXES,
    SITE_PREVIEW_TRACK, SITE_PREVIEW_LINES, SITE_PREVIEW_CELLS, SITE_PREVIEW_CELLS,
    SITE_PREVIEW_TRACK, SITE_PREVIEW_BOXES, SITE_PREVIEW_PILL,
};

cf_node site_page_showcase(cf_ctx *ctx, const site_view *view) {
    cf_node cards[SITE_COUNT(chapters)];

    for (uint32_t i = 0u; i < SITE_COUNT(chapters); ++i) {
        char slug[32];
        snprintf(slug, sizeof slug, "chapter-%s", chapters[i][0]);

        /* One link, to the source that declares the chapter: the gallery is one
         * scrolling page and reads no route, so a card promising to land a reader on
         * chapter six would be a link that goes nowhere. Pointing at the code is the
         * honest version of the same gesture, and the code is the thing a reader of a
         * UI framework actually wants. */
        const cf_node out[] = {
            site_link(ctx, view, slug, "The chapter's source", SITE_BLOB "examples/gallery_page.h"),
        };
        const cf_node footer = site_strip(ctx, out, SITE_COUNT(out));
        const cf_node card = site_card(ctx, view, slug, chapters[i][1], chapters[i][2], NULL);

        /* The preview above the words, the way out below them: a card is a heading, a
         * shape and a sentence, and the shape is what a reader scans a grid for. */
        cards[i] =
            site_stack(ctx, (cf_node[]){site_preview_of(ctx, view, previews[i]), card, footer}, 3u);
    }

    const cf_node live[] = {
        site_link(ctx, view, "open-gallery", "Open the gallery", SITE_GALLERY),
        site_link(ctx, view, "open-source", "The example's source", SITE_BLOB "examples/gallery.c"),
    };

    const cf_node running = site_card(ctx, view, "the-live-one", "The gallery, running",
                                      "Every widget, every rule and every interaction on this "
                                      "site's other page, drawn by the same code in a browser "
                                      "tab through WebAssembly - the identical source, the "
                                      "identical backend, one file with the font inside it.",
                                      NULL);

    const cf_node blocks[] = {
        /* The live card first: it is the answer to "show me", and a reader who came
         * for the pictures should not have to scroll past eleven descriptions of
         * them. The preview above it is the page the gallery is: a column of cards. */
        site_stack(ctx,
                   (cf_node[]){site_preview_of(ctx, view, SITE_PREVIEW_LINES), running,
                               site_strip(ctx, live, SITE_COUNT(live))},
                   3u),
        site_grid(ctx, cards, SITE_COUNT(cards)),
        site_note(ctx, view,
                  "Press F12 in the gallery and the frame tells you its own numbers: the nodes it "
                  "built, how many were dirty, the commands it painted, the size it was offered "
                  "and the microseconds it took. The page you are reading is built the same way "
                  "and would say the same."),
    };

    return site_page(ctx, view, SITE_GROUP_SHOWCASE, "Showcase", "showcase",
                     "Eleven chapters, one window, drawn by the library they document.", blocks,
                     SITE_COUNT(blocks));
}
