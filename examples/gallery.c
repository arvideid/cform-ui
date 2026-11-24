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

/* The document's own measure: one column of chapters, each a card, each with
 * the room a heading, a paragraph and something to try need. */
#define PAGE_PADDING 24.0f
#define CHAPTER_GAP 20.0f
#define CARD_PADDING 16.0f
#define CARD_GAP 12.0f

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
    /* Two sizes on one line: the baseline is where their glyphs agree. */
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

    cf_style row = cf_style_default();
    row.column_gap = CF_PX(8.0f);
    row.align_items = CF_ALIGN_START;

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
        item.width = CF_PX(240.0f);
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

    cf_style row = cf_style_default();
    row.wrap = CF_WRAP;
    row.column_gap = CF_PX(8.0f);
    row.row_gap = CF_PX(8.0f);
    row.align_items = CF_ALIGN_START;

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

    cf_style row = cf_style_default();
    row.column_gap = CF_PX(24.0f);
    row.align_items = CF_ALIGN_CENTER;

    return CF_ROW(ctx, row, cf_col(ctx, over, (cf_node[]){image, badge_box}, 2u), flip_button);
}

static cf_node make_note(cf_ctx *ctx, const char *chars) {
    return CF_COL(ctx, inset_style(theme->bar, 12.0f, 0.0f), make_prose(ctx, chars, theme->muted));
}

static cf_node make_header(cf_ctx *ctx) {
    const cf_node name = cf_label(ctx, cf_string_of("cform gallery"), &title, theme->ink);
    const cf_node note =
        cf_label(ctx, cf_string_of("seven rules, each with something to try"), &body, theme->muted);

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
        make_note(ctx,
                  "F12 lays the frame's numbers over the page - the nodes it built, the slots that "
                  "were dirty, the commands it painted, the offer it laid out against and the "
                  "microseconds the frame took."),
    };

    return make_page(ctx, chapters, (uint32_t)(sizeof chapters / sizeof chapters[0]));
}

int main(void) {
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

    const cf_backend backend = cf_backend_sdl(
        &(cf_sdl_config){.title = "cform gallery", .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT});
    if (backend.submit == NULL) {
        printf("gallery: no window\n");
        return 1;
    }

    logo = (cf_image_content){.texture = backend.create_texture(backend.user_data, 8u, 8u, checker),
                              .width = 32.0f,
                              .height = 32.0f};

    cf_app_run(
        &(cf_app_config){.build = build, .backend = backend, .theme = theme, .fonts = &body});
}
