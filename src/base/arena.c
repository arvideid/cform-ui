#include <stdalign.h>
#include <stdlib.h>

#include "cf/base.h"

#define ARENA_ALIGNMENT alignof(max_align_t)

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

bool cf_arena_init(cf_arena *arena, size_t capacity) {
    if (arena == NULL || capacity == 0) {
        return false;
    }

    void *memory = malloc(capacity);
    if (memory == NULL) {
        *arena = (cf_arena){0};
        return false;
    }

    *arena = (cf_arena){
        .memory = memory,
        .capacity = capacity,
        .used = 0,
        .peak = 0,
    };
    return true;
}

void cf_arena_clear(cf_arena *arena) {
    if (arena == NULL) {
        return;
    }
    arena->used = 0;
    /* The refusal belongs to the arena state that produced it; a caller
     * starting a new pass does not want the last frame's verdict. */
    arena->exhausted = false;
}

void cf_arena_free(cf_arena *arena) {
    if (arena == NULL) {
        return;
    }

    free(arena->memory);
    *arena = (cf_arena){0};
}

void *cf_alloc(cf_arena *arena, size_t size) {
    if (arena == NULL || arena->memory == NULL || size == 0) {
        return NULL;
    }

    const size_t offset = align_up(arena->used, ARENA_ALIGNMENT);
    const size_t end = offset + size;

    if (end < offset || end > arena->capacity) {
        arena->exhausted = true;
        return NULL;
    }

    arena->used = end;
    if (end > arena->peak) {
        arena->peak = end;
    }

    return arena->memory + offset;
}

size_t cf_arena_mark(const cf_arena *arena) {
    if (arena == NULL) {
        return 0;
    }

    return arena->used;
}

void cf_arena_rewind(cf_arena *arena, size_t mark) {
    /* A mark above the high-water point of the live allocation describes a
     * call that never allocated, so there is nothing to hand back. */
    if (arena == NULL || mark > arena->used) {
        return;
    }

    arena->used = mark;
}
