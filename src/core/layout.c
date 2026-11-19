#include "internal.h"

static float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

/* The child's sizing axis that runs along the parent's layout direction. */
static UI_Size main_axis_size(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->width : c->height;
}

/* The child's sizing axis perpendicular to the parent's layout direction. */
static UI_Size cross_axis_size(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->height : c->width;
}

static float main_margin(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL
        ? c->margin.left + c->margin.right
        : c->margin.top + c->margin.bottom;
}

static float cross_margin(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL
        ? c->margin.top + c->margin.bottom
        : c->margin.left + c->margin.right;
}

void ui_layout_frame(
    UI_Frame *frame,
    float x,
    float y,
    float width,
    float height
) {
    frame->x = x;
    frame->y = y;
    frame->width = width;
    frame->height = height;

    const UI_FrameConfig *config = &frame->config;

    float content_x = x + config->padding.left;
    float content_y = y + config->padding.top;

    float content_width = clamp_non_negative(
        width - config->padding.left - config->padding.right);
    float content_height = clamp_non_negative(
        height - config->padding.top - config->padding.bottom);

    int count = frame->child_count;

    if (count == 0) {
        return;
    }

    UI_Direction direction = config->direction;
    bool horizontal = direction == UI_HORIZONTAL;
    float track_size = horizontal ? content_width : content_height;

    /* Space left on the main axis once gaps and every child's margins are
     * removed. This is what fixed and fill children share. */
    float available = track_size - config->gap * (float)(count - 1);
    float fixed_total = 0.0f;
    int fill_count = 0;

    for (int i = 0; i < count; i++) {
        const UI_FrameConfig *child = &frame->children[i]->config;

        available -= main_margin(child, direction);

        UI_Size size = main_axis_size(child, direction);

        if (size.type == UI_SIZE_FIXED) {
            fixed_total += size.value;
        } else {
            fill_count++;
        }
    }

    available = clamp_non_negative(available);

    float fill_size = fill_count > 0
        ? clamp_non_negative((available - fixed_total) / (float)fill_count)
        : 0.0f;

    float cursor = 0.0f;

    for (int i = 0; i < count; i++) {
        UI_Frame *child = frame->children[i];
        const UI_FrameConfig *child_config = &child->config;

        UI_Size main = main_axis_size(child_config, direction);
        float main_value = main.type == UI_SIZE_FIXED ? main.value : fill_size;

        UI_Size cross = cross_axis_size(child_config, direction);
        float cross_value = cross.type == UI_SIZE_FIXED
            ? cross.value
            : clamp_non_negative(
                  (horizontal ? content_height : content_width)
                  - cross_margin(child_config, direction));

        float leading_main = horizontal
            ? child_config->margin.left
            : child_config->margin.top;
        float trailing_main = horizontal
            ? child_config->margin.right
            : child_config->margin.bottom;
        float leading_cross = horizontal
            ? child_config->margin.top
            : child_config->margin.left;

        cursor += leading_main;

        float child_x;
        float child_y;

        if (horizontal) {
            child_x = content_x + cursor;
            child_y = content_y + leading_cross;
        } else {
            child_x = content_x + leading_cross;
            child_y = content_y + cursor;
        }

        cursor += main_value + trailing_main + config->gap;

        /* Map the direction-relative main/cross extents back onto
         * absolute width/height for the child. */
        float child_w = horizontal ? main_value : cross_value;
        float child_h = horizontal ? cross_value : main_value;

        ui_layout_frame(child, child_x, child_y, child_w, child_h);
    }
}
