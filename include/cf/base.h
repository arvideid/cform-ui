#ifndef CF_BASE_H
#define CF_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cf/types.h"

/*
 * Bump allocator. Everything the engine builds per frame comes from an arena
 * that is cleared, not freed, so a frame's memory cost is its high-water mark
 * and the reset itself is the only deallocation step.
 */
typedef struct cf_arena {
    uint8_t *memory;
    size_t capacity;
    size_t used;
    size_t peak;
} cf_arena;

bool cf_arena_init(cf_arena *arena, size_t capacity);
void cf_arena_clear(cf_arena *arena);
void cf_arena_free(cf_arena *arena);

/* NULL when the request does not fit; the caller decides what that means. */
void *cf_alloc(cf_arena *arena, size_t size);

/*
 * Recursion uses the arena as a stack: a mark remembers where a call began, so
 * its scratch can be handed back once it returns while anything still live
 * above it stays put. Rewinds must happen in the reverse order of the marks,
 * which is what a call that marks, recurses and rewinds does by construction.
 */
size_t cf_arena_mark(const cf_arena *arena);
void cf_arena_rewind(cf_arena *arena, size_t mark);

/*
 * Fixed-stride slots addressed by cf_id, growing by capacity doubling so no
 * element is ever relocated one at a time. Element pointers are invalidated by
 * the next cf_store_add — hold cf_id, not void *.
 */
typedef struct cf_store {
    size_t element_size;
    size_t capacity;
    size_t extent;
    size_t alive;
    uint32_t free_head;
    uint8_t *elements;
    uint32_t *generations;
    uint32_t *next_free;
    bool *live;
} cf_store;

bool cf_store_init(cf_store *store, size_t element_size, size_t initial_capacity);
void cf_store_free(cf_store *store);

/* out_id is CF_NO_ID when the store cannot grow. */
void *cf_store_add(cf_store *store, cf_id *out_id);

/* NULL for a slot that is free, recycled or never was. */
void *cf_store_get(const cf_store *store, cf_id id);
void cf_store_remove(cf_store *store, cf_id id);

/* Iteration walks live slots in index order; live[] is the predicate. */
void *cf_store_at(const cf_store *store, size_t slot);
cf_id cf_store_id(const cf_store *store, size_t slot);

uint32_t cf_hash_bytes(const void *bytes, size_t length);

/*
 * Text identity by value. Keys are stable indices into the interned table, so
 * a cache keyed on cf_name survives the caller rebuilding its string — the
 * pointer-identity key it replaces silently missed on every rebuild.
 *
 * Zero names nothing and issued names start at one, so a zeroed structure
 * holds no name and the interner never hands out the zero it begins with.
 */
typedef uint32_t cf_name;

#define CF_NO_NAME 0u

/*
 * buckets[] and chains[] both hold name + 1, and 0 ends a chain. A name is an
 * index into texts[], so doubling the table or rehashing the buckets never
 * invalidates one a caller is holding.
 */
typedef struct cf_interner {
    cf_string *texts;
    uint32_t *buckets;
    uint32_t *chains;
    size_t count;
    size_t capacity;
    size_t bucket_count;
} cf_interner;

bool cf_interner_init(cf_interner *interner, size_t initial_capacity);
void cf_interner_free(cf_interner *interner);

/* CF_NO_NAME if the bytes could not be copied. */
cf_name cf_intern(cf_interner *interner, cf_string text);
cf_name cf_intern_text(cf_interner *interner, const char *chars);
cf_string cf_interned(const cf_interner *interner, cf_name name);

cf_string cf_string_of(const char *chars);

typedef enum cf_log_channel {
    CF_LOG_CORE,
    CF_LOG_LAYOUT,
    CF_LOG_TEXT,
    CF_LOG_PAINT,
    CF_LOG_ASSET,
    CF_LOG_BACKEND
} cf_log_channel;

typedef void (*cf_log_sink)(cf_log_channel channel, const char *message, void *user_data);

/* A NULL sink silences everything; the library never formats into the void. */
void cf_log_install(cf_log_sink sink, void *user_data);
void cf_logf(cf_log_channel channel, const char *format, ...);

#endif
