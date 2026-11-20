#ifndef CF_TYPES_H
#define CF_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Slot identity. The index addresses a component array; the generation rejects
 * ids handed out before that slot was recycled, so a stale id reads as absent
 * instead of as someone else's element.
 */
typedef struct cf_id {
    uint32_t index;
    uint32_t generation;
} cf_id;

#define CF_NO_ID ((cf_id){.index = UINT32_MAX, .generation = 0u})

/* All-ones so a byte fill can set an entire link array at once. */
#define CF_NO_LINK UINT32_MAX

#define cf_id_equal(first, second)                                                                 \
    ((first).index == (second).index && (first).generation == (second).generation)

#define cf_id_is_none(id) ((id).index == UINT32_MAX)

typedef struct cf_point {
    float x;
    float y;
} cf_point;

typedef struct cf_size {
    float width;
    float height;
} cf_size;

typedef struct cf_rect {
    float x;
    float y;
    float width;
    float height;
} cf_rect;

typedef struct cf_edges {
    float left;
    float top;
    float right;
    float bottom;
} cf_edges;

typedef struct cf_color {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} cf_color;

/*
 * A length that knows what it means. CF_LENGTH_AUTO is the zero state, so a
 * zero-initialized field means "hug the content", never "zero pixels" and never
 * "no maximum" — those two readings are what an untagged float could not hold.
 */
typedef enum cf_length_kind {
    CF_LENGTH_AUTO,
    CF_LENGTH_PX,
    CF_LENGTH_PERCENT,
    CF_LENGTH_FR
} cf_length_kind;

typedef struct cf_length {
    cf_length_kind kind;
    float value;
} cf_length;

#define CF_AUTO ((cf_length){.kind = CF_LENGTH_AUTO, .value = 0.0f})
#define CF_PX(pixels) ((cf_length){.kind = CF_LENGTH_PX, .value = (pixels)})
#define CF_PERCENT(portion) ((cf_length){.kind = CF_LENGTH_PERCENT, .value = (portion)})
#define CF_FR(shares) ((cf_length){.kind = CF_LENGTH_FR, .value = (shares)})

typedef enum cf_intrinsic { CF_INTRINSIC_MIN_CONTENT, CF_INTRINSIC_MAX_CONTENT } cf_intrinsic;

/* A color a caller may decline to state. */
typedef struct cf_opt_color {
    bool present;
    cf_color value;
} cf_opt_color;

/*
 * Borrowed text. The chars are not guaranteed null-terminated, which is what
 * lets a substring of a larger buffer be a key without copying it first.
 * Nothing stores a cf_string as a key: interning copies the bytes (see base.h).
 */
typedef struct cf_string {
    const char *chars;
    uint32_t length;
} cf_string;

#endif
