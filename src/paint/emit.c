#include <string.h>

#include "cf/paint.h"
#include "cf/text.h"

#include "layout/rect.h"

/*
 * The list's first allocation. A frame's command count is unknown ahead of the
 * walk, so the list grows by doubling from here rather than being sized once.
 */
#define PAINT_FIRST_CAPACITY 64u

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
     * entered, then that box's own rect. Inside it every command names the
     * clip, so whatever has geometry of its own - a stroke, a baseline, a
     * rounded corner, a texture - is cut without being moved. */
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
 * The rect a fill arrives with inside a clip. A square is the whole of what the
 * command draws, so the walk cuts it and the device draws fewer pixels for the
 * same picture. A rounded one is not: the arc is arithmetic on the rect, so
 * cutting first rounds the cut box and its corner stops matching the border
 * stroked at the true one - which is the sliver of the parent that shows
 * through where a clip runs across a rounded box. Radius 0 is the only radius
 * a cut preserves.
 */
static cf_rect filled_rect(const paint_walk *walk, cf_rect box, float radius) {
    return walk->clipping && radius == 0.0f ? rect_intersect(box, walk->clip) : box;
}

/* A caret is a line, not a box: one point wide, the height of the line it
 * stands in. */
#define CARET_WIDTH 1.0f

/* The rule under a marked run, in the same unit: a run is underlined rather than
 * covered, and one point is what reads as a rule rather than a band at any line height
 * the walk lays. */
#define MARKED_RULE 1.0f

/* Where a walk's lines stand: the pen's start after the box's insets and the
 * caret's slide, the rhythm the lines step by, and the clip their commands
 * name. The marks a text box lays read the same four numbers its lines do. */
typedef struct text_view {
    float x;
    float y;
    float line_height;
    cf_rect clip;
} text_view;

/* The part of `rect` that survives `clip`, or an empty rect for a pair that
 * holds nothing in common - which the walk's own cut never asks for. */
static cf_rect cut_to(cf_rect rect, cf_rect clip) {
    const float left = rect.x > clip.x ? rect.x : clip.x;
    const float top = rect.y > clip.y ? rect.y : clip.y;
    const float right =
        rect.x + rect.width < clip.x + clip.width ? rect.x + rect.width : clip.x + clip.width;
    const float bottom =
        rect.y + rect.height < clip.y + clip.height ? rect.y + rect.height : clip.y + clip.height;

    if (right <= left || bottom <= top) {
        return (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    }

    return (cf_rect){.x = left, .y = top, .width = right - left, .height = bottom - top};
}

/*
 * A mark laid under or over the glyphs. It is a fill, so the walk cuts it to
 * the clip it names instead of asking the device to; a mark the clip would
 * erase lays no command at all.
 */
static void emit_mark(paint_walk *walk, cf_rect rect, cf_color color, cf_rect clip) {
    if (clip.width > 0.0f && clip.height > 0.0f) {
        rect = cut_to(rect, clip);

        if (rect.width <= 0.0f) {
            return;
        }
    }

    cf_command *command = next_command(walk);

    if (command == NULL) {
        return;
    }

    *command = (cf_command){.kind = CF_COMMAND_FILL, .rect = rect, .clip = clip, .color = color};
}

/* One line's byte range in the content's own coordinates: the lines the walk
 * answers point into the content's text, so the pair is arithmetic. */
static void line_range(const cf_text_content *content, const cf_text_line *line, uint32_t *first,
                       uint32_t *last) {
    *first = (uint32_t)(size_t)(line->text.chars - content->text.chars);
    *last = *first + line->text.length;
}

/*
 * Where a byte range crosses one laid line, as pen distance from that line's left edge.
 * False when the range is wholly on another line. Both ends go through
 * cf_text_caret_x, which clamps an offset into the line, so a range spanning lines needs
 * no case for the lines it only passes through: each answers its own two ends.
 */
static bool band_edges(const cf_text_content *content, const cf_text_line *line, uint32_t from,
                       uint32_t to, float *left, float *right) {
    uint32_t first = 0u;
    uint32_t last = 0u;

    line_range(content, line, &first, &last);

    if (to <= first || from >= last) {
        return false;
    }

    *left = cf_text_caret_x(content, line, from);
    *right = cf_text_caret_x(content, line, to);
    return true;
}

/* The band a line's part of a selection lays down, under the glyphs. */
static void emit_band(paint_walk *walk, cf_color color, const cf_text_content *content,
                      const cf_text_line *line, uint32_t from, uint32_t to, const text_view *view,
                      float y) {
    float left = 0.0f;
    float right = 0.0f;

    if (!band_edges(content, line, from, to, &left, &right)) {
        return;
    }

    emit_mark(walk, (cf_rect){view->x + left, y, right - left, view->line_height}, color,
              view->clip);
}

/*
 * The rule under a line's part of a marked run. One point along the row's bottom edge
 * rather than the whole band a selection takes: the bytes under it are readable text the
 * person is still choosing, and a fill over them would say they had been picked up.
 */
static void emit_rule(paint_walk *walk, cf_color color, const cf_text_content *content,
                      const cf_text_line *line, uint32_t from, uint32_t to, const text_view *view,
                      float y) {
    float left = 0.0f;
    float right = 0.0f;

    if (!band_edges(content, line, from, to, &left, &right)) {
        return;
    }

    emit_mark(
        walk,
        (cf_rect){view->x + left, y + view->line_height - MARKED_RULE, right - left, MARKED_RULE},
        color, view->clip);
}

/* Where the runs of one line land: the row the line holds, the tone its box states, and the
 * view that carries the row's left edge, its rhythm and the clip that cuts it. */
typedef struct line_sink {
    paint_walk *walk;
    const text_view *view;
    cf_color color;
    float y;
} line_sink;

/*
 * One run of a laid line becomes one command: the bytes a single font drew, the font itself,
 * and the rect that starts where the runs before it ended. A line one font draws whole is one
 * run, so a page whose stack never falls back keeps laying one command per line.
 */
static void emit_run(void *user, const cf_text_run *run) {
    const line_sink *sink = user;
    paint_walk *walk = sink->walk;

    if (walk->truncated) {
        return;
    }

    cf_command *command = next_command(walk);

    if (command == NULL) {
        return;
    }

    *command =
        (cf_command){.kind = CF_COMMAND_TEXT,
                     .rect = {sink->view->x + run->x, sink->y, run->width, sink->view->line_height},
                     .clip = sink->view->clip,
                     .color = sink->color,
                     .text = run->text,
                     .font = run->font,
                     .ellipsis = run->ellipsis};
}

/*
 * A box whose measure slot is the text walk paints its own lines. The wrap
 * width is the content box the style insets from the rect - percentages
 * resolved against the box's own width, the reference the paint input
 * carries - and each line lands one line_height below the last, where the
 * measure that sized the box counted it. A color that is not stated leaves
 * the box without text, which is why the check reads the color first.
 *
 * The marks an editor states ride the same geometry: the band under a selected
 * range goes down before the glyphs so the bytes stay readable over it, with the rule
 * under a run being decided, and the caret after them. Where the walk states a caret
 * the box slides to hold it,
 * and a box that clips its content cuts its own text at its padding box, which
 * is what lets a field scroll a value longer than itself.
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
    const float content_height = box.height - top - insets.bottom - stroke.bottom;

    /* The clip these commands name: what the walk carries, narrowed to the box's
     * own padding box when the box clips its content. Lines and marks keep their
     * true rects and let the clip do the cutting, because moving a baseline to
     * fit a box would draw text the content never said.
     */
    cf_rect clip = carried_clip(walk);

    if (cf_style_clips_content(style)) {
        const cf_rect padding_box = {box.x + stroke.left, box.y + stroke.top,
                                     box.width - stroke.left - stroke.right,
                                     box.height - stroke.top - stroke.bottom};
        clip = walk->clipping ? rect_intersect(padding_box, clip) : padding_box;
    }

    const cf_point slide = cf_text_scroll(content, lines, count, (cf_size){wrap, content_height});
    const text_view view = {.x = box.x + left - slide.x,
                            .y = box.y + top - slide.y,
                            .line_height = line_height,
                            .clip = clip};

    /* The pair in the order the band needs: a selection runs from whichever end
     * came first, whichever one the editor calls the mark. */
    const bool band = style->selection.present && content->caret.present && content->mark.present &&
                      content->mark.value != content->caret.value;
    const uint32_t sel_from =
        band ? (content->mark.value < content->caret.value ? content->mark.value
                                                           : content->caret.value)
             : 0u;
    const uint32_t sel_to =
        band ? (content->mark.value > content->caret.value ? content->mark.value
                                                           : content->caret.value)
             : 0u;

    for (uint32_t i = 0u; band && i < count; ++i) {
        emit_band(walk, style->selection.value, content, &lines[i], sel_from, sel_to, &view,
                  view.y + (float)i * line_height);
    }

    /* The run an input method is still deciding, ruled under on every row it reaches.
     * The pair are offsets in the same bytes the lines lay, so a run a narrow box broke
     * across rows is ruled on each of them, the way a selection is. */
    const bool marked = style->marked.present && content->marked_from.present &&
                        content->marked_to.present &&
                        content->marked_from.value != content->marked_to.value;

    for (uint32_t i = 0u; marked && i < count; ++i) {
        emit_rule(walk, style->marked.value, content, &lines[i], content->marked_from.value,
                  content->marked_to.value, &view, view.y + (float)i * line_height);
    }

    for (uint32_t i = 0u; i < count; ++i) {
        const float y = view.y + (float)i * line_height;

        if (y + line_height <= walk->clip.y) {
            continue; /* wholly above the clip */
        }

        if (y >= walk->clip.y + walk->clip.height) {
            break; /* the lines only descend */
        }

        line_sink sink = {.walk = walk, .view = &view, .color = style->color.value, .y = y};

        cf_text_each_run(content, &lines[i], emit_run, &sink);
    }

    if (style->caret.present && content->caret.present) {
        const uint32_t row = cf_text_line_of(content, lines, count, content->caret.value);
        cf_rect caret = {.x = view.x + cf_text_caret_x(content, &lines[row], content->caret.value),
                         .y = view.y + (float)row * line_height,
                         .width = CARET_WIDTH,
                         .height = line_height};

        /* A caret at the trailing edge stands between the last glyph and the
         * border, so the column it paints is pulled inside rather than cut to
         * nothing - a caret nobody can see is not a caret. */
        if (clip.width > 0.0f && clip.height > 0.0f &&
            caret.x + CARET_WIDTH > clip.x + clip.width) {
            caret.x = clip.x + clip.width - CARET_WIDTH;
        }

        emit_mark(walk, caret, style->caret.value, clip);
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
 * The device maps the whole texture into whatever rect it is handed, so a cut
 * rect would squash the part that is left into the window instead of showing
 * the part that fits it. An image keeps its rect inside a clip, the way a
 * border does.
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
                            .rect = box,
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

    /* A box the layout hid draws nothing and reveals nothing below it: the
     * same declaration that took it out of the flow takes its whole subtree out
     * of the stream, so a zeroed rect never reaches a renderer as a mark. */
    if (style->display == CF_DISPLAY_NONE) {
        return;
    }

    const cf_rect box = walk->rects[slot];
    const bool reaches = rect_intersects(box, walk->clip);

    /* The box draws its own marks only where its box reaches the active clip.
     * Whether its descendants draw is decided below, separately: a child is not
     * bound to its parent's box unless the parent clips. */
    if (reaches) {
        const float radius = clamped_radius(style->radius, box.width, box.height);

        if (style->background.present) {
            cf_command *fill = next_command(walk);

            if (fill == NULL) {
                return;
            }

            *fill = (cf_command){.kind = CF_COMMAND_FILL,
                                 .rect = filled_rect(walk, box, radius),
                                 .clip = carried_clip(walk),
                                 .color = style->background.value,
                                 .radius = radius};
        }

        /* A width written as a percentage resolves against the box's own width:
         * the containing block it means is not part of the paint input. */
        const cf_edges widths = cf_box_resolve(style->border, box.width);
        const bool stroked =
            style->border_color.present && (widths.left > 0.0f || widths.top > 0.0f ||
                                            widths.right > 0.0f || widths.bottom > 0.0f);

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
    }

    /* A box that clips its content bounds the descent: the children see the
     * box's rect as their clip, narrowed by whatever clipped their parent. A
     * border strokes its true edges and names this clip to be cut by - cutting
     * the rect instead would draw lines the box never declared at the cut.
     *
     * Once a clipping box lies wholly outside the clip its region is empty, so
     * the whole subtree is culled. A box that clips nothing hands its children
     * the clip unchanged, so the walk descends even when the box itself did not
     * reach - which is what lets a child overflow an off-screen parent into
     * view, the same freedom the hit walk already grants it. */
    const cf_rect outer_clip = walk->clip;
    const bool outer_clipping = walk->clipping;

    if (cf_style_clips_content(style)) {
        if (!reaches) {
            return;
        }

        walk->clip = rect_intersect(box, walk->clip);
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
        /* The flag names one thing that has no room left - the doubling
         * command array or the lines one text box counted - but the observable
         * outcome is the same: the commands written so far stay, nothing after
         * the refusal is emitted. The count is what a caller sees; the site is
         * not worth threading through the walk to say which array refused. */
        cf_logf(CF_LOG_PAINT, "paint: scratch refused at %u commands", out->count);
    }
}
