#include <string.h>

#include "layout/internal.h"

/*
 * The declared tracks grown to `count`, anything past the declaration
 * content-sized. That is what an implicit row and a container that named no
 * columns have in common, and it keeps the solver reading one flat array.
 */
const cf_track *widen_tracks(const layout_ctx *ctx, const cf_track *declared,
                             uint32_t declared_count, uint32_t count) {
    cf_track *tracks = cf_alloc(ctx->scratch, (size_t)count * sizeof *tracks);

    if (tracks == NULL) {
        return NULL;
    }

    const uint32_t named = declared_count < count ? declared_count : count;

    if (named > 0u) {
        memcpy(tracks, declared, (size_t)named * sizeof *tracks);
    }

    for (uint32_t t = named; t < count; ++t) {
        tracks[t] = (cf_track){.min = CF_AUTO, .max = CF_AUTO};
    }

    return tracks;
}

/* What the tracks between here and the last line of an item add up to. */
float span_size(const float *sizes, uint32_t start, uint32_t span, float gap) {
    float total = gap * (float)(span > 1u ? span - 1u : 0u);

    for (uint32_t t = start; t < start + span; ++t) {
        total += sizes[t];
    }

    return total;
}

/* How far a cell sits inside the grid: every track before it, plus a gap for
 * each one. */
float track_offset(const float *sizes, uint32_t start, float gap) {
    if (start == 0u) {
        return 0.0f;
    }

    return span_size(sizes, 0u, start, gap) + gap;
}

float axis_extent(const float *sizes, uint32_t count, float gap) {
    return span_size(sizes, 0u, count, gap);
}

/*
 * The cell an item fills and where it starts: the tracks it spans plus the gaps
 * between them, measured from the container's content origin.
 */
static cf_size cell_of(const grid_ctx *g, const grid_item *item, cf_point *out_offset) {
    out_offset->x = track_offset(g->column_sizes, item->column_start, g->column_gap);
    out_offset->y = track_offset(g->row_sizes, item->row_start, g->row_gap);

    return (cf_size){
        .width = span_size(g->column_sizes, item->column_start, item->column_span, g->column_gap),
        .height = span_size(g->row_sizes, item->row_start, item->row_span, g->row_gap),
    };
}

static void place_item(const grid_ctx *g, const grid_item *item) {
    const cf_style *child = &g->ctx->tree->styles[item->slot];
    const cf_edges margin = cf_box_resolve(child->margin, g->percent_base);
    cf_point offset;
    const cf_size cell = cell_of(g, item, &offset);

    /* An item that names no size on an axis fills the cell it was placed in; one
     * that names it keeps its own size and sits at the cell's start edge, because
     * nothing distributes space inside a cell. */
    const cf_available verdict = {
        .width = cf_length_is_auto(child->width)
                     ? CF_PX(clamp_non_negative(cell.width - margin.left - margin.right))
                     : CF_AUTO,
        .height = cf_length_is_auto(child->height)
                      ? CF_PX(clamp_non_negative(cell.height - margin.top - margin.bottom))
                      : CF_AUTO,
    };

    const size_t mark = cf_arena_mark(g->ctx->scratch);

    layout_subtree(g->ctx, item->slot,
                   (cf_available){.width = CF_PX(cell.width), .height = CF_PX(cell.height)},
                   verdict,
                   (cf_point){.x = g->origin.x + offset.x + margin.left,
                              .y = g->origin.y + offset.y + margin.top},
                   g->containing);

    cf_arena_rewind(g->ctx->scratch, mark);
}

/*
 * Place, size and - when the caller is arranging rather than measuring - lay out
 * the grid. The extent the tracks ask for comes back either way, which is what
 * lets a container with an auto axis hug its grid.
 */
static cf_size grid_resolve(grid_ctx *g, bool arranging) {
    const layout_ctx *ctx = g->ctx;
    const uint32_t capacity = child_count(ctx, g->slot);
    const uint32_t declared_columns = g->style->column_count > 0u ? g->style->column_count : 1u;

    if (capacity == 0u) {
        return (cf_size){0};
    }

    const size_t mark = cf_arena_mark(ctx->scratch);
    grid_item *items = cf_alloc(ctx->scratch, (size_t)capacity * sizeof *items);

    uint32_t placed = 0u;
    uint32_t rows = g->style->row_count;

    if (items != NULL) {
        placed = grid_place(ctx, g->slot, declared_columns, items, &rows);
    } else {
        cf_logf(CF_LOG_LAYOUT, "layout: scratch refused a grid with %u children", capacity);
    }

    /* The grid is as wide as the widest span and as tall as the deepest one:
     * placement already clamps a span to the declared tracks, so only an
     * implicit row can reach past what was declared. */
    uint32_t column_count = declared_columns;
    uint32_t row_count = rows;

    for (uint32_t i = 0u; i < placed; ++i) {
        const uint32_t columns = items[i].column_start + items[i].column_span;
        const uint32_t row_span_end = items[i].row_start + items[i].row_span;

        column_count = columns > column_count ? columns : column_count;
        row_count = row_span_end > row_count ? row_span_end : row_count;
    }

    const cf_track *columns =
        widen_tracks(ctx, g->style->column_tracks, g->style->column_count, column_count);
    const cf_track *track_rows =
        widen_tracks(ctx, g->style->row_tracks, g->style->row_count, row_count);
    float *column_sizes = cf_alloc(ctx->scratch, (size_t)(column_count > 0u ? column_count : 1u) *
                                                     sizeof *column_sizes);
    float *row_sizes =
        cf_alloc(ctx->scratch, (size_t)(row_count > 0u ? row_count : 1u) * sizeof *row_sizes);

    if (columns == NULL || track_rows == NULL || column_sizes == NULL || row_sizes == NULL) {
        cf_logf(CF_LOG_LAYOUT, "layout: scratch refused a grid of %u by %u tracks", column_count,
                row_count);
        cf_arena_rewind(ctx->scratch, mark);

        return (cf_size){0};
    }

    g->items = items;
    g->item_count = placed;
    g->columns = columns;
    g->column_count = column_count;
    g->rows = track_rows;
    g->row_count = row_count;
    g->column_sizes = column_sizes;
    g->row_sizes = row_sizes;

    if (!grid_size_tracks(g, arranging)) {
        cf_logf(CF_LOG_LAYOUT, "layout: scratch refused the axes of a %u by %u grid", column_count,
                row_count);
        cf_arena_rewind(ctx->scratch, mark);

        return (cf_size){0};
    }

    const cf_size extents = {
        .width = axis_extent(column_sizes, column_count, g->column_gap),
        .height = axis_extent(row_sizes, row_count, g->row_gap),
    };

    if (arranging) {
        for (uint32_t i = 0u; i < placed; ++i) {
            place_item(g, &items[i]);
        }

        /* A grid gives an out-of-flow child no line to sit in: its packing
         * keywords share out the tracks, never a child, so the child lands on
         * the content corner with nothing distributed around it. */
        const absolute_flow flow = {.origin = g->origin,
                                    .content = g->constraint,
                                    .main = CF_JUSTIFY_START,
                                    .cross = CF_ALIGN_START};
        place_absolute(ctx, g->slot, g->containing, &flow);
    }

    cf_arena_rewind(ctx->scratch, mark);

    return extents;
}

static grid_ctx grid_context_of(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                                cf_intrinsic mode, cf_point origin, cf_rect containing) {
    const cf_style *style = &ctx->tree->styles[slot];

    grid_ctx g = {
        .ctx = ctx,
        .style = style,
        .slot = slot,
        .constraint = constraint,
        .intrinsic = mode,
        .origin = origin,
        .containing = containing,
    };

    /* Gaps and percentage tracks resolve against the container's own content
     * box, the reference a percentage margin uses too. */
    definite_offer(constraint.width, &g.percent_base);
    g.column_gap = cf_length_resolve(style->column_gap, g.percent_base);
    g.row_gap = cf_length_resolve(style->row_gap, g.percent_base);

    return g;
}

cf_size grid_extent_of(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                       cf_intrinsic mode) {
    /* Measuring places no child, so the block a placed child would measure
     * against is never read here. */
    grid_ctx g = grid_context_of(ctx, slot, constraint, mode, (cf_point){0.0f, 0.0f},
                                 (cf_rect){0.0f, 0.0f, 0.0f, 0.0f});

    return grid_resolve(&g, false);
}

void grid_arrange(const layout_ctx *ctx, uint32_t slot, cf_available constraint, cf_point origin,
                  cf_rect containing) {
    grid_ctx g =
        grid_context_of(ctx, slot, constraint, CF_INTRINSIC_MAX_CONTENT, origin, containing);

    grid_resolve(&g, true);
}
