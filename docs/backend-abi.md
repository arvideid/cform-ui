# The backend ABI

`cf/backend.h` is the only boundary between the engine and a device: one struct of
function pointers, with nothing in the engine knowing about windows, GPUs, fonts or
input devices. This is the contract a port has to satisfy and the checklist for
writing one; which floor to link and what the build options change are in the
README.

```
engine ──submit──▶ commands ──▶ backend ──▶ device
       ◀─poll──── events  ◀──── surface ── extent
```

## The struct

```c
typedef struct cf_backend {
    uint32_t abi_version;
    void *user_data;
    void (*submit)(void *user_data, const cf_command *commands, uint32_t count);
    void (*present)(void *user_data);
    uint32_t (*create_texture)(void *user_data, uint32_t width, uint32_t height,
                              const void *pixels);
    bool (*poll_event)(void *user_data, cf_event *out);
    cf_size (*surface)(void *user_data);
} cf_backend;
```

Every slot takes `user_data` first, so a backend's state is one pointer and its
own to allocate. `abi_version` is `CF_BACKEND_ABI_VERSION` and is how a stale
`.so` is caught: a stream the backend misreads draws wrong rather than failing.
`cf_app_run` compares it before the first submit and sits idle on a mismatch, so
a host that drives `cf_frame_run` itself has to make the comparison.

| Slot | Job | May be NULL |
|---|---|---|
| `submit` | Receive the frame's commands, in paint order | No — the backend draws nothing |
| `present` | Push what the last `submit` drew to the output. A shell calls it every turn of its loop, including the turns it drew nothing in | No |
| `create_texture` | Upload RGBA8888 pixels, return an id the stream can name | Yes — image commands then draw nothing |
| `poll_event` | Move one device event into `out`, answer whether one came | Yes — the page runs without input |
| `surface` | The output's extent in logical points | Yes — the page lays out against a zero offer |

`cf_app_run` fails as a silent no-op when `submit` is NULL; a backend that
cannot open its window returns one whose slots do nothing, and reports the
reason once on `CF_LOG_BACKEND`. There is no error code to plumb through, and
no callback the engine makes into a backend beyond these five.

## Lifetimes

- The command array is **borrowed for the `submit` call alone**. It lives in the
  frame arena, which the shell clears before the next frame. A backend that
  draws after `submit` returns — a deferred renderer, a recorder, a display-list
  encoder — copies what it needs into memory it owns.
- `cf_command.text.chars` is borrowed the same way, and is **not**
  NUL-terminated: `text.length` is the extent.
- `cf_command.font` carries a `handle` and an `ops` table that outlive every
  frame: fonts belong to the theme, not to the page. A backend never frees one.
- `create_texture` ids are stable for the life of the backend's state. Zero
  names refusal — "no texture" is a real, drawable value, and a command naming
  an id the backend does not know draws nothing.
- `user_data` is never read by the engine.

## Coordinates

Everything above the backend is in **logical points**. `surface` answers points,
`cf_event.position` arrives in points, `cf_command.rect` is in points. The
device pixel density is the backend's private business: the SDL backend reads
`SDL_GetWindowPixelDensity` per submit and multiplies. No layer above sees a
pixel, and a port that exposes pixel sizes to the engine has leaked.

A resize needs no callback: the shell re-reads `surface` every frame, so a new
extent is a new layout offer without the backend arranging anything.

## The command stream

Four kinds, appended in paint order: a parent precedes its children, a border
follows its own fill. Drawing the array front to back, clearing first, is the
whole compositing rule.

| Kind | Fields that matter |
|---|---|
| `CF_COMMAND_FILL` | `rect`, `clip`, `color`, `radius` |
| `CF_COMMAND_BORDER` | `rect`, `clip`, `color`, `radius`, `widths` (per side, painted inside `rect`) |
| `CF_COMMAND_TEXT` | `rect` (one laid line), `clip`, `text`, `font`, `color`, `ellipsis` |
| `CF_COMMAND_IMAGE` | `rect`, `clip`, `texture` |

`radius` is clamped by the sender's intent, not by the stream: a radius past
half the shorter extent is a corner the backend should read as a half-circle.
A side with no `widths` entry paints nothing.

Text lines arrive **already broken and measured** by `cf_text`: one command per
line, `rect.width` the line's measured advance, `rect.height` the font's line
rhythm. A backend must not re-wrap them — that would draw lines the layout
never priced. When `ellipsis` is set the line's bytes stop short and the
backend appends U+2026 (`CF_ELLIPSIS`) after the text; the width already counts
its advance.

A straddling rect keeps its full extent, and `cf_paint` only drops commands that
miss the viewport entirely. What cuts it is `clip`: the nearest box that clips
its content, which a scrolling pane is. A fill and an image arrive already cut
to it, because cutting a rectangle is the whole of what they draw; a border and
a text line cannot be, because cutting their rect would move the corner the
stroke rounds and the baseline the line sits on. Draw those two inside `clip`
with the device's scissor, and nothing a scroll half-hides lands outside the box
that hides it. A `clip` with no extent is the whole output — the device bounds
that without being told, so a page that never clips should cost no state change.

## Events

The queue belongs to whoever reads it. `poll_event` hands over one event per
call and returns false when the queue is dry, so the shell drains it every frame
and nothing piles up unread while a window sits idle.

A port maps device events onto `cf_event_type` and nothing else: `CF_EVENT_QUIT`
closes the loop and never dispatches to the tree, and the shell consumes
`CF_KEY_F12` for its inspector. Key scalars pass through as the platform's own
keycodes — the engine does not translate them, and `cf_input` treats an unknown
scalar as a key nobody bound.

Pointer positions must be in points. `present` may pump the device's own
collection, but it must not drain or flush the queue: a press that arrives
while a frame is being drawn has to survive to the next poll.

## Fonts

`cf_font` is `{handle, ops}`. The `ops` table answers **measuring only** —
`metrics` (line height), `advance` (pen distance, kerning included), `has_glyph`
— and never rasterizes. Rasterizing is the backend's side of the same object:
`cf_command.font.handle` is whatever the backend's own font constructor made,
and a port pairs the two (`cf_font_sdl` goes with `cf_backend_sdl`). A handle
from a different backend is not a portable thing, and a NULL handle draws
nothing.

This is why `cf_text` can be exercised headlessly: the tests answer the same
table with fixed numbers.

## Porting checklist

1. Fill the struct, set `abi_version = CF_BACKEND_ABI_VERSION`, keep state in
   `user_data`.
2. Implement `submit` as: clear, walk front to back, per kind map to the device's
   fill / stroke / glyph-run / textured-quad call, scaled by pixel density, each
   one under the scissor its `clip` names.
3. Implement `present` as the device's swap, and skip the swap when no `submit`
   has drawn since the last one. A shell presents at idle, and a discard-model
   swapchain answers a swap with no drawing in it by showing an undefined buffer
   — a trail of the frame before the last, and two states of one widget
   flickering on the screen.
4. Implement `surface` in points.
5. Translate input in `poll_event`; consume what has no `cf_event` word.
6. Add `create_texture` only when image commands are wanted; take the bytes in
   the memory order the slot documents rather than the packed value of the same
   name, return 0 when an upload fails and log once.
7. Provide a font constructor whose `ops` measure and whose `handle` your
   `submit` can draw.
8. Register the target in `CMakeLists.txt` behind an option, next to
   `cf_backend_sdl`, linking `cf_paint cf_text` and your device library. Only
   `src/backend/*/` and `src/app/` may name a platform header.

A minimal skeleton is what `src/backend/headless/record.c` is: two slots, no
window, and it is a legal backend.

## Testing a port

Nothing in the suite verifies a pixel. The layers below the backend are checked by
the recorder (`cf_backend_recorder`), which keeps the last submitted stream in
memory it owns, and the golden tier snapshots that stream as data. A new backend
therefore earns its keep by running the example: build with the port's option on,
run `gallery`, and a human looks at it. The commands a port must draw are exactly
the ones the golden snapshots already name, so a diff between what the recorder saw
and what the device showed is the whole bug report.
