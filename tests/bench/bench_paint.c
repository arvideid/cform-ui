/*
 * Paint throughput at the three sizes the roadmap's Section 6 acceptance
 * targets name: a 500-node game HUD, a 1k-node desktop page, and a 10k-node
 * stress case. The stream a frame writes doubles from one allocation to the
 * next inside the arena the paint walk was handed, so the byte figure is what
 * a host sizes its frame arena against at that scale. The command count is
 * asserted; the fastest of repeated repaints is printed, because a shared
 * runner spends its wall time on somebody else's process.
 */
#include <stdio.h>
#include <time.h>

#include "cf/node.h"
#include "cf/paint.h"
#include "cf/text.h"

#include "cform_test.h"

#include "bench_meta.h"

/*
 * A deterministic mono font so text nodes can join the stream without
 * requiring SDL or a real TTF file. Every glyph advances 10 points and
 * lines are 20 points tall, matching the fake font in paint_emit.c.
 */
static cf_text_metrics mono_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float mono_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font, (void)codepoint, (void)previous;
    return 10.0f;
}

static bool mono_has_glyph(void *font, uint32_t codepoint) {
    (void)font, (void)codepoint;
    return true;
}

static const cf_text_ops mono_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = mono_has_glyph};
static const cf_font mono_stack[] = {{.handle = NULL, .ops = &mono_ops}};

static const char cell_label[] = "x";
static const cf_text_content cell_text = {
    .stack = {.fonts = mono_stack, .count = 1u},
    .text = {.chars = cell_label, .length = sizeof cell_label - 1u},
};

/* One third of the cells carry text and every fourth strokes a border, so
 * every tier paints a realistic mix of fills, borders and lines rather than
 * only geometry. */
#define TEXT_EVERY 3u
#define BORDERED_EVERY 4u

/* The largest tier the static arrays hold. Smaller tiers reuse a prefix of
 * the same rows and cells rather than reallocating, so the tier's own tree is
 * a whole declaration a diff can compare. */
#define MAX_ROWS 99u
#define MAX_CELLS 100u
#define FRAMES 50u
#define NS_PER_MS 1000000.0
#define FRAME_BYTES (8u * 1024u * 1024u)

#define SURFACE_WIDTH 1280.0f
#define SURFACE_HEIGHT 720.0f

typedef struct {
    const char *label;
    uint32_t rows;
    uint32_t cells;
} paint_tier;

/* The three sizes the roadmap names: a HUD that must paint in 1 ms at 120 Hz,
 * a desktop page at 60 Hz, and the stress tree the existing budget quotes. A
 * row of MAX_CELLS cells at 12 points fits inside 1280, so no tier overflows
 * its parent horizontally, and a row's height of 7 points keeps every tree
 * inside the 720-point surface. */
static const paint_tier kTiers[] = {
    {"hud", 5u, MAX_CELLS},
    {"desktop", 10u, MAX_CELLS},
    {"large", MAX_ROWS, MAX_CELLS},
};

static cf_node cells[MAX_ROWS][MAX_CELLS];
static cf_node rows[MAX_ROWS];
static cf_node root;

static cf_opt_color shade(uint8_t tone) {
    return (cf_opt_color){.present = true, .value = CF_COLOR(tone, tone, tone)};
}

static void build_tree(uint32_t row_count, uint32_t cell_count) {
    root = (cf_node){.style = cf_style_default()};
    root.style.width = CF_PX(SURFACE_WIDTH);
    root.style.height = CF_PX(SURFACE_HEIGHT);
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.background = shade(24u);

    for (uint32_t r = 0u; r < row_count; ++r) {
        rows[r] = (cf_node){.style = cf_style_default()};
        rows[r].style.height = CF_PX(7.0f);
        rows[r].style.background = shade(40u);
        rows[r].children = cells[r];
        rows[r].child_count = cell_count;

        for (uint32_t c = 0u; c < cell_count; ++c) {
            cells[r][c] = (cf_node){.style = cf_style_default()};
            cells[r][c].style.width = CF_PX(12.0f);
            cells[r][c].style.background = shade(120u);

            if (c % BORDERED_EVERY == 0u) {
                cells[r][c].style.border =
                    (cf_box){CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f)};
                cells[r][c].style.border_color = shade(220u);
                cells[r][c].style.radius = 3.0f;
            }

            if (c % TEXT_EVERY == 0u) {
                cells[r][c].style.color = shade(200u);
                cells[r][c].measure = cf_text_measure;
                cells[r][c].measure_data = (void *)&cell_text;
            }
        }
    }

    root.children = rows;
    root.child_count = row_count;
}

/* The stream the tree above paints: one fill per box, a border on every
 * fourth cell whose box reaches the viewport, and one line on every third
 * cell. A tier's counts follow from its rows and cells, so the assertion
 * stays arithmetic rather than a magic number. */
static uint32_t expected_fills(uint32_t row_count, uint32_t cell_count) {
    return 1u + row_count + row_count * cell_count;
}

static uint32_t expected_borders(uint32_t row_count, uint32_t cell_count) {
    return row_count * (((cell_count - 1u) / BORDERED_EVERY) + 1u);
}

static uint32_t expected_texts(uint32_t row_count, uint32_t cell_count) {
    return row_count * (((cell_count - 1u) / TEXT_EVERY) + 1u);
}

static uint32_t expected_commands(uint32_t row_count, uint32_t cell_count) {
    return expected_fills(row_count, cell_count) + expected_borders(row_count, cell_count) +
           expected_texts(row_count, cell_count);
}

static cf_rect surface_viewport(void) {
    return (cf_rect){0.0f, 0.0f, SURFACE_WIDTH, SURFACE_HEIGHT};
}

static double elapsed_ms(const struct timespec *started, const struct timespec *finished) {
    const double seconds = (double)finished->tv_sec - (double)started->tv_sec;
    const double nanos = (double)finished->tv_nsec - (double)started->tv_nsec;

    return (seconds * 1000000000.0 + nanos) / NS_PER_MS;
}

/* The tree, laid out and ready to paint, with the arena mark that keeps the
 * rects alive while every later rewind drops only command lists. */
static size_t lay_out(cf_ctx *ctx) {
    cf_frame_begin(ctx);
    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx,
                    (cf_available){.width = CF_PX(SURFACE_WIDTH), .height = CF_PX(SURFACE_HEIGHT)},
                    (cf_point){0.0f, 0.0f});

    return cf_arena_mark(&ctx->frame);
}

static cf_command_list paint(cf_ctx *ctx, cf_rect viewport) {
    cf_command_list list = {0};

    cf_paint(&ctx->frame, cf_frame_tree(ctx), ctx->rects, ctx->root_slot, viewport, &list);

    return list;
}

/* Whether every command in the stream is one of the three shapes this tree
 * declares, and the count of each matches what the tier said. The stream is
 * deterministic, so a change in the paint walk shows up here rather than in
 * a millisecond. */
static bool stream_matches_tier(const cf_command_list *list, uint32_t row_count,
                                uint32_t cell_count) {
    uint32_t fills = 0u, borders = 0u, texts = 0u;

    for (uint32_t i = 0u; i < list->count; ++i) {
        switch (list->commands[i].kind) {
        case CF_COMMAND_FILL:
            fills++;
            break;
        case CF_COMMAND_BORDER:
            borders++;
            break;
        case CF_COMMAND_TEXT:
            texts++;
            break;
        default:
            return false;
        }
    }

    return fills == expected_fills(row_count, cell_count) &&
           borders == expected_borders(row_count, cell_count) &&
           texts == expected_texts(row_count, cell_count);
}

static void bench_tier(cf_ctx *ctx, const paint_tier *tier) {
    build_tree(tier->rows, tier->cells);
    const size_t laid_out = lay_out(ctx);

    const cf_command_list full = paint(ctx, surface_viewport());
    const uint32_t wanted = expected_commands(tier->rows, tier->cells);

    char key[64];
    snprintf(key, sizeof key, "%s_nodes", tier->label);
    bench_result(key, (double)(1u + tier->rows + tier->rows * tier->cells), "count");
    snprintf(key, sizeof key, "%s_commands", tier->label);
    bench_result(key, (double)full.count, "count");

    CF_CHECK(full.count == wanted, "the tier's stream is what its tree declares");
    CF_CHECK(stream_matches_tier(&full, tier->rows, tier->cells),
             "the counts split into fills, borders and text the way the tier states");
    CF_CHECK(full.count > 0u && full.commands[0].kind == CF_COMMAND_FILL,
             "the page's own fill leads the stream");

    /* The reach check drops a box the viewport cannot see at all: with nothing
     * in front of it the walk appends nothing, which is the paint half of an
     * idle frame's zero. */
    cf_arena_rewind(&ctx->frame, laid_out);
    const cf_command_list empty = paint(ctx, (cf_rect){0.0f, 0.0f, 0.0f, 0.0f});
    CF_CHECK(empty.count == 0u, "a viewport that reaches nothing paints nothing");

    /* Repaint the same laid-out tree into the same arena, rewinding first each
     * time: the per-frame cost a shell pays when something did change. */
    double best = -1.0;
    uint32_t last_count = 0u;
    size_t last_bytes = 0u;

    for (uint32_t i = 0u; i < FRAMES; ++i) {
        struct timespec started;
        struct timespec finished;

        cf_arena_rewind(&ctx->frame, laid_out);
        timespec_get(&started, TIME_UTC);
        const cf_command_list list = paint(ctx, surface_viewport());
        timespec_get(&finished, TIME_UTC);

        const double ms = elapsed_ms(&started, &finished);

        if (best < 0.0 || ms < best) {
            best = ms;
        }

        last_count = list.count;
        last_bytes = cf_arena_mark(&ctx->frame) - laid_out;
    }

    CF_CHECK(last_count == wanted, "every rewind repaints the same stream");
    CF_CHECK(last_bytes > 0u, "the list costs the arena it grew in, not the system's");

    snprintf(key, sizeof key, "%s_repaint_ms", tier->label);
    bench_result(key, best, "ms");
    snprintf(key, sizeof key, "%s_scratch_kib", tier->label);
    bench_result(key, (double)last_bytes / 1024.0, "KiB");
    snprintf(key, sizeof key, "%s_ns_per_command", tier->label);
    bench_result(key, best * NS_PER_MS / (double)wanted, "ns");
}

int main(void) {
    bench_meta("bench_paint", "hud-500+desktop-1k+large-10k rows-of-100-cells fill-border-text");
    bench_result("frames_per_point", (double)FRAMES, "count");
    bench_result("frame_bytes", (double)FRAME_BYTES, "bytes");

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, FRAME_BYTES)) {
        CF_CHECK(false, "the context starts");
        return CF_REPORT();
    }

    const size_t tier_count = sizeof kTiers / sizeof kTiers[0];

    for (size_t i = 0u; i < tier_count; ++i) {
        bench_tier(&ctx, &kTiers[i]);
    }

    cf_ctx_free(&ctx);

    return CF_REPORT();
}
