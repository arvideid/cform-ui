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
| a graphics backend (Skia, ANGLE) | a `cf_backend`: [eleven functions](docs/backend-abi.md) |
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
   unchanged screen and it runs the full build-diff-layout-paint. A host that owns
   its loop makes that test for itself, which is what
   [`examples/host_loop.c`](examples/host_loop.c) is for. Numbers, not a claim:
   [bench.md](docs/bench.md).
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
  still move: the backend contract is at ABI 12 and has been broken on purpose.
- **Seven widgets**: label, button, field, list, scroller, image, checkbox. No
  radio, slider, dropdown, tab, menu, tooltip, dialog or table. Building the one
  your app needs is expected and is a few dozen lines.
- **One struct is the design system.** `cf_theme` holds a page's three surfaces,
  its three text tones and one variant set per widget family, and `cf_use_theme`
  names which table a context styles from. A resolved style lives in the frame
  that computed it, so naming a second one costs no invalidation: the frame after
  the naming is drawn wholly out of it, and a widget that started its declaration
  from its family's resting box takes that family's density with it. Chapter
  seventeen of the gallery swaps between the shipped system and a darker, denser
  one held in one bit; `theme_resolve` checks the other edge — a table that says
  *less* than the one it replaced has to leave the declaration standing.
- **Text is Latin-shaped only.** Wrapping honours spaces, hyphens, ideographs and
  the text's own breaks, and a font stack falls back glyph by glyph — the line is
  drawn in the same faces it was measured in, one command per run of it. There is
  no right-to-left, no bidirectional text, no shaping for Arabic or Indic, no
  justification, no ligatures and no variable fonts.
- **Accessibility is a tree, not a screen reader.** `cf_semantic_visit` hands a
  caller one typed row for every box that declared what it is: the role
  (`cf_access`, separate from the theme family the painter reads, so a box painted
  as a plain bar can still be announced as a button), the words it is named by —
  the name the build stated, or the label it carries — the bytes a field holds and
  which run of them is selected, the states a person is in (disabled, focused,
  pressed, hovered) and only the asks the frame can really serve. Each row carries
  the store's generational id and the live-region level covering it: a build marks
  an area once with `cf_node.announce` — polite waits for the end of a sentence,
  assertive says it now — and the walk hands that level to every box reported
  inside it, so a bridge watches the rows carrying news instead of the whole page.
  Nothing allocates and no name is cut. `cf_semantic_export` stays what it was —
  bounded text for a developer, not a protocol. What is missing is the other side:
  nothing reaches Narrator, VoiceOver or AT-SPI, there is no ARIA mapping and no
  hit-target or contrast checking. A `cf_label` in this project is a box that
  draws glyphs; it names the box it sits inside rather than a sibling beside it,
  and a caption a person reads next to a field is only in the announcement once
  the build says so — `cf_node.described_by` and `controls` carry the key of the
  other box, the walk resolves each name to an identity in the frame it reads, and
  a box that is not declared this frame answers no box rather than the one that
  left.
- **Keyboard focus is declared, not assumed.** A box takes the keyboard when it
  says so — `cf_node.focusable`, which `cf_button`, `cf_field` and `cf_checkbox`
  state for their callers. A press focuses the box it landed on, **Tab** and
  shift+**Tab** walk the
  declared boxes in document order, and **Enter** or the space bar asks the focused box
  to do its job — with no chord held, since **Ctrl**+**Enter** is a binding the page made
  rather than the plain ask. A dialog keeps the keyboard by answering **Tab** itself, and a
  box that stops being reachable hands it back: when a diff retires, hides or switches off
  the focused box, the keyboard goes back to the box it moved away from, and to nobody when
  that one is gone too. The field holding the keyboard is also what asks the device for a
  way to type: the frame's
  `cf_frame_takes_text` answer reaches `set_text_input`, so a screen keyboard or an
  input method is offered while a box can take the words and not while it cannot.
- **A switch's bit belongs to the page.** `cf_checkbox` is handed the `bool` your
  build already keeps and flips that, storing no copy a rebuild would have to be
  told about: the frame after a click declares the box from the bit you read. What
  on and off look like is the theme's `checked` variant, laid under the pointer's
  own marks so a hovered switch still reads as hovered, and a box declared with no
  handler still moves. **Enter** and the space bar ask it the same question a
  release does, and a box the build switched off answers neither hand and stays out
  of the ring. A radio, slider or progress bar is the same shape around a value
  rather than a bit; none of them is here yet.
- **A box a key names can be reached.** `cf_slot_named` answers a key a build
  declared with the slot the retained tree carries that box at, and `cf_reveal`
  brings it inside every pane which scrolls it — the move **Tab** makes when the
  ring lands on a control below the fold. A build runs before the frame has been
  measured, so an ask made there waits for the layout pass that measures it and
  the frame after shows it, while a handler running between frames moves the page
  at once. Every heading on the documentation site is keyed by the slug of its
  title, so an address with a
  [fragment](https://developer.mozilla.org/en-US/docs/Web/URI/Reference/Fragment)
  ending in `#backend` lands on that heading rather than at the top of the page.
- **Copy, cut and paste need a clipboard from the host.** The field answers
  **Ctrl** or **Cmd** with **C**, **X** and **V** through `cf_clipboard`, a read/write
  pair the shell names in its `cf_app_config`; `cf_clipboard_sdl()` is the desktop's,
  and a host that names none leaves those keys asking for nothing.
- **Undo is a ring of copies, not a diff.** The same modifier with **Z** walks back
  over what the field's own keys took and **Y** puts it forward again — whole copies
  of the buffer with the caret and the selection where you left them, kept in the
  field's own state block at sixteen images inside four KiB, so a long value walks
  back through fewer steps and one too large to copy twice keeps none.
- **A character is what a reader sees, not a byte.** The field steps, deletes, jumps
  words and selects over whole grapheme clusters — the accented letter a
  mark was written onto, the family three pictures tie together with zero-width
  joiners, the flag two regional indicators make — on the subset of Unicode's boundary
  rules (UAX #29) that `cf_text_next_cluster` and `cf_text_prev_cluster` implement. A
  byte that decodes to nothing counts as a character of its own, so a truncated paste
  still steps. What the tables leave out is Hangul, whose syllables are spelled with
  separate jamo letters, and the few marks that attach forwards. Wrapping picks its
  points from the spaces, hyphens and ideographs it knows rather than from these
  boundaries, so a word too long for its box can end between a letter and its mark.
- **A word is a step, and the whole value is a chord.** **Ctrl** or **Cmd** with the
  arrows moves the caret over the run it stands in — letters and digits, anything
  that is neither, or the blanks ahead and the run after them — and **Shift** along
  with them selects that run instead. **Ctrl** or **Cmd** with **A** holds every byte
  the buffer carries, for the next key to replace. The run is named by its first byte,
  so anything above ASCII is neither blank nor letter: an unspaced CJK run steps as one
  word and a pasted non-breaking space ends one.
- **A full buffer says so.** The field never inserts half of a UTF-8 sequence, so a run
  that will not fit whole is refused outright. `cf_text_edit.refusals` counts the runs
  it turned away and chapter two's line prints it, so a page can say "there is no room
  left" instead of watching a caret that stopped moving.
- **A field's Enter belongs to the page — until you say otherwise.** The ask a click is, the
  key can be: an unmodified **Enter** runs the focused box's own `activate` handler, so a page
  puts its save code on the field node and chapter two copies the buffer out and prints it. The
  space bar a field holds the keyboard under is the character the device sends beside it,
  so the box keeps that key — typing a word never reads as a submission.
- **The field's mode is the build's word, not a reading of the box.** `cf_field` takes
  `CF_FIELD_SINGLE` or `CF_FIELD_MULTILINE`, and the mode says what the value asks of its box
  and what three keys are. A multiline field breaks a line on **Enter** and asks the page to
  save nothing, takes the newline a paste carries, and answers **Up** and **Down** by stepping
  the rows its box paints; a single-line field keeps one row's height, leaves **Enter** for the
  page and lets both arrows climb. Nothing is inferred from a rendered height: chapter two
  shows a memo two rows tall because it was declared so, and the golden
  `caret_below_the_fold` pins the walk sliding until a caret below the fold is inside the box.
- **A key that moved the value says so.** Every edit the field's own keys make runs the
  node's `change` handler once, on the event that moved the bytes: an arrow that walks the
  caret, a **Ctrl**+**A** that holds the whole value, and a run the buffer turned away all
  leave it shut. Chapter two counts the reports on its fourth line. There is no caret
  blink, and an input method's settled words arrive as ordinary text.
- **An input method's run is shown, not taken.** `CF_EVENT_COMPOSITION` hands over the whole
  text still being decided; the field splices it into the rows it lays, between the offsets of
  the selection it began on, under the theme's marked tone — and keeps it in the field's own
  state block rather than in the buffer the page owns. A run is the keyboard's: while one
  stands, the keys the method already spent (backspace, the arrows, **Enter**) edit nothing, an
  empty run gives it up whole, and the frame that moves the keyboard to another box takes it
  back — by a key, by a press, or by the box it was typed into leaving the page. Beside it, the
  shell reports the run's own row through `set_candidate_rect`, the same numbers the underline
  is laid from and read against the box this frame laid under it, so the platform's window lands
  under the letters rather than wherever the desk decides and walks with a page that scrolls.
  Chapter fifteen lays a strip of its own from those numbers; a real desktop's input method fills
  the place beside it.
- **Images are BMP, loaded once.** The reference backend reads BMP through SDL's
  own loader and uploads RGBA pixels; PNG, JPEG and WebP are a dependency this
  repository hands to the host, and the `load_image` slot is where your decoder
  goes. Retiring an upload is the host's call through `release_texture`, which
  hands the number back to the next image: nothing in the engine decides that a
  picture is finished with.
- **Three pointer shapes, and the node layer picks them.** The shell hands the
  frame's answer to `set_cursor` when it moves: a hand over a box that acts, an
  I-beam over a field, the arrow elsewhere. Nothing declares a shape — there is no
  cursor on `cf_style`, no wait or not-allowed shape and no custom image — so a
  backend that has one shape is only missing the ones the framework never asks for.
- **A run ends two ways, and one of them is the page's.** A window's close button
  reaches the shell as `CF_EVENT_QUIT`, which never arrives at the tree. A control
  inside the page has no event to send, so it asks through its context —
  `cf_request_quit` — and `cf_app_run` leaves after the frame that answered the press,
  so the click the user made is the one they see land. The ask stands until the
  context is freed, and nothing in the framework destroys a window: whether the ask
  means "exit" or "dismiss this dialog" is the host's call, which is why the gallery's
  last chapter ends the program and a harness driving the pipeline itself just reads
  the ask.
- **Missing CSS, in full:** `calc()`, container queries,
  grid named lines and areas, and every transition and animation but the
  ones you write in C against the frame's elapsed seconds.
- No drag-and-drop building, no theming tool, no loading a font from memory
  (`cf_font_sdl` takes a path). Device pixels stop at the
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
ctest --preset dev                                # run the 39 tiers and the 4 benches
build/dev/bin/gallery                             # open the demo window
```

`gallery` is a scrolling page that demonstrates every widget and reacts to the
mouse: type in the field, drag the scrollbars, watch the box the frame clock
animates. Press **F12** in it to see the frame's numbers on screen, with the words
the walk would announce for the box holding the keyboard — Tab across chapter
twelve's three controls to watch that line change. Drag its edge: the window is
resizable, and every line of text re-breaks against the width the shell hands the
page.

The first configure downloads [SDL3](https://github.com/libsdl-org/SDL) (the
library that opens a window) and takes a few minutes. Without
`-DCFORM_WITH_SDL_BACKEND=ON` nothing is downloaded at all, because the engine
never needs a window.

The same page runs in a browser tab, through
[Emscripten](https://emscripten.org/):

```sh
cmake --preset web                                # needs EMSDK in the environment
cmake --build --preset web
```

The build emits one self-contained `build/wasm/bin/gallery.html` - the JS, the
wasm and the preloaded font are base64-embedded - so a double-click opens it.
Serving the directory over HTTP still works and is what CI does:

```sh
python -m http.server 8033 --directory build/wasm/bin
```

## What using it looks like

This is a complete program: a button that prints when you click it.

```c
#include <stdio.h>

#include "cf/app.h"     /* the run loop          */
#include "cf/build.h"   /* CF_COL, CF_ROW        */
#include "cf/widget.h"  /* cf_label, cf_button   */

/* What the run owns and a frame does not: the colors, the face, the stack a label
   measures through. A local main hands to every frame, not a file-scope variable a
   second window in the same process would have to share. */
typedef struct hello {
    const cf_theme *theme;
    cf_font font;
    cf_text_stack fonts;
} hello;

static void say_hello(cf_ctx *ctx, cf_id node, void *user_data) {
    (void)ctx, (void)node, (void)user_data;
    printf("hello\n");
}

/* Runs whenever cform needs to know what the screen should look like, and is handed
   the pointer the program gave the run. */
static cf_node build(cf_ctx *ctx, void *user_data) {
    const hello *const host = user_data;
    const cf_node text = cf_label(ctx, cf_string_of("press me"), &host->fonts,
                                 host->theme->on_ink);
    const cf_node button = cf_button(ctx, "greet", say_hello, NULL, &text, 1u);

    return CF_COL(ctx, host->theme->page, button);
}

int main(void) {
    hello host = {.theme = cf_theme_default()};
    host.font = cf_font_sdl("Lato-Regular.ttf", 14.0f);
    host.fonts = (cf_text_stack){.fonts = &host.font, .count = 1u};

    cf_app_run(&(cf_app_config){.build = build, .build_user_data = &host,
                                .theme = host.theme, .fonts = &host.fonts,
                                .backend = cf_backend_sdl(&(cf_sdl_config){
                                    .title = "hello", .width = 480u, .height = 320u})});
}
```

That is the `<div>` above in C: a column, and inside it a button holding a label.
`cf_app_run` opens the window and keeps calling `build` as often as the screen
needs, handing it the pointer the config kept - which is how `host` reaches the
page. The string `"greet"` is the button's name: it is how cform recognises the
same button from one frame to the next, so the count of clicks, the scroll
position and the keyboard focus survive a rebuild.

[`examples/gallery.c`](examples/gallery.c) is the same API stretched over every
widget, [`examples/layout_only.c`](examples/layout_only.c) is the layout engine as
a program that links nothing else, and
[`examples/host_loop.c`](examples/host_loop.c) is the framework without the
framework's loop: the program keeps its own `cf_ctx`, fills `cf_backend` with its own
submit, present, surface and service answers, and calls `cf_frame_run` when it decides
a frame is worth running. That route opens no window and reads no font file, so it
builds and is asserted over (`host_loop_headless`) wherever the library does.

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
   keystrokes back, answers how big the surface is, reads an image file, puts the
   shape the frame asked for under the pointer, opens or shuts the service that
   takes written text and moves an input method's answer box. One over SDL3 ships
   here, a headless recorder exists for tests, and writing your own means filling
   in [eleven functions](docs/backend-abi.md).

## Words this repository uses

| Word | What it means here |
| --- | --- |
| **node** | One box in the tree, named by a number rather than a pointer, so an out-of-date handle fails safely. Two numbers: see **slot** and **id**. |
| **slot** | Where a box sits in this frame's arrays, and the number the rects and the view are addressed by. The store hands a retired one to the next box the build declares, so it is only good until the next rebuild. |
| **id** | The reference a caller keeps: a slot plus a version, so it either still names the same box or names nothing. `cf_node_id` turns this frame's slot into one, `cf_node_slot` reads the place back. |
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
| `CFORM_BUILD_TESTS` | `OFF` (`dev` turns it on) | the test tiers `ctest` runs — 37 checks, 39 once the SDL backend is on |
| `CFORM_BUILD_EXAMPLES` | `OFF` (`dev` turns it on) | `layout_only` and `host_loop`, and beside the SDL backend `gallery` and `site` |
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
```

The page fills the browser viewport. `examples/web/shell.html` pins the canvas'
CSS box to the layout viewport with `position: fixed; inset: 0`, and SDL's
Emscripten video driver reads that box on every resize and matches the drawing
buffer to it, so the layout re-flows against whatever the tab is rather than
scaling a fixed picture. The same script asks the browser for a WebGL context
that keeps its drawing buffer between composites: SDL asks for one that does
not, and a buffer a browser has cleared shows as black on any frame the shell
presents without redrawing - the idle frame, the wait for the next event. The
build passes `-sSINGLE_FILE=1`, which base64-embeds the wasm and the font into
the emitted JS, so a `file://` tab can open the page directly - without it, the
Same-Origin Policy refuses to fetch the sibling `.wasm` and `.data` and the page
never boots.

### The three scripts

CI runs all three, so they aren't optional. `check_includes.py` fails a layer
that `#include`s a neighbour it isn't allowed to link. `amalgamate.py` bundles
the layout headers into one droppable `cf_layout.h`
(`cmake --build build/dev --target cf_amalgamate`). `gentest.py` rebuilds the
Yoga test corpus from a checkout in `.vendor/yoga`, and is only needed to
re-sync those tests.

## Versioning

[`include/cf/version.h`](include/cf/version.h) states the version once, and
CMake reads it from there rather than carrying a second copy. The numbers mean
what [docs/versioning.md](docs/versioning.md) says they mean — a source and
rebuild promise inside a MAJOR, with the backend ABI as its own axis — and
`tools/check_api.py` refuses a header change the version does not cover.
Whoever cuts a release follows [docs/releases.md](docs/releases.md); whoever
maintains it follows [docs/maintenance.md](docs/maintenance.md).

## Contributing

Same rules CI checks: `clang-format --dry-run --Werror` clean on the files you
touched ([.clang-format](.clang-format)), one logical [Conventional
Commit](https://www.conventionalcommits.org/) each, allocation only through
`src/base/`, and no platform header outside `src/backend/`.

Distributed under the MIT License. See [`LICENSE`](LICENSE).
