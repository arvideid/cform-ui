#include "cf/style.h"

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
}

cf_style cf_style_varied(cf_style base, const cf_variants *variants, uint32_t states) {
    if (variants == NULL) {
        return base;
    }

    /* Enum order is application order, so the last stated bit wins: a
     * disabled box keeps its disabled paint however it is hovered. */
    const cf_variant *const order[] = {&variants->hovered, &variants->pressed, &variants->focused,
                                       &variants->disabled};
    const uint32_t bits[] = {(uint32_t)CF_INTERACT_HOVERED, (uint32_t)CF_INTERACT_PRESSED,
                             (uint32_t)CF_INTERACT_FOCUSED, (uint32_t)CF_INTERACT_DISABLED};

    for (uint32_t i = 0u; i < sizeof order / sizeof *order; ++i) {
        if ((states & bits[i]) != 0u) {
            variant_apply(order[i], &base);
        }
    }

    return base;
}
