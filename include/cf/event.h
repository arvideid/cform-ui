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
    CF_EVENT_QUIT,
    /* What the keyboard produced rather than which key moved: the shift, caps
     * lock, layout and input method that turn a key into a letter are the
     * device's business, and this is the answer they settled on. */
    CF_EVENT_TEXT,
    /*
     * The hand let go without letting go: the window lost the pointer to another
     * one, a touch slid off the surface, a grab was taken elsewhere. The press and
     * the capture it booked end here rather than at a release, and nothing is
     * clicked - a pointer that vanished cannot have come up inside a rect. A box
     * that kept its own state between the press and the release reads this the way
     * it reads the release, because the difference between the two is not its to
     * know.
     */
    CF_EVENT_POINTER_CANCEL = 8,
    /*
     * What an input method is holding but has not decided: the run the keyboard has
     * staged in the box, in `text`, in place of whatever the previous such event
     * staged. The bytes arrive whole rather than as keystrokes, because the input
     * method owns the run and rewrites it as the person keeps typing; an empty run is
     * it saying the run is finished with, which is a cancel rather than a commit - the
     * words the person settled on arrive as a text event, and that is what lets a
     * widget tell the two endings apart instead of guessing. A box that shows the run
     * paints it as marked and keeps it out of its value until the text event comes.
     */
    CF_EVENT_COMPOSITION = 9
} cf_event_type;

typedef enum cf_cursor { CF_CURSOR_DEFAULT, CF_CURSOR_POINTER, CF_CURSOR_TEXT } cf_cursor;

/*
 * The keys a widget names, in the number an event carries. What a device calls a
 * key stays the device's business: the backend turns its codes into these, and a
 * key with no word here keeps its platform number, which no widget reads. A key
 * that types a character types it as a text event, so nothing here names a
 * letter. The letters have one door left open for them: a key down for a Latin
 * letter carries the number of its lowercase ASCII code, which is the number every
 * platform this framework reaches already agrees on and which shift only moves
 * between the two cases of it. That is what lets a field answer Ctrl+C without
 * knowing what any device calls the C key. A backend whose codes differ maps them
 * onto these. The numbers are the agreement a backend and a build share, so a value
 * is appended and never renumbered.
 */
typedef enum cf_key {
    CF_KEY_NONE = 0,
    CF_KEY_BACKSPACE = 1,
    CF_KEY_ENTER = 2,
    CF_KEY_LEFT = 3,
    CF_KEY_RIGHT = 4,
    CF_KEY_UP = 5,
    CF_KEY_DOWN = 6,
    CF_KEY_HOME = 7,
    CF_KEY_END = 8,
    CF_KEY_DELETE = 9,
    CF_KEY_ESCAPE = 10,
    CF_KEY_TAB = 11,
    /* The shell's own key: nothing in the tree dispatches it, the loop that
     * owns the inspector reads it and toggles the frame's numbers. */
    CF_KEY_F12 = 12,
    /* The space bar. It is named because a control reads it as an ask to act, the
     * way a pointer release inside its rect is; what a field types is still the
     * text event the device sends beside it, so naming the key borrows nothing
     * from the character. */
    CF_KEY_SPACE = 13
} cf_key;

/*
 * The modifier keys held when an event was produced, one bit each. Whether a
 * shift makes a capital is settled by the device and arrives inside the text;
 * whether it means "extend what is selected" is the widget's to read, and this is
 * the only place it can. A backend that reports no modifier state leaves the
 * field zero, and a widget sees the keys it names with no shift on them.
 */
typedef enum cf_mod {
    CF_MOD_NONE = 0,
    CF_MOD_SHIFT = 1 << 0,
    CF_MOD_CONTROL = 1 << 1,
    CF_MOD_ALT = 1 << 2,
    CF_MOD_META = 1 << 3
} cf_mod;

/*
 * One input event. Position and wheel are in the coordinate space the rects
 * were laid out in; button and key carry whichever the type names. A scroll's
 * positive axis walks the content toward its end - the wheel rolled toward the
 * hand - whatever sign the device keeps for that. Target, stopped and captured
 * are dispatch state: the engine writes them as the event climbs the tree and a
 * handler reads them.
 *
 * A text or composition event's bytes are UTF-8, borrowed for the handler call alone:
 * whoever keeps them copies them, which is what a field editing a buffer does anyway.
 * A key scalar is one of the cf_key values the widget can name, and the modifier
 * bits say what was held while it went down.
 */
typedef struct cf_event {
    cf_event_type type;
    cf_point position;
    cf_point wheel;
    uint32_t button;
    uint32_t key;
    cf_string text;
    /* The cf_mod bits held for this event. */
    uint32_t mods;
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
 * release keep arriving here until a release or a cancellation is dispatched,
 * wherever the pointer went.
 */
void cf_capture(cf_event *event);

#endif
