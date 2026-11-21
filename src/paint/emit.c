#include <string.h>

#include "cf/paint.h"

/*
 * The list's first allocation. A frame's command count is unknown ahead of the
 * walk, so the list grows by doubling from here rather than being sized once.
 */
#define PAINT_FIRST_CAPACITY 64u

/* Whether the box reaches the viewport at all. A zero-extent box does not. */
static bool reaches_viewport(cf_rect box, cf_rect viewport) {
    return box.x < viewport.x + viewport.width && viewport.x < box.x + box.width &&
           box.y < viewport.y + viewport.height && viewport.y < box.y + box.height;
}

/*
 * CSS clamps a corner radius to half the box's shorter extent: a radius past
 * that would curl the edges past each other instead of rounding a corner.
 */
static float clamped_radius(float radius, float width, float height) {
    const float half = (width < height ? width : height) * 0.5f;

    if (radius < 0.0f) {
        return 0.0f;
    }

    return radius > half ? half : radius;
}

typedef struct paint_walk {
    cf_arena *scratch;
    const cf_tree *tree;
    const cf_rect *rects;
    cf_rect viewport;
    cf_command_list *out;
    bool truncated;
} paint_walk;

/*
 * The next slot in the list, growing it by doubling first. NULL once the arena
 * has refused - the walk checks the truncation flag instead of retrying, so
 * exhaustion stops the walk rather than asking the arena once per node.
 */
static cf_command *next_command(paint_walk *walk) {
    cf_command_list *list = walk->out;

    if (list->count == list->capacity) {
        const uint32_t capacity =
            list->capacity < PAINT_FIRST_CAPACITY ? PAINT_FIRST_CAPACITY : list->capacity * 2u;
        cf_command *commands = cf_alloc(walk->scratch, (size_t)capacity * sizeof *commands);

        if (commands == NULL) {
            walk->truncated = true;
            return NULL;
        }

        memcpy(commands, list->commands, (size_t)list->count * sizeof *commands);
        list->commands = commands;
        list->capacity = capacity;
    }

    return &list->commands[list->count++];
}

/*
 * One box: fill first, then its own border, then the children that paint over
 * both. A border with a color but no width, or widths but no color, paints
 * nothing - half a border is not a border.
 */
static void emit_box(paint_walk *walk, uint32_t slot) {
    if (walk->truncated) {
        return;
    }

    const cf_style *style = &walk->tree->styles[slot];
    const cf_rect box = walk->rects[slot];

    if (!reaches_viewport(box, walk->viewport)) {
        return;
    }

    const float radius = clamped_radius(style->radius, box.width, box.height);

    if (style->background.present) {
        cf_command *fill = next_command(walk);

        if (fill == NULL) {
            return;
        }

        *fill = (cf_command){.kind = CF_COMMAND_FILL,
                             .rect = box,
                             .color = style->background.value,
                             .radius = radius};
    }

    /* A width written as a percentage resolves against the box's own width:
     * the containing block it means is not part of the paint input. */
    const cf_edges widths = cf_box_resolve(style->border, box.width);
    const bool stroked =
        style->border_color.present &&
        (widths.left > 0.0f || widths.top > 0.0f || widths.right > 0.0f || widths.bottom > 0.0f);

    if (stroked) {
        cf_command *border = next_command(walk);

        if (border == NULL) {
            return;
        }

        *border = (cf_command){.kind = CF_COMMAND_BORDER,
                               .rect = box,
                               .color = style->border_color.value,
                               .widths = widths,
                               .radius = radius};
    }

    const uint32_t first = walk->tree->child_start[slot];
    const uint32_t end = walk->tree->child_start[slot + 1u];

    for (uint32_t i = first; i < end; ++i) {
        emit_box(walk, walk->tree->child_slots[i]);
    }
}

void cf_paint(cf_arena *scratch, const cf_tree *tree, const cf_rect *rects, uint32_t root,
              cf_rect viewport, cf_command_list *out) {
    if (scratch == NULL || tree == NULL || rects == NULL || out == NULL ||
        root >= tree->node_count) {
        return;
    }

    paint_walk walk = {
        .scratch = scratch, .tree = tree, .rects = rects, .viewport = viewport, .out = out};

    emit_box(&walk, root);

    if (walk.truncated) {
        cf_logf(CF_LOG_PAINT, "paint: scratch refused the command list at %u commands", out->count);
    }
}
