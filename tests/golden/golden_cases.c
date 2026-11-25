/*
 * The golden tier: each scene lays out, paints and submits through the
 * recorder, and the stream it held is written one command per line beside the
 * code that produced it. The snapshot is the diff surface a paint change is
 * judged on; a plain run compares against it and `--update` rewrites it after
 * an intentional change.
 */
#include <stdio.h>
#include <string.h>

#include "cf/backend.h"

#define MAX_NODES 16u
#define TEXT_SIZE 4096u

typedef struct scene {
    cf_style styles[MAX_NODES];
    cf_measure_fn measures[MAX_NODES];
    void *measure_data[MAX_NODES];
    uint32_t children[MAX_NODES][MAX_NODES];
    uint32_t child_counts[MAX_NODES];
    uint32_t node_count;
} scene;

typedef struct scene_def {
    const char *name;
    cf_rect viewport;
    void (*build)(scene *scene);
} scene_def;

static cf_arena scratch;

static uint32_t root_node(scene *s, cf_style style) {
    const uint32_t slot = s->node_count++;
    s->styles[slot] = style;
    return slot;
}

static uint32_t add_node(scene *s, uint32_t parent, cf_style style) {
    const uint32_t slot = s->node_count++;
    s->styles[slot] = style;
    s->children[parent][s->child_counts[parent]++] = slot;
    return slot;
}

static cf_opt_color stated(uint8_t red, uint8_t green, uint8_t blue) {
    return (cf_opt_color){.present = true, .value = (cf_color){red, green, blue, 255u}};
}

static cf_style filled(cf_opt_color color) {
    cf_style style = cf_style_default();
    style.background = color;
    return style;
}

/*
 * The font the text scenes read: ten points per codepoint, twenty per line,
 * so every line a snapshot pins is arithmetic.
 */
static cf_text_metrics mono_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float mono_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font;
    (void)codepoint;
    (void)previous;
    return 10.0f;
}

static bool mono_has_glyph(void *font, uint32_t codepoint) {
    (void)font;
    (void)codepoint;
    return true;
}

static const cf_text_ops mono_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = mono_has_glyph};
static const cf_font mono_stack[] = {{.handle = NULL, .ops = &mono_ops}};

static const char wrap_bytes[] = "the quick brown fox jumps over the lazy dog";
static const char capped_bytes[] = "click one of the panels to switch its view";

static const cf_text_content wrapped_text = {
    .stack = {.fonts = mono_stack, .count = 1u},
    .text = {wrap_bytes, (uint32_t)(sizeof wrap_bytes - 1u)}};

static const cf_text_content capped_text = {
    .stack = {.fonts = mono_stack, .count = 1u},
    .text = {capped_bytes, (uint32_t)(sizeof capped_bytes - 1u)},
    .max_lines = {.present = true, .value = 2u},
    .ellipsis = true};

static uint32_t add_text(scene *s, uint32_t parent, cf_style style,
                         const cf_text_content *content) {
    const uint32_t slot = s->node_count++;
    s->styles[slot] = style;
    s->measures[slot] = cf_text_measure;
    s->measure_data[slot] = (void *)content;
    s->children[parent][s->child_counts[parent]++] = slot;
    return slot;
}

static const cf_image_content badge = {7u, 100.0f, 30.0f};

static uint32_t add_image(scene *s, uint32_t parent, cf_style style,
                          const cf_image_content *content) {
    const uint32_t slot = s->node_count++;
    s->styles[slot] = style;
    s->measures[slot] = cf_image_measure;
    s->measure_data[slot] = (void *)content;
    s->children[parent][s->child_counts[parent]++] = slot;
    return slot;
}

/* Out of the flow and pinned to the clipping panel's own box, so each child
 * lands exactly where the snapshot has to account for it. */
static void place(cf_style *style, float left, float top) {
    style->position = CF_POSITION_ABSOLUTE;
    style->inset.left = CF_PX(left);
    style->inset.top = CF_PX(top);
}

/*
 * A panel with everything stated - background, border, radius - holding two
 * children, so the snapshot pins the order a frame composites in: a parent's
 * fill, then its border, then the children that paint over both.
 */
static void solid_panel(scene *s) {
    *s = (scene){0};

    cf_style root = filled(stated(42u, 42u, 42u));
    root.width = CF_PX(200.0f);
    root.height = CF_PX(150.0f);
    root.direction = CF_DIRECTION_COLUMN;
    root.padding = (cf_box){CF_PX(8.0f), CF_PX(8.0f), CF_PX(8.0f), CF_PX(8.0f)};
    root.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    root.border_color = stated(30u, 30u, 30u);
    root.radius = 12.0f;
    (void)root_node(s, root);

    cf_style header = filled(stated(70u, 130u, 180u));
    header.height = CF_PX(40.0f);
    header.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    header.border_color = stated(16u, 16u, 16u);
    header.radius = 8.0f;
    (void)add_node(s, 0u, header);

    cf_style footer = filled(stated(220u, 20u, 60u));
    footer.height = CF_PX(40.0f);
    (void)add_node(s, 0u, footer);
}

/*
 * The border rules in one column: a radius past half the shorter side is
 * clamped, a percentage width reads the box's own width, half a border - a
 * color without widths, widths without a color - paints nothing, and sides of
 * different widths ride one command.
 */
static void borders_and_corners(scene *s) {
    *s = (scene){0};

    cf_style root = cf_style_default();
    root.width = CF_PX(200.0f);
    root.height = CF_PX(240.0f);
    root.direction = CF_DIRECTION_COLUMN;
    (void)root_node(s, root);

    cf_style clamped = filled(stated(240u, 240u, 240u));
    clamped.height = CF_PX(40.0f);
    clamped.radius = 30.0f;
    (void)add_node(s, 0u, clamped);

    cf_style percent = cf_style_default();
    percent.height = CF_PX(40.0f);
    percent.border = (cf_box){CF_PERCENT(25.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    percent.border_color = stated(16u, 16u, 16u);
    (void)add_node(s, 0u, percent);

    cf_style color_only = filled(stated(240u, 240u, 240u));
    color_only.height = CF_PX(40.0f);
    color_only.border_color = stated(16u, 16u, 16u);
    (void)add_node(s, 0u, color_only);

    cf_style widths_only = cf_style_default();
    widths_only.height = CF_PX(40.0f);
    widths_only.border = (cf_box){CF_PX(3.0f), CF_PX(3.0f), CF_PX(3.0f), CF_PX(3.0f)};
    (void)add_node(s, 0u, widths_only);

    cf_style sides = filled(stated(250u, 240u, 230u));
    sides.height = CF_PX(40.0f);
    sides.border = (cf_box){CF_PX(1.0f), CF_PX(2.0f), CF_PX(3.0f), CF_PX(4.0f)};
    sides.border_color = stated(112u, 128u, 144u);
    (void)add_node(s, 0u, sides);
}

/*
 * A row wider than the viewport: the first child sits inside it, the second
 * straddles the right edge and keeps its whole rect, the third starts past it
 * and paints nothing.
 */
static void viewport_culling(scene *s) {
    *s = (scene){0};

    cf_style root = cf_style_default();
    root.width = CF_PX(150.0f);
    root.height = CF_PX(90.0f);
    (void)root_node(s, root);

    cf_style boxes[] = {filled(stated(60u, 179u, 113u)), filled(stated(255u, 165u, 0u)),
                        filled(stated(178u, 34u, 34u))};

    for (uint32_t i = 0u; i < 3u; ++i) {
        boxes[i].width = CF_PX(100.0f);
        boxes[i].height = CF_PX(90.0f);
        /* The row cannot hold three hundred points of children; the scene is
         * about what the viewport sees, not what the line gives back. */
        boxes[i].flex_shrink = 0.0f;
        (void)add_node(s, 0u, boxes[i]);
    }
}

static void nested_depth(scene *s) {
    *s = (scene){0};

    cf_style grandparent = filled(stated(176u, 196u, 222u));
    grandparent.width = CF_PX(120.0f);
    grandparent.height = CF_PX(120.0f);
    grandparent.direction = CF_DIRECTION_COLUMN;
    grandparent.padding = (cf_box){CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f)};
    (void)root_node(s, grandparent);

    cf_style parent = filled(stated(255u, 218u, 185u));
    parent.direction = CF_DIRECTION_COLUMN;
    parent.padding = (cf_box){CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f)};
    (void)add_node(s, 0u, parent);

    cf_style child = filled(stated(255u, 105u, 180u));
    child.width = CF_PX(80.0f);
    child.height = CF_PX(80.0f);
    (void)add_node(s, 1u, child);
}

/* A tree that states no color: the idle frame the backend reads as nothing. */
static void idle_frame(scene *s) {
    *s = (scene){0};

    cf_style root = cf_style_default();
    root.width = CF_PX(100.0f);
    root.height = CF_PX(100.0f);
    (void)root_node(s, root);

    cf_style child = cf_style_default();
    child.width = CF_PX(40.0f);
    child.height = CF_PX(40.0f);
    (void)add_node(s, 0u, child);
}

/*
 * The scroller's contract, end to end: a box that clips its content hands every
 * command below it the rect it must not paint outside. A square fill is cut here,
 * because cutting a rectangle is the whole of what it draws; a rounded fill, an
 * image, a border and a text line keep their true rect and name the clip, because
 * cutting those by rewriting the rect would move the corners it rounds, rescale
 * the texture, move the corners a stroke joins and lift a baseline. A child past
 * the clip is not emitted at all, and the box that clips is never clipped by
 * itself.
 */
static void clipped_content(scene *s) {
    *s = (scene){0};

    cf_style root = cf_style_default();
    root.width = CF_PX(200.0f);
    root.height = CF_PX(160.0f);
    root.direction = CF_DIRECTION_COLUMN;
    (void)root_node(s, root);

    /* Relative so the absolute children measure against the panel rather than
     * the page, and clips so the descent carries its rect. */
    cf_style panel = filled(stated(245u, 245u, 245u));
    panel.width = CF_PX(120.0f);
    panel.height = CF_PX(60.0f);
    panel.position = CF_POSITION_RELATIVE;
    panel.clips_content = true;
    panel.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    panel.border_color = stated(16u, 16u, 16u);
    panel.radius = 6.0f;
    (void)add_node(s, 0u, panel);

    /* Wider and taller than the panel: the fill arrives cut at both far edges,
     * which is all a square needs to be drawn inside a scissor. */
    cf_style cut = filled(stated(70u, 130u, 180u));
    place(&cut, 8.0f, 8.0f);
    cut.width = CF_PX(200.0f);
    cut.height = CF_PX(80.0f);
    (void)add_node(s, 1u, cut);

    /* Rounded and running past the right edge: rewriting its rect would round the
     * cut box, so the corners the style states stay where they are and the clip
     * stops the drawing - the fill and the border of one box then share an arc. */
    cf_style rounded = filled(stated(255u, 218u, 185u));
    place(&rounded, 80.0f, 4.0f);
    rounded.width = CF_PX(60.0f);
    rounded.height = CF_PX(40.0f);
    rounded.radius = 10.0f;
    rounded.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    rounded.border_color = stated(16u, 16u, 16u);
    (void)add_node(s, 1u, rounded);

    /* Stroked and straddling the bottom edge: the four sides the box declared
     * stay in the stream, and the device does the cutting. */
    cf_style stroked = cf_style_default();
    place(&stroked, 10.0f, 30.0f);
    stroked.width = CF_PX(100.0f);
    stroked.height = CF_PX(60.0f);
    stroked.border = (cf_box){CF_PX(4.0f), CF_PX(4.0f), CF_PX(4.0f), CF_PX(4.0f)};
    stroked.border_color = stated(255u, 165u, 0u);
    (void)add_node(s, 1u, stroked);

    /* An image at the edge: the texture keeps its rect, so the part that shows
     * is the part that is there rather than the whole of it squashed in. */
    cf_style picture = cf_style_default();
    place(&picture, 60.0f, 40.0f);
    picture.width = CF_PX(100.0f);
    picture.height = CF_PX(30.0f);
    (void)add_image(s, 1u, picture, &badge);

    /* Lines the clip hides are never emitted, so only the first of this
     * paragraph's reaches the stream. */
    cf_style prose = cf_style_default();
    place(&prose, 4.0f, 50.0f);
    prose.width = CF_PX(100.0f);
    prose.color = stated(10u, 10u, 10u);
    (void)add_text(s, 1u, prose, &wrapped_text);

    /* Wholly below the clip: nothing under a clipping box escapes it. */
    cf_style outside = filled(stated(178u, 34u, 34u));
    place(&outside, 0.0f, 200.0f);
    outside.width = CF_PX(40.0f);
    outside.height = CF_PX(40.0f);
    (void)add_node(s, 1u, outside);
}

/*
 * Text a walk laid: a paragraph wraps at the content box its parent's
 * padding leaves, a capped caption closes with the ellipsis, and every line
 * lands where the measure that sized its box counted it.
 */
static void text_wrap(scene *s) {
    *s = (scene){0};

    cf_style root = cf_style_default();
    root.width = CF_PX(200.0f);
    root.height = CF_PX(120.0f);
    root.direction = CF_DIRECTION_COLUMN;
    root.padding = (cf_box){CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f), CF_PX(10.0f)};
    (void)root_node(s, root);

    cf_style paragraph = cf_style_default();
    paragraph.color = stated(10u, 10u, 10u);
    (void)add_text(s, 0u, paragraph, &wrapped_text);

    cf_style caption = cf_style_default();
    caption.color = stated(82u, 82u, 82u);
    (void)add_text(s, 0u, caption, &capped_text);
}

static const scene_def scenes[] = {
    {"solid_panel", {0.0f, 0.0f, 200.0f, 150.0f}, solid_panel},
    {"borders_and_corners", {0.0f, 0.0f, 200.0f, 240.0f}, borders_and_corners},
    {"viewport_culling", {0.0f, 0.0f, 150.0f, 90.0f}, viewport_culling},
    {"nested_depth", {0.0f, 0.0f, 120.0f, 120.0f}, nested_depth},
    {"text_wrap", {0.0f, 0.0f, 200.0f, 120.0f}, text_wrap},
    {"clipped_content", {0.0f, 0.0f, 200.0f, 160.0f}, clipped_content},
    {"idle_frame", {0.0f, 0.0f, 100.0f, 100.0f}, idle_frame},
};

static cf_tree tree_of(scene *s, uint32_t *child_start, uint32_t *child_slots) {
    uint32_t edge = 0u;

    for (uint32_t slot = 0u; slot < s->node_count; ++slot) {
        child_start[slot] = edge;

        for (uint32_t i = 0u; i < s->child_counts[slot]; ++i) {
            child_slots[edge++] = s->children[slot][i];
        }
    }

    child_start[s->node_count] = edge;

    return (cf_tree){.styles = s->styles,
                     .measures = s->measures,
                     .measure_data = s->measure_data,
                     .child_start = child_start,
                     .child_slots = child_slots,
                     .node_count = s->node_count};
}

static void format_stream(const cf_recorder *recorder, char *text) {
    size_t used = 0u;

    for (uint32_t i = 0u; i < recorder->count; ++i) {
        const cf_command *command = &recorder->commands[i];
        const cf_rect rect = command->rect;
        const cf_color color = command->color;

        /* A command that names no clip is one the device bounds by the output
         * itself, so the suffix only appears where paint handed a rect over. */
        char clip[64];
        clip[0] = '\0';

        if (command->clip.width > 0.0f && command->clip.height > 0.0f) {
            snprintf(clip, sizeof clip, " clip=%.2f %.2f %.2f %.2f", command->clip.x,
                     command->clip.y, command->clip.width, command->clip.height);
        }

        int written = 0;

        if (command->kind == CF_COMMAND_FILL) {
            written = snprintf(text + used, TEXT_SIZE - used,
                               "fill %.2f %.2f %.2f %.2f #%02x%02x%02x%02x radius=%.2f%s\n", rect.x,
                               rect.y, rect.width, rect.height, color.red, color.green, color.blue,
                               color.alpha, command->radius, clip);
        } else if (command->kind == CF_COMMAND_BORDER) {
            written =
                snprintf(text + used, TEXT_SIZE - used,
                         "border %.2f %.2f %.2f %.2f #%02x%02x%02x%02x l=%.2f t=%.2f r=%.2f "
                         "b=%.2f radius=%.2f%s\n",
                         rect.x, rect.y, rect.width, rect.height, color.red, color.green,
                         color.blue, color.alpha, command->widths.left, command->widths.top,
                         command->widths.right, command->widths.bottom, command->radius, clip);
        } else if (command->kind == CF_COMMAND_TEXT) {
            written =
                snprintf(text + used, TEXT_SIZE - used,
                         "text %.2f %.2f %.2f %.2f #%02x%02x%02x%02x len=%u ellipsis=%d%s "
                         "\"%.*s\"\n",
                         rect.x, rect.y, rect.width, rect.height, color.red, color.green,
                         color.blue, color.alpha, command->text.length, command->ellipsis ? 1 : 0,
                         clip, (int)command->text.length, command->text.chars);
        } else if (command->kind == CF_COMMAND_IMAGE) {
            written =
                snprintf(text + used, TEXT_SIZE - used, "image %.2f %.2f %.2f %.2f tex=%u%s\n",
                         rect.x, rect.y, rect.width, rect.height, command->texture, clip);
        }

        if (written > 0 && (size_t)written < TEXT_SIZE - used) {
            used += (size_t)written;
        }
    }

    text[used] = '\0';
}

int main(int argc, char **argv) {
    const bool update = argc > 1 && strcmp(argv[1], "--update") == 0;

    if (!cf_arena_init(&scratch, 65536u)) {
        printf("golden: the scenes need scratch\n");
        return 1;
    }

    uint32_t failed = 0u;

    for (uint32_t i = 0u; i < sizeof scenes / sizeof scenes[0]; ++i) {
        const scene_def *def = &scenes[i];

        scene s;
        def->build(&s);

        uint32_t child_start[MAX_NODES + 1u];
        uint32_t child_slots[MAX_NODES * MAX_NODES];
        const cf_tree tree = tree_of(&s, child_start, child_slots);

        cf_arena_clear(&scratch);

        cf_rect rects[MAX_NODES];
        cf_layout(&scratch, &tree, 0u,
                  (cf_available){.width = CF_PX(def->viewport.width),
                                 .height = CF_PX(def->viewport.height)},
                  (cf_point){0.0f, 0.0f}, rects);

        cf_command_list list = {0};
        cf_paint(&scratch, &tree, rects, 0u, def->viewport, &list);

        cf_recorder recorder = {.arena = &scratch};
        const cf_backend backend = cf_backend_recorder(&recorder);
        backend.submit(backend.user_data, list.commands, list.count);

        char text[TEXT_SIZE];
        format_stream(&recorder, text);

        char path[256];
        snprintf(path, sizeof path, "%s/%s.golden", CFORM_GOLDEN_DIR, def->name);

        if (update) {
            FILE *file = fopen(path, "wb");

            if (file == NULL) {
                printf("golden %s: cannot write %s\n", def->name, path);
                failed++;
                continue;
            }

            fwrite(text, 1u, strlen(text), file);
            fclose(file);
            printf("golden %s: wrote\n", def->name);
            continue;
        }

        FILE *file = fopen(path, "rb");

        if (file == NULL) {
            printf("golden %s: no snapshot at %s; run with --update to write it\n", def->name,
                   path);
            failed++;
            continue;
        }

        char expected[TEXT_SIZE];
        const size_t read = fread(expected, 1u, TEXT_SIZE - 1u, file);
        expected[read] = '\0';
        fclose(file);

        if (strcmp(expected, text) == 0) {
            printf("golden %s: ok\n", def->name);
            continue;
        }

        printf("golden %s: mismatch\n--- expected ---\n%s--- actual ---\n%s", def->name, expected,
               text);
        failed++;
    }

    cf_arena_free(&scratch);
    return failed > 0u ? 1 : 0;
}
