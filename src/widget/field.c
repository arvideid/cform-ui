#include <string.h>

#include "cf/widget.h"

/*
 * The lines a click can land on. A field's value refuses to wrap, so one row
 * is what a typed run lays; the rest is the room a buffer holds between the
 * breaks a build put in itself. A point below them all reads the last line
 * rather than nothing.
 */
#define FIELD_VISIBLE_LINES 8u

/*
 * What a handler needs and the declaration does not carry: the context to read
 * the box's rect and the interaction off, the buffer to edit, and the fonts to
 * lay the bytes in - the stack the build handed the field, borrowed for as long
 * as the field is declared, which is the rule a label's stack already follows.
 */
typedef struct field_data {
    cf_ctx *ctx;
    cf_text_edit *text;
    cf_text_stack stack;
} field_data;

static uint32_t sel_from(const cf_text_edit *text) {
    return text->mark < text->caret ? text->mark : text->caret;
}

static uint32_t sel_to(const cf_text_edit *text) {
    return text->mark > text->caret ? text->mark : text->caret;
}

/* A build that wrote the bytes itself can leave the marks past them, and no
 * edit below may read an offset the buffer does not hold. */
static void clamp_marks(cf_text_edit *text) {
    if (text->caret > text->length) {
        text->caret = text->length;
    }

    if (text->mark > text->length) {
        text->mark = text->length;
    }
}

/*
 * The walk the box paints and the click reads, built from the same buffer both
 * times: what the paint drew is what an offset is measured along, so a caret a
 * click places stands under the pointer that placed it.
 */
static cf_text_content content_of(const field_data *field) {
    const cf_text_edit *text = field->text;

    return (cf_text_content){.stack = field->stack,
                             .text = {text->chars, text->length},
                             .caret = {.present = true, .value = text->caret},
                             .mark = {.present = true, .value = text->mark},
                             .nowrap = true};
}

/* One scalar back: over the continuation bytes the scalar owns, then off its
 * lead byte. */
static uint32_t back_one(const cf_text_edit *text, uint32_t at) {
    while (at > 0u && ((unsigned char)text->chars[at - 1u] & 0xC0u) == 0x80u) {
        at--;
    }

    return at > 0u ? at - 1u : 0u;
}

/* One scalar on: past the lead byte and every byte that continues it, stopping
 * at the end of what the buffer holds. */
static uint32_t forward_one(const cf_text_edit *text, uint32_t at) {
    if (at >= text->length) {
        return text->length;
    }

    at++;

    while (at < text->length && ((unsigned char)text->chars[at] & 0xC0u) == 0x80u) {
        at++;
    }

    return at;
}

/* A run carrying a line break is refused whole: the field shows one run of
 * bytes, and a break in the middle would put the caret on a row no key here
 * can reach back to. */
static bool holds_break(cf_string run) {
    for (uint32_t i = 0u; i < run.length; ++i) {
        if (run.chars[i] == '\n' || run.chars[i] == '\r') {
            return true;
        }
    }

    return false;
}

/*
 * What the keyboard wrote, over the selection: the range goes, the bytes come,
 * and the caret lands after them with nothing left selected. A run longer than
 * the room the buffer has left is refused whole rather than cut, because half a
 * UTF-8 sequence is bytes nothing can read back.
 */
static void replace(cf_text_edit *text, cf_string run) {
    if (run.chars == NULL || run.length == 0u || holds_break(run)) {
        return;
    }

    const uint32_t from = sel_from(text);
    const uint32_t to = sel_to(text);
    const uint32_t kept = text->length - (to - from);

    if (kept + run.length > text->capacity) {
        return;
    }

    memmove(text->chars + from + run.length, text->chars + to, text->length - to);
    memcpy(text->chars + from, run.chars, run.length);
    text->length = kept + run.length;
    text->caret = from + run.length;
    text->mark = text->caret;
}

/* The bytes between two offsets go, and the caret where they were. */
static void cut(cf_text_edit *text, uint32_t from, uint32_t to) {
    memmove(text->chars + from, text->chars + to, text->length - to);
    text->length -= to - from;
    text->caret = from;
    text->mark = from;
}

static void field_key(cf_event *event, void *user_data) {
    cf_text_edit *text = ((field_data *)user_data)->text;

    clamp_marks(text);

    if (event->type == CF_EVENT_TEXT) {
        replace(text, event->text);
        cf_stop(event);
        return;
    }

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    /* Whether a shift makes a capital is the device's, already settled inside
     * the text it sent; whether it means "extend what is selected" is the
     * field's, and this is the only place it can read it. */
    const bool shift = (event->mods & (uint32_t)CF_MOD_SHIFT) != 0u;

    switch ((cf_key)event->key) {
    case CF_KEY_LEFT:
    case CF_KEY_RIGHT: {
        const bool back = event->key == (uint32_t)CF_KEY_LEFT;

        /* Nothing held, an arrow collapses a selection to the end it points at
         * rather than stepping away from where the caret already stands. */
        if (!shift && sel_from(text) != sel_to(text)) {
            text->caret = back ? sel_from(text) : sel_to(text);
            text->mark = text->caret;
            break;
        }

        text->caret = back ? back_one(text, text->caret) : forward_one(text, text->caret);

        if (!shift) {
            text->mark = text->caret;
        }
        break;
    }

    case CF_KEY_HOME:
    case CF_KEY_END:
        text->caret = event->key == (uint32_t)CF_KEY_HOME ? 0u : text->length;

        if (!shift) {
            text->mark = text->caret;
        }
        break;

    case CF_KEY_BACKSPACE:
        if (sel_from(text) != sel_to(text)) {
            cut(text, sel_from(text), sel_to(text));
        } else if (text->caret > 0u) {
            cut(text, back_one(text, text->caret), text->caret);
        }
        break;

    case CF_KEY_DELETE:
        if (sel_from(text) != sel_to(text)) {
            cut(text, sel_from(text), sel_to(text));
        } else if (text->caret < text->length) {
            cut(text, text->caret, forward_one(text, text->caret));
        }
        break;

    default:
        /* A key the field has no word for writes nothing and stops nothing:
         * the number a modifier carries is not a character, and an arrow the
         * field does not name belongs to whoever holds it. */
        return;
    }

    cf_stop(event);
}

/*
 * The byte offset a point in the box lands on, read back along the lines the
 * box paints: the same insets, the same wrap and the same slide, so the caret
 * goes where the glyph is. False when the box has nothing to place a caret in.
 */
static bool offset_at(const cf_ctx *ctx, const field_data *field, uint32_t slot, cf_point where,
                      uint32_t *out) {
    const cf_tree *view = cf_frame_tree(ctx);
    const cf_text_edit *text = field->text;

    if (view == NULL || view->styles == NULL || ctx->rects == NULL || field->stack.count == 0u ||
        text->length == 0u) {
        return false;
    }

    const cf_style *style = &view->styles[slot];
    const cf_rect box = ctx->rects[slot];
    const cf_edges insets = cf_box_resolve(style->padding, box.width);
    const cf_edges stroke = cf_box_resolve(style->border, box.width);
    const float left = insets.left + stroke.left;
    const float top = insets.top + stroke.top;
    const float wrap = box.width - left - insets.right - stroke.right;
    const cf_font *primary = &field->stack.fonts[0];
    const float line_height = primary->ops->metrics(primary->handle).line_height;
    const float visible = box.height - top - insets.bottom - stroke.bottom;
    cf_text_content content = content_of(field);
    cf_text_line lines[FIELD_VISIBLE_LINES];
    const uint32_t laid = cf_text_lines(&content, wrap, lines, FIELD_VISIBLE_LINES);

    if (laid == 0u || wrap <= 0.0f) {
        return false;
    }

    const cf_point slide = cf_text_scroll(&content, lines, laid, (cf_size){wrap, visible});
    const float y = where.y - (box.y + top - slide.y);
    uint32_t row =
        line_height > 0.0f && y > 0.0f ? (uint32_t)(y / line_height) : 0u; /* below the last row */

    if (row >= laid) {
        row = laid - 1u;
    }

    *out = cf_text_offset_at(&content, &lines[row], where.x - (box.x + left - slide.x));
    return true;
}

/* Where a pointer puts the caret: the mark rides with it, or stays holding the
 * end of the selection the drag is stretching. */
static void place_at(cf_ctx *ctx, field_data *field, uint32_t slot, cf_point where,
                     bool keep_mark) {
    uint32_t offset = 0u;

    if (!offset_at(ctx, field, slot, where, &offset)) {
        return;
    }

    if (!keep_mark) {
        field->text->mark = offset;
    }

    field->text->caret = offset;
}

static void field_pointer(cf_event *event, void *user_data) {
    field_data *field = user_data;
    cf_ctx *ctx = field->ctx;
    const bool shift = (event->mods & (uint32_t)CF_MOD_SHIFT) != 0u;

    if (event->type == CF_EVENT_POINTER_DOWN) {
        /* The press holds the pointer, so a drag that strays past the box keeps
         * selecting to wherever the hand goes. */
        cf_capture(event);
        place_at(ctx, field, event->target, event->position, shift);
        cf_stop(event);
        return;
    }

    /* Only a move that comes out of a press on this field selects: hovering a
     * field must not move anyone's caret. */
    if (event->type == CF_EVENT_POINTER_MOVE && cf_pressed(ctx, event->target)) {
        place_at(ctx, field, event->target, event->position, true);
        cf_stop(event);
    }
}

cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack *stack) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || text == NULL || text->chars == NULL) {
        return node;
    }

    /* The handler's block lives in the frame arena beside the frame that declared
     * it: both are read by this frame's dispatch alone. */
    field_data *data = cf_alloc(&ctx->frame, sizeof *data);

    if (data == NULL) {
        return node;
    }

    *data = (field_data){
        .ctx = ctx, .text = text, .stack = stack != NULL ? *stack : (cf_text_stack){0}};

    clamp_marks(text);
    node.key = cf_intern_text(&ctx->names, name);
    node.role = CF_ROLE_FIELD;
    node.handlers.pointer = field_pointer;
    node.handlers.pointer_data = data;
    node.handlers.key = field_key;
    node.handlers.key_data = data;

    /* The field paints its own bytes rather than passing them to a label: the
     * box that holds the value is the box the theme styles by focus, and a caret
     * the theme hands out has to reach the lines it stands among. */
    node.style = ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_FIELD].base : cf_style_default();
    node.style.color = (cf_opt_color){.present = true,
                                      .value = ctx->theme != NULL ? ctx->theme->ink
                                                                  : (cf_color){0u, 0u, 0u, 255u}};

    if (stack != NULL && stack->fonts != NULL && stack->count > 0u) {
        cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

        if (content != NULL) {
            *content = content_of(data);
            node.measure = cf_text_measure;
            node.measure_data = content;
        }
    }

    return node;
}
