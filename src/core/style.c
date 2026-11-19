#include "internal.h"

static float clamp_unit(float value) {
    if (value <= 0.0f) {
        return 0.0f;
    }

    if (value >= 1.0f) {
        return 1.0f;
    }

    return value;
}

float ui_opacity(const UI_Frame *frame) {
    float product = 1.0f;

    for (const UI_Frame *node = frame; node != NULL; node = node->parent) {
        float opacity = node->config.opacity;

        /* 0 marks "unset", so a zero-initialized config stays fully opaque. */
        if (opacity <= 0.0f) {
            continue;
        }

        product *= clamp_unit(opacity);

        if (product <= 0.0f) {
            return 0.0f;
        }
    }

    return product;
}

UI_Color ui_color_scale_alpha(UI_Color color, float opacity) {
    float factor = clamp_unit(opacity);
    int alpha = (int)((float)color.a * factor + 0.5f);

    color.a = (unsigned char)(alpha > 255 ? 255 : alpha);

    return color;
}
