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
} cf_text_content;

/*
 * The measure slot's text implementation. Min content is the widest run
 * between break opportunities; max content is the widest line the text's
 * own breaks leave; a definite offered width wraps the text into the lines
 * that fit it, capped by max_lines and closed with the ellipsis when the
 * content says so. The answer's height counts the lines that remain, at
 * the primary font's line_height.
 */
cf_size cf_text_measure(void *content, cf_available available, cf_intrinsic kind);

#endif
