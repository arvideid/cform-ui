#include "cf/style.h"

#include <math.h>

cf_style cf_style_default(void) {
    return (cf_style){
        .margin = {.left = CF_PX(0.0f),
                   .top = CF_PX(0.0f),
                   .right = CF_PX(0.0f),
                   .bottom = CF_PX(0.0f)},
        .flex_shrink = 1.0f,
    };
}

/*
 * A pattern is repeated whole or not at all: a half-written track would leave
 * the caller's count and its array disagreeing about where the grid ends.
 */
uint32_t cf_tracks_repeat(cf_track *out, uint32_t capacity, const cf_track *pattern,
                          uint32_t pattern_count, uint32_t times) {
    if (out == NULL || pattern == NULL || pattern_count == 0u) {
        return 0u;
    }

    const uint32_t repeated = capacity / pattern_count;
    const uint32_t whole = times < repeated ? times : repeated;
    const uint32_t count = whole * pattern_count;

    for (uint32_t i = 0u; i < count; ++i) {
        out[i] = pattern[i % pattern_count];
    }

    return count;
}

float cf_length_resolve(cf_length length, float reference) {
    /* AUTO holds no length of its own - the flex pass decides - and FR is a
     * share of leftover space rather than a measurement. MAX_CONTENT and
     * STRETCH are rules the layout pass runs, not numbers it reads, and a
     * caller summing box edges still wants zero from them. */
    switch (length.kind) {
    case CF_LENGTH_PX:
        return length.value;
    case CF_LENGTH_PERCENT:
        return reference * length.value / 100.0f;
    case CF_LENGTH_AUTO:
    case CF_LENGTH_FR:
    case CF_LENGTH_MAX_CONTENT:
    case CF_LENGTH_STRETCH:
        break;
    }

    return 0.0f;
}

bool cf_length_is_auto(cf_length length) {
    return length.kind == CF_LENGTH_AUTO;
}

bool cf_length_of(cf_length declaration, cf_length offer, float *out) {
    if (declaration.kind == CF_LENGTH_PX) {
        *out = declaration.value;
        return true;
    }

    /* A percentage of an extent still being measured is not a length. Leaving
     * it unresolved is what lets the caller fall back to the content size
     * instead of collapsing the box to zero. Only a pixel offer can resolve
     * one: a percentage of a percentage describes no box anybody has sized.
     * MAX_CONTENT and STRETCH are decided by the layout pass rather than by
     * this scalar reader, so a caller asking for a length gets nothing. */
    if (declaration.kind == CF_LENGTH_PERCENT && offer.kind == CF_LENGTH_PX) {
        *out = offer.value * declaration.value / 100.0f;
        return true;
    }

    return false;
}

/* A negative padding or border would put the content box outside the frame
 * that paints it; margins keep their sign, and their caller resolves them. */
static float box_side(cf_length side, float reference) {
    const float value = cf_length_resolve(side, reference);

    return value < 0.0f ? 0.0f : value;
}

cf_edges cf_box_resolve(cf_box box, float reference) {
    return (cf_edges){
        .left = box_side(box.left, reference),
        .top = box_side(box.top, reference),
        .right = box_side(box.right, reference),
        .bottom = box_side(box.bottom, reference),
    };
}

float cf_size_clamp(float value, cf_length min, cf_length max, cf_length reference) {
    if (value < 0.0f) {
        value = 0.0f;
    }

    float bound;

    if (cf_length_of(max, reference, &bound) && value > bound) {
        value = bound;
    }

    /* Applied after the maximum so a contradictory pair settles on the lower
     * bound, the way CSS lets the content-based minimum have the last word. */
    if (cf_length_of(min, reference, &bound) && value < bound) {
        value = bound;
    }

    return value;
}

bool cf_direction_is_row(cf_direction direction) {
    return direction == CF_DIRECTION_ROW || direction == CF_DIRECTION_ROW_REVERSE;
}

bool cf_direction_is_reverse(cf_direction direction) {
    return direction == CF_DIRECTION_ROW_REVERSE || direction == CF_DIRECTION_COLUMN_REVERSE;
}

/* One state's differences, written over the style the role declared. */
static void variant_apply(const cf_variant *variant, cf_style *style) {
    if (variant->background.present) {
        style->background = variant->background;
    }

    if (variant->border_color.present) {
        style->border_color = variant->border_color;
    }

    if (variant->caret.present) {
        style->caret = variant->caret;
    }

    if (variant->selection.present) {
        style->selection = variant->selection;
    }

    if (variant->marked.present) {
        style->marked = variant->marked;
    }
}

cf_style cf_style_varied(cf_style base, const cf_variants *variants, uint32_t states) {
    if (variants == NULL) {
        return base;
    }

    /* The table's order is the application order, so the last stated bit wins: a
     * disabled box keeps its disabled paint however it is hovered. The switch
     * leads rather than following its enum bit, because the pointer's marks are
     * differences over the state a box is in rather than a replacement for it -
     * a hovered box that is on stays on. */
    const cf_variant *const order[] = {&variants->checked, &variants->hovered, &variants->pressed,
                                       &variants->focused, &variants->disabled};
    const uint32_t bits[] = {(uint32_t)CF_INTERACT_CHECKED, (uint32_t)CF_INTERACT_HOVERED,
                             (uint32_t)CF_INTERACT_PRESSED, (uint32_t)CF_INTERACT_FOCUSED,
                             (uint32_t)CF_INTERACT_DISABLED};

    for (uint32_t i = 0u; i < sizeof order / sizeof *order; ++i) {
        if ((states & bits[i]) != 0u) {
            variant_apply(order[i], &base);
        }
    }

    return base;
}

/*
 * WCAG's relative luminance, one channel at a time: the byte is normalised, taken
 * off the sRGB transfer curve and then weighted by how much the eye reads each
 * primary. The threshold, the linear segment's divisor, the exponent and the three
 * weights are the specification's numbers - the curve is why the answer cannot be a
 * blend of the bytes, and why 0x808080 is nowhere near half as bright as white.
 */
static float channel_luminance(uint8_t byte) {
    const float channel = (float)byte / 255.0f;

    if (channel <= 0.04045f) {
        return channel / 12.92f;
    }

    return powf((channel + 0.055f) / 1.055f, 2.4f);
}

static float color_luminance(cf_color color) {
    return 0.2126f * channel_luminance(color.red) + 0.7152f * channel_luminance(color.green) +
           0.0722f * channel_luminance(color.blue);
}

/* Source-over at the byte a painter would blend with: a theme is free to state a
 * translucent tone, and the ratio that matters is the one after it is laid. The
 * background's alpha is not consulted, because a caller has no tone below it to
 * name - the page it rests on is a second question. */
static cf_color composite_over(cf_color top, cf_color bottom) {
    if (top.alpha == 255u) {
        return top;
    }

    const float cover = (float)top.alpha / 255.0f;
    const float show = 1.0f - cover;

    return (cf_color){(uint8_t)((float)top.red * cover + (float)bottom.red * show + 0.5f),
                      (uint8_t)((float)top.green * cover + (float)bottom.green * show + 0.5f),
                      (uint8_t)((float)top.blue * cover + (float)bottom.blue * show + 0.5f), 255u};
}

/* The definition puts the lighter luminance on top, so the ratio does not depend on
 * which tone the caller names first: a page asks about ink on a fill and about the
 * same fill under other ink, and both questions want one answer. */
float cf_contrast(cf_color foreground, cf_color background) {
    float lighter = color_luminance(composite_over(foreground, background));
    float darker = color_luminance(background);

    if (lighter < darker) {
        const float between = lighter;
        lighter = darker;
        darker = between;
    }

    /* The 0.05 is the specification's allowance for a viewing surround, and it is
     * what keeps the ratio finite: a tone against itself answers 1, black against
     * white the 21 the definition is anchored on. */
    return (lighter + 0.05f) / (darker + 0.05f);
}
