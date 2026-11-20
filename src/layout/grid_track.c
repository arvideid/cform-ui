#include <float.h>

#include "layout/internal.h"

/*
 * One track while its axis is being solved. `base` is the floor it never falls
 * below, `limit` the size its own sizing function caps it at - open-ended for a
 * fractional track - and `flex` the shares it claims of leftover space, zero
 * when it claims none. A track given its floor as its used size is frozen and
 * stops taking part in the flexible distribution.
 */
typedef struct track_state {
    float base;
    float limit;
    float flex;
    bool frozen;
} track_state;

/*
 * A track with no length to call its own takes its floor from the items filling
 * it: `auto` means min-content there, and a fraction only claims space that is
 * left over, so it never sets a floor of its own either.
 */
static bool min_hugs_content(cf_length min) {
    return min.kind == CF_LENGTH_AUTO || min.kind == CF_LENGTH_FR;
}

static bool max_is_flexible(const cf_track *track) {
    return track->max.kind == CF_LENGTH_FR;
}

/* `auto` is the only maximum that means "as much as the content asks for": a
 * length is a ceiling the content cannot push past. */
static bool max_hugs_content(const cf_track *track) {
    return track->max.kind == CF_LENGTH_AUTO;
}

/* Which tracks a contribution pass can still push. A floor follows the items
 * whenever it hugs content; a ceiling follows them when it is open-ended, and
 * a fractional track has no ceiling to reach, so its floor absorbs the weight
 * and the leftover is shared out later. */
static bool can_grow(const cf_track *track, bool to_limit) {
    if (!to_limit) {
        return min_hugs_content(track->min);
    }

    return max_hugs_content(track) || max_is_flexible(track);
}

static float *raise_target(track_state *state, const cf_track *track, bool to_limit) {
    if (!to_limit || max_is_flexible(track)) {
        return &state->base;
    }

    return &state->limit;
}

/*
 * How much a track offers an item that spans it: its floor while floors are
 * being settled, its ceiling once the content that fills it is counted. A
 * fractional track offers its floor on both passes, because its ceiling is
 * whatever the leftover turns out to be.
 */
static float track_offer(const track_state *state, const cf_track *track, bool to_limit) {
    if (!to_limit || max_is_flexible(track)) {
        return state->base;
    }

    return state->limit;
}

static float grow_room(track_state *state, const cf_track *track, bool to_limit) {
    if (to_limit && !max_is_flexible(track)) {
        return FLT_MAX;
    }

    return state->limit - state->base;
}

/*
 * What an item weighs on one axis: its border box plus the margins it carries
 * there. A column is measured against the container, because the cell the item
 * lands in is not known yet; a row is measured against the width the columns
 * settled on, so content that wraps sees the width it will really have.
 */
static float item_weight(const grid_ctx *g, const grid_item *item, cf_intrinsic mode,
                         bool width_axis) {
    const cf_style *child = &g->ctx->tree->styles[item->slot];
    cf_available offer = g->constraint;

    if (!width_axis) {
        const float cell =
            span_size(g->column_sizes, item->column_start, item->column_span, g->column_gap);

        offer = (cf_available){.width = CF_PX(cell), .height = CF_AUTO};
    }

    const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};
    const cf_size box = border_size_of(g->ctx, item->slot, mode, offer, undecided);
    const cf_edges margin = cf_box_resolve(child->margin, g->percent_base);

    return width_axis ? box.width + margin.left + margin.right
                      : box.height + margin.top + margin.bottom;
}

static void init_states(const cf_track *tracks, uint32_t count, float percent_base,
                        track_state *state) {
    for (uint32_t t = 0u; t < count; ++t) {
        track_state *s = &state[t];
        const cf_track *track = &tracks[t];

        s->frozen = false;
        s->flex = max_is_flexible(track) ? clamp_non_negative(track->max.value) : 0.0f;
        s->base = min_hugs_content(track->min) ? 0.0f : cf_length_resolve(track->min, percent_base);

        if (max_is_flexible(track)) {
            s->limit = FLT_MAX;
        } else if (max_hugs_content(track)) {
            s->limit = s->base;
        } else {
            s->limit = cf_length_resolve(track->max, percent_base);
        }

        /* minmax(a, b) with b below a is the pair nobody asked for: the floor
         * keeps the last word, as it does for a box's own min and max. */
        if (s->limit < s->base) {
            s->limit = s->base;
        }
    }
}

/*
 * Grow the tracks an item spans until they can hold what it weighs, sharing what
 * is missing between the ones their sizing function still lets grow. A track
 * that cannot take its whole share hands the rest on, so a span across a fixed
 * track still fits.
 */
static void distribute(track_state *state, const cf_track *tracks, uint32_t start, uint32_t span,
                       float gap, float weight, bool to_limit) {
    for (;;) {
        float missing = weight - gap * (float)(span > 1u ? span - 1u : 0u);

        for (uint32_t t = start; t < start + span; ++t) {
            missing -= track_offer(&state[t], &tracks[t], to_limit);
        }

        if (missing <= 0.0f) {
            return;
        }

        uint32_t growable = 0u;

        for (uint32_t t = start; t < start + span; ++t) {
            if (can_grow(&tracks[t], to_limit) &&
                grow_room(&state[t], &tracks[t], to_limit) > 0.0f) {
                ++growable;
            }
        }

        if (growable == 0u) {
            return;
        }

        const float share = missing / (float)growable;
        float moved = 0.0f;

        for (uint32_t t = start; t < start + span; ++t) {
            if (!can_grow(&tracks[t], to_limit)) {
                continue;
            }

            const float room = grow_room(&state[t], &tracks[t], to_limit);
            const float given = room < share ? room : share;

            if (given <= 0.0f) {
                continue;
            }

            *raise_target(&state[t], &tracks[t], to_limit) += given;
            moved += given;
        }

        /* Every track that could move is at its ceiling: what is still missing
         * has nowhere left to go, so the span overflows rather than looping. */
        if (moved <= 0.0f) {
            return;
        }
    }
}

static void collect_contributions(const grid_ctx *g, const cf_track *tracks, track_state *state,
                                  float gap, cf_intrinsic mode, bool to_limit, bool width_axis) {
    for (uint32_t i = 0u; i < g->item_count; ++i) {
        const grid_item *item = &g->items[i];
        const uint32_t start = width_axis ? item->column_start : item->row_start;
        const uint32_t span = width_axis ? item->column_span : item->row_span;

        distribute(state, tracks, start, span, gap, item_weight(g, item, mode, width_axis),
                   to_limit);
    }
}

/*
 * §12.7: what the other tracks leave over is shared between the fractional ones
 * in proportion to their factors. A track whose floor is more than its share
 * freezes at the floor and the rest is worked out again, so a content-heavy
 * minmax does not take space from the tracks that can still use it.
 */
static void resolve_flexible(track_state *state, uint32_t count, float *sizes, float room) {
    float claimed = 0.0f;
    float flex_total = 0.0f;

    for (uint32_t t = 0u; t < count; ++t) {
        if (state[t].flex > 0.0f) {
            flex_total += state[t].flex;
            continue;
        }

        /* A track with no share to claim is settled: it keeps its floor and its
         * size never depends on the leftover. */
        state[t].frozen = true;
        sizes[t] = state[t].base;
        claimed += state[t].base;
    }

    while (flex_total > 0.0f) {
        float leftover = room - claimed;

        if (leftover < 0.0f) {
            leftover = 0.0f;
        }

        const float fr_size = leftover / flex_total;
        uint32_t froze = 0u;
        float frozen_flex = 0.0f;

        for (uint32_t t = 0u; t < count; ++t) {
            if (state[t].frozen || state[t].base <= state[t].flex * fr_size) {
                continue;
            }

            state[t].frozen = true;
            sizes[t] = state[t].base;
            claimed += state[t].base;
            frozen_flex += state[t].flex;
            ++froze;
        }

        if (froze == 0u) {
            for (uint32_t t = 0u; t < count; ++t) {
                if (!state[t].frozen) {
                    sizes[t] = larger(state[t].base, state[t].flex * fr_size);
                }
            }

            return;
        }

        flex_total -= frozen_flex;
    }
}

/*
 * The alignment a grid container starts with behaves as stretch: what a box
 * that is already sized leaves over is shared between the tracks that named no
 * ceiling, so a container with a definite height fills it instead of drawing a
 * band of cells at the top. A track that named a length keeps exactly that.
 */
static void stretch_tracks(track_state *state, const cf_track *tracks, uint32_t count, float room) {
    float leftover = room;
    uint32_t growable = 0u;

    for (uint32_t t = 0u; t < count; ++t) {
        leftover -= state[t].base;

        if (max_hugs_content(&tracks[t])) {
            ++growable;
        }
    }

    if (growable == 0u || leftover <= 0.0f) {
        return;
    }

    const float share = leftover / (float)growable;

    for (uint32_t t = 0u; t < count; ++t) {
        if (max_hugs_content(&tracks[t])) {
            state[t].base += share;
        }
    }
}

/*
 * §12.4 through §12.7 over one axis: the floors and ceilings the items ask for,
 * then whatever the container's own size allows. Leftover space is only shared
 * out when `settled_box` says the axis is already sized: the offer a node is
 * measured against comes from its parent's box, and a parent that took that
 * height from this measurement would otherwise hand space back to the tracks
 * that asked for it. An axis with no leftover gives a fractional track nothing
 * beyond its floor.
 */
static void size_axis(const grid_ctx *g, const cf_track *tracks, uint32_t count, float *sizes,
                      track_state *state, float gap, cf_length offer_length, bool width_axis,
                      bool settled_box) {
    float percent_base = 0.0f;
    definite_offer(offer_length, &percent_base);
    init_states(tracks, count, percent_base, state);

    collect_contributions(g, tracks, state, gap, CF_INTRINSIC_MIN_CONTENT, false, width_axis);
    collect_contributions(g, tracks, state, gap, g->intrinsic, true, width_axis);

    float offer;

    if (!settled_box || !definite_offer(offer_length, &offer)) {
        for (uint32_t t = 0u; t < count; ++t) {
            sizes[t] = state[t].flex > 0.0f ? state[t].base : larger(state[t].base, state[t].limit);
        }

        return;
    }

    /* §12.6 step 1: a track stops at its ceiling as soon as there is room to say
     * it has one. A fractional track keeps its floor, because the space it wants
     * is what the ceilings leave over. */
    const float room = offer - gap * (float)(count > 1u ? count - 1u : 0u);
    bool flexible = false;

    for (uint32_t t = 0u; t < count; ++t) {
        if (state[t].flex > 0.0f) {
            flexible = true;
            continue;
        }

        state[t].base = larger(state[t].base, state[t].limit);
    }

    if (flexible) {
        resolve_flexible(state, count, sizes, room);

        return;
    }

    stretch_tracks(state, tracks, count, room);

    for (uint32_t t = 0u; t < count; ++t) {
        sizes[t] = state[t].base;
    }
}

/*
 * Size both axes of `g` into the arrays it carries. Columns come first: a row
 * measures its items at the width the cells turned out to have, so an item that
 * wraps reports the height it will really need.
 */
bool grid_size_tracks(grid_ctx *g, bool settled_box) {
    const size_t mark = cf_arena_mark(g->ctx->scratch);
    track_state *column_state =
        cf_alloc(g->ctx->scratch, (size_t)g->column_count * sizeof *column_state);
    track_state *row_state = cf_alloc(g->ctx->scratch, (size_t)g->row_count * sizeof *row_state);

    if (column_state == NULL || row_state == NULL) {
        cf_arena_rewind(g->ctx->scratch, mark);

        return false;
    }

    size_axis(g, g->columns, g->column_count, g->column_sizes, column_state, g->column_gap,
              g->constraint.width, true, settled_box);
    size_axis(g, g->rows, g->row_count, g->row_sizes, row_state, g->row_gap, g->constraint.height,
              false, settled_box);

    cf_arena_rewind(g->ctx->scratch, mark);

    return true;
}
