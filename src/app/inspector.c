#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cf/version.h"

/* The panel is a corner, not a windshield: the padding keeps its numbers off
 * the edges and the width holds the longest labeled line. */
#define PANEL_WIDTH 168.0f
#define PANEL_PADDING 8.0f
#define PANEL_LINES 8u
#define LINE_BYTES 32u

/* "says " plus the widest word the line can carry: the panel prints through the same buffer every
 * line gets, and a name longer than what is left draws nothing at all. */
#define SAYS_BYTES 26u

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

/* The panel grows its commands out of the same frame arena a build does, so
 * an exhausted one is refused the way a refused label is: the panel simply
 * stops drawing rather than writing past a stream nobody sized. */
static bool list_room(cf_ctx *ctx, cf_command_list *out) {
    if (out->count < out->capacity) {
        return true;
    }

    const uint32_t capacity = out->capacity < FIRST_COMMANDS ? FIRST_COMMANDS : out->capacity * 2u;
    cf_command *store = cf_frame_alloc(ctx, (size_t)capacity * sizeof *store);

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

static void append_fill(cf_ctx *ctx, cf_command_list *out, cf_rect rect) {
    if (!list_room(ctx, out)) {
        return;
    }

    out->commands[out->count++] =
        (cf_command){.kind = CF_COMMAND_FILL, .rect = rect, .color = PANEL_FILL};
}

static void append_edge(cf_ctx *ctx, cf_command_list *out, cf_rect rect) {
    if (!list_room(ctx, out)) {
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
    char *bytes = cf_frame_alloc(ctx, LINE_BYTES);

    if (bytes == NULL) {
        return;
    }

    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(bytes, LINE_BYTES, format, arguments);
    va_end(arguments);

    if (written <= 0 || (uint32_t)written >= LINE_BYTES || !list_room(ctx, out)) {
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

/*
 * The hardest pair a reader has on this page: each text tone against each surface
 * the theme says it paints. A surface that states no fill is passed over - nothing
 * is laid there, so no text is read over it - and a context with no design system
 * at all answers a negative, which the line prints as an empty reading rather than
 * a ratio of 1 that would read as a failure of the page's own choosing.
 *
 * The number is on the panel because a contrast rule is invisible in exactly the
 * way it matters: a page at 3.2:1 looks like a page, and only the arithmetic says a
 * person with low vision cannot read it. This is the same reading a tier asserts.
 */
static float weakest_text_contrast(const cf_theme *theme) {
    if (theme == NULL) {
        return -1.0f;
    }

    const cf_style *const surfaces[] = {&theme->page, &theme->bar, &theme->body};
    const cf_color tones[] = {theme->ink, theme->muted};
    float weakest = -1.0f;

    for (uint32_t s = 0u; s < sizeof surfaces / sizeof *surfaces; ++s) {
        if (!surfaces[s]->background.present) {
            continue;
        }

        for (uint32_t t = 0u; t < sizeof tones / sizeof *tones; ++t) {
            const float pair = cf_contrast(tones[t], surfaces[s]->background.value);

            if (weakest < 0.0f || pair < weakest) {
                weakest = pair;
            }
        }
    }

    return weakest;
}

/*
 * What a bridge would say about the box the keyboard stands on. Reading it off the panel is how a
 * developer catches a control nobody named: the page looks right and a person hears nothing, so
 * the words have to be written somewhere a sighted person can see them.
 *
 * The walk is the one a bridge is handed, so a name the frame derived from a label reads derived
 * here too rather than being reconstructed by a second, weaker rule. It costs a pass over the
 * tree, and only on the frames a developer asked for with F12.
 */
typedef struct focus_words {
    char bytes[SAYS_BYTES + 1u];
    bool focused;
    bool spoken;
} focus_words;

static void copy_focus_words(const cf_semantic *node, void *user_data) {
    focus_words *words = user_data;

    if ((node->states & (uint32_t)CF_SEMANTIC_FOCUSED) == 0u) {
        return;
    }

    words->focused = true;
    /* A focused box with no name still has its value to read, which is what a field is for: the
     * caret moves and the announced bytes are what was typed. */
    const cf_string text = node->name.length != 0u ? node->name : node->value;

    if (text.chars == NULL || text.length == 0u) {
        return;
    }

    words->spoken = true;
    words->bytes[0] = '\0';
    const uint32_t length = text.length < SAYS_BYTES ? text.length : SAYS_BYTES;
    memcpy(words->bytes, text.chars, length);
    words->bytes[length] = '\0';
}

void app_inspector_append(cf_ctx *ctx, const cf_text_stack *fonts, uint32_t painted,
                          uint32_t frame_us, cf_command_list *out) {
    if (ctx == NULL || out == NULL) {
        return;
    }

    const float height = line_height(fonts);
    /* The panel is pinned to the page it reports on. A frame that laid nothing answers no root and
     * no box, and the corner the reading starts from is then the origin rather than a rectangle
     * that was never measured. */
    const uint32_t root = cf_frame_root(ctx);
    const cf_rect *page = root != CF_NO_LINK ? cf_frame_rect(ctx, root) : NULL;
    const cf_rect canvas = page != NULL ? *page : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    const float panel_height = PANEL_PADDING * 2.0f + height * (float)PANEL_LINES;

    /* The corner the reading starts away from. A panel pinned to the origin
     * sits over the first line a reader meets, and the numbers are meant to be
     * read beside a page rather than instead of it. */
    const cf_rect panel = {
        .x = canvas.width > PANEL_WIDTH ? canvas.x + canvas.width - PANEL_WIDTH : canvas.x,
        .y = canvas.height > panel_height ? canvas.y + canvas.height - panel_height : canvas.y,
        .width = PANEL_WIDTH,
        .height = panel_height};

    append_fill(ctx, out, panel);
    append_edge(ctx, out, panel);

    if (fonts == NULL || height == 0.0f) {
        return;
    }

    const panel_counts counts = count_of(ctx);

    /* The version leads the reading, because a number reported from a screen is
     * only worth reporting next to the number it was built as. */
    append_line(ctx, out, fonts, panel, 0u, height, "cform v%s", cf_version_string());
    append_line(ctx, out, fonts, panel, 1u, height, "nodes %u", counts.nodes);
    append_line(ctx, out, fonts, panel, 2u, height, "dirty %u", counts.dirty);
    append_line(ctx, out, fonts, panel, 3u, height, "commands %u", painted);
    append_line(ctx, out, fonts, panel, 4u, height, "page %ux%u", round_points(canvas.width),
                round_points(canvas.height));
    append_line(ctx, out, fonts, panel, 5u, height, "frame %uus", frame_us);

    /* The reading of the table the frame styled from, not of the panel's own fixed
     * tones: the numbers a page is checked against are the ones it chose. */
    const float weakest = weakest_text_contrast(cf_theme_of(ctx));

    if (weakest >= 0.0f) {
        append_line(ctx, out, fonts, panel, 6u, height, "contrast %.1f", weakest);
    } else {
        append_line(ctx, out, fonts, panel, 6u, height, "contrast -");
    }

    /* Read last: the walk wants the rects cf_frame_layout wrote, and a page with no focused box is
     * the common case rather than a failure, so it prints as an empty ask. */
    focus_words words = {0};
    cf_semantic_visit(ctx, copy_focus_words, &words);
    append_line(ctx, out, fonts, panel, 7u, height, "says %s",
                !words.focused ? "-" : (words.spoken ? words.bytes : "nothing"));
}
