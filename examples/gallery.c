/*
 * One page that reads top to bottom: a rule of the framework, the words that
 * say what it does, and the box underneath that lets a hand check it. The
 * document is a single scroller, so the wheel is the only navigation.
 */
#include <stdio.h>

#include "cf/app.h"
#include "cf/build.h"
#include "cf/widget.h"

#define WINDOW_WIDTH 900u
#define WINDOW_HEIGHT 620u

/* Where the drag stops. Every chapter reads at this width and the sweep of them
 * overflows nothing; below it a stated number - a chapter number beside its
 * title, a box with padding on both sides - has less line than the smallest
 * piece it is made of, and the page can only be cut or pushed out. */
#define WINDOW_MIN_WIDTH 360u
#define WINDOW_MIN_HEIGHT 240u

/* The document's own measure: one column of chapters, each a card, each with
 * the room a heading, a paragraph and something to try need. */
#define PAGE_PADDING 24.0f
#define CHAPTER_GAP 20.0f
#define CARD_PADDING 16.0f
#define CARD_GAP 12.0f

/* The widest the document grows. A window broader than this gets the page
 * centred in it rather than a paragraph a mile long: filling every pixel is what
 * a responsive layout does at the sizes a reader sits at, not past them. */
#define PAGE_MAX_WIDTH 900.0f

/* The panel's geometry is stated rather than measured: the thumb has to be
 * placed by the numbers the scroller will read, and no layout has run when the
 * build declares it. */
#define PANEL_VIEWPORT 180.0f
#define PANEL_ROW 60.0f
#define PANEL_ROWS 8u
#define PANEL_TRACK 10.0f

#define LIST_VIEWPORT 200.0f
#define LIST_ROW 34.0f
#define LIST_ROWS 40u

/* Four boxes, in a strip tall enough for the cross axis to have something to
 * do with them. */
#define STRIP_HEIGHT 200.0f
#define STRIP_ITEM 40.0f
#define STRIP_BOXES 4u

/* An empty buffer measures no line, so the field has to state the height it
 * keeps open until something is typed into it. */
#define FIELD_HEIGHT 34.0f

/* What the text demo pinches its well to. Stated in points rather than as a
 * percentage: a percentage declared inside a measuring pass waits for the
 * arranging one, so an auto-height ancestor books the height of the wider run
 * and paints the narrower, and the last lines land outside the box. */
#define TEXT_NARROW 380.0f

/* The floor a grid cell keeps for itself, so a track the content does not fill
 * is still a box a reader can see the edge of. */
#define GRID_CELL 44.0f

/* The lane the puck crosses and how long a run of it takes. The lane's width is
 * stated in points rather than stretched because the build places the puck by
 * the number the tween holds, and no layout has run to say how wide anything
 * is when the declaration is made. */
#define SLIDE_TRAVEL 360.0f
#define SLIDE_PUCK 40.0f
#define SLIDE_SECONDS 0.9f

/* What a drag keeps between the press that starts it and the moves that
 * follow: where the hand took hold of the thumb, and the offset then. */
typedef struct thumb_drag {
    bool active;
    cf_point grab;
    float offset;
} thumb_drag;

static const cf_theme *theme;
static cf_font body_font;
static cf_font title_font;
static cf_text_stack body;
static cf_text_stack title;
static cf_image_content logo;

/* The buffer the field edits in place: its extent is the build's to own, so it
 * outlives every frame that declares the field. */
static char typed_bytes[64];
static cf_text_edit typed = {.chars = typed_bytes, .capacity = sizeof typed_bytes};

/* Eight panel boxes and eight list rows are ever named at once, and a ring of
 * eight each never rewrites bytes a label in the same frame still points at.
 * The two keep separate rings because both declare in one frame. */
static char row_bytes[8u][16u];
static char panel_bytes[8u][16u];
/* Generated, not fetched: the repo carries no binary it can regenerate. */
static uint8_t checker[8u * 8u * 4u];

/* The two lines that report state, each into its own buffer: the frame that
 * writes the bytes is the frame that borrows them. */
static char status_bytes[32];
static char echo_bytes[96];
static char list_bytes[64];
static char motion_bytes[96];

/*
 * The eight rules a chip picks. The names are what the chips read and the
 * numbers what the strip reads: one list, written twice, so a rule added to
 * one belongs beside the same word in the other.
 */
enum {
    RULE_ROW,
    RULE_COLUMN,
    RULE_WRAP,
    RULE_GAP,
    RULE_RADIUS,
    RULE_CLAMP,
    RULE_ALIGN,
    RULE_OVERLAY
};

static const char *rule_names[] = {"row",    "column", "wrap",  "gap",
                                   "radius", "clamp",  "align", "overlay"};
#define RULE_COUNT (uint32_t)(sizeof rule_names / sizeof rule_names[0])

/* The state each chapter reads back: keyed blocks in the arena that outlives
 * the frame, so a rebuild cannot lose what a click wrote. */
static uint32_t *click_count(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "clicks"), sizeof(uint32_t));
}

static bool *switch_off(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "off"), sizeof(bool));
}

static bool *narrow_text(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "narrow"), sizeof(bool));
}

static bool *badge_in_flow(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "flow"), sizeof(bool));
}

static uint32_t *picked_rule(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "rule"), sizeof(uint32_t));
}

static uint32_t *picked_plan(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "plan"), sizeof(uint32_t));
}

static uint32_t *picked_ease(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "ease"), sizeof(uint32_t));
}

/* The one tween the motion chapter runs, in the keyed block that outlives the
 * frame it was aimed in. */
static cf_tween *slide_tween(cf_ctx *ctx) {
    return cf_tween_of(ctx, cf_intern_text(&ctx->names, "slide"));
}

/* Which end the tween is aimed at: the block carries the answer, so neither the
 * press that aims it nor the label that says so keeps a second copy. */
static bool aimed_far(const cf_tween *tween) {
    return tween != NULL && tween->to >= SLIDE_TRAVEL;
}

/* How many rows the list's factory was asked for this frame, read back under
 * the list as the virtualization it is meant to show. */
static uint32_t *rows_built(cf_ctx *ctx) {
    return cf_state(ctx, cf_intern_text(&ctx->names, "built"), sizeof(uint32_t));
}

static void count_click(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot, (void)user_data;
    uint32_t *clicks = click_count(ctx);

    if (clicks != NULL) {
        *clicks += 1u;
    }
}

static void clear_count(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot, (void)user_data;
    uint32_t *clicks = click_count(ctx);

    if (clicks != NULL) {
        *clicks = 0u;
    }
}

/* One handler for every switch on the page: the block it turns over arrives as
 * the user pointer rather than as a name to look up. A state block lives in the
 * arena that outlives the frame, so it is a stable address to hand a button. */
static void flip(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)ctx, (void)slot;
    bool *held = user_data;

    if (held != NULL) {
        *held = !*held;
    }
}

/* The rule arrives as its index: a pointer wide enough for the number, so the
 * eight chips share one handler and the file keeps no table to look it up in. */
static void pick_rule(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot;
    uint32_t *rule = picked_rule(ctx);

    if (rule != NULL) {
        *rule = (uint32_t)(uintptr_t)user_data;
    }
}

static void pick_plan(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot;
    uint32_t *plan = picked_plan(ctx);

    if (plan != NULL) {
        *plan = (uint32_t)(uintptr_t)user_data;
    }
}

/* The chips carry the cf_ease ordinals, which are the numbers they select. */
static void pick_ease(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot;
    uint32_t *held = picked_ease(ctx);

    if (held != NULL) {
        *held = (uint32_t)(uintptr_t)user_data;
    }
}

/* One press aims the tween at whichever end it is not already holding: the
 * target is the number the block carries, so the chapter keeps no second copy of
 * it to fall out of step with. */
static void send_it(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)slot, (void)user_data;
    cf_tween *tween = slide_tween(ctx);
    const uint32_t *held = picked_ease(ctx);

    if (tween == NULL) {
        return;
    }

    cf_tween_to(tween, aimed_far(tween) ? 0.0f : SLIDE_TRAVEL, SLIDE_SECONDS,
                held != NULL ? (cf_ease)*held : CF_EASE_LINEAR);
}

/* The scrollbar's whole geometry in one place, so the thumb the build places
 * and the offset the drag writes are the same box's two readings. */
static cf_scrollbar panel_bar(void) {
    return (cf_scrollbar){
        .viewport = PANEL_VIEWPORT,
        .content = (float)PANEL_ROWS * PANEL_ROW,
        .track = {.x = 0.0f, .y = 0.0f, .width = PANEL_TRACK, .height = PANEL_VIEWPORT}};
}

static cf_scroll *panel_scroll(cf_ctx *ctx) {
    return cf_scroll_of(ctx, cf_intern_text(&ctx->names, "panel"));
}

/* The thumb holds the pointer from its press to its release, so the drag keeps
 * arriving here while the hand travels past the track. */
static void drag_thumb(cf_event *event, void *user_data) {
    cf_ctx *ctx = user_data;
    cf_scroll *held = panel_scroll(ctx);
    thumb_drag *drag = cf_state(ctx, cf_intern_text(&ctx->names, "thumb"), sizeof *drag);

    if (held == NULL || drag == NULL) {
        return;
    }

    if (event->type == CF_EVENT_POINTER_DOWN) {
        drag->grab = event->position;
        drag->offset = held->offset;
        drag->active = true;
        cf_capture(event);
        return;
    }

    if (event->type == CF_EVENT_POINTER_UP) {
        drag->active = false;
        return;
    }

    if (event->type == CF_EVENT_POINTER_MOVE && drag->active) {
        held->offset = cf_scroll_drag(panel_bar(), drag->grab, event->position, drag->offset);
    }
}

/* A surface with its own inner rhythm: the cards and wells differ only in the
 * theme box they start from and the space they keep. */
static cf_style inset_style(cf_style base, float padding, float gap) {
    cf_style style = base;
    style.padding = (cf_box){CF_PX(padding), CF_PX(padding), CF_PX(padding), CF_PX(padding)};
    style.row_gap = CF_PX(gap);

    return style;
}

/* The column every chapter's demo is: one thing under the next, with the
 * reading tone for whatever the demo says rather than the theme's. */
static cf_node demo_column(cf_ctx *ctx, float gap, const cf_node *children, uint32_t count) {
    cf_style column = cf_style_default();
    column.row_gap = CF_PX(gap);

    return cf_col(ctx, column, children, count);
}

/* A row of things a narrow window cannot hold side by side: what does not fit
 * the line comes round onto the next one instead of overhanging the card. The gap
 * is stated on both axes because a row that wraps needs the cross one as much as
 * the main. */
static cf_style wrap_row(float gap) {
    cf_style row = cf_style_default();
    row.wrap = CF_WRAP;
    row.column_gap = CF_PX(gap);
    row.row_gap = CF_PX(gap);
    row.align_items = CF_ALIGN_START;

    return row;
}

/* One line of text that takes the width its container gives it and breaks as
 * often as that width asks - the measure, not a guess, sizes the box. */
static cf_node make_prose(cf_ctx *ctx, const char *chars, cf_color ink) {
    cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

    if (content == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    *content = (cf_text_content){.stack = body, .text = cf_string_of(chars)};

    cf_style style = cf_style_default();
    style.color = (cf_opt_color){.present = true, .value = ink};

    return (cf_node){.style = style, .measure = cf_text_measure, .measure_data = content};
}

/* The same walk capped: the content counts the lines it will let through and
 * the paint walk closes the last one with the ellipsis it asked for. */
static cf_node make_capped(cf_ctx *ctx, const char *chars, uint32_t lines, cf_color ink) {
    cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

    if (content == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    *content = (cf_text_content){.stack = body,
                                 .text = cf_string_of(chars),
                                 .max_lines = {.present = true, .value = lines},
                                 .ellipsis = true};

    cf_style style = cf_style_default();
    style.color = (cf_opt_color){.present = true, .value = ink};

    return (cf_node){.style = style, .measure = cf_text_measure, .measure_data = content};
}

static cf_node make_button(cf_ctx *ctx, const char *name, cf_on_press on_press, void *user_data) {
    const cf_node label = cf_label(ctx, cf_string_of(name), &body, theme->on_ink);

    return cf_button(ctx, name, on_press, user_data, &label, 1u);
}

/* One chip of a picker row: the chosen one wears the accent and the rest the
 * bar, and what pressing it selects arrives as the button's own user pointer -
 * a number small enough to fit in one, so the chapter keeps no table to look
 * the choice up in. */
static cf_node make_chip(cf_ctx *ctx, const char *name, bool chosen, cf_on_press on_press,
                         uint32_t value) {
    const cf_node label =
        cf_label(ctx, cf_string_of(name), &body, chosen ? theme->on_ink : theme->muted);
    cf_node chip = cf_button(ctx, name, on_press, (void *)(uintptr_t)value, &label, 1u);

    if (!chosen) {
        /* A bar rather than the accent: an unselected chip is still a press
         * target, it is just not the answer. */
        chip.style.background = theme->bar.background;
        chip.style.border_color = theme->bar.border_color;
    }

    return chip;
}

/* A chapter: its number beside its title on one line, the rule in prose under
 * it, and the thing that lets a hand check the prose at the bottom. */
static cf_node make_chapter(cf_ctx *ctx, const char *number, const char *name, const char *prose,
                            cf_node demo) {
    const cf_node mark = cf_label(ctx, cf_string_of(number), &body, theme->muted);
    const cf_node head = cf_label(ctx, cf_string_of(name), &title, theme->ink);

    cf_style line = cf_style_default();
    line.column_gap = CF_PX(10.0f);
    line.row_gap = CF_PX(2.0f);
    /* Two sizes on one line: the baseline is where their glyphs agree. The line
     * may break, so a title whose longest word no longer fits the card drops
     * under its number instead of running past the card's edge. */
    line.wrap = CF_WRAP;
    line.align_items = CF_ALIGN_BASELINE;

    const cf_node children[] = {
        cf_row(ctx, line, (cf_node[]){mark, head}, 2u),
        make_prose(ctx, prose, theme->muted),
        demo,
    };

    return cf_col(ctx, inset_style(theme->body, CARD_PADDING, CARD_GAP), children,
                  (uint32_t)(sizeof children / sizeof children[0]));
}

/* Chapter one: three buttons, and the count the first of them keeps. */
static cf_node make_buttons_demo(cf_ctx *ctx) {
    cf_node count = make_button(ctx, "count", count_click, NULL);
    const cf_node clear = make_button(ctx, "clear", clear_count, NULL);
    const cf_node off = make_button(ctx, "switch off", flip, switch_off(ctx));

    const bool *disabled = switch_off(ctx);

    if (disabled != NULL) {
        count.disabled = *disabled;
    }

    const uint32_t *clicks = click_count(ctx);
    const int length = snprintf(status_bytes, sizeof status_bytes, "clicked %u times",
                                clicks != NULL ? *clicks : 0u);

    /* Three buttons and a gap: what does not fit the line comes round onto the
     * next one, so the toolbar narrows with the window rather than leaving the
     * last button outside the card. */
    const cf_style row = wrap_row(8.0f);

    return demo_column(
        ctx, 8.0f,
        (cf_node[]){
            cf_row(ctx, row, (cf_node[]){count, clear, off}, 3u),
            cf_label(ctx, (cf_string){status_bytes, (uint32_t)length}, &body, theme->muted),
        },
        2u);
}

/* Chapter two: the field, and the same buffer read straight back. */
static cf_node make_field_demo(cf_ctx *ctx) {
    cf_node entry = cf_field(ctx, "entry", &typed, &body);
    entry.style.width = CF_PERCENT(100.0f);
    /* Stated rather than measured: a field with nothing in it would otherwise
     * be a hairline, and the reader cannot click a hairline. */
    entry.style.height = CF_PX(FIELD_HEIGHT);
    entry.style.align_items = CF_ALIGN_CENTER;

    const int length = snprintf(echo_bytes, sizeof echo_bytes, "the buffer holds %u bytes: %.*s",
                                typed.length, (int)typed.length, typed_bytes);

    return demo_column(
        ctx, 8.0f,
        (cf_node[]){
            entry,
            cf_label(ctx, (cf_string){echo_bytes, (uint32_t)length}, &body, theme->muted),
        },
        2u);
}

/* Chapter three: a paragraph that breaks itself, the same words capped, and the
 * button that changes the width they break at. */
static cf_node make_text_demo(cf_ctx *ctx) {
    static const char *paragraph =
        "A box that measures its own text breaks the run at spaces, hyphens and "
        "east-asian glyphs, trims what a break leaves at the edges of a line, and "
        "answers the same walk to the layout that paints it. The height the "
        "resolver sees is therefore the height that draws, never a guess at it - "
        "which is why a window resize re-breaks every line here instead of "
        "clipping the ones that ran past the edge.";

    static const char *capped =
        "The same walk capped at two lines: the third is never counted, and the "
        "second closes with the ellipsis the content asks for.";

    const bool *held = narrow_text(ctx);
    const bool pinched = held != NULL && *held;

    const cf_node text = make_prose(ctx, paragraph, theme->muted);

    const cf_node squeeze =
        make_button(ctx, pinched ? "give it the whole width" : "narrow it", flip, narrow_text(ctx));

    /* The pinch goes on the well, which is the box the card stretches or sizes,
     * and not on the measured text: the width the well is handed is the offer
     * the paragraph breaks against, so the lines and the height booked for them
     * are the ones that paint. See TEXT_NARROW for why the number is points. */
    cf_style well = inset_style(theme->bar, 12.0f, 10.0f);
    well.width = pinched ? CF_PX(TEXT_NARROW) : CF_AUTO;
    well.align_items = CF_ALIGN_START;

    const cf_node children[] = {text, squeeze, make_capped(ctx, capped, 2u, theme->ink)};

    return cf_col(ctx, well, children, (uint32_t)(sizeof children / sizeof children[0]));
}

/* The one field of the style the chip picks, stated differently. Everything
 * else about the strip is the box it was before the press. */
static void apply_rule(cf_style *strip, cf_style *item, uint32_t rule, uint32_t index) {
    switch (rule) {
    case RULE_COLUMN:
        strip->direction = CF_DIRECTION_COLUMN;
        break;
    case RULE_WRAP:
        strip->wrap = CF_WRAP;
        break;
    case RULE_GAP:
        strip->column_gap = CF_PX(32.0f);
        strip->row_gap = CF_PX(32.0f);
        break;
    case RULE_RADIUS:
        item->radius = 999.0f;
        break;
    case RULE_CLAMP:
        item->max_width = CF_PX(90.0f);
        break;
    case RULE_ALIGN:
        /* A cross size that is not stated is the one a container may stretch. */
        item->height = CF_AUTO;
        break;
    case RULE_OVERLAY:
        if (index + 1u == STRIP_BOXES) {
            item->position = CF_POSITION_ABSOLUTE;
            item->inset = (cf_box){CF_AUTO, CF_AUTO, CF_PX(0.0f), CF_PX(0.0f)};
        }
        break;
    default:
        break;
    }
}

static cf_node make_rule_strip(cf_ctx *ctx, uint32_t rule) {
    static const char *marks[] = {"a", "b", "c", "d"};

    /* The strip is a row of stated height, and its own box is the containing
     * block an overlay child leaves the flow to be placed against. */
    cf_style strip = cf_style_default();
    strip.height = CF_PX(STRIP_HEIGHT);
    strip.position = CF_POSITION_RELATIVE;
    strip.column_gap = CF_PX(8.0f);
    strip.row_gap = CF_PX(8.0f);

    cf_node boxes[STRIP_BOXES];

    for (uint32_t i = 0u; i < STRIP_BOXES; ++i) {
        cf_style item = inset_style(theme->body, 8.0f, 0.0f);
        /* Shared out rather than stated: a box fixed wider than the line leaves
         * the strip with nothing to distribute, and on the column rule it would
         * run past the well at any width the window is not. */
        item.flex_grow = 1.0f;
        item.height = CF_PX(STRIP_ITEM);
        item.justify_content = CF_JUSTIFY_CENTER;

        apply_rule(&strip, &item, rule, i);

        const cf_node label = cf_label(ctx, cf_string_of(marks[i]), &body, theme->ink);
        boxes[i] = cf_col(ctx, item, &label, 1u);
    }

    /* cf_row and cf_col state the direction by which they are called, so the
     * rule that picks the axis has to pick the factory as well. */
    const cf_node packed = rule == RULE_COLUMN ? cf_col(ctx, strip, boxes, STRIP_BOXES)
                                               : cf_row(ctx, strip, boxes, STRIP_BOXES);

    /* The well gives the strip a floor to be read against, and clips it: a box
     * that overhangs must not run down into the chapter below. */
    cf_style well = inset_style(theme->bar, 8.0f, 0.0f);
    well.clips_content = true;

    return cf_col(ctx, well, &packed, 1u);
}

/* Chapter four: eight chips, one strip. The chip that is picked is painted as
 * picked, so the choice reads without a caption for it. */
static cf_node make_rules_demo(cf_ctx *ctx) {
    const uint32_t *held = picked_rule(ctx);
    const uint32_t rule = held != NULL ? *held : RULE_ROW;

    cf_node chips[RULE_COUNT];

    for (uint32_t i = 0u; i < RULE_COUNT; ++i) {
        chips[i] = make_chip(ctx, rule_names[i], i == rule, pick_rule, i);
    }

    const cf_style row = wrap_row(8.0f);

    return demo_column(ctx, 12.0f,
                       (cf_node[]){cf_row(ctx, row, chips, RULE_COUNT), make_rule_strip(ctx, rule)},
                       2u);
}

/* Chapter five: a pane that walks inside the page that walks. */
static cf_node make_panel(cf_ctx *ctx) {
    cf_node rows[PANEL_ROWS];

    for (uint32_t i = 0u; i < PANEL_ROWS; ++i) {
        char *bytes = panel_bytes[i % 8u];
        const int length = snprintf(bytes, 16u, "box %u", i);

        const cf_node label =
            cf_label(ctx, (cf_string){bytes, (uint32_t)length}, &body, theme->ink);

        cf_style row = inset_style(theme->body, 12.0f, 0.0f);
        row.height = CF_PX(PANEL_ROW);
        /* The box runs a column, so the axis the label sits on is the main one:
         * centering it there is justify_content's, and leaving the cross axis to
         * stretch is what makes the side padding read instead of a centered run. */
        row.justify_content = CF_JUSTIFY_CENTER;
        row.flex_shrink = 0.0f;

        rows[i] = cf_col(ctx, row, &label, 1u);
    }

    cf_node panel = cf_scroller(ctx, "panel", rows, PANEL_ROWS);
    panel.style.height = CF_PX(PANEL_VIEWPORT);
    panel.style.flex_grow = 1.0f;

    const cf_scroll *held = panel_scroll(ctx);
    const cf_rect thumb = cf_scrollbar_thumb(panel_bar(), held != NULL ? held->offset : 0.0f);

    cf_style track = inset_style(theme->bar, 0.0f, 0.0f);
    track.width = CF_PX(PANEL_TRACK);
    track.height = CF_PX(PANEL_VIEWPORT);
    track.radius = 3.0f;

    cf_style handle = cf_style_default();
    handle.width = CF_PERCENT(100.0f);
    handle.height = CF_PX(thumb.height);
    /* The thumb rides the track's axis by its own margin: the distance the
     * math measured, stated as the box's offset rather than a position. */
    handle.margin.top = CF_PX(thumb.y);
    handle.flex_shrink = 0.0f;
    handle.radius = 3.0f;
    handle.background = (cf_opt_color){.present = true, .value = theme->muted};

    cf_node grip = cf_col(ctx, handle, NULL, 0u);
    grip.handlers.pointer = drag_thumb;
    grip.handlers.pointer_data = ctx;

    return CF_ROW(ctx, cf_style_default(), panel, cf_col(ctx, track, &grip, 1u));
}

/* The list declares only the rows the window can show; the row carries the
 * list role so the theme paints the one the pointer is on. */
static cf_node make_row(cf_ctx *ctx, uint32_t index, void *user_data) {
    (void)user_data;
    char *bytes = row_bytes[index % 8u];
    const int length = snprintf(bytes, 16u, "row %u", index);

    /* The factory is called once per row the list decides to build, so counting
     * the calls reads the virtualization back off the frame instead of
     * asserting it in prose. */
    uint32_t *built = rows_built(ctx);

    if (built != NULL) {
        *built += 1u;
    }

    const cf_node label = cf_label(ctx, (cf_string){bytes, (uint32_t)length}, &body, theme->ink);

    cf_style row = cf_style_default();
    /* The list holds every row to the height it was given, so the padding only
     * has to clear the axis the label runs along. */
    row.padding.left = CF_PX(12.0f);
    row.padding.right = CF_PX(12.0f);
    /* The row box is a column: the label rides inside it on the main axis, so
     * that is the axis the leftover height is distributed on. */
    row.justify_content = CF_JUSTIFY_CENTER;

    cf_node box = cf_col(ctx, row, &label, 1u);
    box.role = CF_ROLE_LIST;
    return box;
}

/* Chapter six: the virtualized list, with what the frame built written under
 * it. The count is cleared before the list is declared and read after it, so
 * the number is this frame's and not the last one's. */
static cf_node make_list_demo(cf_ctx *ctx) {
    uint32_t *built = rows_built(ctx);

    if (built != NULL) {
        *built = 0u;
    }

    const cf_node list = cf_list(ctx, "rows", LIST_VIEWPORT, LIST_ROW, LIST_ROWS, make_row, NULL);
    const int length =
        snprintf(list_bytes, sizeof list_bytes, "%u of %u rows declared by this frame",
                 built != NULL ? *built : 0u, LIST_ROWS);

    return demo_column(
        ctx, 10.0f,
        (cf_node[]){
            list,
            cf_label(ctx, (cf_string){list_bytes, (uint32_t)length}, &body, theme->muted),
        },
        2u);
}

/* Chapter seven: a texture, and the badge that leaves the flow to hang off it. */
static cf_node make_badge_demo(cf_ctx *ctx) {
    cf_node image = cf_image(ctx, "logo", &logo);
    image.style.width = CF_PX(64.0f);
    image.style.height = CF_PX(64.0f);

    const bool *held = badge_in_flow(ctx);
    const bool in_flow = held != NULL && *held;

    cf_style badge = theme->variants[CF_ROLE_BUTTON].base;
    badge.padding = (cf_box){CF_PX(6.0f), CF_PX(2.0f), CF_PX(6.0f), CF_PX(2.0f)};
    badge.radius = 8.0f;

    if (!in_flow) {
        /* Out of the flow and pinned past the corner: the negative insets are
         * the half of the box that hangs outside the box it belongs to. */
        badge.position = CF_POSITION_ABSOLUTE;
        badge.inset = (cf_box){CF_AUTO, CF_PX(-8.0f), CF_PX(-8.0f), CF_AUTO};
    }

    const cf_node tag = cf_label(ctx, cf_string_of("v1"), &body, theme->on_ink);
    cf_node badge_box = cf_col(ctx, badge, &tag, 1u);
    badge_box.role = CF_ROLE_BUTTON;

    /* The column the image and the badge share is the containing block: pin the
     * badge to anything else and it hangs off the row, not off the logo. The gap
     * and the bottom room are for both readings - the in-flow badge sits off the
     * logo rather than flush to it, and the overhanging one hangs into space the
     * box keeps for it instead of into the card's padding. */
    cf_style over = cf_style_default();
    over.position = CF_POSITION_RELATIVE;
    over.align_items = CF_ALIGN_START;
    over.row_gap = CF_PX(10.0f);
    over.padding.bottom = CF_PX(8.0f);

    const cf_node flip_button =
        make_button(ctx, in_flow ? "let it leave the flow" : "put it back in the flow", flip,
                    badge_in_flow(ctx));

    cf_style row = wrap_row(24.0f);
    /* The button sits level with the middle of what it acts on, not with the
     * first line of it. */
    row.align_items = CF_ALIGN_CENTER;

    return CF_ROW(ctx, row, cf_col(ctx, over, (cf_node[]){image, badge_box}, 2u), flip_button);
}

/* The three track plans the grid chapter switches between, named for what each
 * one declares: a written set of columns, a pattern the helper repeats, or no
 * columns at all. */
enum { PLAN_COLUMNS, PLAN_REPEAT, PLAN_FREE };

static const char *plan_names[] = {"three tracks", "repeated pair", "no tracks"};
#define PLAN_COUNT (uint32_t)(sizeof plan_names / sizeof plan_names[0])

/* Chapter eight: the six cells the container places. What an item says about the
 * grid is only how many tracks it takes; which tracks there are is the
 * container's declaration. */
static cf_node make_grid_cell(cf_ctx *ctx, const char *mark, uint32_t column_span,
                              uint32_t row_span) {
    cf_style cell = inset_style(theme->body, 8.0f, 0.0f);
    cell.min_height = CF_PX(GRID_CELL);
    cell.justify_content = CF_JUSTIFY_CENTER;
    cell.align_items = CF_ALIGN_CENTER;
    /* Zero is the grid's own auto: one track, at whatever cell the placement
     * cursor had reached. The cells that state a number are the chapter. */
    cell.column_span = column_span;
    cell.row_span = row_span;

    const cf_node label = cf_label(ctx, cf_string_of(mark), &body, theme->ink);

    return cf_col(ctx, cell, &label, 1u);
}

/* Room for the widest declaration a plan makes, and for the two rows every
 * plan but the last declares. */
#define GRID_COLUMNS 4u
#define GRID_ROWS 2u

static cf_node make_grid_demo(cf_ctx *ctx) {
    const uint32_t *held = picked_plan(ctx);
    const uint32_t plan = held != NULL ? *held : PLAN_COLUMNS;

    /* The engine reads a flat array and a count, so a pattern handed to the
     * helper and a list written out by longhand are one declaration by the time
     * the pass runs. That pass is not this function's: an array riding this
     * stack leaves the grid sizing whatever the pointer reached once the frame
     * unwound, so both arrays come out of the arena the pass still reads. */
    cf_track *tracks = cf_alloc(&ctx->frame, (GRID_COLUMNS + GRID_ROWS) * sizeof *tracks);

    if (tracks == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    cf_track *columns = tracks;
    cf_track *rows = tracks + GRID_COLUMNS;
    uint32_t column_count = 0u;

    if (plan == PLAN_COLUMNS) {
        columns[0] = (cf_track){CF_PX(120.0f), CF_PX(120.0f)};
        columns[1] = (cf_track){CF_PX(64.0f), CF_FR(1.0f)};
        columns[2] = (cf_track){CF_AUTO, CF_FR(2.0f)};
        column_count = 3u;
    } else if (plan == PLAN_REPEAT) {
        /* The pattern is the one array here the helper only reads: it copies
         * what it is given into `columns` and never holds the pointer. */
        const cf_track pattern[] = {{CF_AUTO, CF_FR(1.0f)}, {CF_PX(72.0f), CF_PX(72.0f)}};

        column_count = cf_tracks_repeat(columns, GRID_COLUMNS, pattern, 2u, 2u);
    }

    for (uint32_t row = 0u; row < GRID_ROWS; ++row) {
        rows[row] = (cf_track){CF_AUTO, CF_AUTO};
    }

    /* Two rows are written and six cells want more, so the rest land in rows the
     * grid adds for them as content-sized tracks of their own. */
    cf_style grid = inset_style(theme->bar, 12.0f, 12.0f);
    grid.display = CF_DISPLAY_GRID;
    grid.width = CF_PERCENT(100.0f);
    /* Auto tracks are sized by what they hold and CSS lets a grid whose plans do
     * not fit overflow - a declared 120 point column stays 120 points wide. Cutting
     * here is what keeps the last column from being painted over the chapter that
     * follows instead of ending at the card's edge. */
    grid.clips_content = true;
    /* The helper states the rhythm between rows only, and a grid whose cells
     * touch side to side reads as one box with lines drawn in it. */
    grid.column_gap = CF_PX(12.0f);
    grid.column_tracks = column_count != 0u ? columns : NULL;
    grid.column_count = column_count;
    grid.row_tracks = plan == PLAN_FREE ? NULL : rows;
    grid.row_count = plan == PLAN_FREE ? 0u : GRID_ROWS;

    const cf_node cells[] = {
        make_grid_cell(ctx, "two columns", 2u, 0u), make_grid_cell(ctx, "auto", 0u, 0u),
        make_grid_cell(ctx, "auto", 0u, 0u),        make_grid_cell(ctx, "two rows", 0u, 2u),
        make_grid_cell(ctx, "auto", 0u, 0u),        make_grid_cell(ctx, "auto", 0u, 0u),
    };

    cf_node chips[PLAN_COUNT];

    for (uint32_t i = 0u; i < PLAN_COUNT; ++i) {
        chips[i] = make_chip(ctx, plan_names[i], i == plan, pick_plan, i);
    }

    const cf_style row = wrap_row(8.0f);

    return demo_column(
        ctx, 12.0f, (cf_node[]){cf_row(ctx, row, chips, PLAN_COUNT), cf_row(ctx, grid, cells, 6u)},
        2u);
}

/* The three curves a run of the lane may take, named as the chips read and
 * numbered in the order cf_ease numbers them. */
static const char *ease_names[] = {"linear", "ease out", "ease in out"};
#define EASE_COUNT (uint32_t)(sizeof ease_names / sizeof ease_names[0])

/* Chapter nine: one number, restated by every frame that moves it. The press
 * aims the tween and the build steps it, which is the whole division between an
 * event and an animation: nothing on the input side is left running once the
 * button has been let go. */
static cf_node make_motion_demo(cf_ctx *ctx) {
    cf_tween *tween = slide_tween(ctx);
    bool travelling = false;
    float at = 0.0f;

    if (tween != NULL) {
        at = cf_tween_step(tween, cf_frame_seconds(ctx));

        /* The step clamps what it advances, so the only reading left is whether
         * it ran out: the frame that lands the value is the last one the page
         * has a reason to ask for. */
        travelling = tween->elapsed < tween->duration;

        if (travelling) {
            cf_request_frame(ctx);
        }
    }

    /* The lane both carries the puck and cuts it: a puck past the edge is
     * clipped rather than painted over the chapter below. Its width is the card's
     * up to the length the tween runs, so a thin window shortens the track instead
     * of pushing it off the screen, and the right padding is the room the puck
     * needs at the far end - the full run lands flush with the border rather than
     * half past it. */
    cf_style lane = inset_style(theme->bar, 0.0f, 0.0f);
    lane.width = CF_PERCENT(100.0f);
    lane.max_width = CF_PX(SLIDE_TRAVEL + SLIDE_PUCK);
    lane.padding.right = CF_PX(SLIDE_PUCK);
    lane.height = CF_PX(SLIDE_PUCK);
    lane.clips_content = true;

    cf_style puck = theme->variants[CF_ROLE_BUTTON].base;
    puck.width = CF_PX(SLIDE_PUCK);
    puck.height = CF_PX(SLIDE_PUCK);
    puck.radius = 8.0f;
    puck.flex_shrink = 0.0f;
    /* The one number, as a share of the track the card was able to give it. A
     * percentage margin is measured against that track, so the puck keeps its
     * size and moves the whole way whatever the width turns out to be. */
    puck.margin.left = CF_PERCENT(at / SLIDE_TRAVEL * 100.0f);

    /* The same number read the other way, as a share of the width the card
     * happens to have. Both boxes now follow the window: what differs is the
     * lever - a margin that moves a fixed box along the lane against a width that
     * grows from the lane's own start. */
    cf_style meter = inset_style(theme->bar, 0.0f, 0.0f);
    meter.width = CF_PERCENT(100.0f);
    meter.height = CF_PX(12.0f);

    /* The ink of a button and none of its padding: a box states its border box,
     * so a 12-point track wearing 8 points of padding top and bottom cannot be
     * 12 points tall, and a share of a track that has to leave 24 points for
     * nothing is a fill that shows progress when the tween has none. */
    cf_style fill = inset_style(theme->variants[CF_ROLE_BUTTON].base, 0.0f, 0.0f);
    fill.width = CF_PERCENT(at / SLIDE_TRAVEL * 100.0f);

    const uint32_t *held = picked_ease(ctx);
    const uint32_t ease = held != NULL ? *held : (uint32_t)CF_EASE_LINEAR;

    cf_node chips[EASE_COUNT];

    for (uint32_t i = 0u; i < EASE_COUNT; ++i) {
        chips[i] = make_chip(ctx, ease_names[i], i == ease, pick_ease, i);
    }

    const cf_style row = wrap_row(8.0f);

    const int length =
        snprintf(motion_bytes, sizeof motion_bytes, "%.0f of %.0f points, %s", at, SLIDE_TRAVEL,
                 travelling ? "and the page asked for another frame" : "and the page is at rest");

    const cf_node parts[] = {
        cf_row(ctx, lane, (cf_node[]){(cf_node){.style = puck}}, 1u),
        cf_row(ctx, meter, (cf_node[]){(cf_node){.style = fill}}, 1u),
        cf_row(ctx, row, chips, EASE_COUNT),
        make_button(ctx, aimed_far(tween) ? "bring it back" : "send it", send_it, NULL),
        cf_label(ctx, (cf_string){motion_bytes, (uint32_t)length}, &body, theme->muted),
    };

    return demo_column(ctx, 12.0f, parts, (uint32_t)(sizeof parts / sizeof parts[0]));
}

static cf_node make_note(cf_ctx *ctx, const char *chars) {
    return CF_COL(ctx, inset_style(theme->bar, 12.0f, 0.0f), make_prose(ctx, chars, theme->muted));
}

static cf_node make_header(cf_ctx *ctx) {
    const cf_node name = cf_label(ctx, cf_string_of("cform gallery"), &title, theme->ink);
    const cf_node note =
        cf_label(ctx, cf_string_of("nine rules, each with something to try"), &body, theme->muted);

    cf_style row = cf_style_default();
    row.justify_content = CF_JUSTIFY_SPACE_BETWEEN;
    row.align_items = CF_ALIGN_BASELINE;

    return cf_row(ctx, row, (cf_node[]){name, note}, 2u);
}

/* The page is the document: one scroller over the chapters, in the field's own
 * tone and with no corner. The margins ride the column the chapters sit in
 * rather than the window they scroll through, because the range a scroller
 * walks is its content's height - padding on the viewport would be there at
 * the top of the document and gone at the bottom of it. */
static cf_node make_page(cf_ctx *ctx, const cf_node *chapters, uint32_t count) {
    cf_style sheet = inset_style(theme->page, PAGE_PADDING, CHAPTER_GAP);
    sheet.width = CF_PERCENT(100.0f);
    sheet.max_width = CF_PX(PAGE_MAX_WIDTH);
    sheet.direction = CF_DIRECTION_COLUMN;

    const cf_node document = cf_col(ctx, sheet, chapters, count);
    cf_node page = cf_scroller(ctx, "page", &document, 1u);

    /* The style is restated whole, so the direction and the clip the widget
     * asked for are restated with it; the wheel handler the scroller installed
     * rides the node rather than the style and stays. */
    page.style = theme->page;
    page.style.width = CF_PERCENT(100.0f);
    page.style.height = CF_PERCENT(100.0f);
    page.style.direction = CF_DIRECTION_COLUMN;
    page.style.clips_content = true;
    /* The wrapper caps itself above; this is where the capped column sits in a
     * window wider than it. The wheel's range is the wrapper's height, which no
     * cross-axis placement moves. */
    page.style.align_items = CF_ALIGN_CENTER;

    return page;
}

static cf_node build(cf_ctx *ctx) {
    const cf_node chapters[] = {
        make_header(ctx),
        make_chapter(
            ctx, "01", "Buttons, and the state they keep",
            "A button's whole job is the click it fires: the release that starts inside its "
            "rect and ends inside it. What a click changes goes into a block the name keys, "
            "in the arena that outlives the frame, so the count survives a rebuild - and the "
            "line under the buttons is written again by whichever build shows it.",
            make_buttons_demo(ctx)),
        make_chapter(
            ctx, "02", "A field that edits a buffer you own",
            "Click the box and type. The press that starts inside it books focus, and what a "
            "focused field is handed after that is what the keyboard wrote rather than which keys "
            "moved: shift, caps lock, the layout and an input method are settled before the bytes "
            "arrive, so a capital lands as the capital that was typed. Backspace drops the last "
            "character, and a run too long for the buffer is refused whole rather than cut in "
            "half. The widget copies the text nowhere, so the line under it reads the same bytes "
            "back.",
            make_field_demo(ctx)),
        make_chapter(
            ctx, "03", "Text that measures itself",
            "The paragraph below is a box that asks its own text how tall it is: the measure walks "
            "the run, and the height it counts is the height the layout books and the paint draws. "
            "Press the button and the one number that changes is the width the grey well is given; "
            "the lines the text breaks into and the height of the box follow from it, so nothing "
            "ends up outside the edge it drew itself - the same rule a window resize runs on.",
            make_text_demo(ctx)),
        make_chapter(
            ctx, "04", "Eight rules, one row",
            "The strip is four boxes in one row. The chip you press picks which field of the "
            "style the build states differently and nothing else changes: wrap breaks the "
            "line before it squeezes it, gap is the space between, radius past half the "
            "shorter side is a pill, clamp bounds the main size, align stretches what never "
            "stated a height, and overlay takes the last box out of the flow. The well clips "
            "what the row runs past: an overhanging box is cut at its edge rather than drawn "
            "over the chapters below it.",
            make_rules_demo(ctx)),
        make_chapter(
            ctx, "05", "A pane that scrolls itself",
            "A scroller walks a content box taller than the rect that shows it: the wheel writes "
            "the offset the next declaration reads back. All eight boxes are declared every frame, "
            "on screen or not - this chapter is about the offset itself, and the thumb beside the "
            "pane is the same number drawn a second way, which is why it has to be placed by the "
            "numbers rather than by a layout that has not run. Drag it. Once the pane has run out "
            "of content it stops taking notches, and the ones it cannot spend walk this page "
            "instead - a column that swallowed the wheel would read as a stuck window.",
            make_panel(ctx)),
        make_chapter(
            ctx, "06", "A list that builds only what shows",
            "What separates this from the pane above is what the frame declares, not what the eye "
            "sees. Forty rows cost it the handful the viewport can show: the list works out which "
            "indices the offset puts on screen and asks the row factory for those alone, so the "
            "line under it stays at a few of forty the whole way down. Each row is held to one "
            "height, so the range is arithmetic and one notch walks one row, and the row the "
            "pointer rests on takes the theme's hover tone.",
            make_list_demo(ctx)),
        make_chapter(
            ctx, "07", "An image, and a badge that leaves the flow",
            "A texture the backend uploaded draws into the box the measure sized. The badge has to "
            "be a child of the same column as the logo, because an absolute box is placed against "
            "the padding box of the nearest ancestor that is not static - hang it off anything "
            "else and it moves with that instead. Out of the flow it asks for no room: the column "
            "is as tall with the badge as without it, and the negative insets are the half that "
            "hangs past the corner. Put it back in the flow and it takes its own line under the "
            "logo, gap and all.",
            make_badge_demo(ctx)),
        make_chapter(
            ctx, "08", "Tracks a container declares",
            "A grid is the one layout whose sizes come from the parent rather than from the "
            "children: the style states the column tracks and the cells are placed into them. An "
            "item says only how many tracks it takes - the first cell here claims two columns and "
            "the fourth two rows, and zero on either is the grid's own auto of one track at the "
            "next free cell. Press a plan and only the track array changes: three written out, one "
            "pair repeated by the helper into the same flat array the pass reads, or none, which "
            "leaves a single content-sized column. Two rows are declared and six cells want more, "
            "so the rest land in rows the grid adds as tracks of their own.",
            make_grid_demo(ctx)),
        make_chapter(
            ctx, "09", "A value that moves between frames",
            "A tween holds one number and the time it was aimed at. The press aims it and the "
            "build steps it by the seconds this frame covers, which is the whole division between "
            "an event and an animation - nothing is left running on the input side once the button "
            "has been let go. The shell's idle rule can only see events and a moved surface, so a "
            "page whose own state is mid-motion asks for the next frame by name, and stops asking "
            "in the frame that lands the value: the line below says which of the two this frame "
            "was. The puck is placed in points inside a lane of stated width; the bar under it is "
            "the same number as a share, so it keeps stretching when the window does.",
            make_motion_demo(ctx)),
        make_note(ctx,
                  "F12 lays the frame's numbers over the page - the nodes it built, the slots that "
                  "were dirty, the commands it painted, the offer it laid out against and the "
                  "microseconds the frame took."),
    };

    return make_page(ctx, chapters, (uint32_t)(sizeof chapters / sizeof chapters[0]));
}

int main(int argc, char **argv) {
    /* Route the library's diagnostics through SDL's log viewer. A host that
     * owns a sink of its own would leave this out and the backend would not
     * overwrite it. */
    cf_sdl_install_log_sink();

    theme = cf_theme_default();
    body_font = cf_font_sdl(CFORM_GALLERY_FONT, 14.0f);
    title_font = cf_font_sdl(CFORM_GALLERY_FONT, 22.0f);
    body = (cf_text_stack){.fonts = &body_font, .count = body_font.ops != NULL ? 1u : 0u};
    title = (cf_text_stack){.fonts = &title_font, .count = title_font.ops != NULL ? 1u : 0u};

    for (uint32_t i = 0u; i < 64u; ++i) {
        uint8_t *pixel = &checker[i * 4u];
        pixel[0] = pixel[1] = pixel[2] = ((i / 8u + i % 8u) & 1u) != 0u ? 150u : 60u;
        pixel[3] = 255u;
    }

    const cf_backend backend = cf_backend_sdl(&(cf_sdl_config){.title = "cform gallery",
                                                               .width = WINDOW_WIDTH,
                                                               .height = WINDOW_HEIGHT,
                                                               .min_width = WINDOW_MIN_WIDTH,
                                                               .min_height = WINDOW_MIN_HEIGHT});
    if (backend.submit == NULL) {
        printf("gallery: no window\n");
        return 1;
    }

    /* An image is the backend's to read, so a path named on the command line
     * goes through its decoder and lands as a texture. Without one the page
     * still has a texture to show - a pattern uploaded here, in the bytes the
     * slot promises - because a demo that needs an asset it cannot ship is a
     * demo that does not run. */
    if (argc > 1 && backend.load_image != NULL) {
        if (!backend.load_image(backend.user_data, argv[1], &logo)) {
            printf("gallery: %s is not an image this backend reads\n", argv[1]);
        }
    } else {
        logo = (cf_image_content){.texture =
                                      backend.create_texture(backend.user_data, 8u, 8u, checker),
                                  .width = 32.0f,
                                  .height = 32.0f};
    }

    cf_app_run(
        &(cf_app_config){.build = build, .backend = backend, .theme = theme, .fonts = &body});
}
