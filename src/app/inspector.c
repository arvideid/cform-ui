#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The panel is a corner, not a windshield: the padding keeps its numbers off
 * the edges and the width holds the longest labeled line. */
#define PANEL_WIDTH 168.0f
#define PANEL_PADDING 8.0f
#define PANEL_LINES 5u
#define LINE_BYTES 32u

/* The list the page painted through grows the same way the panel's own
 * commands do, out of the frame arena. */
#define FIRST_COMMANDS 64u

/* A panel that reads on any page: the devtools dark, one edge, one ink. */
static const cf_color PANEL_FILL = {30u, 32u, 36u, 235u};
static const cf_color PANEL_EDGE = {80u, 84u, 92u, 255u};
static const cf_color PANEL_INK = {232u, 232u, 232u, 255u};

static float line_height(const cf_text_stack *fonts) {
    if (fonts == NULL || fonts->count == 0u) {
        return 0.0f;
    }

    const cf_font font = fonts->fonts[0];

    if (font.handle == NULL || font.ops == NULL || font.ops->metrics == NULL) {
        return 0.0f;
    }

    return font.ops->metrics(font.handle).line_height;
}

typedef struct panel_counts {
    uint32_t nodes;
    uint32_t dirty;
} panel_counts;

static panel_counts count_of(const cf_ctx *ctx) {
    panel_counts counts = {0u, 0u};
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL) {
        return counts;
    }

    counts.nodes = tree->node_count;

    for (uint32_t slot = 0u; slot < tree->node_count; ++slot) {
        if (cf_dirty(ctx, slot)) {
            counts.dirty++;
        }
    }

    return counts;
}

static bool list_room(cf_arena *arena, cf_command_list *out) {
    if (out->count < out->capacity) {
        return true;
    }

    const uint32_t capacity = out->capacity < FIRST_COMMANDS ? FIRST_COMMANDS : out->capacity * 2u;
    cf_command *store = cf_alloc(arena, (size_t)capacity * sizeof *store);

    if (store == NULL) {
        return false;
    }

    /* The page's stream is already in the list, and a frame that fills its
     * capacity exactly is the ordinary case rather than a rare one: grow
     * without carrying the old commands over and the panel is drawn on a
     * window the page has vanished from. */
    memcpy(store, out->commands, (size_t)out->count * sizeof *store);

    out->commands = store;
    out->capacity = capacity;
    return true;
}

static void append_fill(cf_arena *arena, cf_command_list *out, cf_rect rect) {
    if (!list_room(arena, out)) {
        return;
    }

    out->commands[out->count++] =
        (cf_command){.kind = CF_COMMAND_FILL, .rect = rect, .color = PANEL_FILL};
}

static void append_edge(cf_arena *arena, cf_command_list *out, cf_rect rect) {
    if (!list_room(arena, out)) {
        return;
    }

    out->commands[out->count++] = (cf_command){.kind = CF_COMMAND_BORDER,
                                               .rect = rect,
                                               .color = PANEL_EDGE,
                                               .widths = (cf_edges){1.0f, 1.0f, 1.0f, 1.0f}};
}

/* The bytes a line is printed into come out of the frame arena, so they live
 * as long as the submit the stream rides - the way a label's text does. A
 * line the arena cannot hold draws nothing. */
static void append_line(cf_ctx *ctx, cf_command_list *out, const cf_text_stack *fonts,
                        cf_rect panel, uint32_t line, float height, const char *format, ...) {
    char *bytes = cf_alloc(&ctx->frame, LINE_BYTES);

    if (bytes == NULL) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(bytes, LINE_BYTES, format, arguments);
    va_end(arguments);

    if (written <= 0 || (uint32_t)written >= LINE_BYTES || !list_room(&ctx->frame, out)) {
        return;
    }

    out->commands[out->count++] = (cf_command){
        .kind = CF_COMMAND_TEXT,
        .rect = {panel.x + PANEL_PADDING, panel.y + PANEL_PADDING + height * (float)line,
                 panel.width - PANEL_PADDING * 2.0f, height},
        .color = PANEL_INK,
        .text = {bytes, (uint32_t)written},
        .font = fonts->fonts[0]};
}

/* Whole points, because the panel reads as text and a fraction of a point is
 * noise at the size a rect is reported. */
static uint32_t round_points(float value) {
    return value > 0.0f ? (uint32_t)(value + 0.5f) : 0u;
}

void app_inspector_append(cf_ctx *ctx, const cf_text_stack *fonts, uint32_t painted,
                          uint32_t frame_us, cf_command_list *out) {
    if (ctx == NULL || out == NULL) {
        return;
    }

    const float height = line_height(fonts);
    const cf_rect page =
        ctx->rects != NULL ? ctx->rects[ctx->root_slot] : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    const float panel_height = PANEL_PADDING * 2.0f + height * (float)PANEL_LINES;

    /* The corner the reading starts away from. A panel pinned to the origin
     * sits over the first line a reader meets, and the numbers are meant to be
     * read beside a page rather than instead of it. */
    const cf_rect panel = {
        .x = page.width > PANEL_WIDTH ? page.x + page.width - PANEL_WIDTH : page.x,
        .y = page.height > panel_height ? page.y + page.height - panel_height : page.y,
        .width = PANEL_WIDTH,
        .height = panel_height};

    append_fill(&ctx->frame, out, panel);
    append_edge(&ctx->frame, out, panel);

    if (fonts == NULL || height == 0.0f) {
        return;
    }

    const panel_counts counts = count_of(ctx);

    append_line(ctx, out, fonts, panel, 0u, height, "nodes %u", counts.nodes);
    append_line(ctx, out, fonts, panel, 1u, height, "dirty %u", counts.dirty);
    append_line(ctx, out, fonts, panel, 2u, height, "commands %u", painted);
    append_line(ctx, out, fonts, panel, 3u, height, "page %ux%u", round_points(page.width),
                round_points(page.height));
    append_line(ctx, out, fonts, panel, 4u, height, "frame %uus", frame_us);
}
