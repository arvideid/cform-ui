#include "cf/build.h"
#include "cf/widget.h"

/* What the switch keeps for the door it drives: the context to read the press
 * and the rect off, the bit to write, and the caller's word that it moved. The
 * bytes live in the frame arena beside the frame that declared them. */
typedef struct checkbox_data {
    cf_ctx *ctx;
    bool *checked;
    cf_on_press on_toggle;
    void *user_data;
} checkbox_data;

/* Flipping is one act whatever opened the door: the caller's bit moves, then
 * whoever asked to be told is told. The handler is handed the box's identity
 * rather than this frame's slot, because a page that reaches for the box after
 * a rebuild should find the box, or find nothing. */
static void checkbox_flip(cf_event *event, checkbox_data *box) {
    *box->checked = !*box->checked;

    if (box->on_toggle != NULL) {
        box->on_toggle(box->ctx, cf_node_id(box->ctx, event->target), box->user_data);
    }
}

/* The same contract a button keeps: the down books the pointer so the hold keeps its
 * paint, and the up counts only while the book is still open and the pointer came back
 * inside the box. A drag that leaves is nobody's answer, and so is a release the frame
 * laid no box for. */
static void checkbox_pointer(cf_event *event, void *user_data) {
    checkbox_data *box = user_data;

    if (event->type == CF_EVENT_POINTER_DOWN) {
        cf_capture(event);
        return;
    }

    if (event->type != CF_EVENT_POINTER_UP) {
        return;
    }

    const cf_rect *rect = cf_frame_rect(box->ctx, event->target);

    if (rect == NULL) {
        return;
    }

    if (!cf_pressed(box->ctx, cf_node_id(box->ctx, event->target))) {
        return;
    }

    if (event->position.x < rect->x || event->position.x >= rect->x + rect->width ||
        event->position.y < rect->y || event->position.y >= rect->y + rect->height) {
        return;
    }

    checkbox_flip(event, box);
}

/* The keyboard asks about the box holding the focus, so no rect is read: a box
 * that was laid out of sight is still the box a person is standing on. */
static void checkbox_activate(cf_event *event, void *user_data) {
    checkbox_flip(event, user_data);
}

cf_node cf_checkbox(cf_ctx *ctx, const char *name, bool *checked, cf_on_press on_toggle,
                    void *user_data) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || checked == NULL) {
        return node;
    }

    checkbox_data *data = cf_frame_alloc(ctx, sizeof *data);

    if (data == NULL) {
        return node;
    }

    *data = (checkbox_data){
        .ctx = ctx, .checked = checked, .on_toggle = on_toggle, .user_data = user_data};

    /* The resting box is the family's; the resolver reads the checked variant over it
     * along with the pointer's and the keyboard's, so the widget states the switch and
     * nothing about its colour. */
    const cf_theme *theme = cf_theme_of(ctx);

    node.style = theme != NULL ? theme->variants[CF_ROLE_CHECKBOX].base : cf_style_default();
    node.key = cf_ctx_key(ctx, name);
    node.role = CF_ROLE_CHECKBOX;
    node.access = CF_ACCESS_CHECKBOX;
    node.checked = *checked;
    node.focusable = true;
    node.handlers.pointer = checkbox_pointer;
    node.handlers.pointer_data = data;
    node.handlers.activate = checkbox_activate;
    node.handlers.activate_data = data;
    return node;
}
