#include <string.h>

#include "layout/internal.h"

/*
 * The length an axis has without measuring: the parent's verdict, then the
 * node's own declaration. False means the axis is content-based and has to be
 * measured before it exists. A percentage declared inside a measuring pass is
 * a percentage of the extent that pass exists to find, so it waits for the
 * arranging pass, where the offer is the settled containing box.
 */
static bool settled_length(cf_length verdict, cf_length declaration, cf_length constraint,
                           bool measuring, float *out) {
    if (measuring && declaration.kind == CF_LENGTH_PERCENT) {
        return definite_offer(verdict, out);
    }

    return definite_offer(verdict, out) || cf_length_of(declaration, constraint, out);
}

/*
 * A callback claims a node's content even when the node has children: the
 * engine lays those children out inside whatever the callback reported, which
 * is the only way a foreign subtree can keep its own layout rules. Neither
 * table has to be there: a caller that measures nothing carries neither, and a
 * caller whose callbacks need no state carries only the function table.
 */
static bool measured_extent(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                            cf_intrinsic mode, cf_size *out) {
    const cf_measure_fn *measures = ctx->tree->measures;

    if (measures == NULL || measures[slot] == NULL) {
        return false;
    }

    void *user_data = ctx->tree->measure_data != NULL ? ctx->tree->measure_data[slot] : NULL;
    const cf_size measured = measures[slot](user_data, constraint, mode);

    *out = (cf_size){.width = clamp_non_negative(measured.width),
                     .height = clamp_non_negative(measured.height)};

    return true;
}

/* A percentage on any of these resolves against the room a pass offers,
 * which is exactly the dependence the flag below rules out. */
static bool length_is_percent(cf_length length) {
    return length.kind == CF_LENGTH_PERCENT;
}

static bool box_has_percent(cf_box box) {
    return length_is_percent(box.left) || length_is_percent(box.top) ||
           length_is_percent(box.right) || length_is_percent(box.bottom);
}

static bool style_offers_nothing(const cf_style *style) {
    return style->wrap == CF_NO_WRAP && !box_has_percent(style->padding) &&
           !box_has_percent(style->border) && !box_has_percent(style->margin) &&
           !length_is_percent(style->column_gap) && !length_is_percent(style->row_gap) &&
           !length_is_percent(style->min_width) && !length_is_percent(style->max_width) &&
           !length_is_percent(style->min_height) && !length_is_percent(style->max_height);
}

/*
 * How far the content box sits inside the border box. Padding and border are
 * only ever added together - the engine paints no separate frame - so they
 * resolve into one set of edges.
 */
static cf_edges resolve_frame(const layout_ctx *ctx, uint32_t slot, float percent_base) {
    const cf_style *style = &ctx->tree->styles[slot];
    const cf_edges padding = cf_box_resolve(style->padding, percent_base);
    const cf_edges border = cf_box_resolve(style->border, percent_base);

    return (cf_edges){.left = padding.left + border.left,
                      .top = padding.top + border.top,
                      .right = padding.right + border.right,
                      .bottom = padding.bottom + border.bottom};
}

/*
 * A frame without a percentage side reads no offer, so it is one answer for
 * the whole layout and the sweep below resolves it once per node. A frame
 * with one is resolved against the base the caller was given.
 */
cf_edges content_insets(const layout_ctx *ctx, uint32_t slot, float percent_base) {
    if (ctx->frames != NULL && ctx->frames[slot].no_percent) {
        return ctx->frames[slot].inset;
    }

    return resolve_frame(ctx, slot, percent_base);
}

/*
 * The bits of a float, so an offer reads as the key it is rather than as the
 * quantity it means: two offers that compare equal in `measure_same` below
 * must land on one cell, and -0.0 is the only pair that does not.
 */
static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);

    return bits;
}

/* One word into the running key. The rotates keep a slot number, whose low
 * bits change every node, from lining up with the axis above it. */
static uint32_t fold_key(uint32_t hash, uint32_t word) {
    hash ^= word * 0x85EBCA6Bu;

    return (hash << 16 | hash >> 16) * 0xC2B2AE35u;
}

/*
 * The question a measuring pass asks: one slot, one intrinsic kind, one
 * content offer. A length is a kind and a value, so each axis folds to one
 * word and the whole key costs four multiplies - walked a byte at a time the
 * hash alone would rival the probe it indexes.
 */
static uint32_t measure_hash(uint32_t slot, uint32_t mode, cf_available constraint) {
    uint32_t hash = 0x9E3779B9u ^ slot ^ (mode << 8);

    hash = fold_key(hash, float_bits(constraint.width.value) +
                              (uint32_t)constraint.width.kind * 0x9E3779B9u);
    hash = fold_key(hash, float_bits(constraint.height.value) +
                              (uint32_t)constraint.height.kind * 0x85EBCA6Bu);

    return hash;
}

/* The cell an ask lands on, from a hash the ask already paid for. */
static uint32_t measure_probe(const layout_ctx *ctx, uint32_t hash) {
    return hash & (ctx->measure_capacity - 1u);
}

/* The slot a measuring pass asks about first. */
static bool measure_same(const measure_cell *cell, uint32_t slot, uint32_t mode,
                         cf_available constraint) {
    return cell->slot == slot && cell->mode == mode &&
           cell->constraint.width.kind == constraint.width.kind &&
           cell->constraint.width.value == constraint.width.value &&
           cell->constraint.height.kind == constraint.height.kind &&
           cell->constraint.height.value == constraint.height.value;
}

/* Stores an answer found after the recursion below it may have filled
 * cells, so the probe restarts from the hash rather than a spot taken
 * before the descent. The caller guarantees an empty cell exists. */
static void measure_store(const layout_ctx *ctx, uint32_t slot, uint32_t mode,
                          cf_available constraint, cf_size extent, uint32_t hash) {
    /* The memo extends as the queries run: an answer stored here is the
     * answer the query that found it would have returned, so the const
     * guards the tree and the rects, not the table of answers. */
    layout_ctx *const table = (layout_ctx *)ctx;

    uint32_t at = measure_probe(table, hash);

    while (table->measures[at].slot != MEASURE_SLOT_NONE) {
        at = (at + 1u) & (table->measure_capacity - 1u);
    }

    table->measures[at] =
        (measure_cell){.slot = slot, .mode = mode, .constraint = constraint, .extent = extent};
    table->measure_count++;

    /* A full table has no empty cell to end a probe, so every later ask
     * recomputes rather than walks the table forever. */
    table->measure_full = table->measure_count == table->measure_capacity;
}

/*
 * The table pays when subtrees are asked once for every level above them,
 * which only a tree this tall can do; a shallow tree re-asks nothing and
 * would only pay for the probes.
 */
#define CACHE_MIN_DEPTH 3u

/*
 * Whether the intrinsic extent of a subtree answers the same whatever room
 * it is offered. A wrapping container breaks its lines at the offer, a
 * measure callback wraps its content at the width it is handed, a grid
 * distributes the offer across its tracks, and a percentage on the frame,
 * the gaps or the bounds resolves against it. A node free of all of those
 * measures to one extent, and so does every node below it that is free too.
 * The height returned is the longest run of edges down from the slot.
 */
static uint32_t measure_offers_nothing(layout_ctx *ctx, uint32_t slot) {
    const cf_style *style = &ctx->tree->styles[slot];
    bool unconstrained = style_offers_nothing(style) &&
                         (ctx->tree->measures == NULL || ctx->tree->measures[slot] == NULL) &&
                         style->display != CF_DISPLAY_GRID;

    uint32_t height = 0u;
    const uint32_t first = ctx->tree->child_start[slot];
    const uint32_t last = ctx->tree->child_start[slot + 1u];

    for (uint32_t i = first; i < last; ++i) {
        const uint32_t child_height = measure_offers_nothing(ctx, ctx->tree->child_slots[i]);

        if (child_height > height) {
            height = child_height;
        }

        unconstrained = unconstrained && ctx->offer_free[ctx->tree->child_slots[i]];
    }

    ctx->offer_free[slot] = unconstrained;

    return height + 1u;
}

void measure_cache_init(layout_ctx *ctx) {
    uint32_t capacity = 16u;

    while ((uint64_t)capacity < (uint64_t)ctx->tree->node_count * 3u) {
        capacity *= 2u;
    }

    const uint32_t node_count = ctx->tree->node_count;
    bool *offers = cf_alloc(ctx->scratch, (size_t)node_count * sizeof *offers);
    frame_cell *frames = cf_alloc(ctx->scratch, (size_t)node_count * sizeof *frames);

    if (offers == NULL || frames == NULL) {
        return;
    }

    ctx->offer_free = offers;
    ctx->frames = frames;

    /* Every slot in the declaration, not only the ones reachable from the
     * root: a layout of a subtree can be handed a slot number that belongs to
     * a sibling, and a frame read past the sweep would be a frame never
     * resolved. */
    for (uint32_t slot = 0u; slot < node_count; ++slot) {
        const cf_style *style = &ctx->tree->styles[slot];

        frames[slot].no_percent =
            !box_has_percent(style->padding) && !box_has_percent(style->border);

        if (frames[slot].no_percent) {
            frames[slot].inset = resolve_frame(ctx, slot, 0.0f);
        }
    }

    if (measure_offers_nothing(ctx, ctx->root) < CACHE_MIN_DEPTH) {
        return;
    }

    measure_cell *cells = cf_alloc(ctx->scratch, capacity * sizeof *cells);

    if (cells == NULL) {
        return;
    }

    memset(cells, 0xFF, capacity * sizeof *cells);

    ctx->measures = cells;
    ctx->measure_capacity = capacity;
}

bool extent_is_empty(const layout_ctx *ctx, uint32_t slot) {
    const cf_measure_fn *measures = ctx->tree->measures;

    return child_count(ctx, slot) == 0u && (measures == NULL || measures[slot] == NULL);
}

/*
 * The content extent of a node: what a callback reports, what its children
 * fill, or nothing at all when it is a leaf nobody can measure. The answer
 * is a pure function of the slot, the intrinsic kind and the content offer,
 * so the table serves every ask that repeats one, whatever pass it comes
 * from.
 */
cf_size content_extent_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                          cf_available constraint) {
    /* Leaves are the majority of the asks a deep tree makes, and each one
     * saved is a line of the table that stays warm. */
    if (extent_is_empty(ctx, slot)) {
        return (cf_size){0};
    }

    const uint32_t mode_word = (uint32_t)mode;
    const bool cached = ctx->measures != NULL && !ctx->measure_full;

    /* An offer-free subtree answers one extent for every room it is
     * offered, so its cached asks share a canonical key: the table then
     * holds one entry per slot and kind instead of one per ancestor. The
     * computation itself still sees the real constraint, whose percent
     * lengths resolve against it. */
    cf_available key = constraint;

    if (cached && ctx->offer_free[slot]) {
        key = (cf_available){.width = CF_AUTO, .height = CF_AUTO};
    }

    const uint32_t hash = measure_hash(slot, mode_word, key);

    if (cached) {
        uint32_t at = measure_probe(ctx, hash);

        while (ctx->measures[at].slot != MEASURE_SLOT_NONE) {
            if (measure_same(&ctx->measures[at], slot, mode_word, key)) {
                return ctx->measures[at].extent;
            }

            at = (at + 1u) & (ctx->measure_capacity - 1u);
        }
    }

    cf_size extent = {0};

    if (!measured_extent(ctx, slot, constraint, mode, &extent)) {
        if (ctx->tree->styles[slot].display == CF_DISPLAY_GRID) {
            extent = grid_extent_of(ctx, slot, constraint, mode);
        } else {
            const flex_pass pass = {
                .constraint = constraint, .intrinsic = mode, .mode = FLEX_MEASURE};

            extent = flex_extent_of(ctx, slot, &pass);
        }
    }

    if (cached) {
        measure_store(ctx, slot, mode_word, key, extent, hash);
    }

    return extent;
}

cf_size border_size_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                       cf_available constraint, cf_available verdict, bool measuring) {
    const cf_style *style = &ctx->tree->styles[slot];

    float percent_base = 0.0f;
    definite_offer(constraint.width, &percent_base);

    const cf_edges inset = content_insets(ctx, slot, percent_base);

    float width = 0.0f;
    float height = 0.0f;
    const bool has_width =
        settled_length(verdict.width, style->width, constraint.width, measuring, &width);
    const bool has_height =
        settled_length(verdict.height, style->height, constraint.height, measuring, &height);

    /* An axis that is already settled bounds the box the other axis measures
     * itself into. The measure below sizes content, so the settled border box
     * is offered taken inside the frame: a child's percentages read the box
     * its content fills, not the one the frame wraps. */
    const cf_available content_offer = {
        .width = has_width ? CF_PX(clamp_non_negative(width - inset.left - inset.right))
                           : constraint.width,
        .height = has_height ? CF_PX(clamp_non_negative(height - inset.top - inset.bottom))
                             : constraint.height,
    };

    if (!has_width || !has_height) {
        const cf_size content = content_extent_of(ctx, slot, mode, content_offer);

        if (!has_width) {
            width = content.width + inset.left + inset.right;
        }
        if (!has_height) {
            height = content.height + inset.top + inset.bottom;
        }

        /* Shrink-to-fit, and only on the inline axis: content with no verdict
         * takes the width it was offered rather than pushing past it, but never
         * the width below what the content cannot break. CSS caps no auto height
         * against an offered height, so overflowing vertically stays legal. */
        float offered_width;
        if (!has_width && mode == CF_INTRINSIC_MAX_CONTENT &&
            definite_offer(constraint.width, &offered_width) && width > offered_width) {
            const cf_size unbreakable =
                border_size_of(ctx, slot, CF_INTRINSIC_MIN_CONTENT, constraint, verdict, measuring);
            width = larger(offered_width, unbreakable.width);
        }
    }

    /* min and max clamp the border box, and min keeps the last word when the
     * pair contradicts, so a misconfigured node still lands somewhere. */
    const float clamped_width =
        cf_size_clamp(width, style->min_width, style->max_width, constraint.width);
    const float clamped_height =
        cf_size_clamp(height, style->min_height, style->max_height, constraint.height);

    /* A border box is drawn no smaller than the padding and border it has to
     * hold: the frame has the last word, the way min does when min and max
     * contradict. A declared or clamped size below it is impossible to paint. */
    return (cf_size){.width = larger(clamped_width, inset.left + inset.right),
                     .height = larger(clamped_height, inset.top + inset.bottom)};
}
