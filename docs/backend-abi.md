# Writing a backend

An **ABI** is the agreement between two pieces of code that were compiled
separately: which functions exist, what their arguments mean, and which version
of that agreement each side was built against. This file is that agreement
between cform and a device.

`cf/backend.h` is the only place the engine touches a window, a GPU, a font file
or a mouse. It is one struct of eight function pointers, and nothing else crosses
the boundary in either direction — which is how the engine runs headless in tests
and in a browser. Four are the engine's to call: `submit`, `present`,
`poll_event` and `surface`. Four are the host's: the two uploads, the release and
the teardown, which a backend fills in but never waits to be asked for. If you
only want to *use* cform, you can stop here.

```
engine ──submit──▶ shapes ──▶ backend ──▶ screen
       ◀─poll──── input  ◀──── size ──── device
```

## The eight functions

```c
typedef struct cf_backend {
    uint32_t abi_version;   /* must equal CF_BACKEND_ABI_VERSION */
    uint32_t size;          /* must equal sizeof(cf_backend) as you compile it */
    void *user_data;        /* your state; the engine never reads it */
    void (*submit)(void *user_data, const cf_command *commands, uint32_t count);
    void (*present)(void *user_data);
    uint32_t (*create_texture)(void *user_data, uint32_t width, uint32_t height,
                              const void *pixels);
    bool (*poll_event)(void *user_data, cf_event *out);
    cf_size (*surface)(void *user_data);
    bool (*load_image)(void *user_data, const char *path, cf_image_content *out);
    void (*release_texture)(void *user_data, uint32_t texture);
    void (*destroy)(void *user_data);
} cf_backend;
```

Every one takes `user_data` first, so all of your state hangs off a single
pointer that you allocate.

| Function | Job | Can be left NULL |
|---|---|---|
| `submit` | Receive this frame's shapes, in the order to draw them | **No** — `cf_app_run` silently does nothing without it |
| `present` | Push what the last `submit` drew onto the screen | **No** |
| `create_texture` | Upload RGBA8888 pixels, return an id the shapes can name | Yes — image shapes then draw nothing |
| `poll_event` | Copy one waiting input event into `out`, say whether there was one | Yes — the page runs but nothing is clickable |
| `surface` | How big the output is, in logical points | Yes — the page lays out against zero space |
| `load_image` | Decode a file, upload it, name its texture and its extent | Yes — the app uploads its own pixels through `create_texture` |
| `release_texture` | Hand back the bytes behind an id the page stopped showing (the host calls it) | Yes — a backend that keeps every upload says so by leaving it NULL |
| `destroy` | Free the backend's own state: the uploads still held, the window, the bytes beside them (the host calls it once the shell has returned) | Yes — a backend with no state of its own has nothing to free |

There is no error code to return, and no callback from the engine into your code
beyond the four it calls. A backend that can't open its window returns a struct whose
functions do nothing and logs the reason once on `CF_LOG_BACKEND`. The two numbers at
the front catch a mismatched library: `abi_version` is the revision you were built
against and `size` is how big the struct was where you compiled. Either one is enough
to fail, and they catch different mistakes — a porter can copy a number and get the
layout wrong, and a library can be the layout you expect with a number you never saw.
A stream read with the wrong layout draws *wrong* rather than failing, so `cf_app_run`
checks both before the first frame and idles if either disagrees. A program calling
`cf_frame_run` itself has to make that comparison.

A capability is advertised by the function that would provide it: you test the
pointer and a NULL means this backend does not offer that service. That is why only
`submit`, `present` and `surface` have to be filled — the rest are optional by their
own value. A separate table of bits saying the same thing would be a second answer
to keep in agreement, so there isn't one yet. Bits earn their place when a single
call has to speak for several services at once, which is the shape the clipboard and
the cursor will have; until then every service has a slot that can be asked directly.

## What you may keep, and what you must copy

- **The shape list is on loan for the duration of `submit` only.** It lives in
  memory the frame owns, recycled before the next frame. Draw later — deferred,
  display list, recorder — and copy what you need into memory you own.
- `cf_command.text.chars` is on loan the same way, and is **not** zero-terminated;
  `text.length` is how far it goes.
- `cf_command.font` outlives every frame. Fonts belong to the theme, not the page.
  Never free one.
- `create_texture` ids are yours until `release_texture` takes one back. **Zero
  means "no texture"** — a drawable answer, not an error. An id past every upload,
  and one just retired, are the same ask and the same nothing: a shape naming
  either draws no pixels.
- A retired id may be handed out again by the next upload. That is what the slot is
  for — a screen that loads images as it goes would otherwise grow one texture per
  visit forever — and it is why a retired id has to draw nothing rather than
  whatever the number came to mean.
- **Release only what no frame can still name.** A backend that defers its drawing
  past `submit` (a queue, a display list built for later) keeps the textures that
  stream named until it has drawn them, and only then lets a release destroy them.
  Nothing above this boundary can see that queue, so the duty is the backend's.
- **An upload is a promise about a buffer.** The caller states a width, a height and
  where the RGBA8888 bytes start, and the backend reads `width * height * 4` of them.
  A side of zero, absent pixels and a size no device could carry are therefore refused
  with 0 rather than half-uploaded: a width large enough to wrap the row stride turns
  the copy into a read past whatever the caller had. `cf_backend_sdl` caps a side at
  16384 and logs the ones it turns away, because the dimensions a decoded file states
  are not a choice the host made.
- `destroy` is the host's, not the shell's: `cf_app_run` borrows the backend and
  hands it back, so a window outlives the loop unless the host closes it.
- A teardown gives back only what the backend took. If starting it found the
  platform already running, leaving the platform running is the right half of the
  job: a host that had its renderer up before your window opened does not expect
  that window to shut the subsystem down on its way out.
- `user_data` is never read by the engine. It's yours.

## When the output goes away

A reset driver, a removed adapter and a window the desktop closed under you all look
the same from the draw path: the frame you made is not on screen, and the next one
will not be either. The contract does not ask a backend to win the device back — the
device belongs to the host — but it does ask three things of it:

- Say it once on `CF_LOG_BACKEND`, not once per shape or per frame.
- Draw nothing after it. A stream pushed at an output that refuses frames costs a
  failing call per command and keeps showing the screen the last frame it took.
- Keep answering `release_texture` and `destroy`. A host that cannot finish a
  teardown is left holding bytes with no route to them.

`cf_backend_sdl` takes a refused `SDL_RenderPresent` as the signal: it stops drawing,
answers 0 to an upload, and lets the release and the teardown run as normal. A loss
that shows up only as a failed draw, never a failed present, is not detected — no
test in this repository can make SDL refuse a frame, so that half stays a desktop
case for a person and a window. Recovery in every case is the host's: destroy the
backend, open a new one, upload again.

## Everything is in points, not pixels

`surface`, `cf_event.position` and `cf_command.rect` are all in **logical
points**. The device pixel ratio is your private business: the SDL backend reads
it per `submit` and multiplies. No layer above this one has ever seen a pixel, so
if your port passes pixel sizes upward, the boundary has leaked.

You don't need to report resizes — the engine asks `surface` again every frame, so
a new window size becomes a new layout on its own.

## The shape list

Four kinds, appended in paint order — a parent before its children, a border
after the fill it surrounds. Clear the surface, walk the array front to back, and
that is the entire compositing rule.

| Kind | Fields that matter |
|---|---|
| `CF_COMMAND_FILL` | `rect`, `clip`, `color`, `radius` |
| `CF_COMMAND_BORDER` | `rect`, `clip`, `color`, `radius`, `widths` (one per side, drawn inside `rect`) |
| `CF_COMMAND_TEXT` | `rect` (one line), `clip`, `text`, `font`, `color`, `ellipsis` |
| `CF_COMMAND_IMAGE` | `rect`, `clip`, `texture` |

A radius larger than half the shorter side means a half-circle; the sender
intends that, so don't clamp it away. A side with no `widths` entry draws
nothing.

Text lines arrive **already broken and measured** — one command per line,
`rect.width` the width the layout charged for it, `rect.height` the line spacing.
Don't re-wrap them, or you'll draw lines the layout never made room for. When
`ellipsis` is set, the bytes stop short and you append `…` (U+2026,
`CF_ELLIPSIS`); the width already includes it.

**`clip` is the part that trips people up.** A shape that sticks out of a
scrolling box keeps its full size, because `cf_paint` only drops shapes that miss
the viewport entirely. What cuts it is `clip`: the nearest box that hides its
overflow.

- A **sharp fill** (radius `0`) arrives **already cut** to `clip`: a plain
  rectangle is the whole of what it draws, so cutting the rect is free and the
  device paints fewer pixels for the same picture.
- A **rounded fill, an image, a border and a text line cannot** be cut, because
  shrinking the rect would move the corner a radius rounds, remap the UVs an
  image's texture is drawn through, shift the stroke a border rounds, or lift the
  baseline text sits on. They keep their true `rect` and the exact `clip`, and
  you must scissor to `clip` for all four. Skipping the scissor lets them paint
  over the pane they are meant to spill out of.

A `clip` with no extent means the whole output: bound to the device edges without
being told, so a page that never scrolls costs you no state change at all.

## Input

`poll_event` hands over one event per call and returns false when none are left,
so the engine drains the queue every frame and nothing piles up while a window
sits idle. Map your device's input onto `cf_event_type` and nothing else:

- `CF_EVENT_QUIT` closes the loop; it never reaches the tree.
- The engine uses **F12** — `CF_KEY_F12` — for its own inspector, before your page
  sees it.
- A key event carries one of the `cf_key` numbers, not your platform's. Map
  backspace, enter, the four arrows, home, end, delete, escape, tab, the space
  bar and F12 onto them. A key you have no word for keeps its own number, which
  no widget reads, and a key that types a character types it through
  `CF_EVENT_TEXT` — so letters never need mapping, and a widget never guesses
  what a keycode spelled.
- The space bar is the one key that does both jobs: report `CF_KEY_SPACE` while the
  key is down and let the text event carry the character it typed. A control reads
  the first as an ask to act, a field reads the second as a space, and neither is
  confused because the two events say which of them they are.
- `cf_event.mods` carries the `cf_mod` bits held when the key went down: it is how
  a field learns that shift meant "extend the selection" rather than "type a
  capital". Report zero if you would rather not track it, and the widgets that
  read it see no modifier, which is honest.
- Pointer positions must be in points.
- `present` may collect your platform's events but must not empty the queue: a
  click landing during a frame has to survive to the next `poll_event`.

## Fonts

`cf_font` is a handle plus a table of functions, and that table answers
**measuring only**: `metrics` (line height), `advance` (how far the pen moves,
kerning included), `has_glyph`. It never draws a glyph — that's the other half of
the same object. `cf_command.font.handle` is whatever your own font constructor
made, so a port pairs the two (`cf_font_sdl` goes with `cf_backend_sdl`). A handle
from a different backend means nothing to you, and a NULL handle draws nothing.

A font is the host's, and the constructor has a pair: nothing in the framework
closes one, so `cf_font_sdl_close` is the host handing back what its own open took,
and it leaves the zero struct behind so a page still naming that font measures and
draws nothing instead of reading through bytes that stopped being one. Close fonts
before you call `destroy` — a font sits on the platform, and the teardown may be the
thing that stops it.

This split is why text layout is testable with no window: the tests answer the
same three questions with fixed numbers.

## Porting, in order

1. Fill the struct, set `abi_version` and `size`, keep your state in `user_data`.
2. `submit`: clear, walk front to back, map each kind onto your fill / stroke /
   glyph-run / textured-quad call, scaled by pixel density, each one under the
   scissor its `clip` names.
3. `present`: your device's swap. Don't collect input here.
4. `surface`: the size, in points.
5. `poll_event`: translate input; swallow events that have no `cf_event` meaning.
6. `create_texture`: only when you want images. Return 0 on a failed upload and
   log it once.
7. `load_image`: decode a file into pixels, upload them through the same path, and
   answer the extent in points. `cf_backend_sdl` reads BMP because SDL already
   does; a port with a wider decoder is the reason the slot exists. Answer false
   for a file you cannot read and leave `*out` alone.
8. `release_texture`, once your device can hand bytes back: destroy what the id
   named and mark the slot free, so the number can be handed out again. Then
   `destroy`, which tears down every upload still held, the window and your own
   state — last-opened first, because a window that outlives its renderer is a
   use-after-free the other way round. Both are the host's to call, so a page like
   `examples/gallery.c` is the pattern: upload before the loop, release and destroy
   after it.
9. A font constructor whose table measures and whose handle your `submit` can
   draw.
10. Add a CMake target beside `cf_backend_sdl`, behind its own option, linking
    `cf_paint cf_text` and your device library. Only `src/backend/*/` and
    `src/app/` may name a platform header.

`src/backend/headless/record.c` does two of the eight and is a legal backend —
that is the minimum. It takes no uploads and keeps no device state, so its
resource slots stay NULL; a host that releases through a backend has to ask the
slot first either way.

## Checking your work

Nothing in the test suite looks at a pixel, on purpose. The layers below the
backend are covered by the recorder backend, which keeps the last shape list in
memory it owns, and the golden tier snapshots that list as text. So a new backend
proves itself by running the demo: build with your option on, run `gallery`, and
**look at it**. The shapes you must draw are exactly the ones the golden files
already name, so a disagreement between what the recorder saw and what your window
showed is the whole bug report.
