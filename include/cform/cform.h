#ifndef CFORM_H
#define CFORM_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef struct UI_Context UI_Context;
typedef struct UI_Frame UI_Frame;

/* Sizing */
typedef enum {
    UI_SIZE_FIXED,
    UI_SIZE_FILL
} UI_SizeType;

typedef struct {
    UI_SizeType type;
    float value;
} UI_Size;

/* Spacing */
typedef struct {
    float top;
    float right;
    float bottom;
    float left;
} UI_Edges;

/* Layout direction */
typedef enum {
    UI_HORIZONTAL,
    UI_VERTICAL
} UI_Direction;

/* Color */
typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} UI_Color;

/* Frame configuration */
typedef struct {
    const char *name;

    UI_Size width;
    UI_Size height;

    UI_Edges padding;
    UI_Edges margin;

    float gap;

    UI_Direction direction;

    UI_Color background;
} UI_FrameConfig;

/* Context */
UI_Context *ui_create(void);
void ui_destroy(UI_Context *ctx);

/* Frame creation */
UI_Frame *ui_frame(
    UI_Context *ctx,
    UI_FrameConfig config
);

/* Tree management */
void ui_add(UI_Frame *parent, UI_Frame *child);

/* Rendering */
void ui_render(UI_Context *ctx, UI_Frame *root);

/* Sizing helpers */
#define UI_FIXED(value) \
    ((UI_Size){ UI_SIZE_FIXED, (float)(value) })

#define UI_FILL \
    ((UI_Size){ UI_SIZE_FILL, 0 })

/* Spacing helpers */
#define UI_ALL(value) \
    ((UI_Edges){value, value, value, value})

#define UI_EDGES(t, r, b, l) \
    ((UI_Edges){t, r, b, l})

/* Color helpers */
#define UI_COLOR(r, g, b) \
    ((UI_Color){r, g, b, 255})

#endif