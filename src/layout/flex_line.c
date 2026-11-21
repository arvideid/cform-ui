#include "layout/internal.h"

/*
 * A line whose hypothetical main size exceeds the container's by less than this
 * is a float artifact rather than a real overflow, and breaking it would move
 * an item onto a second line the declaration never asked for.
 */
#define MAIN_FIT_TOLERANCE 0.01f

/*
 * §9.7 restarts from the sizes a round begins with: an item already frozen
 * contributes the size it was clamped to, one still flexing contributes its
 * hypothetical size rather than the target the previous round handed it.
 */
static float line_flexing_outer_main(const flex_context *f, const flex_item *items,
                                     const flex_line *line) {
    float total = f->gap_main * (float)(line->count > 1u ? line->count - 1u : 0u);

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        const float main = items[i].frozen ? items[i].main : items[i].hypothetical_main;
        total += main + items[i].margin_main_start + items[i].margin_main_end;
    }

    return total;
}

/*
 * §9.2.2: wrapping happens between items, never inside one. An intrinsic
 * measure is content-driven and has no main size to break against: max-content
 * keeps every item on one line and min-content gives each a line of its own,
 * which is how the narrowest a wrapped container can go is found. A measure of
 * a container that declared its own main size does have one to break against,
 * and it breaks there like the arrangement pass does - that is the width the
 * lines will really be packed into.
 */
void collect_lines(const flex_context *f, const flex_item *items, uint32_t count, flex_line *lines,
                   uint32_t *line_count) {
    const bool wraps = f->style->wrap != CF_NO_WRAP;
    const bool one_per_line = wraps && f->measuring && f->intrinsic == CF_INTRINSIC_MIN_CONTENT;
    const bool may_break = one_per_line || (wraps && f->main_definite);

    lines[0] = (flex_line){.first = 0u, .count = 0u, .outer_main = 0.0f};
    *line_count = 1u;

    for (uint32_t i = 0; i < count; ++i) {
        flex_line *line = &lines[*line_count - 1u];
        const float gap = line->count > 0u ? f->gap_main : 0.0f;
        const float outer = item_outer_main(&items[i]);
        const bool overflow =
            one_per_line || (f->main_definite &&
                             line->outer_main + gap + outer - f->main_size > MAIN_FIT_TOLERANCE);

        if (may_break && line->count > 0u && overflow) {
            ++*line_count;
            line = &lines[*line_count - 1u];
            *line = (flex_line){.first = i, .count = 0u, .outer_main = 0.0f};
        }

        line->outer_main += gap + outer;
        line->count++;
    }
}

/*
 * The size a grow round books and grows from: the item's base size, or, when it
 * declared a base of zero, the hypothetical size its bounds settled - there is
 * nothing in a zero base to grow into, and the line still has to book the floor
 * the item cannot go under.
 */
static float grow_base_size(const flex_item *item) {
    return item->basis > 0.0f ? item->basis : item->hypothetical_main;
}

/*
 * §9.7 step 3: a round books an item already frozen at the size it was clamped
 * to and an item still flexing at the size it grows from. The minimum an item
 * declares is a violation the round catches, not a size it starts from - an item
 * a minimum holds above its base size still owes the line only the base, and the
 * room between is what the rest grow into.
 */
static float grow_outer_main(const flex_context *f, const flex_item *items, const flex_line *line) {
    float total = f->gap_main * (float)(line->count > 1u ? line->count - 1u : 0u);

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        const float main = items[i].frozen ? items[i].main : grow_base_size(&items[i]);
        total += main + items[i].margin_main_start + items[i].margin_main_end;
    }

    return total;
}

/*
 * §9.7 steps 1-2: grow. An item whose target breaks a bound is frozen there and
 * the round restarts, so the room it cannot take goes to the rest. A round with
 * no room left still runs, because a base size already outside an item's bounds
 * is a violation the next round has room to hand over. Each round freezes at
 * least one item, so the line's item count bounds the loop.
 */
void grow_line(const flex_context *f, flex_item *items, const flex_line *line) {
    const uint32_t end = line->first + line->count;

    for (uint32_t i = line->first; i < end; ++i) {
        items[i].frozen = items[i].grow <= 0.0f;
    }

    for (uint32_t round = 0; round < line->count; ++round) {
        const float free_space = clamp_non_negative(f->main_size - grow_outer_main(f, items, line));

        float pool = 0.0f;
        for (uint32_t i = line->first; i < end; ++i) {
            if (!items[i].frozen) {
                pool += items[i].grow;
            }
        }

        if (pool <= 0.0f) {
            return;
        }

        bool refroze = false;
        for (uint32_t i = line->first; i < end; ++i) {
            if (items[i].frozen) {
                continue;
            }

            /* §9.7 step 3: factors that do not add up to one hand each item its
             * own share of the space and leave the rest of it unused, rather
             * than normalising the shares until the line is filled. */
            const float share =
                pool < 1.0f ? free_space * items[i].grow : free_space * items[i].grow / pool;

            const float target = grow_base_size(&items[i]) + share;
            /* A size that only reaches a bound is not a violation of it, and
             * freezing it there would end the rounds early - the item stays
             * flexing so a later round can hand it the room an item clamped to
             * its maximum gives up. The floor keeps the last word when a min
             * and a max contradict. */
            if (target < items[i].min_main) {
                items[i].main = items[i].min_main;
                items[i].frozen = true;
                refroze = true;
            } else if (target > items[i].max_main) {
                items[i].main = items[i].max_main;
                items[i].frozen = true;
                refroze = true;
            } else {
                items[i].main = target;
            }
        }

        if (!refroze) {
            return;
        }
    }
}

/*
 * §9.9.1: with no declared minimum an item cannot be shrunk below its
 * content-based minimum, which is the smaller of its min-content contribution
 * and the size its own declaration asked for - and never below the base size
 * the pass already settled on. The answer costs a measure of the item's
 * content, so it is resolved on first violation and then kept in min_main.
 */
static float shrink_floor(const flex_context *f, flex_item *item) {
    if (!item->min_is_auto) {
        return item->min_main;
    }

    const cf_size content =
        content_extent_of(f->ctx, item->slot, CF_INTRINSIC_MIN_CONTENT, f->constraint);
    const cf_edges inset = content_insets(f->ctx, item->slot, f->percent_base);
    const float content_main = f->row ? content.width + inset.left + inset.right
                                      : content.height + inset.top + inset.bottom;

    item->min_is_auto = false;
    item->min_main = clamp_range(content_main, 0.0f, item->hypothetical_main);

    return item->min_main;
}

/*
 * §9.7 steps 3-5: shrink, weighted by flex-shrink times the base size so an
 * item with nothing to give gives nothing. An item that reaches its floor is
 * kept there while the rounds restart, so the space it cannot give goes to the
 * rest. An empty pool would leave the line overflowing even though its items
 * asked to shrink, so the shortfall is split between them instead.
 *
 * The used sizes are not re-initialised here: a line that grew has nothing to
 * shrink and returns from the first round, and rewriting the sizes would undo
 * what §9.7 steps 1-2 just decided.
 */
void shrink_line(const flex_context *f, flex_item *items, const flex_line *line) {
    const uint32_t end = line->first + line->count;

    for (uint32_t i = line->first; i < end; ++i) {
        items[i].frozen = items[i].shrink <= 0.0f;
    }

    for (uint32_t round = 0; round < line->count; ++round) {
        float pool = 0.0f;
        uint32_t unfrozen = 0u;
        for (uint32_t i = line->first; i < end; ++i) {
            if (!items[i].frozen) {
                pool += items[i].shrink * items[i].basis;
                ++unfrozen;
            }
        }

        const float shortage = line_flexing_outer_main(f, items, line) - f->main_size;
        if (shortage <= 0.0f || unfrozen == 0u) {
            return;
        }

        /* Every base size is zero, so the weighted factors are too and the
         * shortfall has nothing to be weighted against. */
        const bool equal_split = pool <= 0.0f;

        bool refroze = false;
        for (uint32_t i = line->first; i < end; ++i) {
            if (items[i].frozen) {
                continue;
            }

            const float reduction = equal_split
                                        ? shortage / (float)unfrozen
                                        : shortage * (items[i].shrink * items[i].basis) / pool;
            const float target = items[i].hypothetical_main - reduction;

            /* The hypothetical size is already inside the maximum and shrinking
             * only moves it down, so the floor is the sole violation here - and
             * the floor keeps the last word when a min and a max contradict. */
            if (target <= shrink_floor(f, &items[i])) {
                items[i].main = items[i].min_main;
                items[i].frozen = true;
                refroze = true;
            } else {
                items[i].main = target;
            }
        }

        if (!refroze) {
            return;
        }
    }
}

/* §9.6.1: a line is as wide across as its tallest item. */
void collect_line_cross(const flex_context *f, const flex_item *items, flex_line *lines,
                        uint32_t line_count) {
    for (uint32_t line_index = 0; line_index < line_count; ++line_index) {
        flex_line *line = &lines[line_index];
        line->cross = 0.0f;

        for (uint32_t i = line->first; i < line->first + line->count; ++i) {
            line->cross = larger(line->cross, item_outer_cross(&items[i]));
        }
    }

    /* §9.6.1: a single line in a container whose cross size is settled takes
     * that size, which is what lets align-items centre or stretch an item in a
     * tall box. A wrapping container packed by any other align-content keyword
     * keeps the line at its content size and moves the whole line instead. An
     * intrinsic measure has no settled cross size to take. */
    const bool packed_apart =
        f->style->wrap != CF_NO_WRAP && content_mode(f) != CF_ALIGN_CONTENT_STRETCH;
    if (line_count == 1u && !packed_apart && !f->measuring && f->cross_definite &&
        lines[0].cross < f->cross_size) {
        lines[0].cross = f->cross_size;
    }
}
