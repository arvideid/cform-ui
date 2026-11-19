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

    /* Scroll offset applied to a scrollable frame's children during layout.
     * content_width/height are the natural child extents, recomputed each
     * pass, so the wheel handler can clamp the offset without a second pass. */
    float scroll_x;
    float scroll_y;
    float content_width;
    float content_height;

    bool hovered;
    bool pressed;
    bool focused;
};

struct UI_Context {
    /* Ownership of every frame created through ui_frame(), so that
     * ui_destroy() can release them in one place. */
    UI_Frame **frames;
    int frame_count;
    int frame_capacity;

    /* Pointer interaction state, set by ui_render's event pump. */
    UI_Frame *root;
    UI_Frame *hovered;
    UI_Frame *pressed_frame;
    UI_Frame *focused;
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

/* Returns the frame whose click completed (press and release in the same
 * frame), or NULL when no click fired. The renderer opens its link_url. */
UI_Frame *ui_pointer_up(UI_Context *ctx, float x, float y);

/* Applies a wheel delta to the nearest scrollable frame under (x, y). */
void ui_pointer_wheel(UI_Context *ctx, float x, float y, float dx, float dy);

/* Keyboard focus. A frame is focusable when it carries a click handler or a
 * link. ui_focus_next walks the tree in reading order (reverse for Shift+Tab),
 * wraps at the ends, and returns the newly focused frame. ui_activate_focused
 * runs the focused frame's click handler and returns it so the renderer can
 * open its link_url. ui_clear_focus drops focus. */
UI_Frame *ui_focus_next(UI_Context *ctx, bool reverse);
UI_Frame *ui_activate_focused(UI_Context *ctx);
void ui_clear_focus(UI_Context *ctx);

/* Keyboard scrolling. Deltas apply to the focused frame's scrollable ancestor,
 * falling back to the hovered one, then the first scrollable in the tree.
 * ui_scroll_extreme jumps to the start (end false) or the end (end true). */
void ui_scroll_by(UI_Context *ctx, float dx, float dy);
void ui_scroll_extreme(UI_Context *ctx, bool end);

/* Applies a frame's own opacity and every ancestor's to a color's alpha.
 * Pure so the renderer stays a thin draw pass and the math stays testable
 * without a window. */
UI_Color ui_color_scale_alpha(UI_Color color, float opacity);

#endif
