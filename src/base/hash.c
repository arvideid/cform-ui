#include "cf/base.h"

#define FNV_OFFSET_BASIS 2166136261u
#define FNV_PRIME 16777619u

uint32_t cf_hash_fold(uint32_t seed, const void *bytes, size_t length) {
    if (bytes == NULL) {
        return seed;
    }

    const uint8_t *data = bytes;
    uint32_t hash = seed;

    for (size_t i = 0; i < length; ++i) {
        hash ^= (uint32_t)data[i];
        hash *= FNV_PRIME;
    }

    return hash;
}

uint32_t cf_hash_bytes(const void *bytes, size_t length) {
    return cf_hash_fold(FNV_OFFSET_BASIS, bytes, length);
}
