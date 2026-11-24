#include "cf/node.h"

#include "node/internal.h"

void cf_stop(cf_event *event) {
    if (event != NULL) {
        event->stopped = true;
    }
}

void cf_capture(cf_event *event) {
    if (event != NULL) {
        event->captured = true;
    }
}

/* An id held across frames is only as good as the store's answer for it:
 * a retired or recycled node reads as nothing. */
static uint32_t live_slot(const cf_ctx *ctx, cf_id id) {
    return cf_store_get(&ctx->nodes, id) != NULL ? id.index : CF_NO_LINK;
}

static bool holds_slot(const cf_ctx *ctx, cf_id id, uint32_t slot) {
    return slot != CF_NO_LINK && cf_id_equal(id, cf_store_id(&ctx->nodes, slot)) &&
           cf_store_get(&ctx->nodes, id) != NULL;
}

/*
 * The node an interaction books its state on: the nearest record that acts on
 * the pointer - its own handler, or a themed family the resolver paints by
 * state - skipping the ones switched off. A label inside a button presses the
 * button; a disabled button presses nothing.
 */
static uint32_t interactive_of(const cf_ctx *ctx, uint32_t slot) {
    for (uint32_t at = slot; at != CF_NO_LINK; at = record_of(ctx, at)->parent) {
        const node_record *record = record_of(ctx, at);

        if (!record->disabled &&
            (record->handlers.pointer != NULL || record->role != CF_ROLE_NONE)) {
            return at;
        }
    }

    return CF_NO_LINK;
}

/*
 * Later siblings paint above earlier ones and children above the parent, so
 * the walk keeps the last hit and answers the topmost node. Records cannot
 * cycle - depth increases along every parent chain - which bounds the
 * recursion by construction.
 */
static uint32_t hit_walk(const cf_ctx *ctx, uint32_t slot, cf_point position) {
    uint32_t found = CF_NO_LINK;

    for (uint32_t child = record_of(ctx, slot)->child_first; child != CF_NO_LINK;
         child = record_of(ctx, child)->next_sibling) {
        const uint32_t in_child = hit_walk(ctx, child, position);

        if (in_child != CF_NO_LINK) {
            found = in_child;
        }
    }

    if (found != CF_NO_LINK) {
        return found;
    }

    const cf_rect *rect = &ctx->rects[slot];

    return position.x >= rect->x && position.x < rect->x + rect->width && position.y >= rect->y &&
                   position.y < rect->y + rect->height
               ? slot
               : CF_NO_LINK;
}

uint32_t cf_hit_test(const cf_ctx *ctx, cf_point position) {
    if (ctx == NULL || ctx->rects == NULL || ctx->root_slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return hit_walk(ctx, ctx->root_slot, position);
}

/* Runs the handlers from the target up through the parents, each with fresh
 * dispatch flags: stopping is this handler's decision about the ones above
 * it, and capturing binds the node whose handler ran. */
static void deliver(cf_ctx *ctx, cf_event *event, bool pointer, uint32_t slot) {
    for (uint32_t at = slot; at != CF_NO_LINK; at = record_of(ctx, at)->parent) {
        const node_record *record = record_of(ctx, at);

        /* A switched-off node stands out of the walk: its handlers stay
         * silent and the event keeps climbing to whoever is still on. */
        if (record->disabled) {
            continue;
        }

        cf_event_fn handler = pointer ? record->handlers.pointer : record->handlers.key;

        if (handler == NULL) {
            continue;
        }

        event->target = at;
        event->stopped = false;
        event->captured = false;
        handler(event, pointer ? record->handlers.pointer_data : record->handlers.key_data);

        if (pointer && event->captured) {
            ctx->capture_id = cf_store_id(&ctx->nodes, at);
        }

        if (event->stopped) {
            return;
        }
    }
}

void cf_input(cf_ctx *ctx, cf_event event) {
    if (ctx == NULL) {
        return;
    }

    /* A capture holds every pointer event until the release, wherever the
     * pointer went; hover follows the capture so pressed styling stays. */
    const uint32_t captured = live_slot(ctx, ctx->capture_id);

    switch (event.type) {
    case CF_EVENT_POINTER_MOVE: {
        if (captured != CF_NO_LINK) {
            ctx->hover_id = cf_store_id(&ctx->nodes, captured);
            deliver(ctx, &event, true, captured);
            return;
        }

        const uint32_t hit = cf_hit_test(ctx, event.position);

        /* A move over nothing leaves the pointer hovering nothing. */
        const uint32_t hover = hit != CF_NO_LINK ? interactive_of(ctx, hit) : CF_NO_LINK;

        ctx->hover_id = hover != CF_NO_LINK ? cf_store_id(&ctx->nodes, hover) : CF_NO_ID;

        if (hit != CF_NO_LINK) {
            deliver(ctx, &event, true, hit);
        }

        return;
    }

    case CF_EVENT_POINTER_DOWN: {
        if (captured != CF_NO_LINK) {
            deliver(ctx, &event, true, captured);
            return;
        }

        const uint32_t hit = cf_hit_test(ctx, event.position);

        if (hit == CF_NO_LINK) {
            return;
        }

        /* The press takes focus with it: a click lands on what the keyboard
         * should be talking to. */
        const uint32_t interactive = interactive_of(ctx, hit);

        if (interactive == CF_NO_LINK) {
            return;
        }

        ctx->press_id = cf_store_id(&ctx->nodes, interactive);
        ctx->focus_id = ctx->press_id;
        deliver(ctx, &event, true, hit);
        return;
    }

    case CF_EVENT_POINTER_UP: {
        /* The release goes to the node that captured, else to the node the
         * press landed on - letting go outside a button still ends the
         * press it started. */
        uint32_t target = captured;

        if (target == CF_NO_LINK) {
            target = live_slot(ctx, ctx->press_id);
        }

        if (target != CF_NO_LINK) {
            deliver(ctx, &event, true, target);
        }

        ctx->capture_id = CF_NO_ID;
        ctx->press_id = CF_NO_ID;

        const uint32_t hit = cf_hit_test(ctx, event.position);
        const uint32_t hover = hit != CF_NO_LINK ? interactive_of(ctx, hit) : CF_NO_LINK;

        ctx->hover_id = hover != CF_NO_LINK ? cf_store_id(&ctx->nodes, hover) : CF_NO_ID;
        return;
    }

    case CF_EVENT_SCROLL: {
        if (captured != CF_NO_LINK) {
            deliver(ctx, &event, true, captured);
            return;
        }

        const uint32_t hovered = live_slot(ctx, ctx->hover_id);

        /* A wheel arrives with its own pointer position, so the box under the
         * hand is knowable without a move having booked a hover first: the
         * notch that comes the moment a window takes focus, with the cursor
         * already resting inside it, has no move ahead of it to lean on. */
        const uint32_t target = hovered != CF_NO_LINK ? hovered : cf_hit_test(ctx, event.position);

        if (target != CF_NO_LINK) {
            deliver(ctx, &event, true, target);
        }

        return;
    }

    case CF_EVENT_KEY_DOWN:
    case CF_EVENT_KEY_UP: {
        const uint32_t focused = live_slot(ctx, ctx->focus_id);

        if (focused != CF_NO_LINK) {
            deliver(ctx, &event, false, focused);
        }

        return;
    }

    case CF_EVENT_QUIT:
        /* The device's quit never dispatches; the shell that owns the loop
         * reads it and stops. */
        break;
    }
}

void cf_focus(cf_ctx *ctx, uint32_t slot) {
    if (ctx == NULL || !holds_slot(ctx, cf_store_id(&ctx->nodes, slot), slot)) {
        return;
    }

    ctx->focus_id = cf_store_id(&ctx->nodes, slot);
}

bool cf_hovered(const cf_ctx *ctx, uint32_t slot) {
    return ctx != NULL && holds_slot(ctx, ctx->hover_id, slot);
}

bool cf_pressed(const cf_ctx *ctx, uint32_t slot) {
    return ctx != NULL && holds_slot(ctx, ctx->press_id, slot);
}

bool cf_focused(const cf_ctx *ctx, uint32_t slot) {
    return ctx != NULL && holds_slot(ctx, ctx->focus_id, slot);
}

cf_cursor cf_frame_cursor(const cf_ctx *ctx) {
    if (ctx == NULL) {
        return CF_CURSOR_DEFAULT;
    }

    const uint32_t hovered = live_slot(ctx, ctx->hover_id);

    if (hovered == CF_NO_LINK) {
        return CF_CURSOR_DEFAULT;
    }

    return record_of(ctx, hovered)->handlers.pointer != NULL && !record_of(ctx, hovered)->disabled
               ? CF_CURSOR_POINTER
               : CF_CURSOR_DEFAULT;
}
