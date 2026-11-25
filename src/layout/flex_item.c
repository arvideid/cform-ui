#include <float.h>

#include "layout/internal.h"

/*
 * A bound as a float. An absent bound is 0 below and unlimited above: a
 * percentage against a container that is still being measured bounds nothing,
 * and treating it as zero would collapse every box it applied to.
 */
static float resolve_bound(cf_length bound, cf_length offer, float absent) {
    float value;
    if (!cf_length_of(bound, offer, &value)) {
        return absent;
    }

    return clamp_non_negative(value);
}

static void axes_of_size(const flex_context *f, cf_size box, float *main, float *cross) {
    *main = f->row ? box.width : box.height;
    *cross = f->row ? box.height : box.width;
}

/*
 * A declared size the item can use as-is. A percentage measured while the
 * pass is still looking for the item's extent is a percentage of the answer
 * the pass exists to find, so it reads as auto here; the arranging pass that
 * follows resolves it against the settled box.
 */
static bool declared_size(const flex_context *f, cf_length declaration, cf_length offer,
                          float *out) {
    if (f->measuring && declaration.kind == CF_LENGTH_PERCENT) {
        return false;
    }

    return cf_length_of(declaration, offer, out);
}

/*
 * §9.3: reversing a direction moves which physical edge is main-start. §9.4
 * does the same for cross-start when the wrap is reversed, and only then -
 * reversing the main axis leaves the block and inline directions alone.
 */
static void assign_margins(const flex_context *f, const cf_box *margin, flex_item *item) {
    const cf_length main_start = f->row ? (f->reverse ? margin->right : margin->left)
                                        : (f->reverse ? margin->bottom : margin->top);
    const cf_length main_end = f->row ? (f->reverse ? margin->left : margin->right)
                                      : (f->reverse ? margin->top : margin->bottom);
    const cf_length cross_start = f->row ? (f->cross_reverse ? margin->bottom : margin->top)
                                         : (f->cross_reverse ? margin->right : margin->left);
    const cf_length cross_end = f->row ? (f->cross_reverse ? margin->top : margin->bottom)
                                       : (f->cross_reverse ? margin->left : margin->right);

    const cf_length sides[4] = {main_start, main_end, cross_start, cross_end};
    float *resolved[4] = {&item->margin_main_start, &item->margin_main_end,
                          &item->margin_cross_start, &item->margin_cross_end};
    bool *is_auto[4] = {&item->auto_margin_main_start, &item->auto_margin_main_end,
                        &item->auto_margin_cross_start, &item->auto_margin_cross_end};

    for (uint32_t side = 0; side < 4u; ++side) {
        *is_auto[side] = cf_length_is_auto(sides[side]);
        /* Auto reads as zero here, which is what a line's hypothetical outer
         * size needs; §9.5.2 gives the side its share later. */
        *resolved[side] = cf_length_resolve(sides[side], f->percent_base);
    }
}

static void measure_item(const flex_context *f, flex_item *item) {
    const cf_style *child = &f->ctx->tree->styles[item->slot];

    /* §9.2.1 steps 1-2: padding and border are part of the border box the
     * item reserves, so no base size is booked smaller than the frame it
     * carries. The line that books the item advances by the box the child is
     * really painted at, not the raw number. */
    const cf_edges inset = content_insets(f->ctx, item->slot, f->percent_base);
    const float main_frame = f->row ? inset.left + inset.right : inset.top + inset.bottom;
    const float cross_frame = f->row ? inset.top + inset.bottom : inset.left + inset.right;

    /* The item's own sizes and bounds, chosen for this pass's main and cross. */
    cf_length main_declared = f->row ? child->width : child->height;
    cf_length cross_declared = f->row ? child->height : child->width;
    cf_length main_min = f->row ? child->min_width : child->min_height;
    cf_length main_max = f->row ? child->max_width : child->max_height;
    cf_length cross_min = f->row ? child->min_height : child->min_width;
    cf_length cross_max = f->row ? child->max_height : child->max_width;
    cf_length main_basis = child->flex_basis;

    /* A content-box item measures each of these inside its frame, so each is
     * rewritten to the border box it stands for before the flex rules reserve
     * the item's main and cross sizes; the pass then reads border boxes. */
    if (child->box_sizing == CF_BOX_CONTENT) {
        main_declared =
            border_of_declaration(main_declared, f->main_offer, main_frame, f->measuring);
        cross_declared =
            border_of_declaration(cross_declared, f->cross_offer, cross_frame, f->measuring);
        main_basis = border_of_declaration(main_basis, f->main_offer, main_frame, f->measuring);
        main_min = border_of_declaration(main_min, f->main_offer, main_frame, false);
        main_max = border_of_declaration(main_max, f->main_offer, main_frame, false);
        cross_min = border_of_declaration(cross_min, f->cross_offer, cross_frame, false);
        cross_max = border_of_declaration(cross_max, f->cross_offer, cross_frame, false);
    }

    item->min_main = resolve_bound(main_min, f->main_offer, 0.0f);
    item->max_main = resolve_bound(main_max, f->main_offer, FLT_MAX);
    item->min_cross = resolve_bound(cross_min, f->cross_offer, 0.0f);
    item->max_cross = resolve_bound(cross_max, f->cross_offer, FLT_MAX);
    /* §4.5: an automatic main-axis minimum is the item's min-content size, so a
     * shrinking item is never packed below what its content cannot break. An
     * item that cuts its own overflow is the exception - it has somewhere to put
     * the part that does not fit - and without that exemption a scroller would
     * refuse to shrink and push out of its line instead of scrolling. */
    item->min_is_auto = cf_length_is_auto(main_min) && !cf_style_clips_content(child);
    item->grow = child->flex_grow;
    item->shrink = child->flex_shrink;

    /* §9.2.1 steps 3-5: flex-basis, then the declared main size, then the
     * content. Nothing declared means nothing measured yet either. */
    float declared;
    const bool main_is_declared = declared_size(f, main_basis, f->main_offer, &declared) ||
                                  declared_size(f, main_declared, f->main_offer, &declared);

    float cross_settled;
    item->cross_is_auto = !declared_size(f, cross_declared, f->cross_offer, &cross_settled);

    if (main_is_declared) {
        item->basis = clamp_non_negative(declared);
    } else {
        float content_main;
        float content_cross;
        axes_of_size(f,
                     border_size_of(f->ctx, item->slot, f->intrinsic, f->constraint,
                                    verdict_for(f, CF_AUTO, CF_AUTO), f->measuring),
                     &content_main, &content_cross);
        item->basis = content_main;
        item->cross = content_cross;
    }

    item->basis = larger(item->basis, main_frame);

    /* The automatic minimum size cannot exceed the base size it is compared
     * against, so clamping here without resolving it loses nothing. */
    item->hypothetical_main =
        larger(clamp_range(item->basis, item->min_main, item->max_main), main_frame);
    item->main = item->hypothetical_main;

    /* §9.2.1 steps 16-18: an item whose main size a declaration settled has
     * never been measured across it, and the line it joins needs the extent its
     * content asks for at the size the item will have. */
    if (main_is_declared && item->cross_is_auto) {
        float content_main;
        axes_of_size(f,
                     border_size_of(f->ctx, item->slot, f->intrinsic, f->constraint,
                                    verdict_for(f, CF_PX(item->main), CF_AUTO), f->measuring),
                     &content_main, &item->cross);
    }

    if (!item->cross_is_auto) {
        item->cross =
            larger(clamp_range(cross_settled, item->min_cross, item->max_cross), cross_frame);
    }
}

/*
 * The cross size of an item whose main size the pass has settled. The verdict
 * carries the main size so a wrapping callback sees the width the item will
 * actually have rather than the one it asked for.
 */
static void measure_item_cross(const flex_context *f, flex_item *item) {
    float main;
    axes_of_size(f,
                 border_size_of(f->ctx, item->slot, CF_INTRINSIC_MAX_CONTENT, f->constraint,
                                verdict_for(f, CF_PX(item->main), CF_AUTO), false),
                 &main, &item->cross);

    item->cross = clamp_range(item->cross, item->min_cross, item->max_cross);
}

/* §9.1 item 4: items pack in `order` and declaration order breaks a tie. The
 * sort is stable and is skipped entirely when nothing declares an order, which
 * is every container that has never used the property. */
static void sort_by_order(const flex_context *f, flex_item *items, uint32_t count) {
    uint32_t first_with_order = 0;
    while (first_with_order < count &&
           f->ctx->tree->styles[items[first_with_order].slot].order == 0) {
        ++first_with_order;
    }

    if (first_with_order == count) {
        return;
    }

    for (uint32_t i = 1; i < count; ++i) {
        const flex_item moving = items[i];
        const int32_t moving_order = f->ctx->tree->styles[moving.slot].order;
        uint32_t tail = i;

        while (tail > 0u && f->ctx->tree->styles[items[tail - 1u].slot].order > moving_order) {
            items[tail] = items[tail - 1u];
            --tail;
        }

        items[tail] = moving;
    }
}

uint32_t build_items(const flex_context *f, flex_item *items, uint32_t capacity) {
    const uint32_t first = f->ctx->tree->child_start[f->slot];
    uint32_t count = 0u;

    for (uint32_t i = 0; i < capacity; ++i) {
        const uint32_t slot = f->ctx->tree->child_slots[first + i];
        const cf_style *child = &f->ctx->tree->styles[slot];
        /* A box that leaves the flow takes no slot on the line: absolute ones
         * are placed against their containing block, and a `display: none` one
         * takes part in nothing, so it claims neither space nor a gap. */
        if (child->position == CF_POSITION_ABSOLUTE || child->display == CF_DISPLAY_NONE) {
            continue;
        }

        flex_item *item = &items[count++];
        *item = (flex_item){.slot = slot};
        assign_margins(f, &child->margin, item);
        measure_item(f, item);
    }

    sort_by_order(f, items, count);

    return count;
}

/* §9.2.2 step 5 and §9.6.1: an item whose main size changed since it was
 * measured is measured again, because its content can wrap differently at the
 * size the pass settled on. */
void remeasure_item_cross(const flex_context *f, flex_item *items, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (!f->measuring && items[i].cross_is_auto &&
            items[i].main != items[i].hypothetical_main) {
            measure_item_cross(f, &items[i]);
        }
    }
}
