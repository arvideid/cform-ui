#include <string.h>

#include "cf/paint.h"
#include "cf/text.h"

/*
 * The list's first allocation. A frame's command count is unknown ahead of the
 * walk, so the list grows by doubling from here rather than being sized once.
 */
#define PAINT_FIRST_CAPACITY 64u

/* Whether the box reaches the clip at all. A zero-extent box does not. */
static bool reaches_viewport(cf_rect box, cf_rect viewport) {
    return box.x < viewport.x + viewport.width && viewport.x < box.x + box.width &&
           box.y < viewport.y + viewport.height && viewport.y < box.y + box.height;
}

/* The part of the box that survives the clip. The walk never asks for a
 * disjoint pair - it has already refused the box outright. */
static cf_rect clipped_to(cf_rect box, cf_rect clip) {
    const float left = box.x > clip.x ? box.x : clip.x;
    const float top = box.y > clip.y ? box.y : clip.y;
    const float box_right = box.x + box.width;
    const float box_bottom = box.y + box.height;
    const float clip_right = clip.x + clip.width;
    const float clip_bottom = clip.y + clip.height;
    const float right = box_right < clip_right ? box_right : clip_right;
    const float bottom = box_bottom < clip_bottom ? box_bottom : clip_bottom;

    return (cf_rect){.x = left, .y = top, .width = right - left, .height = bottom - top};
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
    /* The active clip: the screen until a box that clips its content is
     * entered, then that box's own rect. Inside it, fills cut to the clip and
     * every command names it, so a stroke and a line can be cut without being
     * moved. */
    cf_rect clip;
    bool clipping;
    cf_command_list *out;
    bool truncated;
} paint_walk;

/*
 * The clip a command carries. Outside every clipping box the walk names none,
 * rather than the viewport it happens to hold: the screen bounds itself, and a
 * backend told to scissor there would pay a state change to say nothing.
 */
static cf_rect carried_clip(const paint_walk *walk) {
    return walk->clipping ? walk->clip : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
}

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
 * A box whose measure slot is the text walk paints its own lines. The wrap
 * width is the content box the style insets from the rect - percentages
 * resolved against the box's own width, the reference the paint input
 * carries - and each line lands one line_height below the last, where the
 * measure that sized the box counted it. A color that is not stated leaves
 * the box without text, which is why the check reads the color first.
 */
static void emit_text(paint_walk *walk, uint32_t slot, const cf_style *style, cf_rect box) {
    if (!style->color.present || walk->tree->measures == NULL ||
        walk->tree->measures[slot] != cf_text_measure) {
        return;
    }

    const cf_text_content *content =
        walk->tree->measure_data == NULL ? NULL : walk->tree->measure_data[slot];
    const cf_edges insets = cf_box_resolve(style->padding, box.width);
    const cf_edges stroke = cf_box_resolve(style->border, box.width);
    const float left = insets.left + stroke.left;
    const float top = insets.top + stroke.top;
    const float wrap = box.width - left - insets.right - stroke.right;

    if (wrap <= 0.0f) {
        return;
    }

    /* The walk answers the count first: a stack that cannot measure lays no
     * lines, and the usable-stack check inside it is the one paint borrows
     * before it reads the primary font's rhythm. The walk is deterministic,
     * so the second pass fills every slot the count names. */
    const uint32_t count = cf_text_lines(content, wrap, NULL, 0u);

    if (count == 0u) {
        return;
    }

    cf_text_line *lines = cf_alloc(walk->scratch, (size_t)count * sizeof *lines);

    if (lines == NULL) {
        walk->truncated = true;
        return;
    }

    (void)cf_text_lines(content, wrap, lines, count);

    const cf_font font = content->stack.fonts[0];
    const float line_height = font.ops->metrics(font.handle).line_height;

    for (uint32_t i = 0u; i < count; ++i) {
        const float y = box.y + top + (float)i * line_height;

        if (y + line_height <= walk->clip.y) {
            continue; /* wholly above the clip */
        }

        if (y >= walk->clip.y + walk->clip.height) {
            break; /* the lines only descend */
        }

        cf_command *command = next_command(walk);

        if (command == NULL) {
            return;
        }

        *command = (cf_command){.kind = CF_COMMAND_TEXT,
                                .rect = {box.x + left, y, lines[i].width, line_height},
                                .clip = carried_clip(walk),
                                .color = style->color.value,
                                .text = lines[i].text,
                                .font = font,
                                .ellipsis = lines[i].ellipsis};
    }
}

cf_size cf_image_measure(void *content, cf_available available, cf_intrinsic kind) {
    const cf_image_content *image = content;

    if (image == NULL) {
        return (cf_size){.width = 0.0f, .height = 0.0f};
    }

    (void)available;
    (void)kind;
    return (cf_size){.width = image->width, .height = image->height};
}

/*
 * An image paints its texture into its box: the measure that sized the box
 * answered the content's own extent, so the rect is where the texture goes.
 * Inside a clip it cuts like a fill - the texture draws where the rect says.
 */
static void emit_image(paint_walk *walk, uint32_t slot, cf_rect box) {
    if (walk->tree->measures == NULL || walk->tree->measures[slot] != cf_image_measure) {
        return;
    }

    const cf_image_content *content = walk->tree->measure_data[slot];

    if (content == NULL) {
        return;
    }

    cf_command *command = next_command(walk);

    if (command == NULL) {
        return;
    }

    *command = (cf_command){.kind = CF_COMMAND_IMAGE,
                            .rect = walk->clipping ? clipped_to(box, walk->clip) : box,
                            .clip = carried_clip(walk),
                            .texture = content->texture};
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

    if (!reaches_viewport(box, walk->clip)) {
        return;
    }

    const float radius = clamped_radius(style->radius, box.width, box.height);

    if (style->background.present) {
        cf_command *fill = next_command(walk);

        if (fill == NULL) {
            return;
        }

        *fill = (cf_command){.kind = CF_COMMAND_FILL,
                             .rect = walk->clipping ? clipped_to(box, walk->clip) : box,
                             .clip = carried_clip(walk),
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
                               .clip = carried_clip(walk),
                               .color = style->border_color.value,
                               .widths = widths,
                               .radius = radius};
    }

    emit_text(walk, slot, style, box);
    emit_image(walk, slot, box);

    /* A box that clips its content bounds the descent: the children see the
     * box's rect as their clip, narrowed by whatever clipped their parent.
     * A border strokes its true edges and names this clip to be cut by -
     * cutting the rect instead would draw lines the box never declared at the
     * cut. */
    const cf_rect outer_clip = walk->clip;
    const bool outer_clipping = walk->clipping;

    if (style->clips_content) {
        walk->clip = clipped_to(box, walk->clip);
        walk->clipping = true;
    }

    const uint32_t first = walk->tree->child_start[slot];
    const uint32_t end = walk->tree->child_start[slot + 1u];

    for (uint32_t i = first; i < end; ++i) {
        emit_box(walk, walk->tree->child_slots[i]);
    }

    walk->clip = outer_clip;
    walk->clipping = outer_clipping;
}

void cf_paint(cf_arena *scratch, const cf_tree *tree, const cf_rect *rects, uint32_t root,
              cf_rect viewport, cf_command_list *out) {
    if (scratch == NULL || tree == NULL || rects == NULL || out == NULL ||
        root >= tree->node_count) {
        return;
    }

    paint_walk walk = {
        .scratch = scratch, .tree = tree, .rects = rects, .clip = viewport, .out = out};

    emit_box(&walk, root);

    if (walk.truncated) {
        cf_logf(CF_LOG_PAINT, "paint: scratch refused the command list at %u commands", out->count);
    }
}
