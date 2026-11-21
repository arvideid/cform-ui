#include "cf/node.h"

#include "node/internal.h"

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
        const char c = text.chars[i];
        out[used++] = c >= 0x20 && c != '\'' ? c : ' ';
    }

    if (used == SEMANTIC_TEXT_BYTES && text.length > SEMANTIC_TEXT_BYTES) {
        memcpy(out + used, "...", 3u);
        used += 3u;
    }

    return (int)used;
}

static void walk(const cf_ctx *ctx, const cf_tree *tree, uint32_t slot, uint32_t depth,
                 semantic_writer *writer) {
    if (!writer->fits) {
        return;
    }

    const node_record *record = record_of(ctx, slot);
    char line[160];
    char text[SEMANTIC_TEXT_BYTES + 4u];
    int used =
        snprintf(line, sizeof line, "%*s%s", (int)(depth * 2u), "", role_names[record->role]);

    if (tree->measures != NULL && tree->measure_data != NULL &&
        tree->measures[slot] == cf_text_measure && tree->measure_data[slot] != NULL) {
        const cf_text_content *content = tree->measure_data[slot];
        const int length = excerpt(content->text, text);
        used += snprintf(line + used, sizeof line - (size_t)used, " '%.*s'", length, text);
    }

    const cf_rect rect = ctx->rects != NULL ? ctx->rects[slot] : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    used += snprintf(line + used, sizeof line - (size_t)used, " %gx%g", (double)rect.width,
                     (double)rect.height);

    if (record->disabled) {
        snprintf(line + used, sizeof line - (size_t)used, " disabled");
    }

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
