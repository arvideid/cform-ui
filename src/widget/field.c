#include "cf/widget.h"

/* Backspace drops the last scalar: the continuation bytes it owns first,
 * then the lead byte that started it. */
static void drop_last(cf_text_edit *text) {
    while (text->length > 0u && ((unsigned char)text->chars[text->length - 1u] & 0xC0u) == 0x80u) {
        text->length--;
    }

    if (text->length > 0u) {
        text->length--;
    }
}

/* A run that would pass the end of the buffer is refused whole rather than
 * cut, because half a UTF-8 sequence is not text anything can read back. */
static void append(cf_text_edit *text, cf_string run) {
    if (run.chars == NULL || text->length + run.length > text->capacity) {
        return;
    }

    for (uint32_t i = 0u; i < run.length; ++i) {
        text->chars[text->length++] = run.chars[i];
    }
}

static void field_key(cf_event *event, void *user_data) {
    cf_text_edit *text = user_data;

    if (event->type == CF_EVENT_TEXT) {
        append(text, event->text);
        return;
    }

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    /* Only the keys that carry no text are read here. What the keyboard wrote
     * arrives as bytes the device already settled - shift, caps lock, the
     * layout and an input method are its business, not the field's - and a
     * keycode turned into a code point would write the shape of the number a
     * modifier is, which is nothing anyone typed. */
    if (event->key == 8u || event->key == 127u) {
        drop_last(text);
    }
}

cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack *stack) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || text == NULL || text->chars == NULL) {
        return node;
    }

    /* The child nodes live in the frame arena beside the frame that declared
     * them: both are read by this frame's diff alone. */
    cf_node *child = cf_alloc(&ctx->frame, sizeof *child);
    cf_node *children = cf_alloc(&ctx->frame, sizeof *children);

    if (child == NULL || children == NULL) {
        return node;
    }

    /* The field's resting box is a light surface, so what it edits reads in the
     * ink the page uses: the accent's own tone would draw the bytes in the
     * color of the box behind them. */
    *child = cf_label(ctx, (cf_string){.chars = text->chars, .length = text->length}, stack,
                      ctx->theme != NULL ? ctx->theme->ink : (cf_color){0u, 0u, 0u, 255u});
    children[0] = *child;

    node.key = cf_intern_text(&ctx->names, name);
    node.style = ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_FIELD].base : cf_style_default();
    node.role = CF_ROLE_FIELD;
    node.handlers.key = field_key;
    node.handlers.key_data = text;
    node.children = children;
    node.child_count = 1u;
    return node;
}
