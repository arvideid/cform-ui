#ifndef CF_EXAMPLES_SITE_DOCUMENT_H
#define CF_EXAMPLES_SITE_DOCUMENT_H

/*
 * The document every page is drawn in: the site's name, the pages to choose from down
 * the left, the prose the address names, and the way on to the next one.
 *
 * One viewport holds all of it, because a document is a column a reader moves down
 * rather than a frame of panels a reader steers between. Two panes cost three things: an
 * edge every pane has to agree about, a wheel notch that has to guess which pane the
 * reader meant - and a notch that guesses wrong scrolls nothing at all - and a viewport
 * whose role gets traded away for a colour, which is the role that lets a Tab the reader
 * has not finished bring the control it reached on screen.
 *
 * The navigation is a list beside the prose and not a row of chips above it because the
 * site holds seven pages: seven words in a column are a table of contents, and seven
 * words in a line are a crowd. The list is filed by group, and the group is the same
 * word the page itself puts over its title, so the sidebar and the prose cannot disagree
 * about where a page belongs.
 *
 * `static inline` rather than `static`, and in a header rather than in the driver,
 * because two translation units build this and must build the same one: the program that
 * opens the window, and the headless tier that measures what ships instead of a hand-made
 * copy of it.
 */

#include <stdio.h>
#include <string.h>

#include "cf/version.h"

#include "nav.h"

/* The longest a pager's words can be: the two ends of the site name a page beside
 * them, and a row of the table is what the table has already agreed to hold. */
#define SITE_STEP_BYTES 96u

/*
 * The mark: a rounded square in the accent tone with two columns of the tone a
 * button's label carries inside it. Drawn, not loaded - the repository carries no
 * binary it can regenerate, and an image would mean a decoder, which is a
 * backend-ABI conversation rather than a logo.
 */
static inline cf_node site_mark(cf_ctx *ctx, const site_view *view) {
    cf_style column = cf_style_default();
    column.width = CF_PT(MARK_STROKE);
    column.height = CF_PERCENT(56.0f);
    column.background = (cf_opt_color){.present = true, .value = view->on_accent};

    cf_style pair = cf_style_default();
    pair.column_gap = CF_PT(MARK_STROKE);
    pair.align_items = CF_ALIGN_CENTER;
    pair.justify_content = CF_JUSTIFY_CENTER;

    const cf_node columns[] = {{.style = column}, {.style = column}};

    cf_style square = view->accent;
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
 * by is in the list beside this column, so the two ways out of the document are the
 * only things standing next to the name.
 */
static inline cf_node site_masthead(cf_ctx *ctx, const site_view *view) {
    cf_style pair = cf_style_default();
    pair.column_gap = CF_PT(CHIP_GAP);
    pair.align_items = CF_ALIGN_CENTER;

    cf_style line = cf_style_default();
    line.width = CF_PERCENT(100.0f);
    line.align_items = CF_ALIGN_CENTER;
    line.justify_content = CF_JUSTIFY_SPACE_BETWEEN;

    const cf_node leading[] = {
        site_mark(ctx, view),
        site_text(ctx, view, SITE_FACE_HEAD, view->prose, "cform"),
        site_link(ctx, view, "version", "v" CF_VERSION_STRING, SITE_RELEASES),
    };

    const cf_node out[] = {site_link(ctx, view, "github", "GitHub", SITE_REPO)};

    return cf_row(ctx, line,
                  (cf_node[]){cf_row(ctx, pair, leading, SITE_COUNT(leading)),
                              cf_row(ctx, pair, out, SITE_COUNT(out))},
                  2u);
}

/*
 * One group of the table: its name over the rows under it.
 *
 * The label is the eyebrow a page already puts over its own title, so the list says
 * what the page says. A row stretches, because a list of words a reader chooses between
 * is a column of ends lined up, not a row of them.
 */
static inline cf_node site_group(cf_ctx *ctx, const site_view *view, const char *label,
                                 const site_entry *const *rows, uint32_t count,
                                 const site_entry *current, cf_on_press go_to) {
    cf_node chips[SITE_NAV_COUNT];
    const uint32_t taken = count < SITE_NAV_COUNT ? count : SITE_NAV_COUNT;

    for (uint32_t i = 0u; i < taken; ++i) {
        chips[i] = site_row_button(ctx, view, rows[i]->route, rows[i]->label, rows[i] == current,
                                   true, go_to, (void *)rows[i]);
    }

    cf_style list = cf_style_default();
    list.direction = CF_DIRECTION_COLUMN;
    list.row_gap = CF_PT(SIDEBAR_ROW_GAP);
    list.width = CF_PERCENT(100.0f);
    list.align_items = CF_ALIGN_STRETCH;

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(SIDEBAR_HEAD_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_STRETCH;

    const cf_node children[] = {site_eyebrow(ctx, view, label), cf_col(ctx, list, chips, taken)};

    return cf_col(ctx, sheet, children, SITE_COUNT(children));
}

/*
 * The pages to choose from, down the left of the document.
 *
 * The table is walked rather than grouped by a second piece of data: the rows of a
 * group are the rows the table holds together, so a page filed under a group cannot be
 * missing from it, and a page added to the table appears in the list in the same frame
 * it appears anywhere else. A row filed out of order gets a group of its own, which is
 * visible in the list rather than silent.
 */
static inline cf_node site_sidebar(cf_ctx *ctx, const site_view *view, const site_entry *current,
                                   cf_on_press go_to) {
    cf_node groups[SITE_NAV_COUNT];
    uint32_t count = 0u;
    uint32_t at = 0u;

    while (at < SITE_NAV_COUNT) {
        const site_entry *rows[SITE_NAV_COUNT];
        const char *group = site_entry_at(at)->group;
        uint32_t taken = 0u;

        while (at + taken < SITE_NAV_COUNT &&
               strcmp(site_entry_at(at + taken)->group, group) == 0) {
            rows[taken] = site_entry_at(at + taken);
            ++taken;
        }

        groups[count++] = site_group(ctx, view, group, rows, taken, current, go_to);
        at += taken;
    }

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(SIDEBAR_GROUP_GAP);
    sheet.width = CF_PT(SIDEBAR_WIDTH);
    /* The list keeps its measure and the prose gives up its own: a column that shrank
     * beside a paragraph would be a table of contents with words on two lines each. */
    sheet.flex_shrink = 0.0f;
    sheet.align_items = CF_ALIGN_STRETCH;

    return cf_col(ctx, sheet, groups, count);
}

/*
 * One end of the pager, in words.
 *
 * "Previous: Concepts" is what the box says and what a reader who never sees the
 * drawing hears. An arrow would be shorter, and it would be a glyph neither of the two
 * faces here is obliged to carry and a label no semantic tree can name.
 */
static inline cf_node site_pager_step(cf_ctx *ctx, const site_view *view, const char *word,
                                      const site_entry *entry, cf_on_press go_to) {
    char *label = cf_alloc(&ctx->frame, SITE_STEP_BYTES);

    if (label == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    snprintf(label, SITE_STEP_BYTES, "%s: %s", word, entry->label);

    return site_row_button(ctx, view, word, label, false, false, go_to, (void *)entry);
}

/* The two pages beside this one, at the two ends of the line the document ends on. */
static inline cf_node site_pager(cf_ctx *ctx, const site_view *view, const site_entry *entry,
                                 cf_on_press go_to) {
    const uint32_t at = site_index_of(entry);
    cf_node steps[2];
    uint32_t count = 0u;

    if (at > 0u) {
        steps[count++] = site_pager_step(ctx, view, "previous", site_entry_at(at - 1u), go_to);
    }

    if (at + 1u < SITE_NAV_COUNT) {
        steps[count++] = site_pager_step(ctx, view, "next", site_entry_at(at + 1u), go_to);
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
 * The whole frame: one document, in one viewport that walks it.
 *
 * The prose is capped rather than filled, because a line a reader cannot track back
 * along is not what a wide tab asked for. The pair of columns wraps, so the list moves
 * above the prose where the two stop fitting and there is no second layout to keep in
 * step with the first - which is also why a phone gets the same seven rows in the same
 * order as a wide tab, only higher up.
 *
 * The scroller keeps the role the widget asked for. The role is what lets a Tab the
 * reader has not finished scroll the page to whatever it moved focus onto - a control
 * below the fold is reachable but invisible without it - and the theme's scroll family
 * states a resting box and no marks for the pointer's states, so restating the style
 * whole takes the page's tone and leaves nothing to paint over it.
 */
static inline cf_node site_document(cf_ctx *ctx, const site_view *view, const site_entry *current,
                                    cf_node page, cf_on_press go_to) {
    cf_style prose = cf_style_default();
    prose.direction = CF_DIRECTION_COLUMN;
    prose.row_gap = CF_PT(DOC_GAP);
    /* The measure is a length rather than the rest of the line, because a percentage is
     * what a wrapping row packs on: a column asking for all of the space is told,
     * correctly, that it does not fit beside the list, and the two columns end up one
     * above the other in the widest window. Given a length, the row puts them side by
     * side while they fit and the column shrinks to the window when they do not. */
    prose.width = CF_PT(DOC_WIDTH);
    prose.max_width = CF_PT(DOC_WIDTH);
    prose.align_items = CF_ALIGN_STRETCH;

    const cf_node body =
        cf_col(ctx, prose, (cf_node[]){page, site_pager(ctx, view, current, go_to)}, 2u);

    cf_style pair = cf_style_default();
    pair.column_gap = CF_PT(SIDEBAR_GAP);
    pair.row_gap = CF_PT(DOC_GAP);
    pair.width = CF_PERCENT(100.0f);
    pair.align_items = CF_ALIGN_START;
    pair.align_content = CF_ALIGN_CONTENT_START;
    pair.wrap = CF_WRAP;

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(DOC_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.max_width = CF_PT(SHELL_WIDTH);
    sheet.padding = site_pad(DOC_PADDING);
    /* Centred by auto margins rather than by the viewport's alignment. The scroller's
     * content wrapper hugs what it carries, so a document that is aligned inside it is
     * as wide as its own widest fixed measure - 1156 points on a 420-point phone - and
     * centring that walks the page off the left edge. An auto margin claims only what is
     * left over, which is nothing once the window is narrower than the measure. */
    sheet.margin.left = CF_AUTO;
    sheet.margin.right = CF_AUTO;

    const cf_node document =
        cf_col(ctx, sheet,
               (cf_node[]){site_masthead(ctx, view),
                           cf_row(ctx, pair,
                                  (cf_node[]){site_sidebar(ctx, view, current, go_to), body}, 2u)},
               2u);

    /* Restated whole, the way a scroller's viewport is always restated: the widget asked
     * for the clip and the direction, and a style handed back over it keeps the wheel
     * handler it installed on the node. */
    cf_node root = cf_scroller(ctx, "page", &document, 1u);
    root.style = view->surface;
    root.style.width = CF_PERCENT(100.0f);
    root.style.height = CF_PERCENT(100.0f);
    root.style.direction = CF_DIRECTION_COLUMN;
    /* Stretched, not centred: this wrapper is the measure the document is capped
     * against, and a wrapper that hugs a fixed measure instead of taking the window's
     * width is a page that overflows it. The document centres itself inside the wrapper,
     * by the auto margin above. */
    root.style.align_items = CF_ALIGN_STRETCH;
    root.style.clips_content = true;

    return root;
}

#endif
