#ifndef CF_EXAMPLES_SITE_TOKENS_H
#define CF_EXAMPLES_SITE_TOKENS_H

/*
 * The site's one table of values.
 *
 * Every other file in this directory says what a box is *for* - a panel, a rule,
 * a paragraph - and never how wide, how round or what colour. That is the whole
 * discipline the docs pages are held to by a CI gate rather than by review: a
 * page cannot keep the design system honest if a copied hex can land in it, and a
 * design system cannot be changed in one place if the number is stated in forty.
 *
 * Nothing here is mutable and nothing here is a global, because this header is
 * included by five translation units that one program links together - the shell
 * and four pages - and a `static` value in a header would be five copies that
 * agree only by accident. The driver fills one `site_view` and hands a pointer to
 * it down, so a page reads the same table the bar above it read.
 */

#include "cf/style.h"
#include "cf/text.h"

/*
 * The faces, named for the job rather than the size. Five, because a document
 * has five jobs to do with type: an eyebrow over a heading, the heading itself,
 * the title of a page, prose at the measure a reader sits at, and code in the one
 * family that lines a column of glyphs up.
 */
typedef enum site_face {
    SITE_FACE_EYEBROW,
    SITE_FACE_PROSE,
    SITE_FACE_HEAD,
    SITE_FACE_TITLE,
    SITE_FACE_MONO,
    SITE_FACE_COUNT
} site_face;

/* Point sizes. The framework lays out in logical points, so a number here is the
 * size on any display and the backend's density does the rest. */
#define EYEBROW_POINT 11.0f
#define PROSE_POINT 16.0f
#define HEAD_POINT 20.0f
#define TITLE_POINT 34.0f
#define MONO_POINT 14.0f

/*
 * The chrome's geometry, taken from the layout this site mirrors: a bar of a
 * stated height over a fixed sidebar beside a column of prose that stops growing
 * at a readable measure. None of these is a suggestion the page may overrule - a
 * second number for the same edge is how a layout starts to disagree with itself.
 */
#define BAR_HEIGHT 64.0f
#define BAR_PADDING 24.0f
#define BAR_ITEM_GAP 8.0f
/* The one width every rule on the page is drawn at - the bar's bottom, the
 * sidebar's edge, the outline of an external-link mark. A theme states the same
 * number on a card's border; this is the same hairline, named once. */
#define HAIRLINE 1.0f
#define SIDEBAR_WIDTH 260.0f
#define SIDEBAR_PADDING 16.0f
#define SIDEBAR_GAP 20.0f
/* The active row's rule. Two points is the narrowest line that survives a
 * density of one and still reads as a marker rather than a border. */
#define NAV_RULE_WIDTH 2.0f
#define NAV_ROW_PADDING 10.0f
/* The measure prose stops at, and the room it gets to sit in. A line of a
 * hundred characters is not a line a reader tracks back along. */
#define CONTENT_WIDTH 760.0f
#define CONTENT_PADDING 32.0f
#define CONTENT_GAP 20.0f
/* One block of prose under the next, and one line of code under the last. */
#define BLOCK_GAP 12.0f
#define PANEL_PADDING 16.0f
#define PANEL_LINE_GAP 2.0f
#define CARD_PADDING 16.0f
#define CARD_GAP 12.0f
/* The narrowest a card gets before the row gives it a line of its own. This is
 * the one layout number stated as a length rather than as a count: a grid asks how
 * many columns a card wants and the point below which two columns stop being
 * readable is the same answer at any window width. */
#define CARD_MIN_WIDTH 240.0f
/*
 * The card's preview band: the height a chapter's own shape is drawn at, the boxes
 * it is drawn out of and the space between them. A preview says what a chapter is
 * made of at one scale for every card rather than at the scale the chapter runs at
 * - the gallery is the page for that - so the numbers belong to the block.
 */
#define PREVIEW_HEIGHT 64.0f
#define PREVIEW_ITEM 16.0f
#define PREVIEW_TALL 32.0f
#define PREVIEW_LINE 6.0f
#define PREVIEW_GAP 6.0f
#define PREVIEW_COUNT 6u
/* The first column of a word table: as wide as the longest word the table defines,
 * so every meaning starts on the same edge without a column count in the page. */
#define WORD_WIDTH 120.0f
/* The same column where the left side holds a symbol rather than a word. A name in
 * the API table is longer than any word in the glossary, and a table whose first
 * column wraps is a table that has stopped aligning anything. */
#define TERM_WIDTH 200.0f
/* The mark's square and the two columns drawn inside it. Stated as points of the
 * bar, so the mark is the same object at any window size. */
#define MARK_SIZE 28.0f
#define MARK_STROKE 4.0f

/*
 * The bound a tab block's caller cannot cross, stated here because it is a
 * property of the block rather than of the page using it: a page whose install
 * routes outnumber these tabs has stopped being an installation page. A panel of
 * code has no such bound - the rows it holds are allocated from the frame, so a
 * command list is as long as the commands are.
 */
#define SITE_TAB_COUNT 6u
/* The rows one sidebar group can hold. Longer than the table has today, so a
 * page added to a group still lands and a group that outgrows it is a group that
 * needs splitting rather than a number to bump. */
#define SITE_GROUP_ROWS 12u

/* The size a face is read at. One table, so the driver that opens the fonts and
 * the page that names the face cannot disagree about what title means. */
static inline float site_face_point(site_face face) {
    switch (face) {
    case SITE_FACE_EYEBROW:
        return EYEBROW_POINT;
    case SITE_FACE_PROSE:
        return PROSE_POINT;
    case SITE_FACE_HEAD:
        return HEAD_POINT;
    case SITE_FACE_TITLE:
        return TITLE_POINT;
    case SITE_FACE_MONO:
        return MONO_POINT;
    case SITE_FACE_COUNT:
        break;
    }

    return PROSE_POINT;
}

/* Which of the two files a face comes from: code in the monospaced one, every
 * piece of prose in the other. */
static inline bool site_face_mono(site_face face) {
    return face == SITE_FACE_MONO;
}

/* Box edges all the same, because a block that pads one side and not the other
 * has stated an alignment problem rather than solved one. */
static inline cf_box site_pad(float points) {
    return (cf_box){CF_PT(points), CF_PT(points), CF_PT(points), CF_PT(points)};
}

/*
 * The boxes and the tones a page paints with, under the words a page uses. A
 * field here is a theme slot renamed, and the two exceptions say so where they
 * stand.
 */
typedef struct site_view {
    /* Surfaces, each read from the theme's own box. */
    cf_style surface; /* the field behind the whole page */
    cf_style chrome;  /* the top bar, the sidebar, a note */
    cf_style card;    /* a callout, a showcase tile */
    cf_style accent;  /* the primary action's box, used for the mark */
    cf_style panel;   /* a code panel: the one box the theme has no slot for */

    /* Tones. */
    cf_color prose;     /* the ink a body of text reads in */
    cf_color aside;     /* secondary text: an eyebrow, a caption, a nav row */
    cf_color on_accent; /* the tone text carries over the accent */
    cf_color on_panel;  /* the tone code carries over the panel */

    /* The states a nav row wears, read from the family a nav row belongs to. */
    const cf_variants *nav;

    /* Leaving the document. A page names an address and never a platform: the
     * driver decides whether this build has a browser to ask, and a build with
     * none either leaves the pointer unset or points it at the answer that does
     * nothing. Either way the page has said something true about the link and
     * nothing about the machine showing it. */
    void (*open_url)(const char *url);

    /* The faces the driver opened, and the stacks that walk them. Filled by
     * site_view_bind; nobody else decides whether a stack has fonts. */
    cf_font fonts[SITE_FACE_COUNT];
    cf_text_stack faces[SITE_FACE_COUNT];
} site_view;

/*
 * Fills the boxes and the tones from the design system over the fonts the caller
 * opened - one per face, at `site_face_point`, because a font carries its size
 * and no layer above the backend resizes one. The handles are borrowed: whoever
 * opened them keeps them open for the run and closes them after it, and a face
 * whose font did not open simply walks no glyphs, which paints no text rather
 * than a wrong one.
 */
static inline void site_view_bind(site_view *view, const cf_font *fonts) {
    const cf_theme *theme = cf_theme_default();

    view->surface = theme->page;
    view->chrome = theme->bar;
    view->card = theme->body;
    view->prose = theme->ink;
    view->aside = theme->muted;
    view->on_accent = theme->on_ink;
    view->nav = &theme->variants[CF_ROLE_BUTTON];

    /* The mark wears the accent the theme hands its primary action, with the
     * button's padding off: a mark is a shape, not something to press. */
    view->accent = theme->variants[CF_ROLE_BUTTON].base;
    view->accent.padding = site_pad(0.0f);
    view->accent.margin = site_pad(0.0f);
    view->accent.border_color.present = false;

    /*
     * A code panel is a dark box on a light page, and no slot of a theme answers
     * that: cf_theme carries the boxes a *control* paints - a field, a card, a
     * bar - and a code block is prose furniture, not a widget family. So the
     * panel takes the accent's own tone where the theme states one, which is
     * every box on the page that is already dark, and the neutral that tone is
     * made of where it does not. If wanting a panel becomes wanting a slot, the
     * slot is appended - a MINOR - and this is the only line that goes away.
     */
    view->panel = theme->body;
    view->panel.background = theme->variants[CF_ROLE_BUTTON].base.background.present
                                 ? theme->variants[CF_ROLE_BUTTON].base.background
                                 : (cf_opt_color){.present = true, .value = CF_COLOR(23, 23, 23)};
    view->panel.border = site_pad(0.0f);
    view->panel.border_color = view->panel.background;
    view->panel.padding = site_pad(PANEL_PADDING);
    view->panel.row_gap = CF_PT(PANEL_LINE_GAP);
    view->on_panel = theme->on_ink;

    /* The stacks, one font each: a docs page has one family for prose and one for
     * code, so a run neither can draw is a font the fetch skipped rather than a
     * glyph the page wanted and did not have. */
    for (uint32_t i = 0u; i < SITE_FACE_COUNT; ++i) {
        view->fonts[i] = fonts[i];
        view->faces[i] = (cf_text_stack){.fonts = &view->fonts[i],
                                         .count = view->fonts[i].ops != NULL ? 1u : 0u};
    }
}

#endif
