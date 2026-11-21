#include <stdlib.h>
#include <string.h>

#include "cf/base.h"

/* cf_id.index is 32 bits, so that is as many slots a store can ever address. */
#define STORE_SLOT_LIMIT UINT32_MAX

/* A component array almost never starts empty, and the first doubling is the
 * one worth skipping. */
#define STORE_INITIAL_CAPACITY 8

static void store_release(cf_store *store) {
    free(store->elements);
    free(store->generations);
    free(store->next_free);
    free(store->live);
    *store = (cf_store){0};
}

static bool store_reserve(cf_store *store, size_t needed) {
    if (needed > STORE_SLOT_LIMIT) {
        return false;
    }

    uint8_t *elements = realloc(store->elements, needed * store->element_size);
    if (elements == NULL) {
        return false;
    }
    store->elements = elements;

    uint32_t *generations = realloc(store->generations, needed * sizeof *store->generations);
    if (generations == NULL) {
        return false;
    }
    store->generations = generations;

    uint32_t *next_free = realloc(store->next_free, needed * sizeof *store->next_free);
    if (next_free == NULL) {
        return false;
    }
    store->next_free = next_free;

    bool *live = realloc(store->live, needed * sizeof *store->live);
    if (live == NULL) {
        return false;
    }
    store->live = live;

    const size_t added = needed - store->capacity;
    memset(store->generations + store->capacity, 0, added * sizeof *store->generations);
    memset(store->next_free + store->capacity, 0xFF, added * sizeof *store->next_free);
    memset(store->live + store->capacity, 0, added * sizeof *store->live);
    store->capacity = needed;

    return true;
}

bool cf_store_init(cf_store *store, size_t element_size, size_t initial_capacity) {
    if (store == NULL || element_size == 0) {
        return false;
    }

    *store = (cf_store){
        .element_size = element_size,
        .free_head = CF_NO_LINK,
    };

    return store_reserve(store, initial_capacity == 0 ? STORE_INITIAL_CAPACITY : initial_capacity);
}

void cf_store_free(cf_store *store) {
    if (store == NULL) {
        return;
    }
    store_release(store);
}

void *cf_store_add(cf_store *store, cf_id *out_id) {
    if (store == NULL || out_id == NULL) {
        return NULL;
    }

    if (store->extent == store->capacity && !store_reserve(store, store->capacity * 2)) {
        *out_id = CF_NO_ID;
        return NULL;
    }

    size_t slot;
    if (store->free_head == CF_NO_LINK) {
        slot = store->extent;
        store->extent++;
    } else {
        slot = store->free_head;
        store->free_head = store->next_free[slot];
    }

    store->live[slot] = true;
    store->alive++;
    *out_id = cf_store_id(store, slot);

    void *element = store->elements + slot * store->element_size;
    memset(element, 0, store->element_size);

    return element;
}

void *cf_store_get(const cf_store *store, cf_id id) {
    if (store == NULL || id.index >= store->extent) {
        return NULL;
    }
    if (!store->live[id.index] || store->generations[id.index] != id.generation) {
        return NULL;
    }

    return store->elements + (size_t)id.index * store->element_size;
}

void cf_store_remove(cf_store *store, cf_id id) {
    if (cf_store_get(store, id) == NULL) {
        return;
    }

    store->live[id.index] = false;
    store->generations[id.index]++;
    store->next_free[id.index] = (uint32_t)store->free_head;
    store->free_head = id.index;
    store->alive--;
}

void *cf_store_at(const cf_store *store, size_t slot) {
    if (store == NULL || slot >= store->extent) {
        return NULL;
    }

    return store->elements + slot * store->element_size;
}

cf_id cf_store_id(const cf_store *store, size_t slot) {
    if (store == NULL || slot >= store->extent) {
        return CF_NO_ID;
    }

    return (cf_id){
        .index = (uint32_t)slot,
        .generation = store->generations[slot],
    };
}
