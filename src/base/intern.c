#include <stdlib.h>
#include <string.h>

#include "cf/base.h"

#define INTERNER_LOAD_FACTOR_PERCENT 70

/* Roughly the number of distinct property and node names one screen uses, so a
 * typical UI never pays for the first doubling. */
#define INTERNER_INITIAL_CAPACITY 64

static bool text_equal(cf_string stored, cf_string other) {
    if (stored.length != other.length) {
        return false;
    }
    if (stored.length == 0) {
        return true;
    }

    return memcmp(stored.chars, other.chars, other.length) == 0;
}

cf_string cf_string_of(const char *chars) {
    if (chars == NULL) {
        return (cf_string){0};
    }

    return (cf_string){.chars = chars, .length = (uint32_t)strlen(chars)};
}

static bool interned_reserve(cf_interner *interner, size_t needed) {
    cf_string *texts = realloc(interner->texts, needed * sizeof *interner->texts);
    if (texts == NULL) {
        return false;
    }
    interner->texts = texts;

    uint32_t *chains = realloc(interner->chains, needed * sizeof *interner->chains);
    if (chains == NULL) {
        return false;
    }
    interner->chains = chains;

    interner->capacity = needed;
    return true;
}

/*
 * Bucket count never affects correctness — a full chain still resolves by
 * value — so a failed allocation here leaves the table deep but valid.
 */
static void interned_rehash(cf_interner *interner, size_t bucket_count) {
    uint32_t *buckets = calloc(bucket_count, sizeof *buckets);
    if (buckets == NULL) {
        return;
    }

    for (cf_name name = 0; name < interner->count; ++name) {
        const uint32_t hash =
            cf_hash_bytes(interner->texts[name].chars, interner->texts[name].length);
        uint32_t *slot = &buckets[hash % bucket_count];

        interner->chains[name] = *slot;
        *slot = name + 1u;
    }

    free(interner->buckets);
    interner->buckets = buckets;
    interner->bucket_count = bucket_count;
}

bool cf_interner_init(cf_interner *interner, size_t initial_capacity) {
    if (interner == NULL) {
        return false;
    }

    *interner = (cf_interner){0};

    const size_t capacity = initial_capacity == 0 ? INTERNER_INITIAL_CAPACITY : initial_capacity;
    interner->texts = calloc(capacity, sizeof *interner->texts);
    interner->chains = calloc(capacity, sizeof *interner->chains);
    interner->buckets = calloc(capacity, sizeof *interner->buckets);

    if (interner->texts == NULL || interner->chains == NULL || interner->buckets == NULL) {
        cf_interner_free(interner);
        return false;
    }

    interner->capacity = capacity;
    interner->bucket_count = capacity;

    return true;
}

void cf_interner_free(cf_interner *interner) {
    if (interner == NULL) {
        return;
    }

    for (cf_name name = 0; name < interner->count; ++name) {
        free((void *)interner->texts[name].chars);
    }

    free(interner->texts);
    free(interner->chains);
    free(interner->buckets);
    *interner = (cf_interner){0};
}

cf_name cf_intern(cf_interner *interner, cf_string text) {
    if (interner == NULL || interner->buckets == NULL || text.chars == NULL) {
        return CF_NO_NAME;
    }

    const uint32_t hash = cf_hash_bytes(text.chars, text.length);
    const size_t head = hash % interner->bucket_count;

    cf_name previous = CF_NO_NAME;
    uint32_t *link = &interner->buckets[head];

    while (*link != 0u) {
        const cf_name existing = *link - 1u;
        if (text_equal(interner->texts[existing], text)) {
            return existing;
        }
        previous = existing;
        link = &interner->chains[existing];
    }

    if (interner->count == interner->capacity &&
        !interned_reserve(interner, interner->capacity * 2)) {
        return CF_NO_NAME;
    }

    char *owned = malloc(text.length + 1u);
    if (owned == NULL) {
        return CF_NO_NAME;
    }

    memcpy(owned, text.chars, text.length);
    owned[text.length] = '\0';

    const cf_name fresh = (cf_name)interner->count;
    interner->texts[fresh] = (cf_string){.chars = owned, .length = text.length};
    interner->chains[fresh] = 0u;
    interner->count++;

    /* The doubling above moved the chain array, so the tail of the chain is
     * found again by index rather than through the pointer held before it. */
    uint32_t *slot =
        previous == CF_NO_NAME ? &interner->buckets[head] : &interner->chains[previous];
    *slot = fresh + 1u;

    if (interner->count * 100u > interner->bucket_count * INTERNER_LOAD_FACTOR_PERCENT) {
        interned_rehash(interner, interner->bucket_count * 2);
    }

    return fresh;
}

cf_name cf_intern_text(cf_interner *interner, const char *chars) {
    return cf_intern(interner, cf_string_of(chars));
}

cf_string cf_interned(const cf_interner *interner, cf_name name) {
    if (interner == NULL || name >= interner->count) {
        return (cf_string){0};
    }

    return interner->texts[name];
}
