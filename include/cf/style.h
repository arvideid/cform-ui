#ifndef CF_STYLE_H
#define CF_STYLE_H

#include "cf/types.h"

/*
 * Flex formatting context, CSS Flexible Box Layout §9.1. The four directions
 * are written in append-only order so a stored style from an earlier build
 * keeps meaning the same thing.
 */
typedef enum cf_direction {
    CF_DIRECTION_ROW,
    CF_DIRECTION_ROW_REVERSE,
    CF_DIRECTION_COLUMN,
    CF_DIRECTION_COLUMN_REVERSE
} cf_direction;

typedef enum cf_wrap { CF_NO_WRAP, CF_WRAP, CF_WRAP_REVERSE } cf_wrap;

typedef enum cf_justify {
    CF_JUSTIFY_START,
    CF_JUSTIFY_CENTER,
    CF_JUSTIFY_END,
    CF_JUSTIFY_SPACE_BETWEEN,
    CF_JUSTIFY_SPACE_AROUND,
    CF_JUSTIFY_SPACE_EVENLY
} cf_justify;

/*
 * One enum for align-items and align-self, as in CSS. CF_ALIGN_AUTO means
 * "whatever the container does" for an item and "stretch" for a container, so
 * the zero state of a style is the CSS initial value on both sides.
 */
typedef enum cf_align {
    CF_ALIGN_AUTO,
    CF_ALIGN_START,
    CF_ALIGN_END,
    CF_ALIGN_CENTER,
    CF_ALIGN_STRETCH,
    CF_ALIGN_BASELINE
} cf_align;

typedef enum cf_align_content {
    CF_ALIGN_CONTENT_AUTO,
    CF_ALIGN_CONTENT_START,
    CF_ALIGN_CONTENT_END,
    CF_ALIGN_CONTENT_CENTER,
    CF_ALIGN_CONTENT_STRETCH,
    CF_ALIGN_CONTENT_SPACE_BETWEEN,
    CF_ALIGN_CONTENT_SPACE_AROUND,
    CF_ALIGN_CONTENT_SPACE_EVENLY
} cf_align_content;

/*
 * Whether a child takes part in its container's flex layout, and what its
 * insets do. A relative box stays in the flow and moves by its own insets. An
 * absolute child leaves the flow and is measured and placed against its
 * containing block: the padding box of the nearest ancestor that is not
 * static, the root's own box when the chain never leaves `static`. Its static
 * position still comes from the parent that holds it, so a child of a static
 * box takes one ancestor's edges and its own parent's flow at once. This is
 * the overlay a toolkit needs. CF_POSITION_RELATIVE is appended, so a stored
 * style from before it existed keeps its meaning.
 */
typedef enum cf_position {
    CF_POSITION_STATIC,
    CF_POSITION_ABSOLUTE,
    CF_POSITION_RELATIVE
} cf_position;

/*
 * The formatting context a container gives its children: lines that pack along
 * a main axis, or tracks that place into a grid. The zero state is a flex
 * container, so a style written before grids existed keeps its meaning.
 * CF_DISPLAY_NONE is appended and means something else entirely: the box and
 * its whole subtree leave the layout, the paint walk and hit-testing, as if
 * they were never declared. Their rects come back zeroed at the container's
 * content corner, so a stale box from an earlier frame cannot be painted or
 * hit. A stored style from before it existed reads as flex, never as hidden.
 */
typedef enum cf_display { CF_DISPLAY_FLEX, CF_DISPLAY_GRID, CF_DISPLAY_NONE } cf_display;

/*
 * What a declared width or height measures. CF_BOX_BORDER is the whole border
 * box - padding and border sit inside it - and is the zero state, so a style
 * written before the field existed keeps sizing the way it always did.
 * CF_BOX_CONTENT makes the declaration measure the content area and adds the
 * box's own padding and border outside it, the way CSS writes
 * `box-sizing: content-box`. A stretched or parent-decided size is a border box
 * either way; only the node's own width, height, flex-basis and bounds read as
 * content. Appended, so CF_DISPLAY_* and the direction enums keep their numbers.
 */
typedef enum cf_box_sizing { CF_BOX_BORDER, CF_BOX_CONTENT } cf_box_sizing;

/*
 * How a box treats content that reaches past its own border box. Layout does
 * not care - the children measure the same either way - so the axis is a paint
 * and hit-test contract: the walk clips descendants when it hands them off.
 * CF_OVERFLOW_VISIBLE (the zero value) lets a child overhang; HIDDEN cuts the
 * descendant at the box and hands the pointer to whatever shows through;
 * SCROLL is HIDDEN for now, the scrollbar being the widget's own concern;
 * CLIP is a stronger HIDDEN whose rule reaches the border box rather than the
 * padding edge - the same rectangle is used either way until a border is
 * drawn into the clip. Appended, so the display and box-sizing enums keep
 * their numbers.
 */
typedef enum cf_overflow {
    CF_OVERFLOW_VISIBLE,
    CF_OVERFLOW_HIDDEN,
    CF_OVERFLOW_SCROLL,
    CF_OVERFLOW_CLIP
} cf_overflow;

/*
 * Margin, padding and border as tagged lengths, in the same physical order as
 * cf_edges so a paint pass can read the two side by side. Auto is a real value
 * for margin (§9.5.2 absorbs free space before justify-content gets any) and
 * reads as zero for padding and border, which is the only honest meaning left.
 * Percent padding and border resolve against the containing block's width, as
 * CSS does, never against the axis they sit on.
 */
typedef struct cf_box {
    cf_length left;
    cf_length top;
    cf_length right;
    cf_length bottom;
} cf_box;

/*
 * One grid track, written as CSS's minmax(min, max).
 *
 * `min` is the floor the track never drops below: a length fixes it, CF_AUTO
 * lets the track collapse when space runs short. `max` is the ceiling: a
 * length fixes it, CF_AUTO grows the track to the content of the items that
 * fill it alone, and CF_FR(v) claims v shares of whatever the fixed and
 * content-sized tracks leave over. `auto` is {CF_AUTO, CF_AUTO}, a fixed track
 * repeats its length in both slots, and minmax(a, b) is {a, b}.
 */
typedef struct cf_track {
    cf_length min;
    cf_length max;
} cf_track;

/*
 * The declarations of one node. The zero state is valid: a row container that
 * hugs its content, wraps nothing, stretches its items and never shrinks them.
 * cf_style_default adds the two CSS initial values zero cannot express - a
 * flex-shrink of 1, and margins of 0 rather than margins that auto-absorb every
 * bit of leftover space.
 *
 * A declared width or height is the border box by default: padding and border
 * sit inside it, and min and max clamp the border box with it. The CSS
 * content-box default is not inherited - a toolkit box that has to be measured
 * twice to learn its own padding is not worth the spec citation - but a node may
 * ask for it by setting `box_sizing` to CF_BOX_CONTENT, which makes its own
 * width, height, flex-basis and bounds measure the content area instead.
 *
 * In a grid container the two track arrays describe the explicit grid and are
 * borrowed for the duration of the pass, like every other array the engine
 * reads: a track array that dies mid-frame gives the grid whatever its pointer
 * held. A container with no column tracks has one content-sized column, and
 * rows the items overflow into are added as content-sized tracks of their own.
 * Placement is 1-based, as CSS writes it: `column_start` is the line the item
 * begins at and `column_span` the number of tracks it fills, so a start of 2
 * and a span of 3 covers the middle three columns. Zero on either is `auto`:
 * the next free cell the placement cursor reaches, and one track.
 */
typedef struct cf_style {
    cf_length width;
    cf_length height;
    cf_length min_width;
    cf_length max_width;
    cf_length min_height;
    cf_length max_height;
    cf_length flex_basis;
    cf_box margin;
    cf_box padding;
    cf_box border;
    cf_box inset;
    const cf_track *column_tracks;
    const cf_track *row_tracks;
    uint32_t column_count;
    uint32_t row_count;
    uint32_t column_start;
    uint32_t column_span;
    uint32_t row_start;
    uint32_t row_span;
    cf_length column_gap;
    cf_length row_gap;
    float flex_grow;
    float flex_shrink;
    cf_position position;
    cf_display display;
    cf_box_sizing box_sizing;
    cf_direction direction;
    cf_wrap wrap;
    cf_justify justify_content;
    cf_align align_items;
    cf_align align_self;
    cf_align_content align_content;
    int32_t order;

    /*
     * What the box paints once layout has placed it. The background fills the
     * border box, the way CSS paints under its border, and the border color
     * strokes the border widths into it; a color that is not stated paints
     * nothing. The radius rounds both, clamped to half the shorter extent,
     * and a negative one is a square box.
     */
    cf_opt_color background;
    cf_opt_color border_color;
    float radius;

    /*
     * The preferred ratio between the box's border width and its border
     * height, CSS's `aspect-ratio: <number>`. A ratio of zero states
     * nothing, so a stored style from before the field existed keeps
     * sizing the way it always did. When the ratio is stated and exactly
     * one axis is settled, the other axis reads as its border box divided
     * by the ratio, bounded against the box's own min and max before
     * anything inside it is measured. Both axes declared, both auto, or
     * no stated ratio leaves the ratio out of the answer.
     */
    float aspect_ratio;

    /*
     * The tone this box's own text paints with. A box that states none has
     * no text to paint, so the color is the text's on-switch as well as its
     * ink; the widget that owns a box decides which theme tone it carries.
     */
    cf_opt_color color;

    /*
     * Whether the box's children paint only inside its own rect - the
     * scroller's contract. The zero state lets a child overhang, as a
     * relative box always has. overflow_x and overflow_y are the same rule
     * stated per axis and are what CSS writes; a box clips when either axis
     * says HIDDEN/SCROLL/CLIP or when clips_content is set directly (the
     * legacy spelling the scroller widget still uses).
     */
    cf_overflow overflow_x;
    cf_overflow overflow_y;
    bool clips_content;

    /*
     * The two marks an editing box lays over its own text: the caret the
     * content's offset stands at, and the band under the range its offset pair
     * selects. A mark whose tone is not stated draws nothing, which is what
     * keeps a label - a box that only ever shows bytes - from painting marks it
     * was never meant to hold. A theme hands them out from a state, so the caret
     * arrives with the focus that makes it worth showing.
     */
    cf_opt_color caret;
    cf_opt_color selection;
} cf_style;

/* The CSS initial values: shrink 1 and zero margins; all else as the zero state. */
cf_style cf_style_default(void);

/*
 * Whether a box cuts its descendants at its own rect: true when either axis
 * names hidden, scroll or clip, or when the legacy clips_content flag is set.
 * The paint walk and the hit-test walk both read the answer, so the rule sits
 * next to the style rather than being reconstructed in each caller.
 */
static inline bool cf_style_clips_content(const cf_style *style) {
    return style->clips_content || style->overflow_x != CF_OVERFLOW_VISIBLE ||
           style->overflow_y != CF_OVERFLOW_VISIBLE;
}

/*
 * `pattern` written into `out` end to end `times`, stopping at `capacity`. The
 * count returned is how many tracks `out` now holds - what the caller puts in
 * `column_count`, because the engine reads a flat array and never sees the
 * pattern.
 */
uint32_t cf_tracks_repeat(cf_track *out, uint32_t capacity, const cf_track *pattern,
                          uint32_t pattern_count, uint32_t times);

/*
 * A length read as a plain measurement: auto and fr hold nothing and read as
 * zero, so a caller summing box edges can ignore them. A percentage resolves
 * against the reference the caller supplies; an unknown reference gives zero,
 * which is CSS's used value for a percentage of an indefinite extent.
 */
float cf_length_resolve(cf_length length, float reference);

/*
 * A declaration turned into a length, or false when it holds none: auto, fr,
 * or a percentage measured against an offer that is still being sized. The
 * false answer is the engine's signal to measure the content instead, so it
 * must not be replaced by a zero.
 */
bool cf_length_of(cf_length declaration, cf_length offer, float *out);

/* A length that asks for nothing: CF_LENGTH_AUTO. */
bool cf_length_is_auto(cf_length length);

/*
 * A box's four sides as measurements. Auto is zero here even though it means
 * something for margin, which is the caller's to read off the box first.
 * Negative padding and border read as zero: they are insets, and a negative
 * one would put a content box outside the frame that paints it.
 */
cf_edges cf_box_resolve(cf_box box, float reference);

/*
 * A box extent forced inside its constraints. An absent bound asks for
 * nothing, so min and max are lengths rather than floats; min keeps the last
 * word when the pair contradicts. Negative input reads as zero - a box has no
 * negative extent. A percentage bound against an indefinite reference bounds
 * nothing: dropping it is honest, resolving it to zero would collapse the box.
 */
float cf_size_clamp(float value, cf_length min, cf_length max, cf_length reference);

/* True for the directions that pack from the far edge: row- and column-reverse. */
bool cf_direction_is_reverse(cf_direction direction);

/* True when children are packed along the horizontal axis. */
bool cf_direction_is_row(cf_direction direction);

/*
 * The interaction states a node styles across, one bit each so a resolver
 * reads one mask. The node layer books hovered, pressed and focused from
 * the pointer and the keyboard; disabled is the widget's own declaration.
 */
typedef enum cf_interact {
    CF_INTERACT_NONE = 0,
    CF_INTERACT_HOVERED = 1 << 0,
    CF_INTERACT_PRESSED = 1 << 1,
    CF_INTERACT_FOCUSED = 1 << 2,
    CF_INTERACT_DISABLED = 1 << 3
} cf_interact;

/*
 * The widget family a node belongs to. The resolver reads it to pick the
 * theme's variant set; CF_ROLE_NONE means the declared style is the whole
 * truth and nothing resolves over it.
 */
typedef enum cf_role {
    CF_ROLE_NONE,
    CF_ROLE_BUTTON,
    CF_ROLE_FIELD,
    CF_ROLE_LIST,
    CF_ROLE_SCROLL,
    CF_ROLE_IMAGE,
    CF_ROLE_COUNT
} cf_role;

/*
 * What one interaction state paints differently. Every field is optional:
 * a field the variant leaves unstated inherits the declared style, so a
 * variant states only its differences from the resting box.
 */
typedef struct cf_variant {
    cf_opt_color background;
    cf_opt_color border_color;
    cf_opt_color caret;
    cf_opt_color selection;
} cf_variant;

/*
 * One widget family's paint: the resting box a widget starts its
 * declaration from, and the differences each state applies over whatever
 * the declaration ended up stating.
 */
typedef struct cf_variants {
    cf_style base;
    cf_variant hovered;
    cf_variant pressed;
    cf_variant focused;
    cf_variant disabled;
} cf_variants;

/*
 * The design system a build reads its surfaces from: the resting boxes of
 * the page's regions, and one variant set per widget family. The zero
 * state paints nothing and hugs its content, so a theme may be built field
 * by field.
 */
typedef struct cf_theme {
    cf_style page;   /* the field everything sits on */
    cf_style bar;    /* wells and bars that lift off the page */
    cf_style body;   /* card surfaces */
    cf_color ink;    /* the tone a body of text reads in */
    cf_color muted;  /* the tone secondary text reads in */
    cf_color on_ink; /* the tone text carries over the accent */
    cf_variants variants[CF_ROLE_COUNT];
} cf_theme;

/*
 * The base style with the states' differences applied over it, in enum
 * order so the later bits overwrite the earlier: a disabled box reads
 * disabled whatever else it is. A NULL variant set answers the base whole.
 */
cf_style cf_style_varied(cf_style base, const cf_variants *variants, uint32_t states);

/*
 * The neutral scale the gallery reads: a 60-30-10 field of near-whites with
 * one dark accent reserved for the primary action, every fill clearing
 * WCAG-AA against the text tone it carries.
 */
const cf_theme *cf_theme_default(void);

#endif
