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

float cf_length_resolve(cf_length length, float reference) {
    /* AUTO holds no length of its own - the flex pass decides - and FR is a
     * share of leftover space rather than a measurement. Both read as zero so
     * a caller summing box edges can ignore them. */
    switch (length.kind) {
    case CF_LENGTH_PX:
        return length.value;
    case CF_LENGTH_PERCENT:
        return reference * length.value / 100.0f;
    case CF_LENGTH_AUTO:
    case CF_LENGTH_FR:
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
     * one: a percentage of a percentage describes no box anybody has sized. */
    if (declaration.kind == CF_LENGTH_PERCENT && offer.kind == CF_LENGTH_PX) {
        *out = offer.value * declaration.value / 100.0f;
        return true;
    }

    return false;
}

cf_edges cf_box_resolve(cf_box box, float reference) {
    return (cf_edges){
        .left = cf_length_resolve(box.left, reference),
        .top = cf_length_resolve(box.top, reference),
        .right = cf_length_resolve(box.right, reference),
        .bottom = cf_length_resolve(box.bottom, reference),
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
