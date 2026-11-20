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
        return NULL;
    }

    arena->used = end;
    if (end > arena->peak) {
        arena->peak = end;
    }

    return arena->memory + offset;
}
