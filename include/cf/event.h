#ifndef CF_EVENT_H
#define CF_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "cf/types.h"

typedef enum cf_event_type {
    CF_EVENT_POINTER_MOVE,
    CF_EVENT_POINTER_DOWN,
    CF_EVENT_POINTER_UP,
    CF_EVENT_SCROLL,
    CF_EVENT_KEY_DOWN,
    CF_EVENT_KEY_UP,
    /* The device asked the window to close. Nothing in the tree dispatches
     * it; the shell that owns the loop reads it and stops. */
    CF_EVENT_QUIT
} cf_event_type;

typedef enum cf_cursor { CF_CURSOR_DEFAULT, CF_CURSOR_POINTER, CF_CURSOR_TEXT } cf_cursor;

/*
 * The shell's own key, in the value the SDL backend hands key events: F12 is
 * scan-code 69 under the scan-code bit there. Nothing in the tree dispatches
 * it; the shell that owns the loop reads it and toggles the inspector.
 */
#define CF_KEY_F12 0x40000045u

/*
 * One input event. Position and wheel are in the coordinate space the rects
 * were laid out in; button and key carry whichever the type names. Target,
 * stopped and captured are dispatch state: the engine writes them as the
 * event climbs the tree and a handler reads them.
 */
typedef struct cf_event {
    cf_event_type type;
    cf_point position;
    cf_point wheel;
    uint32_t button;
    uint32_t key;
    /* The slot whose handler is running; meaningless outside dispatch. */
    uint32_t target;
    bool stopped;
    bool captured;
} cf_event;

/*
 * One handler answers every event kind a node takes: the pointer handler
 * sees moves, presses, releases and the wheel, the key handler the
 * keyboard. The user_data pointer is handed back unread and unchanged,
 * borrowed for the dispatch call like measure_data.
 */
typedef void (*cf_event_fn)(cf_event *event, void *user_data);

/* Keeps the event from reaching the handlers above the node that stopped it. */
void cf_stop(cf_event *event);

/*
 * Holds the pointer on the node whose handler calls this: moves and the
 * release keep arriving here until a release is dispatched, wherever the
 * pointer went.
 */
void cf_capture(cf_event *event);

#endif
