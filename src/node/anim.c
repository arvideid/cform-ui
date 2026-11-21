#include "cf/node.h"

cf_tween *cf_tween_of(cf_ctx *ctx, cf_name key) {
    return cf_state(ctx, key, sizeof(cf_tween));
}

static float eased(float p, cf_ease ease) {
    /* Quadratic out settles into the target; the in-out half mirrors it
     * around the middle so both ends leave slowly. */
    switch (ease) {
    case CF_EASE_LINEAR:
        return p;
    case CF_EASE_OUT:
        return 1.0f - (1.0f - p) * (1.0f - p);
    case CF_EASE_IN_OUT:
        return p < 0.5f ? 2.0f * p * p : 1.0f - 2.0f * (1.0f - p) * (1.0f - p);
    }

    return p;
}

static float value_of(const cf_tween *tween) {
    if (tween->duration <= 0.0f) {
        return tween->to;
    }

    const float p = tween->elapsed / tween->duration;
    const float clamped = p > 1.0f ? 1.0f : p;

    return tween->from + (tween->to - tween->from) * eased(clamped, tween->ease);
}

void cf_tween_to(cf_tween *tween, float to, float duration, cf_ease ease) {
    if (tween == NULL) {
        return;
    }

    tween->from = value_of(tween);
    tween->to = to;
    tween->duration = duration;
    tween->elapsed = 0.0f;
    tween->ease = ease;
}

float cf_tween_step(cf_tween *tween, float seconds) {
    if (tween == NULL) {
        return 0.0f;
    }

    if (tween->elapsed < tween->duration) {
        tween->elapsed += seconds;

        if (tween->elapsed > tween->duration) {
            tween->elapsed = tween->duration;
        }
    }

    return value_of(tween);
}
