#ifndef CF_TEXT_H
#define CF_TEXT_H

#include "cf/base.h"

/*
 * U+2026 closes a capped walk's last line: a fixed codepoint keeps the
 * ellipsis a property of the layer instead of a string every caller carries.
 */
#define CF_ELLIPSIS 0x2026u

/* The vertical rhythm of a walk's lines: one line advances by line_height,
 * so n remaining lines want n times it. */
typedef struct cf_text_metrics {
    float line_height;
} cf_text_metrics;

/*
 * What a walk reads a font through. Measuring never rasterizes: the pen
 * distance per glyph, the line rhythm and whether a codepoint can be drawn
 * at all are the whole contract, so a headless test answers through the
 * same table a real font backend does.
 */
typedef struct cf_text_ops {
    cf_text_metrics (*metrics)(void *font);
    /* The pen distance drawing `codepoint` after `previous` moves by,
     * kerning included. A run's first codepoint arrives with previous 0,
     * and so does the first glyph after a font switch or a line break. */
    float (*advance)(void *font, uint32_t codepoint, uint32_t previous);
    /* False when this font cannot draw the codepoint. */
    bool (*has_glyph)(void *font, uint32_t codepoint);
} cf_text_ops;

typedef struct cf_font {
    void *handle;
    const cf_text_ops *ops;
} cf_font;

/*
 * The fonts one walk reads through, primary first. Where a font cannot draw
 * a codepoint the next one takes over glyph by glyph: kerning holds inside
 * a run and gives way only where the switch splits a pair.
 */
typedef struct cf_text_stack {
    const cf_font *fonts;
    uint32_t count;
} cf_text_stack;

/*
 * The text a node measures: measure_data points at one of these when the
 * node's measure slot is cf_text_measure. A line breaks at a space, after
 * a hyphen or an east-asian glyph, and where the text itself forces one;
 * the spaces a break leaves at a line's edges are trimmed, not counted.
 */
typedef struct cf_text_content {
    cf_text_stack stack;
    cf_string text;
    cf_opt_u32 max_lines; /* absent: as many lines as the text breaks into */
    bool ellipsis;        /* a capped walk closes its last line with U+2026 */
    /*
     * The byte offset a caret stands at, and the offset a selection runs back
     * to: both are the editor's state, and a box that paints text for something
     * other than an editor leaves them absent. A caret the text does not hold is
     * clamped to its end. The band is the pair, in whichever order they came:
     * `mark` equal to `caret` selects nothing and lays no band down.
     */
    cf_opt_u32 caret;
    cf_opt_u32 mark;
    /*
     * Only the text's own breaks end a line: nothing wraps, so a walk too wide
     * for its box reports the width it wants rather than the lines it would fit.
     * An editor needs the one behavior and a paragraph the other.
     */
    bool nowrap;
} cf_text_content;

/*
 * The measure slot's text implementation. Min content is the widest run
 * between break opportunities - and, for a walk that refuses to wrap, the
 * whole of its widest line, since nothing bounds it tighter than itself;
 * max content is the widest line the text's own breaks leave; a definite
 * offered width wraps the text into the lines that fit it, capped by
 * max_lines and closed with the ellipsis when the content says so. The
 * answer's height counts the lines that remain, at the primary font's
 * line_height.
 */
cf_size cf_text_measure(void *content, cf_available available, cf_intrinsic kind);

/*
 * One line a walk laid: the line's own bytes inside the content's text, the
 * width its glyphs measure, and whether the walk closed it with the
 * ellipsis. The bytes borrow the content's, so they live as long as the
 * content's text does.
 */
typedef struct cf_text_line {
    cf_string text;
    float width;
    bool ellipsis;
} cf_text_line;

/*
 * The lines a definite width wraps the content into - the same walk
 * cf_text_measure answers from, so paint places exactly what measure
 * counted. Lines fill `out` up to `capacity`; the count returned is how
 * many the walk laid out altogether, so a caller that sized its buffer
 * from the measure's height can trust the count over the fill.
 */
uint32_t cf_text_lines(const cf_text_content *content, float max_width, cf_text_line *out,
                       uint32_t capacity);

/*
 * The pen distance from a laid line's left edge to a byte offset of the
 * content's own text, clamped into the line: an offset the line does not hold
 * stands at one of its ends, because a caret that floats past the ink is a caret
 * in the wrong place. Zero for a line this content never laid.
 */
float cf_text_caret_x(const cf_text_content *content, const cf_text_line *line, uint32_t offset);

/*
 * The byte offset whose caret sits nearest a point along a laid line - the
 * inverse of the distance above, and the answer a click needs. A point before
 * the line's first glyph answers the offset it starts at and a point past its
 * last glyph answers the offset one beyond, so a click at the end of a word
 * lands after the word rather than inside it.
 */
uint32_t cf_text_offset_at(const cf_text_content *content, const cf_text_line *line, float x);

/*
 * How far a box of `window` must slide a laid walk so the caret the content
 * states stays inside it: never negative, and no further than the caret at
 * the trailing edge. A walk that states no caret, or one the window already
 * holds, needs no slide and answers zero - which is why a page of labels
 * paints exactly where it did before this function existed.
 *
 * The slide is derived from the caret rather than remembered, so an offset
 * that jumps back to the start of a long value brings the view back with it.
 * Both the paint walk and the widget that turns a click into an offset read
 * this answer, which is what lets a click land where the glyph it points at
 * was drawn.
 */
cf_point cf_text_scroll(const cf_text_content *content, const cf_text_line *lines, uint32_t count,
                        cf_size window);

/* The line a byte offset falls on: the first whose ink reaches it, the last
 * when the offset sits past all of them, and zero lines for a walk of none. */
uint32_t cf_text_line_of(const cf_text_content *content, const cf_text_line *lines, uint32_t count,
                         uint32_t offset);

/*
 * The bytes a control moves out of the page and back in: what copy, cut and paste
 * carry between this program's buffers and whatever the host keeps beside it.
 *
 * The host owns the pair and decides where it reaches - a desktop clipboard, a
 * buffer inside the process, or nowhere. A context with no pair, or a pair whose
 * slots refuse, is a page whose fields still select and still type and simply move
 * nothing: a refusal is an answer rather than an error, and the widget that asked
 * has no better way to spend it than keeping its bytes.
 *
 * `read` fills `out` with up to `capacity` bytes of UTF-8 and answers the run it
 * wrote there, pointing into `out` rather than at a buffer of its own. Asked with
 * no buffer it answers the length alone, which is how a caller that wants the whole
 * run allocates for it once. Bytes past the capacity drop at the edge of a scalar,
 * so a reader that asked for less than the whole never sees half a character. A
 * clipboard with no text - empty, holding a picture, refused by the platform -
 * answers a run of none.
 *
 * `write` is handed the bytes the caller holds, borrowed for the call alone: a host
 * that keeps them copies them. False says they went nowhere.
 */
typedef struct cf_clipboard {
    cf_string (*read)(void *user_data, char *out, uint32_t capacity);
    bool (*write)(void *user_data, cf_string utf8);
    void *user_data;
} cf_clipboard;

#endif
