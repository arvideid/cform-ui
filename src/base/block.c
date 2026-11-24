#include <stdlib.h>
#include <string.h>

#include "cf/base.h"

/*
 * The only place in the tree that asks the heap for bytes a frame does not own.
 * An arena serves what dies with the frame that built it; a state block outlives
 * every frame after the one that asked for it, so it needs an allocation that can
 * be handed back. One seam keeps that accounting in a file someone can read.
 */
void *cf_alloc_block(size_t size) {
    if (size == 0u) {
        return NULL;
    }

    void *block = malloc(size);

    if (block != NULL) {
        memset(block, 0, size);
    }

    return block;
}

/* realloc leaves the bytes it grew by untouched, and a caller that just asked
 * for a bigger block reads them as its own fresh state: zeroing them here is
 * what keeps a zeroed start true of a grown block too. */
void *cf_resize_block(void *block, size_t held, size_t size) {
    if (size == 0u) {
        return NULL;
    }

    if (held >= size) {
        return block;
    }

    void *grown = realloc(block, size);

    if (grown != NULL) {
        memset((uint8_t *)grown + held, 0, size - held);
    }

    return grown;
}

void cf_free_block(void *block) {
    free(block);
}
