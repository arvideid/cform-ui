#include "cf/node.h"

#include "node/internal.h"

#include "layout/rect.h"

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
 *
 * clip and clipping carry an ancestor's cut down the tree exactly as the paint
 * walk does. A node wholly inside no part of the clip painted nothing, so a
 * point landing where a clipping box carved it out belongs to whatever shows
 * through, not to the child the clip hid.
 */
static uint32_t hit_walk(const cf_ctx *ctx, uint32_t slot, cf_point position, cf_rect clip,
                         bool clipping) {
    /* A hidden box answers for no point and hides nothing above it from a
     * pointer that lands where its subtree used to reach - the same rule paint
     * applies when it drops the box from the stream. */
    if (ctx->layout.styles[slot].display == CF_DISPLAY_NONE) {
        return CF_NO_LINK;
    }

    const cf_rect box = ctx->rects[slot];

    if (clipping && !rect_intersects(box, clip)) {
        return CF_NO_LINK;
    }

    /* A box that clips its content bounds the descent: its children see the
     * box as their clip, narrowed by whatever already clipped this node - the
     * same rule paint hands down. Without an active clip the box is the clip. */
    const bool self_clips = cf_style_clips_content(&ctx->layout.styles[slot]);
    const cf_rect child_clip = self_clips ? (clipping ? rect_intersect(box, clip) : box) : clip;
    const bool child_clipping = clipping || self_clips;

    uint32_t found = CF_NO_LINK;

    for (uint32_t child = record_of(ctx, slot)->child_first; child != CF_NO_LINK;
         child = record_of(ctx, child)->next_sibling) {
        const uint32_t in_child = hit_walk(ctx, child, position, child_clip, child_clipping);

        if (in_child != CF_NO_LINK) {
            found = in_child;
        }
    }

    if (found != CF_NO_LINK) {
        return found;
    }

    /* The node answers only for a point that survives both its own box and the
     * clip it sits inside: past the clip these pixels were never drawn. */
    if (!rect_contains(box, position) || (clipping && !rect_contains(clip, position))) {
        return CF_NO_LINK;
    }

    return slot;
}

uint32_t cf_hit_test(const cf_ctx *ctx, cf_point position) {
    if (ctx == NULL || ctx->rects == NULL || ctx->root_slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    /* The root sits in no clip: the position is already in window coordinates,
     * so a box off-screen cannot hold it, and only a clips_content ancestor
     * narrows the region a click is allowed to land in. */
    return hit_walk(ctx, ctx->root_slot, position, (cf_rect){0.0f, 0.0f, 0.0f, 0.0f}, false);
}

/* Whether the box is hidden from every system that reads the view: the same
 * display:none that drops it from the stream and from the paint. Before the
 * layout has run there is no view to read, so nothing is hidden. */
static bool hidden_box(const cf_ctx *ctx, uint32_t slot) {
    return ctx->layout.styles != NULL && ctx->layout.styles[slot].display == CF_DISPLAY_NONE;
}

/*
 * Whether the keyboard may land here: the box says it takes focus, it is
 * switched on, and it is reachable. Being switched off or hidden is inherited by
 * the whole subtree, so the climb up has to be clean as well; declaring focus
 * is not inherited, because a focusable panel full of buttons is exactly the
 * page where both have to answer.
 */
static bool takes_focus(const cf_ctx *ctx, uint32_t slot) {
    const node_record *record = record_of(ctx, slot);

    if (!record->focusable || record->disabled || hidden_box(ctx, slot)) {
        return false;
    }

    for (uint32_t at = record->parent; at != CF_NO_LINK; at = record_of(ctx, at)->parent) {
        const node_record *ancestor = record_of(ctx, at);

        if (ancestor->disabled || hidden_box(ctx, at)) {
            return false;
        }
    }

    return true;
}

typedef struct focus_ring {
    const cf_ctx *ctx;
    /* Where focus is now, or the root when nothing holds it - a slot that never
     * answers as focusable, so the pass reads as having not passed it. */
    uint32_t from;
    uint32_t first;
    uint32_t last;
    /* The nearest focusable ahead of and behind `from` in document order. */
    uint32_t after;
    uint32_t before;
    bool past;
} focus_ring;

/* One visit in the document-order walk: the focusable boxes are met in the order
 * a person tabbing through them wants them, so a single pass over the tree
 * answers both directions without building a ring to keep. */
static void ring_visit(focus_ring *ring, uint32_t slot) {
    if (!takes_focus(ring->ctx, slot)) {
        return;
    }

    if (ring->first == CF_NO_LINK) {
        ring->first = slot;
    }

    ring->last = slot;

    if (slot == ring->from) {
        ring->past = true;
        return;
    }

    if (ring->past) {
        if (ring->after == CF_NO_LINK) {
            ring->after = slot;
        }
    } else {
        ring->before = slot;
    }
}

static void ring_walk(focus_ring *ring, uint32_t slot) {
    ring_visit(ring, slot);

    for (uint32_t child = record_of(ring->ctx, slot)->child_first; child != CF_NO_LINK;
         child = record_of(ring->ctx, child)->next_sibling) {
        /* A hidden box takes nothing with it: the paint walk and the hit test
         * drop its subtree, and a keyboard ring that kept it would tab to a box
         * no one can see. */
        if (!hidden_box(ring->ctx, child)) {
            ring_walk(ring, child);
        }
    }
}

/*
 * The box Tab lands on. `from` is the focused slot, or the root when nothing is
 * focused - which makes the first pass answer the first focusable box going
 * forward and the last one going back. When the ring holds nothing the answer is
 * CF_NO_LINK, so a page whose only focusable box was switched off loses focus
 * rather than keeping it somewhere the keyboard cannot return to.
 */
static uint32_t focus_neighbor(const cf_ctx *ctx, uint32_t from, bool backward) {
    focus_ring ring = {.ctx = ctx,
                       .from = from,
                       .first = CF_NO_LINK,
                       .last = CF_NO_LINK,
                       .after = CF_NO_LINK,
                       .before = CF_NO_LINK};

    ring_walk(&ring, ctx->root_slot);

    if (ring.first == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    /* Wrapping at the ends: past the last box comes the first, and before the
     * first comes the last, so the keyboard never gets stuck at a margin. */
    if (backward) {
        return ring.before != CF_NO_LINK ? ring.before : ring.last;
    }

    return ring.after != CF_NO_LINK ? ring.after : ring.first;
}

/*
 * Brings the box the keyboard just landed on back into every pane that scrolls
 * it, so a hand on Tab alone reaches the whole page. The rects are the frame
 * that last laid out, so the shift is measured against where the target sat,
 * not where the offset will put it; a build reads the offset back at
 * declaration time, so the reveal lands on the frame after the key rather than
 * the one the key fired in. Only a CF_ROLE_SCROLL ancestor qualifies - the
 * widget guarantees the block under its key is a cf_scroll, so the walk never
 * grows state some nameless node owns.
 */
static void reveal_slot(cf_ctx *ctx, uint32_t slot) {
    if (ctx->rects == NULL || slot == CF_NO_LINK) {
        return;
    }

    const cf_rect target = ctx->rects[slot];

    for (uint32_t at = record_of(ctx, slot)->parent; at != CF_NO_LINK;
         at = record_of(ctx, at)->parent) {
        const node_record *record = record_of(ctx, at);

        if (record->role != CF_ROLE_SCROLL || record->key == CF_NO_NAME) {
            continue;
        }

        const uint32_t content = record->child_first;
        cf_scroll *held = cf_scroll_of(ctx, record->key);

        if (held == NULL || content == CF_NO_LINK) {
            continue;
        }

        const cf_rect view = ctx->rects[at];
        const float top = target.y - view.y;
        const float bottom = top + target.height;
        float walked = held->offset;

        /* The shortest nudge in. A box taller than its pane can never sit
         * wholly inside, so it is brought to the top - where its reading
         * starts - rather than chased by an edge that keeps slipping out. */
        if (top < 0.0f) {
            walked += top;
        } else if (bottom > view.height) {
            walked += bottom - view.height;
        }

        held->offset = cf_scroll_clamp(walked, view.height, ctx->rects[content].height);
    }
}

/*
 * What a move of the keyboard does to a run an input method was holding: the claim goes back,
 * because the method feeds the keyboard and the keyboard has changed hands. The box that held
 * the run finds the claim gone at its next build and drops the run with it, so a preedit never
 * outlives the box it was being typed into - and a candidate window the shell was told about
 * goes empty in the same frame, since cf_frame_composition_rect reads the same claim.
 */
static void hand_keyboard_back(cf_ctx *ctx) {
    ctx->composing = CF_NO_NAME;
    ctx->composition_box = (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    ctx->composition_id = CF_NO_ID;
}

/*
 * One move of the keyboard, so every door that changes it leaves the same record
 * behind: the box it came from. A move onto the box that already holds it records
 * nothing, because the box would then become its own predecessor and the page
 * would have nowhere to come back to.
 */
static void move_focus(cf_ctx *ctx, cf_id next) {
    if (cf_id_equal(ctx->focus_id, next)) {
        return;
    }

    hand_keyboard_back(ctx);
    ctx->prior_id = ctx->focus_id;
    ctx->focus_id = next;
}

/*
 * The keyboard after the tree changed: the box holding it may have been retired,
 * hidden or switched off, which leaves the ask with nowhere to go. It goes back to
 * the box the keyboard came from, so a dialog that opens and closes leaves the page
 * where it found it rather than waiting for a Tab that starts at the top.
 *
 * One box is remembered, so the hand-back is one deep: after it the remembered box
 * is the one that just went away, and a second death leaves the keyboard with
 * nobody rather than resurrecting a box nobody pressed. A page recovers with one
 * Tab, which enters the ring from its start.
 */
void cf_focus_repair(cf_ctx *ctx) {
    if (cf_id_is_none(ctx->focus_id)) {
        /* Nobody holds the keyboard, so there is nothing to hand back: a box that
         * became reachable again did not ask for the keyboard and must not take it.
         */
        return;
    }

    const uint32_t focused = live_slot(ctx, ctx->focus_id);

    if (focused != CF_NO_LINK && takes_focus(ctx, focused)) {
        return;
    }

    const uint32_t prior = live_slot(ctx, ctx->prior_id);

    hand_keyboard_back(ctx);

    if (prior != CF_NO_LINK && takes_focus(ctx, prior)) {
        ctx->focus_id = ctx->prior_id;
    } else {
        ctx->focus_id = CF_NO_ID;
    }
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

        /* The press books its paint on whatever took the pointer; focus is a
         * different ask. A box that never said it takes the keyboard does not get
         * it because a mouse happened to be resting on it. */
        if (takes_focus(ctx, interactive)) {
            move_focus(ctx, ctx->press_id);
        }

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

    case CF_EVENT_POINTER_CANCEL: {
        /* The device says the hand is gone: the interaction ends where a release
         * would have ended it and nothing is clicked. The box that held the press
         * hears it, so a drag it was running can stop, and the pointer's
         * bookkeeping is cleared whether or not a handler was listening. Hover
         * goes with the hand - a pointer the window lost is not resting on a
         * control, and a box left wearing its hover tone would read as a stuck
         * one. */
        uint32_t target = captured;

        if (target == CF_NO_LINK) {
            target = live_slot(ctx, ctx->press_id);
        }

        if (target != CF_NO_LINK) {
            deliver(ctx, &event, true, target);
        }

        ctx->capture_id = CF_NO_ID;
        ctx->press_id = CF_NO_ID;
        ctx->hover_id = CF_NO_ID;
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

    case CF_EVENT_KEY_DOWN: {
        const uint32_t focused = live_slot(ctx, ctx->focus_id);

        if (focused != CF_NO_LINK) {
            deliver(ctx, &event, false, focused);
        }

        /* Tab is the tree's move rather than the box's: every page with two
         * controls needs the same one, and the exception - a dialog that keeps the
         * keyboard inside itself - is a handler that answers the key. So the key
         * goes to the focused node first and the shell picks it up only when
         * nobody answered. Reverse is the same key with shift, because there is no
         * second key on the device to ask with. */
        if (event.key == CF_KEY_TAB && !event.stopped) {
            const uint32_t from = focused != CF_NO_LINK ? focused : ctx->root_slot;
            const uint32_t next = focus_neighbor(ctx, from, (event.mods & CF_MOD_SHIFT) != 0u);

            /* The move is recorded through the one door, so the box the keyboard came
             * from is kept whichever door the key walked out of. */
            move_focus(ctx, next != CF_NO_LINK ? cf_store_id(&ctx->nodes, next) : CF_NO_ID);

            /* The new focus is only worth having if the eye can find it, so the
             * page that hides it moves out of the way. */
            if (next != CF_NO_LINK) {
                reveal_slot(ctx, next);
            }
        }

        /* Enter or the space bar on the box the keyboard is standing on is the same
         * ask as a release inside its rect. The question stops at the focused node
         * rather than climbing: the box asked is the box that declared it takes the
         * keyboard, and a label inside a button never holds the focus to be asked.
         * A node that answered the key is not asked again, so a box that keeps a key
         * for itself - a dialog that stops Tab to hold the keyboard inside, a field
         * that stops an arrow to move its caret - hears no second ask out of the same
         * press. A shift does not change what a key asks, so it is read straight
         * through; the modifiers that name a binding are the page's own to handle, and
         * running the plain ask beside them would do the thing twice. */
        const bool plain = (event.mods & ~(uint32_t)CF_MOD_SHIFT) == 0u;

        if (!event.stopped && plain && focused != CF_NO_LINK &&
            (event.key == CF_KEY_ENTER || event.key == CF_KEY_SPACE)) {
            const node_record *record = record_of(ctx, focused);

            if (!record->disabled && record->handlers.activate != NULL) {
                event.target = focused;
                event.stopped = false;
                event.captured = false;
                record->handlers.activate(&event, record->handlers.activate_data);
            }
        }

        return;
    }

    case CF_EVENT_KEY_UP:
    case CF_EVENT_TEXT:
    case CF_EVENT_COMPOSITION: {
        /* What the keyboard wrote goes to whoever holds focus, the same way
         * the keys that carry no text do: a widget cannot be asked to sort
         * what the device already sorted. A run an input method is deciding walks
         * the same door because it is the same kind of ask - bytes the keyboard is
         * producing for one box - and a page with no focus has no box to be deciding
         * anything in. */
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
    if (ctx == NULL || !holds_slot(ctx, cf_store_id(&ctx->nodes, slot), slot) ||
        !takes_focus(ctx, slot)) {
        return;
    }

    move_focus(ctx, cf_store_id(&ctx->nodes, slot));
    reveal_slot(ctx, slot);
}

bool cf_hovered(const cf_ctx *ctx, uint32_t slot) {
    return ctx != NULL && holds_slot(ctx, ctx->hover_id, slot);
}

/*
 * The door a widget knocks on once it has edited the value its node holds. The event the
 * widget was handed names the node in `target`, so the ask goes to that node alone and
 * carries the key that moved the bytes: a page validating a form reads its own buffer out
 * of the event's frame rather than waiting for the next build to notice.
 */
void cf_report_change(cf_ctx *ctx, cf_event *event) {
    if (ctx == NULL || event == NULL ||
        !holds_slot(ctx, cf_store_id(&ctx->nodes, event->target), event->target)) {
        return;
    }

    const node_record *record = record_of(ctx, event->target);

    if (record->disabled || record->handlers.change == NULL) {
        return;
    }

    record->handlers.change(event, record->handlers.change_data);
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

    const node_record *record = record_of(ctx, hovered);

    if (record->disabled || record->handlers.pointer == NULL) {
        return CF_CURSOR_DEFAULT;
    }

    /* The hand promises a press; a text box wants the pointer set where the
     * bytes are, which is the other ask a node can make. */
    return record->role == CF_ROLE_FIELD ? CF_CURSOR_TEXT : CF_CURSOR_POINTER;
}

bool cf_frame_takes_text(const cf_ctx *ctx) {
    if (ctx == NULL) {
        return false;
    }

    const uint32_t focused = live_slot(ctx, ctx->focus_id);

    if (focused == CF_NO_LINK) {
        return false;
    }

    /* The gate is the one Tab walks, so the keyboard service is up only where the
     * keyboard could have reached by hand: a field that has been switched off or
     * hidden since it took focus loses the ask along with its reach. Past that
     * gate, the role is the widget's own word for a box that edits bytes. */
    return takes_focus(ctx, focused) && record_of(ctx, focused)->role == CF_ROLE_FIELD;
}

/*
 * The box a composition is standing in, and nothing where the keyboard has nowhere for text
 * to go. Both halves matter: the claim says a box is holding a run, and the gate says the
 * keyboard is still where a run can be committed. A page that stopped taking text has no
 * field left to be deciding anything, whatever the last field wrote into the context.
 *
 * The field stages where the run sits inside its box; the box is read from the frame this is
 * asked in, so a page that scrolled or resized under a standing run moves the answer along with
 * the underline it drew from the same numbers. A slot that no longer holds the claiming box has
 * no answer: the box is going, and the run goes with it.
 */
cf_rect cf_frame_composition_rect(const cf_ctx *ctx) {
    const cf_rect nothing = {0.0f, 0.0f, 0.0f, 0.0f};

    if (ctx == NULL || ctx->composing == CF_NO_NAME || !cf_frame_takes_text(ctx) ||
        ctx->rects == NULL) {
        return nothing;
    }

    const uint32_t held = live_slot(ctx, ctx->composition_id);
    const cf_tree *view = cf_frame_tree(ctx);

    if (held == CF_NO_LINK || view == NULL || held >= view->node_count) {
        return nothing;
    }

    const cf_rect box = ctx->rects[held];

    return (cf_rect){box.x + ctx->composition_box.x, box.y + ctx->composition_box.y,
                     ctx->composition_box.width, ctx->composition_box.height};
}
