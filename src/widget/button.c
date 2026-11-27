#include "cf/build.h"
#include "cf/widget.h"

/* The bytes one button keeps for the dispatch it drives: the handler needs
 * the context to read the press and the rect, the build's callback, and the
 * pointer to hand back. They live in the frame arena beside the frame that
 * declared them. */
typedef struct button_data {
    cf_ctx *ctx;
    cf_on_press on_press;
    void *user_data;
} button_data;

/* The click is the release that starts on the button and ends inside it: the
 * down captures the pointer so the hold keeps the pressed paint, and the up
 * fires only while the press is still booked - the dispatcher clears it
 * after the walk - and the pointer is back inside the rect. */
static void button_pointer(cf_event *event, void *user_data) {
    button_data *button = user_data;

    if (event->type == CF_EVENT_POINTER_DOWN) {
        cf_capture(event);
        return;
    }

    if (event->type != CF_EVENT_POINTER_UP) {
        return;
    }

    /* A frame that laid nothing has no box a release could land inside, and a slot this frame
     * did not declare reads the same way. */
    const cf_rect *rect = cf_frame_rect(button->ctx, event->target);

    if (rect == NULL) {
        return;
    }

    if (!cf_pressed(button->ctx, cf_node_id(button->ctx, event->target))) {
        return;
    }

    if (event->position.x < rect->x || event->position.x >= rect->x + rect->width ||
        event->position.y < rect->y || event->position.y >= rect->y + rect->height) {
        return;
    }

    button->on_press(button->ctx, cf_node_id(button->ctx, event->target), button->user_data);
}

/* The keyboard asks for the same thing a release inside the rect asks. No rect is
 * read: the box holding the focus is the box that was standing there, and the
 * press the pointer booked is nobody's business here. */
static void button_activate(cf_event *event, void *user_data) {
    button_data *button = user_data;

    button->on_press(button->ctx, cf_node_id(button->ctx, event->target), button->user_data);
}

cf_node cf_button(cf_ctx *ctx, const char *name, cf_on_press on_press, void *user_data,
                  const cf_node *children, uint32_t child_count) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || on_press == NULL) {
        return node;
    }

    button_data *data = cf_frame_alloc(ctx, sizeof *data);

    if (data == NULL) {
        return node;
    }

    *data = (button_data){.ctx = ctx, .on_press = on_press, .user_data = user_data};

    /* The declared style is the resting box: the resolver reads the theme's
     * variants over it by interaction state, so the widget only has to start
     * from the family's base. */
    const cf_theme *theme = cf_theme_of(ctx);

    node.style = theme != NULL ? theme->variants[CF_ROLE_BUTTON].base : cf_style_default();
    node.key = cf_ctx_key(ctx, name);
    node.role = CF_ROLE_BUTTON;
    /* The family above is what the painter reads; this is what a bridge calls the box. */
    node.access = CF_ACCESS_BUTTON;
    /* A button is pressable by keyboard as well as by pointer, so it takes the
     * focus that makes the key reach it. The declaration is the widget's, not the
     * caller's: a box themed as a button without this is a picture of one. */
    node.focusable = true;
    node.handlers.pointer = button_pointer;
    node.handlers.pointer_data = data;
    node.handlers.activate = button_activate;
    node.handlers.activate_data = data;
    node.children = cf_children(ctx, children, child_count);
    node.child_count = node.children != NULL ? child_count : 0u;
    return node;
}
