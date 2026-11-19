#ifndef CFORM_INTERNAL_H
#define CFORM_INTERNAL_H

#include "cform/cform.h"

struct UI_Frame {
    UI_FrameConfig config;

    struct UI_Frame *parent;
    struct UI_Frame **children;

    int child_count;
    int child_capacity;

    float x;
    float y;
    float width;
    float height;

    bool hovered;
    bool pressed;
};

struct UI_Context {
    int window_width;
    int window_height;

    /* Ownership of every frame created through ui_frame(), so that
     * ui_destroy() can release them in one place. */
    UI_Frame **frames;
    int frame_count;
    int frame_capacity;

    /* Pointer interaction state, set by ui_render's event pump. */
    UI_Frame *root;
    UI_Frame *hovered;
    UI_Frame *pressed_frame;
};

/* Layout is resolved by the core and consumed by the renderer. */
void ui_layout_frame(
    UI_Frame *frame,
    float x,
    float y,
    float width,
    float height
);

/* Pointer event translation. The renderer owns raw SDL input and calls these
 * with window-relative coordinates; the core resolves hit tests, hover
 * transitions and click dispatch. `ui_render` registers the root before the
 * first call; hit tests are a no-op until then. */
void ui_pointer_move(UI_Context *ctx, float x, float y);
void ui_pointer_leave(UI_Context *ctx);
void ui_pointer_down(UI_Context *ctx, float x, float y);
void ui_pointer_up(UI_Context *ctx, float x, float y);

#endif
