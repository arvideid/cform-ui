#include "cf/node.h"

#include "node/internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The names a semantic line speaks: the role families the theme styles. */
static const char *const role_names[CF_ROLE_COUNT] = {"box",  "button", "field",
                                                      "list", "scroll", "image"};

/* A line quotes a bounded excerpt: a label is one word, a field's buffer is
 * one line, and a dump that runs away helps nobody. */
#define SEMANTIC_TEXT_BYTES 48u

typedef struct semantic_writer {
    char *out;
    size_t capacity;
    size_t used;
    bool fits;
} semantic_writer;

static void put_line(semantic_writer *writer, const char *line) {
    const size_t length = strlen(line);

    if (!writer->fits || writer->used + length + 2u > writer->capacity) {
        writer->fits = false;
        return;
    }

    memcpy(writer->out + writer->used, line, length);
    writer->used += length;
    writer->out[writer->used++] = '\n';
}

/* Printable bytes stand as declared; anything else reads as a space, and an
 * excerpt the bound cut closes with an ellipsis. */
static int excerpt(const cf_string text, char *out) {
    size_t used = 0u;

    for (uint32_t i = 0u; i < text.length && used < SEMANTIC_TEXT_BYTES; ++i) {
        /* Read the byte unsigned: a UTF-8 lead or continuation is above 0x7F
         * and a signed char would call it negative, so the old predicate
         * wiped every non-ASCII scalar to a space on its way out. Control
         * bytes below 0x20 still become a space and a quote is dropped
         * rather than escaping the surrounding single quotes. */
        const unsigned char byte = (unsigned char)text.chars[i];
        out[used++] = byte >= 0x20u && byte != (unsigned char)'\'' ? (char)byte : ' ';
    }

    if (used == SEMANTIC_TEXT_BYTES && text.length > SEMANTIC_TEXT_BYTES) {
        memcpy(out + used, "...", 3u);
        used += 3u;
    }

    return (int)used;
}

/* A role the theme never styled still names a box; reading the table at an
 * index past its end is not an answer anyone can trust. */
static const char *role_name(cf_role role) {
    return (unsigned)role < (unsigned)CF_ROLE_COUNT ? role_names[role] : role_names[CF_ROLE_NONE];
}

/* The indent can run wide and the excerpt a whole line, so the pieces are
 * joined through a writer that never steps past the line's buffer. snprintf
 * reports the bytes it would have written, and trusting that as the offset
 * let a deep node run the cursor off the end and hand a wrapped size back. */
typedef struct line_writer {
    char *buf;
    size_t size;
    size_t used;
} line_writer;

static void line_printf(line_writer *w, const char *fmt, ...) {
    if (w->used + 1u > w->size) {
        return;
    }

    const int room = (int)(w->size - w->used);
    va_list args;
    va_start(args, fmt);
    const int would = vsnprintf(w->buf + w->used, (size_t)room, fmt, args);
    va_end(args);

    if (would < 0) {
        return;
    }

    /* Advance by the bytes actually present, not the ones the call wanted to
     * write: a truncated line stops growing and stays inside its buffer. */
    w->used += (size_t)would < (size_t)room ? (size_t)would : (size_t)room - 1u;
}

static void walk(const cf_ctx *ctx, const cf_tree *tree, uint32_t slot, uint32_t depth,
                 semantic_writer *writer) {
    if (!writer->fits) {
        return;
    }

    /* A hidden box is not in the tree a bridge would walk, so it earns no
     * line here either, and nothing beneath it does. */
    if (tree->styles[slot].display == CF_DISPLAY_NONE) {
        return;
    }

    const node_record *record = record_of(ctx, slot);
    char line[160];
    char text[SEMANTIC_TEXT_BYTES + 4u];
    line_writer out = {.buf = line, .size = sizeof line, .used = 0u};

    line_printf(&out, "%*s%s", (int)(depth * 2u), "", role_name(record->role));

    if (tree->measures != NULL && tree->measure_data != NULL &&
        tree->measures[slot] == cf_text_measure && tree->measure_data[slot] != NULL) {
        const cf_text_content *content = tree->measure_data[slot];
        const int length = excerpt(content->text, text);
        line_printf(&out, " '%.*s'", length, text);
    }

    const cf_rect rect = ctx->rects != NULL ? ctx->rects[slot] : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    line_printf(&out, " %gx%g", (double)rect.width, (double)rect.height);

    if (record->disabled) {
        line_printf(&out, " disabled");
    }

    line[out.used] = '\0';
    put_line(writer, line);

    for (uint32_t i = tree->child_start[slot]; i < tree->child_start[slot + 1u]; ++i) {
        walk(ctx, tree, tree->child_slots[i], depth + 1u, writer);
    }
}

bool cf_semantic_export(const cf_ctx *ctx, char *out, size_t capacity) {
    if (ctx == NULL || out == NULL || capacity == 0u) {
        return false;
    }

    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL) {
        return false;
    }

    semantic_writer writer = {.out = out, .capacity = capacity, .fits = true};
    walk(ctx, tree, ctx->root_slot, 0u, &writer);

    if (!writer.fits) {
        return false;
    }

    writer.out[writer.used] = '\0';
    return true;
}

/*
 * The bridge walk.
 *
 * Everything a screen reader needs comes out of the retained tree and the frame's layout, so
 * nothing here allocates and nothing here copies: each string is the build's own bytes, handed
 * over for the length of one call.
 */

/* A hidden box is not painted and cannot be hit, so it is not announced either - the same rule
 * the paint walk and the hit test follow, and the reason a closed dialog goes silent. */
static const node_record *visible_record(const cf_ctx *ctx, const cf_tree *tree, uint32_t slot) {
    return tree->styles[slot].display == CF_DISPLAY_NONE ? NULL : record_of(ctx, slot);
}

/* The bytes a box lays as its own text: a label's line, a field's whole buffer. A box that
 * measures an image or nothing at all lays no text, and answers NULL rather than guessing. */
static const cf_text_content *laid_content(const cf_tree *tree, uint32_t slot) {
    if (tree->measures == NULL || tree->measure_data == NULL ||
        tree->measures[slot] != cf_text_measure || tree->measure_data[slot] == NULL) {
        return NULL;
    }

    return tree->measure_data[slot];
}

static cf_string laid_text(const cf_tree *tree, uint32_t slot) {
    const cf_text_content *content = laid_content(tree, slot);

    return content != NULL ? content->text : (cf_string){NULL, 0u};
}

/*
 * The label whose words a control borrows when the build did not state them: the first one met
 * depth-first below it that has something to say. Depth first rather than nearest, because a
 * button's own line is the one a person hears before anything nested inside it.
 *
 * A field and the caption beside it are siblings, so no walk can pair them and this answers only
 * for words a control carries inside itself. That is why a build names its fields.
 */
static uint32_t label_below(const cf_ctx *ctx, const cf_tree *tree, uint32_t slot) {
    for (uint32_t i = tree->child_start[slot]; i < tree->child_start[slot + 1u]; ++i) {
        const uint32_t child = tree->child_slots[i];

        if (visible_record(ctx, tree, child) == NULL) {
            continue;
        }

        if (record_of(ctx, child)->access == CF_ACCESS_LABEL &&
            laid_text(tree, child).length != 0u) {
            return child;
        }

        const uint32_t deeper = label_below(ctx, tree, child);

        if (deeper != CF_NO_LINK) {
            return deeper;
        }
    }

    return CF_NO_LINK;
}

static uint32_t states_of(const cf_ctx *ctx, const node_record *record, const cf_id id) {
    uint32_t states = 0u;

    if (record->disabled) {
        states |= (uint32_t)CF_SEMANTIC_DISABLED;
    }

    if (record->focusable) {
        states |= (uint32_t)CF_SEMANTIC_FOCUSABLE;
    }

    /* The interaction ids are generational, so a slot the store recycled inherits nothing from
     * the box that was being hovered or held before it. */
    if (cf_id_equal(ctx->focus_id, id)) {
        states |= (uint32_t)CF_SEMANTIC_FOCUSED;
    }

    if (cf_id_equal(ctx->press_id, id)) {
        states |= (uint32_t)CF_SEMANTIC_PRESSED;
    }

    if (cf_id_equal(ctx->hover_id, id)) {
        states |= (uint32_t)CF_SEMANTIC_HOVERED;
    }

    if (record->access == CF_ACCESS_TEXT_BOX) {
        states |= (uint32_t)CF_SEMANTIC_EDITABLE;
    }

    return states;
}

/*
 * Each action bit names a door the frame already has, so a bridge never offers a person an ask
 * the page cannot serve: activation runs the node's `activate`, focus is `cf_focus` on the box's
 * slot for this frame, and a value goes in the way a field takes it - the slot holding focus and
 * the text or key events the field's own handler answers.
 */
static uint32_t actions_of(const node_record *record, uint32_t states) {
    uint32_t actions = 0u;

    if (record->handlers.activate != NULL) {
        actions |= (uint32_t)CF_SEMANTIC_ACTIVATE;
    }

    if ((states & (uint32_t)CF_SEMANTIC_EDITABLE) != 0u) {
        actions |= (uint32_t)CF_SEMANTIC_SET_VALUE;
    }

    if (record->focusable) {
        actions |= (uint32_t)CF_SEMANTIC_SET_FOCUS;
    }

    return actions;
}

/*
 * The roles a person meets as one thing, so that their words are the words written inside them.
 * A container - a group, a list, a scroller - holds boxes rather than words, and naming it by the
 * first label below it would announce the same sentence at every box on the way down to it.
 */
static bool speaks_its_content(cf_access access) {
    return access == CF_ACCESS_BUTTON || access == CF_ACCESS_ITEM || access == CF_ACCESS_IMAGE ||
           access == CF_ACCESS_TEXT_BOX;
}

/*
 * What a box has selected, as offsets into the bytes it lays. The field keeps the caret and the
 * mark wherever the hand left them, and a bridge wants the run rather than which end the typing
 * started from, so the pair is handed in document order.
 */
static void selection_of(const cf_tree *tree, uint32_t slot, cf_opt_u32 *out_start,
                         cf_opt_u32 *out_end) {
    const cf_text_content *content = laid_content(tree, slot);

    *out_start = (cf_opt_u32){0};
    *out_end = (cf_opt_u32){0};

    if (content == NULL || !content->caret.present) {
        return;
    }

    const uint32_t caret = content->caret.value;
    const uint32_t mark = content->mark.present ? content->mark.value : caret;

    *out_start = (cf_opt_u32){.present = true, .value = caret < mark ? caret : mark};
    *out_end = (cf_opt_u32){.present = true, .value = caret > mark ? caret : mark};
}

static void announce(const cf_ctx *ctx, const cf_tree *tree, uint32_t slot,
                     void (*visit)(const cf_semantic *node, void *user_data), void *user_data) {
    const node_record *record = visible_record(ctx, tree, slot);

    if (record == NULL) {
        return;
    }

    if (record->access != CF_ACCESS_NONE) {
        const cf_id id = cf_store_id(&ctx->nodes, slot);
        const cf_string value = laid_text(tree, slot);
        cf_string name = record->name;
        cf_id labelled_by = CF_NO_ID;

        if (name.length == 0u) {
            /* Text whose meaning is itself speaks for itself, and a box that holds words rather
             * than boxes takes them as its name - the reading a sighted person has. Nothing else
             * borrows: a group that spoke its child control's name would say the same sentence for
             * every box above it, and a scroller's words are the ones its build would have stated.
             */
            if (record->access == CF_ACCESS_LABEL) {
                name = value;
            } else if (speaks_its_content(record->access)) {
                const uint32_t label = label_below(ctx, tree, slot);

                if (label != CF_NO_LINK) {
                    name = laid_text(tree, label);
                    labelled_by = cf_store_id(&ctx->nodes, label);
                }
            }
        }

        cf_opt_u32 selection_start;
        cf_opt_u32 selection_end;
        selection_of(tree, slot, &selection_start, &selection_end);
        const uint32_t states = states_of(ctx, record, id);
        const cf_rect rect =
            ctx->rects != NULL ? ctx->rects[slot] : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};

        visit(&(cf_semantic){.id = id,
                             .slot = slot,
                             .access = record->access,
                             .name = name,
                             .description = record->description,
                             .value = value,
                             .selection_start = selection_start,
                             .selection_end = selection_end,
                             .labelled_by = labelled_by,
                             .rect = rect,
                             .states = states,
                             .actions = actions_of(record, states)},
              user_data);
    }

    for (uint32_t i = tree->child_start[slot]; i < tree->child_start[slot + 1u]; ++i) {
        announce(ctx, tree, tree->child_slots[i], visit, user_data);
    }
}

void cf_semantic_visit(const cf_ctx *ctx, void (*visit)(const cf_semantic *node, void *user_data),
                       void *user_data) {
    if (ctx == NULL || visit == NULL) {
        return;
    }

    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL) {
        return;
    }

    announce(ctx, tree, ctx->root_slot, visit, user_data);
}
