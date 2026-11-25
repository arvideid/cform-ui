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
