#include "internal.h"

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

void ui_pointer_up(UI_Context *ctx, float x, float y) {
    UI_Frame *pressed = ctx->pressed_frame;

    if (pressed == NULL) {
        return;
    }

    pressed->pressed = false;
    ctx->pressed_frame = NULL;

    if (ui_hit_test(ctx, x, y) == pressed) {
        pressed->config.on_click(pressed, pressed->config.user_data);
    }
}

bool ui_is_hovered(const UI_Frame *frame) {
    return frame != NULL && frame->hovered;
}

bool ui_is_pressed(const UI_Frame *frame) {
    return frame != NULL && frame->pressed;
}
