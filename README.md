# cform

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A user-interface toolkit written in plain C17 — no C++, no dependencies inside
the engine.

## What a UI toolkit is, in the terms you already have

A window is a rectangle of pixels. Nothing about a pixel says *button*. Between
"here is a rectangle" and "here is a screen with a title, a paragraph and a
button in it" sits a body of work: turning nesting into positions, breaking text
into lines, clipping a scrolled list to its window, working out which box the
pointer is over, and redoing all of it when the window changes size.

A **UI toolkit** is that body of work written once, so the hundredth application
does not write it again. Every browser contains one — which is why HTML works the
way it does:

```html
<div class="card">              <!-- a box that holds boxes -->
  <h1>Title</h1>
  <p>A paragraph of text that wraps when the window gets narrow.</p>
  <button>press me</button>
</div>
```

Nowhere in that page is the button's position written down. You say which box is
inside which, CSS says how they pack — a column, a gap, something that stretches
— and the browser measures the tree and paints it. *Describe the nesting, get the
positions back.* That is what a toolkit does, and cform is the part of a browser
that does the measuring and the painting, with the parser, the stylesheet and the
other hundred megabytes taken out.

The `<div>` above is this in C, and this one line is the whole of a screen:

```c
return CF_COL(ctx, theme->page, title, paragraph, button);
```

`CF_COL` is the `div` laid out as a column, `cf_label` is the text inside an `<h1>`,
`cf_button` is the `button`, and a `cf_style` is a rule block whose properties
are C fields: `width`, `margin`, `gap`, `flex_grow`, `justify_content`. You never
write an `x` or a `y`, and you never draw anything: cform hands a flat list of
rectangles, text and images to a backend that puts them on a surface.

| you know | here it is |
| --- | --- |
| an element in the DOM | a **node** — one box, named by an integer rather than a pointer |
| the CSS box model, flexbox, grid | `cf_style` and `cf_layout` |
| the browser's layout pass | step 2 of [how a frame happens](#how-a-frame-happens) |
| a display list the compositor builds | `cf_command_list`, the shape list `cf_paint` emits |
| a graphics backend (Skia, ANGLE) | a `cf_backend`: [eight functions](docs/backend-abi.md) |
| React re-rendering your component tree | describing the whole screen every frame and letting the diff decide |
| a `click` listener on an ancestor | a handler on a parent box: they run from the one the pointer hit up through its parents, and `cf_stop` ends the walk |

What you *don't* get from a browser: no markup language, no stylesheet, no
cascade, no selector engine. That omission is the point. The tree is C function
calls and the styles are structs, which is what keeps the engine small enough to
read in an afternoon and linkable into a program that has no business carrying a
browser.

## Describe it, don't manage it

If you learned widgets as objects, this is the one habit to swap. The usual shape
is: make a button, keep a pointer to it, and call `set_label` on it whenever the
data changes. You are then responsible for remembering that the button exists,
where it is, and what else needs updating beside it.

Here, `build` returns the whole screen as a value and nothing is kept. The
description is made again every frame; cform compares it with the last one, and
when nothing changed it lays out nothing, draws nothing and allocates nothing.
What you cannot hold in a local variable you ask `ctx` for by name:

```c
/* The number of clicks survives the rebuild because the name does, not the box. */
uint32_t *clicks = cf_state(ctx, cf_intern_text(&ctx->names, "clicks"), sizeof *clicks);
```

That answers the same bytes every frame (NULL if the heap refused), and they go
back to the heap the frame after the builds stop asking for that name. That is the
whole trick, and it is why "declarative" is not a marketing word here — you state
what the screen looks like rather than how it got that way.

## Goals

The things this repository is trying to do, in the order it tries them:

1. **Layout that is right, not approximately right.** Flexbox follows the
   [CSS specification](https://www.w3.org/TR/css-flexbox-1/) and is scored
   against [Yoga](https://github.com/facebook/yoga)'s own test corpus — someone
   else's answers, not ours. [The score](docs/conformance.md) is public.
2. **A screen that costs nothing at rest.** The shell notices when nothing moved
   and skips the frame whole: nothing laid out, emitted or allocated. That saving
   is `cf_app_run`'s idle test, not the pipeline's - call `cf_frame_run` on an
   unchanged screen and it runs the full build-diff-layout-paint. Numbers, not a
   claim: [bench.md](docs/bench.md).
3. **Readable end to end.** C17, no templates, no code generation, no reflection.
   Every layer is a CMake target you can open and finish reading.
4. **Runs where C runs**, including a browser tab through WebAssembly.

Just as deliberate are the things this is *not* trying to be: not a CSS engine,
not a widget catalog, not an application framework, not a browser. The gaps that
follow from those choices are the **Honest limits** list below.

## Is this the right tool?

**Good for**

- Learning how a UI toolkit works end to end. "End to end" means the whole
  pipeline above — describe, measure, place, emit, draw — with no layer hidden
  behind a generated header or a platform SDK. Every one of them is small,
  headless and readable, and the layout maths is proved against a real engine
  rather than against our own opinion of it.
- A project that wants layout only — a game HUD, an embedded screen, a renderer
  of your own — and can link `cf_layout` with nothing else.
- Small binaries and browsers: the same C compiles to
  [WebAssembly](https://emscripten.org/) and runs in a tab.
- People who want their own widgets. There is no designer tool and no styling
  language; you write C, and you get the whole tree.

**Honest limits**

Read this list before planning an application around it. None of it is silently
accepted and ignored — there is no field that pretends to take these things.

- **Not production-ready.** Version 1.0 is in development, and the API numbers
  still move: the backend contract is at ABI 9 and has been broken on purpose.
- **Six widgets**: label, button, field, list, scroller, image. No checkbox,
  radio, slider, dropdown, tab, menu, tooltip, dialog or table. Building the one
  your app needs is expected and is a few dozen lines.
- **Text is Latin-shaped only.** Wrapping honours spaces, hyphens, ideographs and
  the text's own breaks, and a font stack falls back glyph by glyph. There is no
  right-to-left, no bidirectional text, no shaping for Arabic or Indic, no
  justification, no ligatures and no variable fonts. An editor's field deletes
  one whole scalar at a time, so a combining mark comes apart as two steps rather
  than one.
- **Accessibility is a tree, not a feature.** `cf_semantic_export` writes the
  role, the text and the disabled state of every box — the data a screen reader
  reads — and that is where it stops. Nothing reaches Narrator, VoiceOver or
  AT-SPI, there is no focus order announced, no live region, no ARIA and no
  hit-target or contrast checking. A `cf_label` in this project is a box that
  draws glyphs, not an accessible name for the box beside it.
- **Keyboard focus follows the pointer.** Clicking a box focuses it and
  `cf_focus` moves it by hand; nothing implements **Tab**, and a field cannot be
  reached without the mouse.
- **An editing field has no clipboard.** Cut, copy and paste are not wired to
  the platform. Enter submits nothing. There is no undo, no selection by word,
  no caret blink and no IME, so composing Japanese types nothing.
- **Images are BMP, loaded once.** The reference backend reads BMP through SDL's
  own loader and uploads RGBA pixels; PNG, JPEG and WebP are a dependency this
  repository hands to the host, and the `load_image` slot is where your decoder
  goes. Retiring an upload is the host's call through `release_texture`, which
  hands the number back to the next image: nothing in the engine decides that a
  picture is finished with.
- **The cursor never changes.** A hovered field reports that it wants an I-beam
  and the frame carries that as far as the backend, which ignores it: no backend
  here sets a system cursor.
- **Missing CSS, in full:** `calc()`, container queries,
  grid named lines and areas, and every transition and animation but the
  ones you write in C against the frame's elapsed seconds.
- No drag-and-drop building, no theming tool, no loading a font from memory
  (`cf_font_sdl` takes a path), no multiline field. Device pixels stop at the
  backend: only the SDL one reads a pixel density, and a window whose surface
  changes shape is resized by relayout, not by scaling a bitmap.

Each gap above has a row in [docs/backlog.md](docs/backlog.md) naming the test tier
that would prove it closed, and the backlog is the place to look for what to work
on next.

## Try it

You need [CMake](https://cmake.org/) 3.25+, [Ninja](https://ninja-build.org/),
and a C17 compiler ([GCC](https://gcc.gnu.org/) or
[Clang](https://clang.llvm.org/)). Then:

```sh
cmake --preset dev -DCFORM_WITH_SDL_BACKEND=ON   # set up a Debug build
cmake --build --preset dev                        # compile everything
ctest --preset dev                                # run the 36 headless checks
build/dev/bin/gallery                             # open the demo window
```

`gallery` is a scrolling page that demonstrates every widget and reacts to the
mouse: type in the field, drag the scrollbars, watch the box the frame clock
animates. Press **F12** in it to see the frame's numbers on screen. Drag its
edge: the window is resizable, and every line of text re-breaks against the
width the shell hands the page.

The first configure downloads [SDL3](https://github.com/libsdl-org/SDL) (the
library that opens a window) and takes a few minutes. Without
`-DCFORM_WITH_SDL_BACKEND=ON` nothing is downloaded at all, because the engine
never needs a window.

The same page runs in a browser tab, through
[Emscripten](https://emscripten.org/):

```sh
cmake --preset web                                # needs EMSDK in the environment
cmake --build --preset web
python -m http.server 8033 --directory build/wasm/bin
```

and then open <http://localhost:8033/gallery.html>. It has to be served rather
than opened, because a browser will not fetch a `.wasm` over `file://`.

## What using it looks like

This is a complete program: a button that prints when you click it.

```c
#include <stdio.h>

#include "cf/app.h"     /* the run loop          */
#include "cf/build.h"   /* CF_COL, CF_ROW        */
#include "cf/widget.h"  /* cf_label, cf_button   */

static const cf_theme *theme;   /* the stock colors and spacing */
static cf_font font;            /* one loaded typeface          */
static cf_text_stack fonts;     /* the stack a walk reads it through */

static void say_hello(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)ctx, (void)slot, (void)user_data;
    printf("hello\n");
}

/* Runs whenever cform needs to know what the screen should look like. */
static cf_node build(cf_ctx *ctx) {
    const cf_node text = cf_label(ctx, cf_string_of("press me"), &fonts, theme->on_ink);
    const cf_node button = cf_button(ctx, "greet", say_hello, NULL, &text, 1u);

    return CF_COL(ctx, theme->page, button);
}

int main(void) {
    theme = cf_theme_default();
    font = cf_font_sdl("Lato-Regular.ttf", 14.0f);
    fonts = (cf_text_stack){.fonts = &font, .count = 1u};

    cf_app_run(&(cf_app_config){.build = build, .theme = theme, .fonts = &fonts,
                                .backend = cf_backend_sdl(&(cf_sdl_config){
                                    .title = "hello", .width = 480u, .height = 320u})});
}
```

That is the `<div>` above in C: a column, and inside it a button holding a label.
`cf_app_run` opens the window and keeps calling `build` as often as the screen
needs. The string `"greet"` is the button's name: it is how cform recognises the
same button from one frame to the next, so the count of clicks, the scroll
position and the keyboard focus survive a rebuild.

[`examples/gallery.c`](examples/gallery.c) is the same API stretched over every
widget, and [`examples/layout_only.c`](examples/layout_only.c) is the layout
engine as a program that links nothing else.

> `cf_font_sdl` takes a path to a `.ttf` you actually have. A file that won't open
> measures nothing — logged once on `CF_LOG_BACKEND` — so the window, the button
> and the click all still work, but the text is invisible. The demo build
> downloads `build/dev/fonts/Lato-Regular.ttf` and passes that path in.

## How a frame happens

```
your build()  →  tree of boxes  →  sizes worked out  →  list of shapes  →  backend
   (cf_widget)       (cf_node)         (cf_layout)         (cf_paint)     (cf_backend_sdl)
```

Four steps, and each one can be used on its own:

1. **Describe.** `build` returns boxes carrying a `cf_style` — the same
   vocabulary as CSS: `width`, `margin`, `flex_grow`, `justify_content`, `gap`.
2. **Measure.** `cf_layout` is pure arithmetic: give it the tree and the room
   available, and it writes one position and size per box. It implements
   [flexbox](https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_flexible_box_layout/Basic_concepts_of_flexbox)
   the way the [CSS specification](https://www.w3.org/TR/css-flexbox-1/) defines
   it — directions, wrapping, grow and shrink, gaps, percentages, `auto` margins,
   baseline alignment, absolutely positioned children — plus a useful subset of
   CSS grid. Ask it for the size of text you never drew by handing it a measure
   function.
3. **Emit.** `cf_paint` walks the laid-out tree and appends the shapes to draw:
   fills, borders, lines of text, images. In order, back to front, each one
   carrying the rectangle it may not be seen outside — that is how a scrolled
   list stays inside its window. The paint pass is not incremental: an unchanged
   screen emits the same shapes as the one before. It costs nothing only because
   the shell stops calling the pipeline once it sees nothing moved.
4. **Draw.** A backend turns that list onto a real surface, reports clicks and
   keystrokes back, answers how big the surface is and reads an image file. One
   over SDL3 ships here, a headless recorder exists for tests, and writing your
   own means filling in [eight functions](docs/backend-abi.md).

## Words this repository uses

| Word | What it means here |
| --- | --- |
| **node** | One box in the tree. An integer, not a pointer, so an out-of-date handle fails safely. |
| **ctx** | Everything a frame is allowed to keep: its memory, its state, its events. |
| **arena** | Memory handed out in one block and thrown away in one block, so a frame cannot leak. [Region-based allocation](https://en.wikipedia.org/wiki/Region-based_memory_management). |
| **state** | What a named box keeps across frames: a click count, a scroll offset, a typed buffer. It lives in `ctx`, not in your node, because your node is thrown away every frame. |
| **backend** | The only part that knows about windows, GPUs and pixels. Everything above counts logical points. |
| **ABI** | The exact list of functions two separately-compiled pieces of code agree on, plus the version of that agreement. [More.](https://en.wikipedia.org/wiki/Application_binary_interface) |
| **unit test** | A small check of one layer, run without a window. |
| **conformance** | Checking our flexbox against someone else's answer, not against our own. [Details.](docs/conformance.md) |
| **golden test** | Saving a whole frame's shape list as a text file, then failing if a change writes a different one. You review the diff. [Characterization test.](https://en.wikipedia.org/wiki/Characterization_test) |
| **bench** | Timing how long a big screen takes, so a slowdown cannot arrive unnoticed. [Details.](docs/bench.md) |

Names follow the code: functions and types read lowercase with a `cf_` prefix
(`cf_frame_run`, `cf_style`), macros and enum values read uppercase with the
same prefix (`CF_PT`, `CF_ROLE_BUTTON`). Nothing carries a `_t` suffix — the
type is `cf_style`, not `cf_style_t` — because the prefix already says where
the name lives and a second marker only lengthens every call site.

## Using it as a library

```sh
cmake --install build/dev --prefix /where/you/want
```

```cmake
find_package(cform-ui REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE cform::cf_app)   # or cform::cf_layout alone
```

Embedding the sources instead with `add_subdirectory` is the same: the layers
carry their own C17 requirement and the same `cform::` target names, so a host
that states neither still builds against them.

`pkg-config --cflags --libs cform` covers builds that don't use CMake. The SDL
backend isn't installed on purpose — a program that opens a window already links
SDL itself.

### Build options

| Option | Default | Does |
| --- | --- | --- |
| `CFORM_BUILD_TESTS` | `OFF` (`dev` turns it on) | the 36 checks `ctest` runs |
| `CFORM_BUILD_EXAMPLES` | `OFF` (`dev` turns it on) | `gallery` and `layout_only` |
| `CFORM_WITH_SDL_BACKEND` | `OFF` | the SDL window backend, and the SDL download |
| `CFORM_WITH_SANITIZERS` | `OFF` | memory and undefined-behaviour checkers |

`--preset dev` is Debug with tests and examples on; `--preset release` is the
same graph optimised, which is where the timing numbers come from.

### What the library guarantees

**Thread confinement.** A `cf_ctx` is not safe to touch from more than one
thread at a time, and neither are the arena or interner it owns. A host may run
several contexts in parallel, one per thread; it must not share a context
across threads and call it from both without external synchronisation. The log
sink is process-global and writes through one pointer: installing it while
another thread logs is a data race.

**Supported compilers and platforms.** CI compiles and tests Debug and Release
on Windows, macOS and Linux. The default compiler on each (MSVC, Apple Clang,
GCC) is the one exercised; the ASan/UBSan job adds Clang on Linux. Emscripten
builds a WebAssembly gallery but does not run tests there. A C17-conforming
compiler is the only language requirement; no GNU or vendor extensions appear
in portable code.

**Lifetime.** A built declaration (`cf_node`) is borrowed only for the duration
of one `cf_diff` call — after it returns the retained tree is independent.
Public rects, view arrays and the command list live in the frame arena and are
valid until the next `cf_frame_begin`. An interned name lives until the
interner is destroyed, and a state block until it is released or the context is
freed.

### In a browser

The tab runs the same `examples/gallery.c` through the same SDL backend — SDL's
WebGL path takes the draw calls and its event queue takes the clicks back, so no
line of the example is browser-specific. What differs is the packaging: the
`web` preset compiles through Emscripten's toolchain, preloads the font into a
virtual filesystem (a tab has no path to open) and links `ASYNCIFY`, which lets
the run loop yield at each present instead of freezing the tab.

CI reaches the same graph without the preset, because it installs the SDK into a
temporary directory rather than the environment the preset reads `EMSDK` from:

```sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release -DCFORM_WITH_SDL_BACKEND=ON -DCFORM_BUILD_EXAMPLES=ON
cmake --build build-wasm
python -m http.server 8080 --directory build-wasm/bin   # then open http://localhost:8080/gallery.html
```

Serving it is required — a tab won't load a `.wasm` from a `file://` path. The
page draws into the canvas `examples/web/shell.html` states (960×600): resizing
the browser window scales that picture rather than re-flowing it, so the
drag-the-edge story belongs to the desktop gallery.

### The three scripts

CI runs all three, so they aren't optional. `check_includes.py` fails a layer
that `#include`s a neighbour it isn't allowed to link. `amalgamate.py` bundles
the layout headers into one droppable `cf_layout.h`
(`cmake --build build/dev --target cf_amalgamate`). `gentest.py` rebuilds the
Yoga test corpus from a checkout in `.vendor/yoga`, and is only needed to
re-sync those tests.

## Contributing

Same rules CI checks: `clang-format --dry-run --Werror` clean on the files you
touched ([.clang-format](.clang-format)), one logical [Conventional
Commit](https://www.conventionalcommits.org/) each, allocation only through
`src/base/`, and no platform header outside `src/backend/`.

Distributed under the MIT License. See [`LICENSE`](LICENSE).
