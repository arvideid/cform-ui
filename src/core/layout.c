#include "internal.h"

static float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

/* min wins when min > max so a misconfigured constraint still resolves. */
static float clamp_size(float value, float min, float max) {
    if (min > 0.0f && value < min) {
        value = min;
    }

    if (max > 0.0f && value > max) {
        value = max;
    }

    return value;
}

/* The child's sizing axis that runs along the parent's layout direction. */
static UI_Size main_axis_size(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->width : c->height;
}

/* The child's sizing axis perpendicular to the parent's layout direction. */
static UI_Size cross_axis_size(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->height : c->width;
}

static float main_axis_min(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->min_width : c->min_height;
}

static float main_axis_max(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->max_width : c->max_height;
}

static float cross_axis_min(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->min_height : c->min_width;
}

static float cross_axis_max(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL ? c->max_height : c->max_width;
}

static float main_margin(const UI_FrameConfig *c, UI_Direction direction) {
    return direction == UI_HORIZONTAL
        ? c->margin.left + c->margin.right
        : c->margin.top + c->margin.bottom;
}

typedef struct {
    float before;
    float between;
} JustifySpacing;

/* count is >= 1 whenever this is called. AROUND and EVENLY divide by count
 * or count+1 so halves land on both ends even for a single child. */
static JustifySpacing justify_spacing(UI_Justify mode, int count, float extra) {
    JustifySpacing spacing = { 0.0f, 0.0f };

    switch (mode) {
    case UI_JUSTIFY_START:
        break;
    case UI_JUSTIFY_CENTER:
        spacing.before = extra * 0.5f;
        break;
    case UI_JUSTIFY_END:
        spacing.before = extra;
        break;
    case UI_JUSTIFY_SPACE_BETWEEN:
        if (count > 1) {
            spacing.between = extra / (float)(count - 1);
        }
        break;
    case UI_JUSTIFY_SPACE_AROUND:
        spacing.between = extra / (float)count;
        spacing.before = spacing.between * 0.5f;
        break;
    case UI_JUSTIFY_SPACE_EVENLY:
        spacing.before = extra / (float)(count + 1);
        spacing.between = spacing.before;
        break;
    }

    return spacing;
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
            fixed_total += clamp_size(size.value,
                main_axis_min(child, direction),
                main_axis_max(child, direction));
        } else {
            fill_count++;
        }
    }

    available = clamp_non_negative(available);

    float fill_size = fill_count > 0
        ? clamp_non_negative((available - fixed_total) / (float)fill_count)
        : 0.0f;

    /* Sum post-clamp main-axis consumption so justify knows what's left
     * over once every child has settled at its resolved size. */
    float used = config->gap * (float)(count - 1);

    for (int i = 0; i < count; i++) {
        const UI_FrameConfig *child = &frame->children[i]->config;
        UI_Size size = main_axis_size(child, direction);
        float raw = size.type == UI_SIZE_FIXED ? size.value : fill_size;
        used += clamp_size(raw,
            main_axis_min(child, direction),
            main_axis_max(child, direction))
              + main_margin(child, direction);
    }

    JustifySpacing justify = justify_spacing(config->justify_content, count,
        clamp_non_negative(track_size - used));

    float cursor = justify.before;

    for (int i = 0; i < count; i++) {
        UI_Frame *child = frame->children[i];
        const UI_FrameConfig *child_config = &child->config;

        UI_Size main = main_axis_size(child_config, direction);
        float main_value = main.type == UI_SIZE_FIXED ? main.value : fill_size;
        main_value = clamp_size(main_value,
            main_axis_min(child_config, direction),
            main_axis_max(child_config, direction));

        UI_Size cross = cross_axis_size(child_config, direction);
        float cross_extent = horizontal ? content_height : content_width;
        float leading_cross = horizontal
            ? child_config->margin.top
            : child_config->margin.left;
        float trailing_cross = horizontal
            ? child_config->margin.bottom
            : child_config->margin.right;
        float cross_room = clamp_non_negative(
            cross_extent - leading_cross - trailing_cross);

        bool fills_cross = cross.type != UI_SIZE_FIXED
            || config->align_items == UI_ALIGN_STRETCH;
        float cross_value = clamp_size(fills_cross ? cross_room : cross.value,
            cross_axis_min(child_config, direction),
            cross_axis_max(child_config, direction));

        float cross_offset = leading_cross;
        float extra_cross = clamp_non_negative(cross_room - cross_value);

        if (extra_cross > 0.0f) {
            if (config->align_items == UI_ALIGN_CENTER) {
                cross_offset += extra_cross * 0.5f;
            } else if (config->align_items == UI_ALIGN_END) {
                cross_offset += extra_cross;
            }
        }

        float leading_main = horizontal
            ? child_config->margin.left
            : child_config->margin.top;
        float trailing_main = horizontal
            ? child_config->margin.right
            : child_config->margin.bottom;

        cursor += leading_main;

        float child_x;
        float child_y;

        if (horizontal) {
            child_x = content_x + cursor;
            child_y = content_y + cross_offset;
        } else {
            child_x = content_x + cross_offset;
            child_y = content_y + cursor;
        }

        cursor += main_value + trailing_main;

        if (i + 1 < count) {
            cursor += config->gap + justify.between;
        }

        /* Map the direction-relative main/cross extents back onto
         * absolute width/height for the child. */
        float child_w = horizontal ? main_value : cross_value;
        float child_h = horizontal ? cross_value : main_value;

        ui_layout_frame(child, child_x, child_y, child_w, child_h);
    }
}
