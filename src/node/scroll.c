#include "cf/node.h"

/* A thumb thinner than a finger is ungrabbable; the visible share never
 * goes under this, and the placement spends the difference on the track's
 * free length. */
#define SCROLLBAR_MIN_THUMB 16.0f

cf_scroll *cf_scroll_of(cf_ctx *ctx, cf_name key) {
    return cf_state(ctx, key, sizeof(cf_scroll));
}

float cf_scroll_clamp(float offset, float viewport, float content) {
    const float range = content - viewport;

    if (range <= 0.0f) {
        return 0.0f;
    }

    if (offset < 0.0f) {
        return 0.0f;
    }

    return offset > range ? range : offset;
}

static float thumb_size(float viewport, float content, float track_length) {
    const float share = track_length * viewport / content;

    return share < SCROLLBAR_MIN_THUMB ? SCROLLBAR_MIN_THUMB : share;
}

cf_rect cf_scrollbar_thumb(cf_scrollbar bar, float offset) {
    if (bar.content <= bar.viewport || bar.track.height <= 0.0f) {
        return bar.track;
    }

    const float size = thumb_size(bar.viewport, bar.content, bar.track.height);
    const float free = bar.track.height - size;
    const float at =
        cf_scroll_clamp(offset, bar.viewport, bar.content) / (bar.content - bar.viewport) * free;

    return (cf_rect){
        .x = bar.track.x, .y = bar.track.y + at, .width = bar.track.width, .height = size};
}

float cf_scroll_drag(cf_scrollbar bar, cf_point grab, cf_point position, float offset_at_grab) {
    const float clamped = cf_scroll_clamp(offset_at_grab, bar.viewport, bar.content);

    if (bar.content <= bar.viewport || bar.track.height <= 0.0f) {
        return clamped;
    }

    const float size = thumb_size(bar.viewport, bar.content, bar.track.height);
    const float free = bar.track.height - size;

    if (free <= 0.0f) {
        return clamped;
    }

    const float moved = (position.y - grab.y) * (bar.content - bar.viewport) / free;

    return cf_scroll_clamp(clamped + moved, bar.viewport, bar.content);
}
