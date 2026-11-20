#include "internal.h"

/* Pixels a single wheel notch moves the content. SDL reports a discrete mouse
 * as ±1 per notch and a trackpad as fractional, so scaling here gives both a
 * usable step without the renderer knowing about notch semantics. */
#define WHEEL_LINE_STEP 60.0f

/* Deepest, topmost frame whose rect contains (x, y). Children are scanned in
 * reverse so that later siblings, which draw on top, win the tie. */
static UI_Frame *hit_test(UI_Frame *frame, float x, float y) {
    if (x < frame->x || x >= frame->x + frame->width) {
        return NULL;
    }

    if (y < frame->y || y >= frame->y + frame->height) {
        return NULL;
    }

    for (int i = frame->child_count - 1; i >= 0; i--) {
        UI_Frame *hit = hit_test(frame->children[i], x, y);

        if (hit != NULL) {
            return hit;
        }
    }

    return frame;
}

UI_Frame *ui_hit_test(UI_Context *ctx, float x, float y) {
    if (ctx == NULL || ctx->root == NULL) {
        return NULL;
    }

    return hit_test(ctx->root, x, y);
}

static void set_hovered(UI_Context *ctx, UI_Frame *next) {
    if (ctx->hovered == next) {
        return;
    }

    if (ctx->hovered != NULL) {
        ctx->hovered->hovered = false;
    }

    ctx->hovered = next;

    if (next != NULL) {
        next->hovered = true;
    }
}

static void clear_pressed(UI_Context *ctx) {
    if (ctx->pressed_frame != NULL) {
        ctx->pressed_frame->pressed = false;
        ctx->pressed_frame = NULL;
    }
}

static void set_focused(UI_Context *ctx, UI_Frame *next) {
    if (ctx->focused == next) {
        return;
    }

    if (ctx->focused != NULL) {
        ctx->focused->focused = false;
    }

    ctx->focused = next;

    if (next != NULL) {
        next->focused = true;
    }
}

void ui_pointer_move(UI_Context *ctx, float x, float y) {
    set_hovered(ctx, ui_hit_test(ctx, x, y));
}

void ui_pointer_leave(UI_Context *ctx) {
    set_hovered(ctx, NULL);
    clear_pressed(ctx);
}

void ui_pointer_down(UI_Context *ctx, float x, float y) {
    UI_Frame *hit = ui_hit_test(ctx, x, y);

    if (hit == NULL || hit->config.on_click == NULL) {
        return;
    }

    clear_pressed(ctx);
    ctx->pressed_frame = hit;
    hit->pressed = true;
}

UI_Frame *ui_pointer_up(UI_Context *ctx, float x, float y) {
    UI_Frame *pressed = ctx->pressed_frame;

    if (pressed == NULL) {
        return NULL;
    }

    pressed->pressed = false;
    ctx->pressed_frame = NULL;

    if (ui_hit_test(ctx, x, y) == pressed) {
        pressed->config.on_click(pressed, pressed->config.user_data);
        return pressed;
    }

    return NULL;
}

/* Walks up from a frame to the nearest scrollable ancestor, or NULL. */
static UI_Frame *scrollable_ancestor(UI_Frame *frame) {
    while (frame != NULL && !frame->config.scrollable) {
        frame = frame->parent;
    }

    return frame;
}

/* First frame that scrolls in pre-order, used when neither focus nor hover
 * points inside a scrollable region. */
static UI_Frame *first_scrollable(UI_Frame *frame) {
    if (frame->config.scrollable) {
        return frame;
    }

    for (int i = 0; i < frame->child_count; i++) {
        UI_Frame *hit = first_scrollable(frame->children[i]);

        if (hit != NULL) {
            return hit;
        }
    }

    return NULL;
}

/* Shifts the nearest scrollable under (x, y) by a scaled wheel delta. Clamping
 * to the content extent happens in the next layout pass. */
void ui_pointer_wheel(UI_Context *ctx, float x, float y, float dx, float dy) {
    UI_Frame *frame = scrollable_ancestor(ui_hit_test(ctx, x, y));

    if (frame == NULL) {
        return;
    }

    frame->scroll_x -= dx * WHEEL_LINE_STEP;
    frame->scroll_y -= dy * WHEEL_LINE_STEP;
}

/* The scrollable a keyboard scroll drives: the focused frame's region, else the
 * hovered frame's, else the first scrollable in the tree. */
static UI_Frame *resolve_scroll_target(UI_Context *ctx) {
    UI_Frame *target = NULL;

    if (ctx->focused != NULL) {
        target = scrollable_ancestor(ctx->focused);
    }

    if (target == NULL && ctx->hovered != NULL) {
        target = scrollable_ancestor(ctx->hovered);
    }

    if (target == NULL && ctx->root != NULL) {
        target = first_scrollable(ctx->root);
    }

    return target;
}

void ui_scroll_by(UI_Context *ctx, float dx, float dy) {
    if (ctx == NULL) {
        return;
    }

    UI_Frame *target = resolve_scroll_target(ctx);

    if (target == NULL) {
        return;
    }

    target->scroll_x += dx;
    target->scroll_y += dy;
}

void ui_scroll_extreme(UI_Context *ctx, bool end) {
    if (ctx == NULL) {
        return;
    }

    UI_Frame *target = resolve_scroll_target(ctx);

    if (target == NULL) {
        return;
    }

    target->scroll_x = end ? target->content_width : 0.0f;
    target->scroll_y = end ? target->content_height : 0.0f;
}

static bool is_focusable(const UI_Frame *frame) {
    return frame->config.on_click != NULL || frame->config.link_url != NULL;
}

static UI_Frame *first_focusable(UI_Frame *frame) {
    if (is_focusable(frame)) {
        return frame;
    }

    for (int i = 0; i < frame->child_count; i++) {
        UI_Frame *hit = first_focusable(frame->children[i]);

        if (hit != NULL) {
            return hit;
        }
    }

    return NULL;
}

static UI_Frame *last_focusable(UI_Frame *frame) {
    for (int i = frame->child_count - 1; i >= 0; i--) {
        UI_Frame *hit = last_focusable(frame->children[i]);

        if (hit != NULL) {
            return hit;
        }
    }

    return is_focusable(frame) ? frame : NULL;
}

/* First focusable strictly after `after` in pre-order. *seen flips true once we
 * pass it, so `after`'s own subtree is scanned in reading order. */
static UI_Frame *scan_after(UI_Frame *frame, UI_Frame *after, bool *seen) {
    if (frame == after) {
        *seen = true;
    } else if (*seen && is_focusable(frame)) {
        return frame;
    }

    for (int i = 0; i < frame->child_count; i++) {
        UI_Frame *hit = scan_after(frame->children[i], after, seen);

        if (hit != NULL) {
            return hit;
        }
    }

    return NULL;
}

/* Returns true once `before` is reached, accumulating the last focusable seen
 * before it into *prev. */
static bool scan_before(UI_Frame *frame, UI_Frame *before, UI_Frame **prev) {
    if (frame == before) {
        return true;
    }

    if (is_focusable(frame)) {
        *prev = frame;
    }

    for (int i = 0; i < frame->child_count; i++) {
        if (scan_before(frame->children[i], before, prev)) {
            return true;
        }
    }

    return false;
}

UI_Frame *ui_focus_next(UI_Context *ctx, bool reverse) {
    if (ctx == NULL || ctx->root == NULL) {
        return NULL;
    }

    UI_Frame *current = ctx->focused;
    UI_Frame *next = NULL;

    if (reverse) {
        UI_Frame *prev = NULL;

        if (current != NULL && scan_before(ctx->root, current, &prev) && prev != NULL) {
            next = prev;
        } else {
            next = last_focusable(ctx->root);
        }
    } else {
        bool seen = false;

        if (current != NULL) {
            next = scan_after(ctx->root, current, &seen);
        }

        if (next == NULL) {
            next = first_focusable(ctx->root);
        }
    }

    set_focused(ctx, next);

    return next;
}

UI_Frame *ui_activate_focused(UI_Context *ctx) {
    UI_Frame *frame = (ctx != NULL) ? ctx->focused : NULL;

    if (frame == NULL) {
        return NULL;
    }

    if (frame->config.on_click != NULL) {
        frame->config.on_click(frame, frame->config.user_data);
    }

    return frame;
}

void ui_clear_focus(UI_Context *ctx) {
    if (ctx != NULL) {
        set_focused(ctx, NULL);
    }
}

bool ui_is_hovered(const UI_Frame *frame) {
    return frame != NULL && frame->hovered;
}

bool ui_is_pressed(const UI_Frame *frame) {
    return frame != NULL && frame->pressed;
}
