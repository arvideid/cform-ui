#include "cf/widget.h"

static uint32_t encode(uint32_t codepoint, char *out) {
    if (codepoint < 0x80u) {
        out[0] = (char)codepoint;
        return 1u;
    }

    if (codepoint < 0x800u) {
        out[0] = (char)(0xC0u | (codepoint >> 6));
        out[1] = (char)(0x80u | (codepoint & 0x3Fu));
        return 2u;
    }

    if (codepoint < 0x10000u) {
        out[0] = (char)(0xE0u | (codepoint >> 12));
        out[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (codepoint & 0x3Fu));
        return 3u;
    }

    out[0] = (char)(0xF0u | (codepoint >> 18));
    out[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (codepoint & 0x3Fu));
    return 4u;
}

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

static void field_key(cf_event *event, void *user_data) {
    cf_text_edit *text = user_data;

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    if (event->key == 8u || event->key == 127u) {
        drop_last(text);
        return;
    }

    if (event->key < 32u) {
        return;
    }

    char bytes[4];
    const uint32_t width = encode(event->key, bytes);

    if (text->length + width > text->capacity) {
        return;
    }

    for (uint32_t i = 0u; i < width; ++i) {
        text->chars[text->length++] = bytes[i];
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

    *child = cf_label(ctx, (cf_string){.chars = text->chars, .length = text->length}, stack,
                      ctx->theme != NULL ? ctx->theme->on_ink : (cf_color){0u, 0u, 0u, 255u});
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
