#ifndef CF_EXAMPLES_SITE_BLOCKS_H
#define CF_EXAMPLES_SITE_BLOCKS_H

/*
 * The primitives every page is made of, each one a function that answers a
 * declaration.
 *
 * These are the only place a page's vocabulary is spelled: an eyebrow, a heading,
 * a paragraph, a panel of code, a tabbed block, a nav row. A page says what a
 * thing *is* and never how it is drawn, because the drawing is what the design
 * system is allowed to change. Nothing here names a length, a tone or a radius -
 * they all come out of `site_view`, and that is the seam a gate checks.
 *
 * `static inline` rather than `static` because five translation units include this
 * file into one program: a plain static would be a private copy per unit and a
 * warning about the ones that unit does not call.
 */

#include <stdio.h>

#include "cf/build.h"
#include "cf/widget.h"

#include "tokens.h"

/* How many rows an array of them holds. A page states its content and never counts
 * it by hand, because a hand-counted array is a block that renders one line short
 * the day somebody adds a line - and the line that goes missing is the last one,
 * which is the line a reader is least likely to notice is gone. */
#define SITE_COUNT(array) (uint32_t)(sizeof(array) / sizeof((array)[0]))

/* One run of text at one face in one tone, sized by what it holds. The bytes are
 * borrowed from the caller, which is a page stating a literal, so they outlive the
 * frame that declares them. */
static inline cf_node site_text(cf_ctx *ctx, const site_view *view, site_face face, cf_color ink,
                                const char *chars) {
    cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

    if (content == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    *content = (cf_text_content){.stack = view->faces[face], .text = cf_string_of(chars)};

    cf_style style = cf_style_default();
    style.color = (cf_opt_color){.present = true, .value = ink};
    /* Prose takes the width the column gives it and breaks as often as that
     * width asks; the measure, not a guess, sizes the box. */
    style.max_width = CF_PERCENT(100.0f);

    return (cf_node){.style = style, .measure = cf_text_measure, .measure_data = content};
}

/* The small tracked line above a heading. */
static inline cf_node site_eyebrow(cf_ctx *ctx, const site_view *view, const char *chars) {
    return site_text(ctx, view, SITE_FACE_EYEBROW, view->aside, chars);
}

/*
 * The page's title and a section's heading, each keyed by its own slug.
 *
 * The key is the honest half of an anchor: it is what makes the heading the same
 * node across every rebuild, which is what a later scroll-to-heading needs. The
 * offset is the part this build cannot state - a declaration is made before any
 * layout has run, and the tree hands a caller no way back from a name to a slot -
 * so the rule a reader can copy out of an address bar is the one named in
 * docs/backlog.md rather than one pretended to here.
 */
static inline cf_node site_title(cf_ctx *ctx, const site_view *view, const char *chars,
                                 const char *slug) {
    cf_node title = site_text(ctx, view, SITE_FACE_TITLE, view->prose, chars);
    title.key = cf_intern_text(&ctx->names, slug);

    return title;
}

static inline cf_node site_head(cf_ctx *ctx, const site_view *view, const char *chars,
                                const char *slug) {
    cf_node head = site_text(ctx, view, SITE_FACE_HEAD, view->prose, chars);
    head.key = cf_intern_text(&ctx->names, slug);

    return head;
}

static inline cf_node site_p(cf_ctx *ctx, const site_view *view, const char *chars) {
    return site_text(ctx, view, SITE_FACE_PROSE, view->prose, chars);
}

static inline cf_node site_aside(cf_ctx *ctx, const site_view *view, const char *chars) {
    return site_text(ctx, view, SITE_FACE_PROSE, view->aside, chars);
}

/* The column one block of the page sits under the next in. */
static inline cf_node site_stack(cf_ctx *ctx, const cf_node *children, uint32_t count) {
    cf_style column = cf_style_default();
    column.direction = CF_DIRECTION_COLUMN;
    column.row_gap = CF_PT(BLOCK_GAP);
    column.width = CF_PERCENT(100.0f);
    column.align_items = CF_ALIGN_START;

    return cf_col(ctx, column, children, count);
}

/*
 * A panel of code: the box dark, the lines monospaced, one declaration per line so
 * the command stream carries each as its own text command. A line wider than the
 * panel breaks at its own spaces rather than closing with an ellipsis, because a
 * command a reader cannot finish is worse than one they have to follow a line down,
 * and the column already gives every line the measure the panel has.
 */
static inline cf_node site_panel(cf_ctx *ctx, const site_view *view, const char *const *lines,
                                 uint32_t count) {
    cf_style sheet = view->panel;
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.align_items = CF_ALIGN_START;
    sheet.width = CF_PERCENT(100.0f);

    if (count == 0u) {
        return cf_col(ctx, sheet, NULL, 0u);
    }

    /* The rows are the frame's own bytes rather than an array on the caller's
     * stack: a panel is as long as the command it shows, and the stack a browser
     * thread is given is not a budget a page should be sized against. The
     * container copies what it is handed, so nothing here outlives the
     * declaration it is building. */
    cf_node *rows = cf_alloc(&ctx->frame, (size_t)count * sizeof *rows);

    if (rows == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    for (uint32_t i = 0u; i < count; ++i) {
        cf_node line = site_text(ctx, view, SITE_FACE_MONO, view->on_panel, lines[i]);
        /* The line takes the panel's whole width and breaks as often as that asks:
         * the prose rule again, in the other family. */
        line.style.width = CF_PERCENT(100.0f);
        line.style.max_width = CF_AUTO;
        rows[i] = line;
    }

    return cf_col(ctx, sheet, rows, count);
}

/* A note set off the prose: the same box the bar wears, so a aside reads as
 * furniture rather than as another paragraph. */
static inline cf_node site_note(cf_ctx *ctx, const site_view *view, const char *chars) {
    cf_style well = view->chrome;
    well.padding = site_pad(CARD_PADDING);
    well.direction = CF_DIRECTION_COLUMN;
    well.width = CF_PERCENT(100.0f);

    return cf_col(ctx, well, (cf_node[]){site_aside(ctx, view, chars)}, 1u);
}

/*
 * One press target that is not a pill: a nav row, a top-bar item, a tab.
 *
 * The family it asks to be styled as is a list row, and that is the whole design
 * decision - the theme's list family is the one that hands out a hover well and
 * nothing else, which is what a row in a sidebar is. The resting box therefore
 * stays the page's own, and `active` says what sets this row apart: an ink rule
 * down its leading edge for the row the reader is on, and nothing for the rows
 * beside it. The width is reserved either way, so an inactive row does not slide
 * when it becomes the active one.
 */
static inline cf_node site_row_of(cf_ctx *ctx, const site_view *view, const char *name,
                                  const cf_node *label, uint32_t label_count, bool active,
                                  bool stretch, cf_on_press on_press, void *user_data) {
    cf_node row = cf_button(ctx, name, on_press, user_data, label, label_count);

    /* The button family's resting box is the primary action's dark pill; a row in
     * a list is not that, so the family it belongs to is restated along with a
     * style of its own. */
    row.role = CF_ROLE_LIST;
    row.style = cf_style_default();
    row.style.column_gap = CF_PT(BAR_ITEM_GAP);
    row.style.align_items = CF_ALIGN_CENTER;
    row.style.padding = site_pad(NAV_ROW_PADDING);

    if (stretch) {
        row.style.width = CF_PERCENT(100.0f);
        row.style.align_items = CF_ALIGN_START;
    }

    /* A border on one edge only, and no colour for the rows that do not need it:
     * an unstated tone paints nothing, which leaves the space and takes the
     * line away. */
    row.style.border =
        (cf_box){CF_PT(active ? NAV_RULE_WIDTH : 0.0f), CF_PT(0.0f), CF_PT(0.0f), CF_PT(0.0f)};
    row.style.border_color = active ? (cf_opt_color){.present = true, .value = view->prose}
                                    : (cf_opt_color){.present = false, .value = view->prose};

    return row;
}

/* The row above, with one word for its label - which is what a nav row, a tab and
 * two of the three bar items are. The one that is not is a word with a mark beside
 * it, and calls the form that takes both. */
static inline cf_node site_row_button(cf_ctx *ctx, const site_view *view, const char *name,
                                      const char *label, bool active, bool stretch,
                                      cf_on_press on_press, void *user_data) {
    const cf_node text =
        site_text(ctx, view, SITE_FACE_PROSE, active ? view->prose : view->aside, label);

    return site_row_of(ctx, view, name, &text, 1u, active, stretch, on_press, user_data);
}

/*
 * A press that leaves the document.
 *
 * The address travels with the button that names it, in the arena the dispatch
 * reads from, and so does the one function the driver allowed this build to have
 * for opening it. A page therefore says *where* a link goes and nothing about how
 * anything gets there - the shell hands the view its `open_url` or leaves it
 * unset, and a build with no browser shows the same link and follows it nowhere.
 */
#define SITE_URL_BYTES 160
typedef struct site_address {
    char text[SITE_URL_BYTES];
    void (*open)(const char *url);
} site_address;

static inline void site_follow(cf_ctx *ctx, uint32_t slot, void *user_data) {
    const site_address *address = user_data;

    (void)ctx;
    (void)slot;

    if (address != NULL && address->open != NULL) {
        address->open(address->text);
    }
}

/*
 * A row whose press opens something that is not this page, marked as such.
 *
 * The mark is drawn rather than typed: an arrow glyph is a promise about a font
 * that neither of the two faces here is obliged to keep, and a link that leaves
 * has to say it is leaving whatever the reader is looking at. A box outlined on
 * the two edges the arrow would have crossed is the same statement in the
 * hairline the page already uses for its own edges.
 */
static inline cf_node site_link(cf_ctx *ctx, const site_view *view, const char *name,
                                const char *label, const char *url) {
    site_address *address = cf_alloc(&ctx->frame, sizeof *address);

    if (address == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    snprintf(address->text, sizeof address->text, "%s", url);
    address->open = view->open_url;

    const cf_node text = site_text(ctx, view, SITE_FACE_PROSE, view->aside, label);

    cf_style mark = cf_style_default();
    mark.width = CF_PT(MARK_STROKE * 2.0f);
    mark.height = CF_PT(MARK_STROKE * 2.0f);
    mark.border = (cf_box){CF_PT(HAIRLINE), CF_PT(HAIRLINE), CF_PT(0.0f), CF_PT(0.0f)};
    mark.border_color = (cf_opt_color){.present = true, .value = view->aside};

    const cf_node children[] = {text, {.style = mark}};

    return site_row_of(ctx, view, name, children, 2u, false, false, site_follow, address);
}

/* Which panel a tab block shows, kept between frames under the name the block
 * was declared with. */
typedef struct site_tab_pick {
    char name[40];
    uint32_t index;
} site_tab_pick;

static inline void site_pick_tab(cf_ctx *ctx, uint32_t slot, void *user_data) {
    const site_tab_pick *pick = user_data;
    uint32_t *chosen = cf_state(ctx, cf_intern_text(&ctx->names, pick->name), sizeof *chosen);

    (void)slot;

    if (chosen != NULL) {
        *chosen = pick->index;
    }
}

/*
 * Tabs over panels: the labels in a row, the chosen panel under them.
 *
 * The choice lives in the keyed block the name opens, so a rebuild - a resize, a
 * click three columns away - keeps the tab the reader opened. The underline is a
 * bottom border in the prose tone and nothing else: the resting box is the page's
 * own, and the states come from the same list family a nav row reads.
 */
static inline cf_node site_tabs(cf_ctx *ctx, const site_view *view, const char *name,
                                const char *const *labels, uint32_t count, const cf_node *panels,
                                uint32_t panel_count) {
    uint32_t *chosen = cf_state(ctx, cf_intern_text(&ctx->names, name), sizeof *chosen);
    const uint32_t taken = count < SITE_TAB_COUNT ? count : SITE_TAB_COUNT;
    const uint32_t picked = chosen != NULL && *chosen < taken ? *chosen : 0u;
    cf_node tabs[SITE_TAB_COUNT];

    for (uint32_t i = 0u; i < taken; ++i) {
        /* The pick travels with the button that asks for it, in the arena that
         * holds the frame the dispatch reads it back from. */
        site_tab_pick *pick = cf_alloc(&ctx->frame, sizeof *pick);

        if (pick == NULL) {
            tabs[i] = (cf_node){.style = cf_style_default()};
            continue;
        }

        snprintf(pick->name, sizeof pick->name, "%s", name);
        pick->index = i;

        cf_node tab = site_row_button(ctx, view, labels[i], labels[i], i == picked, false,
                                      site_pick_tab, pick);
        /* The chosen tab's mark moves from the leading edge to the trailing one:
         * the same rule, turned, is what reads as an underline. A tab is a box of
         * its label alone, so it pads its sides with the row's own gap. */
        tab.style.padding = (cf_box){CF_PT(BAR_ITEM_GAP), CF_PT(NAV_ROW_PADDING),
                                     CF_PT(BAR_ITEM_GAP), CF_PT(NAV_ROW_PADDING)};
        tab.style.border = (cf_box){CF_PT(0.0f), CF_PT(0.0f), CF_PT(0.0f),
                                    CF_PT(i == picked ? NAV_RULE_WIDTH : 0.0f)};

        tabs[i] = tab;
    }

    cf_style strip = cf_style_default();
    strip.column_gap = CF_PT(BAR_ITEM_GAP);
    strip.align_items = CF_ALIGN_START;

    const cf_node shown =
        picked < panel_count ? panels[picked] : (cf_node){.style = cf_style_default()};
    const cf_node children[] = {cf_row(ctx, strip, tabs, taken), shown};

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(CONTENT_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;

    return cf_col(ctx, sheet, children, 2u);
}

/*
 * A heading and the blocks under it.
 *
 * The section is what a page is made of: one heading, keyed by its own slug so it
 * stays the same box across rebuilds, over the blocks it introduces. A page that
 * spaced its own blocks would be a page carrying a layout, so the column belongs
 * here and the page only hands the blocks over.
 */
static inline cf_node site_section(cf_ctx *ctx, const site_view *view, const char *heading,
                                   const char *slug, const cf_node *blocks, uint32_t count) {
    cf_node *rows = cf_alloc(&ctx->frame, (size_t)(count + 1u) * sizeof *rows);

    if (rows == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    rows[0] = site_head(ctx, view, heading, slug);

    for (uint32_t i = 0u; i < count; ++i) {
        rows[i + 1u] = blocks[i];
    }

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(BLOCK_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;

    return cf_col(ctx, sheet, rows, count + 1u);
}

/*
 * The whole page: the group it belongs to, the title a reader links to, one line
 * saying what they will get, and the sections under it.
 *
 * Every page starts the same way, which is the point - the shape of a document is
 * not something each author gets to decide, and the headless tier can therefore
 * hunt the same text in the same place on every route.
 */
static inline cf_node site_page(cf_ctx *ctx, const site_view *view, const char *group,
                                const char *title, const char *slug, const char *lede,
                                const cf_node *blocks, uint32_t count) {
    cf_style head = cf_style_default();
    head.direction = CF_DIRECTION_COLUMN;
    head.row_gap = CF_PT(BLOCK_GAP);
    head.width = CF_PERCENT(100.0f);
    head.align_items = CF_ALIGN_START;

    const cf_node opening =
        cf_col(ctx, head,
               (cf_node[]){site_eyebrow(ctx, view, group), site_title(ctx, view, title, slug),
                           site_aside(ctx, view, lede)},
               3u);

    cf_node *rows = cf_alloc(&ctx->frame, (size_t)(count + 1u) * sizeof *rows);

    if (rows == NULL) {
        return opening;
    }

    rows[0] = opening;

    for (uint32_t i = 0u; i < count; ++i) {
        rows[i + 1u] = blocks[i];
    }

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(CONTENT_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;

    return cf_col(ctx, sheet, rows, count + 1u);
}

/* A strip of blocks side by side: two links under a card, the words beside a
 * number. Nothing in it is measured, because everything in it was already
 * declared by the block that owns its size. */
static inline cf_node site_strip(cf_ctx *ctx, const cf_node *items, uint32_t count) {
    cf_style row = cf_style_default();
    row.column_gap = CF_PT(BAR_ITEM_GAP);
    row.align_items = CF_ALIGN_CENTER;

    return cf_row(ctx, row, items, count);
}

/*
 * A tile: the card box the theme states, a heading, a line about it, and the
 * footer the page hands - a preview, a panel, a way out.
 *
 * The two fields that make a card a card rather than a bordered paragraph are
 * stated here: it grows to fill the line it is on and it refuses to shrink past
 * the width where its own prose stops being readable.
 */
static inline cf_node site_card(cf_ctx *ctx, const site_view *view, const char *slug,
                                const char *title, const char *prose, const cf_node *footer) {
    const uint32_t count = footer != NULL ? 3u : 2u;
    cf_node *rows = cf_alloc(&ctx->frame, (size_t)count * sizeof *rows);

    if (rows == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    rows[0] = site_head(ctx, view, title, slug);
    rows[1] = site_aside(ctx, view, prose);

    if (footer != NULL) {
        rows[2] = *footer;
    }

    cf_style sheet = view->card;
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(CARD_GAP);
    sheet.padding = site_pad(CARD_PADDING);
    sheet.align_items = CF_ALIGN_START;
    sheet.min_width = CF_PT(CARD_MIN_WIDTH);
    sheet.flex_grow = 1.0f;

    return cf_col(ctx, sheet, rows, count);
}

/* The row a set of cards rides in: wrapped, so the measure holds as many columns
 * as it can and a card that cannot get its minimum width takes a line of its own.
 * No column count is stated, which is what lets the same grid answer a wide tab and
 * a phone. */
static inline cf_node site_grid(cf_ctx *ctx, const cf_node *cards, uint32_t count) {
    cf_style sheet = cf_style_default();
    sheet.wrap = CF_WRAP;
    sheet.column_gap = CF_PT(CARD_GAP);
    sheet.row_gap = CF_PT(CARD_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_content = CF_ALIGN_CONTENT_START;

    return cf_row(ctx, sheet, cards, count);
}

/* One word and what it means, in the two columns a table of them needs: the word
 * holds the width the caller states so every meaning starts on the same edge. The
 * width is a parameter because two tables on this site hold different left columns
 * - a glossary of words and a list of symbols - and the number belongs to what is
 * being listed rather than to the block that lines it up. */
static inline cf_node site_pair(cf_ctx *ctx, const site_view *view, const char *word,
                                const char *meaning, float word_width) {
    cf_node term = site_text(ctx, view, SITE_FACE_PROSE, view->prose, word);
    term.style.width = CF_PT(word_width);
    term.style.flex_shrink = 0.0f;

    cf_node aside = site_aside(ctx, view, meaning);
    aside.style.flex_grow = 1.0f;

    cf_style row = cf_style_default();
    row.column_gap = CF_PT(BLOCK_GAP);
    row.width = CF_PERCENT(100.0f);
    row.align_items = CF_ALIGN_START;

    return cf_row(ctx, row, (cf_node[]){term, aside}, 2u);
}

/* The table, from an array of the pairs. */
static inline cf_node site_pairs(cf_ctx *ctx, const site_view *view, const char *const *words,
                                 const char *const *meanings, uint32_t count, float word_width) {
    cf_node *rows = cf_alloc(&ctx->frame, (size_t)count * sizeof *rows);

    if (rows == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    for (uint32_t i = 0u; i < count; ++i) {
        rows[i] = site_pair(ctx, view, words[i], meanings[i], word_width);
    }

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(BLOCK_GAP);
    sheet.width = CF_PERCENT(100.0f);

    return cf_col(ctx, sheet, rows, count);
}

/* A numbered step: the number in the code family, the word for what happens, the
 * prose that says it and the panel that shows it. Four of these are how a frame
 * gets explained, and the number belongs to the block rather than to the page
 * because a step with no number beside it is a paragraph. */
static inline cf_node site_step(cf_ctx *ctx, const site_view *view, const char *number,
                                const char *heading, const char *slug, const char *prose,
                                const char *const *lines, uint32_t line_count) {
    cf_node marker = site_text(ctx, view, SITE_FACE_MONO, view->aside, number);
    marker.style.flex_shrink = 0.0f;

    cf_style line = cf_style_default();
    line.column_gap = CF_PT(BLOCK_GAP);
    line.align_items = CF_ALIGN_CENTER;
    line.width = CF_PERCENT(100.0f);

    const cf_node titled =
        cf_row(ctx, line, (cf_node[]){marker, site_head(ctx, view, heading, slug)}, 2u);
    const cf_node body[] = {titled, site_aside(ctx, view, prose),
                            site_panel(ctx, view, lines, line_count)};

    cf_style sheet = cf_style_default();
    sheet.direction = CF_DIRECTION_COLUMN;
    sheet.row_gap = CF_PT(BLOCK_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.align_items = CF_ALIGN_START;

    return cf_col(ctx, sheet, body, 3u);
}

/*
 * A card's preview: a band of boxes drawn in the page's own tones.
 *
 * This is what the showcase exists for - a site about a drawing library describing
 * itself by drawing rather than by a screenshot of a thing that cannot be read by
 * the code. Each kind is one chapter's shape at one scale, and an unknown kind
 * answers the plainest of them rather than nothing.
 */
typedef enum site_preview {
    SITE_PREVIEW_BOXES,
    SITE_PREVIEW_LINES,
    SITE_PREVIEW_TRACK,
    SITE_PREVIEW_PILL,
    SITE_PREVIEW_CELLS,
    SITE_PREVIEW_KINDS
} site_preview;

/* One filled box, sized in points, in a tone the view already owns. */
static inline cf_node preview_box(cf_color tone, float width, float height, float radius) {
    cf_style style = cf_style_default();
    style.width = CF_PT(width);
    style.height = CF_PT(height);
    style.radius = radius;
    style.background = (cf_opt_color){.present = true, .value = tone};

    return (cf_node){.style = style};
}

static inline cf_node site_preview_of(cf_ctx *ctx, const site_view *view, site_preview kind) {
    cf_node items[PREVIEW_COUNT];
    uint32_t count = 0u;
    /* A band is a box the card's own chrome tone, so the boxes inside it need a
     * darker one: the aside tone is the one the page already uses for a line that
     * is there without being read. */
    const cf_style band = view->chrome;

    switch (kind) {
    case SITE_PREVIEW_BOXES:
        while (count < 4u) {
            items[count++] = preview_box(view->aside, PREVIEW_ITEM, PREVIEW_TALL, band.radius);
        }
        break;
    case SITE_PREVIEW_LINES:
        while (count < 3u) {
            /* The middle line in the ink tone: read as the sentence a reader's eye
             * lands on. Spelled as a separate step because `count` is the index this
             * very statement advances, and a read beside a write in one expression is
             * an order the language does not settle. */
            const cf_color tone = count == 1u ? view->prose : view->aside;
            items[count++] = preview_box(tone, PREVIEW_TALL * 3.0f, PREVIEW_LINE, band.radius);
        }
        break;
    case SITE_PREVIEW_TRACK:
        items[count++] = preview_box(view->aside, PREVIEW_TALL * 4.0f, PREVIEW_LINE, band.radius);
        items[count++] = preview_box(view->prose, PREVIEW_TALL, PREVIEW_LINE, band.radius);
        count = 2u;
        break;
    case SITE_PREVIEW_PILL:
        items[count++] =
            preview_box(view->prose, PREVIEW_TALL * 3.0f, PREVIEW_ITEM * 2.0f, PREVIEW_ITEM);
        break;
    case SITE_PREVIEW_CELLS:
    case SITE_PREVIEW_KINDS:
        break;
    }

    if (kind == SITE_PREVIEW_CELLS) {
        while (count < 4u) {
            const cf_color tone = count % 2u == 0u ? view->aside : view->prose;
            items[count++] = preview_box(tone, PREVIEW_TALL, PREVIEW_TALL, band.radius);
        }
    }

    cf_style sheet = band;
    sheet.width = CF_PERCENT(100.0f);
    sheet.height = CF_PT(PREVIEW_HEIGHT);
    sheet.column_gap = CF_PT(PREVIEW_GAP);
    sheet.row_gap = CF_PT(PREVIEW_GAP);
    sheet.align_items = CF_ALIGN_CENTER;
    sheet.wrap = CF_NO_WRAP;

    return cf_row(ctx, sheet, items, count);
}

#endif
