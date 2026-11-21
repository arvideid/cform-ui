#ifndef CF_BACKEND_H
#define CF_BACKEND_H

#include <stdint.h>

#include "cf/paint.h"

/*
 * The one boundary a renderer crosses to draw a frame. `abi_version` names
 * the stream layout the backend reads; a caller checks it before the first
 * submit, because a stream the backend does not understand draws wrong rather
 * than failing.
 */
#define CF_BACKEND_ABI_VERSION 1u

typedef struct cf_backend {
    uint32_t abi_version;
    void *user_data;
    /* Hands the frame's stream over. The commands are borrowed for the call
     * alone - a backend that draws after it returns copies them first. */
    void (*submit)(void *user_data, const cf_command *commands, uint32_t count);
    /* Draws what the last submit carried, to the output the backend owns. */
    void (*present)(void *user_data);
} cf_backend;

/*
 * The headless backend: it keeps the last submitted stream in its own arena
 * so a test reads what a device would have drawn. The store it hands back is
 * owned, so the stream survives the caller rewinding the arena the paint list
 * came out of. The zero state is an empty recorder.
 */
typedef struct cf_recorder {
    cf_arena *arena;
    cf_command *commands;
    uint32_t count;
    uint32_t capacity;
} cf_recorder;

/* A backend that records instead of draws. A NULL recorder submits nothing. */
cf_backend cf_backend_recorder(cf_recorder *recorder);

#endif
