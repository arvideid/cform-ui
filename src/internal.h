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
};

struct UI_Context {
    int window_width;
    int window_height;

    /* Ownership of every frame created through ui_frame(), so that
     * ui_destroy() can release them in one place. */
    UI_Frame **frames;
    int frame_count;
    int frame_capacity;
};

/* Layout is resolved by the core and consumed by the renderer. */
void ui_layout_frame(
    UI_Frame *frame,
    float x,
    float y,
    float width,
    float height
);

#endif
