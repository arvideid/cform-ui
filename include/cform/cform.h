#ifndef CFORM_H
#define CFORM_H

#include <stdbool.h>
#include <stddef.h>

typedef struct UI_Context UI_Context;
typedef struct UI_Frame UI_Frame;

typedef enum {
    UI_SIZE_FIXED,
    UI_SIZE_FILL
} UI_SizeType;

typedef struct {
    UI_SizeType type;
    float value;
} UI_Size;

typedef struct {
    float top;
    float right;
    float bottom;
    float left;
} UI_Edges;

typedef enum {
    UI_HORIZONTAL,
    UI_VERTICAL,
    UI_OVERLAY
} UI_Direction;

typedef enum {
    UI_ALIGN_START,
    UI_ALIGN_CENTER,
    UI_ALIGN_END,
    UI_ALIGN_STRETCH
} UI_Align;

typedef enum {
    UI_JUSTIFY_START,
    UI_JUSTIFY_CENTER,
    UI_JUSTIFY_END,
    UI_JUSTIFY_SPACE_BETWEEN,
    UI_JUSTIFY_SPACE_AROUND,
    UI_JUSTIFY_SPACE_EVENLY
} UI_Justify;

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} UI_Color;

typedef void (*UI_ClickHandler)(UI_Frame *frame, void *user_data);

typedef struct {
    UI_Size width;
    UI_Size height;

    /* Size clamps applied after flex distribution.
     * A min of 0 leaves that side unbounded; a max of 0 means "no maximum". */
    float min_width;
    float max_width;
    float min_height;
    float max_height;

    UI_Edges padding;
    UI_Edges margin;

    float gap;

    UI_Direction direction;

    /* Cross-axis placement of each child within the parent's content box. */
    UI_Align align_items;

    /* Main-axis distribution of leftover space around and between children. */
    UI_Justify justify_content;

    UI_Color background;

    /* Decorative stroke drawn inside the frame's rect; does not affect layout.
     * A width of 0 hides the border; a radius of 0 keeps sharp corners. */
    UI_Color border_color;
    float border_width;
    float corner_radius;

    /* Fires on a press+release pair that ends inside this frame. A NULL
     * handler makes the frame non-clickable but it still tracks hover. */
    UI_ClickHandler on_click;
    void *user_data;
} UI_FrameConfig;

UI_Context *ui_create(void);
void ui_destroy(UI_Context *ctx);

UI_Frame *ui_frame(
    UI_Context *ctx,
    UI_FrameConfig config
);

void ui_add(UI_Frame *parent, UI_Frame *child);

/* Insert at index, shifting later siblings right. Out-of-range indices clamp
 * to [0, child_count], so 0 prepends and any large value appends. */
void ui_insert(UI_Frame *parent, int index, UI_Frame *child);

/* Detach without freeing. The frame stays registered on the context and is
 * still released by ui_destroy. Safe to call for a child that is not a
 * member of parent (silent no-op). */
void ui_remove(UI_Frame *parent, UI_Frame *child);

/* Replace a frame's config in place. Layout recomputes on the next pass;
 * children and tree position are untouched. */
void ui_update(UI_Frame *frame, UI_FrameConfig config);

/* Deepest, topmost frame whose rect contains (x, y), using the layout
 * produced by the most recent ui_layout or ui_render pass. Returns NULL
 * before the first layout or when the point misses the root. */
UI_Frame *ui_hit_test(UI_Context *ctx, float x, float y);

void ui_render(UI_Context *ctx, UI_Frame *root);

/* Pointer interaction state, refreshed by ui_render's event loop. */
bool ui_is_hovered(const UI_Frame *frame);
bool ui_is_pressed(const UI_Frame *frame);

#define UI_FIXED(value) \
    ((UI_Size){ UI_SIZE_FIXED, (float)(value) })

#define UI_FILL \
    ((UI_Size){ UI_SIZE_FILL, 0 })

#define UI_ALL(value) \
    ((UI_Edges){value, value, value, value})

#define UI_EDGES(t, r, b, l) \
    ((UI_Edges){t, r, b, l})

#define UI_COLOR(r, g, b) \
    ((UI_Color){r, g, b, 255})

#endif
