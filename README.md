# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A retained-mode, data-oriented UI framework in C17. You declare a tree of nodes;
systems sweep parallel arrays to resolve styles, lay the tree out and emit draw
commands. No DSL, no C++, no per-node vtables.

Status: `1.0.0` in development, every layer built. The 0.2 `UI_*` API on `main` is
not compatible and has no aliases — `cf_` names are the contract from here.

## Build and run

```sh
cmake --preset dev && cmake --build --preset dev
ctest --preset dev                       # unit, conformance and golden tiers
ctest --preset dev -L bench              # the throughput rigs
build/dev/bin/gallery                    # the one program that opens a window
```

CMake **3.25**+, a **C17** compiler and [Ninja](https://ninja-build.org/). The
layers link libc alone; SDL3 and SDL3_ttf are fetched only when the backend is
asked for, so a headless configure touches no network:

```sh
cmake --preset dev -DCFORM_WITH_SDL_BACKEND=ON
```

`dev` is Debug with tests, examples and a compile database on; `release` is the
same graph in Release, which is where the bench numbers come from.

| Option | Default | Turns on |
| --- | --- | --- |
| `CFORM_BUILD_TESTS` | `OFF` | the unit, conformance, golden and bench tiers |
| `CFORM_BUILD_EXAMPLES` | `OFF` | the programs under `examples/` |
| `CFORM_WITH_SDL_BACKEND` | `OFF` | `cf_backend_sdl`, and the SDL fetch |
| `CFORM_WITH_SANITIZERS` | `OFF` | ASan and UBSan; fatal when the compiler cannot link them |

`bin/` holds the two programs a person runs. The 35 tier binaries build into
`build/dev/tests/` and ctest drives them by target.

## The layout floor

`cf_layout` is usable on its own today: flexbox as CSS Flexible Box Layout §9
specifies it — four directions, wrap, order, grow, shrink, basis, auto margins,
`min`/`max` clamps, percent resolution, all of `justify-content`, `align-content`,
`align-items`, baseline alignment and `position: absolute` children — plus a grid
subset. Content the engine cannot see into arrives through a `cf_measure_fn`, so
the library links nothing else.

```c
#include "cf/layout.h"

/* A row with a 12-point gap: a fixed sidebar, then a child that takes what is
 * left. cf_style_default() carries the two CSS initial values the zero state
 * cannot express - margins of 0 rather than auto, and a flex-shrink of 1. */
cf_style row = cf_style_default();
row.column_gap = CF_PX(12.0f);

cf_style sidebar = cf_style_default();
sidebar.width = CF_PX(120.0f);

cf_style content = cf_style_default();
content.flex_grow = 1.0f;

const cf_style styles[] = {row, sidebar, content};
const uint32_t child_start[] = {0u, 2u, 2u};
const uint32_t child_slots[] = {1u, 2u};
cf_rect rects[3];

cf_arena scratch;
cf_arena_init(&scratch, 16384u);

cf_layout(&scratch,
          &(cf_tree){.styles = styles, .child_start = child_start,
                     .child_slots = child_slots, .node_count = 3u},
          0u, (cf_available){.width = CF_PX(400.0f), .height = CF_PX(200.0f)},
          (cf_point){0.0f, 0.0f}, rects);
```

`cf_layout` is a pure function of its tree and its offer: it writes one absolute
border-box rect per node, allocates only from the caller's arena, and reads an
`auto` axis as the question "what extent do the children need". A declared size is
the border box — `content-box` is not inherited on purpose. Lengths are tagged
(`CF_AUTO`, `CF_PX`, `CF_PERCENT`, `CF_FR`), so `0` never has to be read as either
unset or zero. The measure contract is a cache's contract: the answer must be a
pure function of the node, the intrinsic kind and the offer, because one layout
serves any repeat of that triple from the table it filled first.

The grid subset is the whole contract: `minmax()` over `CF_AUTO`/`CF_PX`/
`CF_PERCENT`/`CF_FR`, `cf_tracks_repeat()` for `repeat()`, items placed by a
1-based start line plus a span, dense auto-placement for items naming no lines,
and rows an item overflows into becoming content-sized tracks. It reads none of the
alignment enums — leftover space goes to the tracks that named no ceiling, which is
CSS's `normal` on both axes.

[`examples/layout_only.c`](examples/layout_only.c) is the same engine as a program
linking `cf_layout` and nothing else, and
[`tools/amalgamate.py`](tools/amalgamate.py) packs it into one header a project
drops beside its own source; a test compiles that header and nothing from
`include/cf`, so the promise stays checked:

```sh
cmake --build build/dev --target cf_amalgamate   # -> build/dev/amalgam/cf_layout.h
```

## The whole stack

Above the layout floor, a page is a function that returns a tree. The shell owns
the loop and the frame arenas; the backend owns pixels and density.

```c
#include "cf/app.h"
#include "cf/build.h"
#include "cf/widget.h"

static const cf_theme *theme;
static cf_text_stack fonts;

static void on_press(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)ctx, (void)slot, (void)user_data;
    printf("pressed\n");
}

static cf_node build(cf_ctx *ctx) {
    const cf_node label = cf_label(ctx, cf_string_of("press me"), &fonts, theme->on_ink);
    const cf_node press = cf_button(ctx, "press", on_press, NULL, &label, 1u);

    return CF_COL(ctx, theme->page, press);
}

cf_app_run(&(cf_app_config){.build = build, .backend = cf_backend_sdl(&(cf_sdl_config){
                                .title = "cform", .width = 480u, .height = 320u}),
                            .theme = theme, .fonts = &fonts});
```

`cf_app_run` drains the backend's events into the dispatch, rebuilds only when a
signature moved, and presents without repainting when nothing woke — an undamaged
frame lays out nothing, emits zero commands and allocates zero bytes. `F12` opens
the inspector: node and dirty counts, commands, the page's rect, the frame's cost.
[`examples/gallery.c`](examples/gallery.c) is every widget on one page, and the
same source under Emscripten is the WebAssembly playground (CI pins the SDK in
`.github/workflows/ci.yml`; serve the output over HTTP, since a tab will not fetch
a `.wasm` over `file://`).

## Layers

| Target | Owns | Reaches |
| --- | --- | --- |
| `cf_base` | arena, generational store, interner, hash, log sink | libc |
| `cf_style` | `cf_style`, length and box math | `cf_base` |
| `cf_theme` | the default palette, one `cf_theme` value | `cf_style` |
| `cf_layout` | `cf_tree`, flex, grid, absolute, intrinsic measurement | `cf_style`, `cf_base` |
| `cf_text` | UTF-8 decode, break opportunities, line walking, measurement | `cf_base` |
| `cf_paint` | the command stream: fills, borders, text, images | `cf_layout`, `cf_text` |
| `cf_node` | context, keyed diff, state, input, focus, scroll, tweens | `cf_layout`, `cf_paint` |
| `cf_widget` | label, button, field, list, scroll, image declarations | `cf_node`, `cf_paint`, `cf_text` |
| `cf_app` | the run loop, the damage rule, the inspector | `cf_widget` |
| `cf_backend_headless` | a recorder that keeps the submitted stream | `cf_paint` |
| `cf_backend_sdl` | window, draw, rasterisation, event pump | `cf_paint`, `cf_text`, SDL3 |

One CMake target per layer is the dependency rule, and
[`tools/check_includes.py`](tools/check_includes.py) checks the include graph
against it. Two edges carry the design: SDL appears in exactly one target, and
device pixels stop there, so every layer above counts logical points.

A node is a slot in a `cf_store` addressed by a generation-checked `cf_id`, and
identity inside the tree is `(depth, key, ordinal)` rather than a pointer, so a
stale handle reads `NULL` instead of editing the next occupant of a recycled slot.
`cf_ctx` owns everything a page keeps and takes no lock — one context per thread —
and the installed log sink is the only process-wide state in the library.

## Embedding

`cmake --install build --prefix <prefix>` lays down the ten headless archives,
`include/cf/`, a CMake package config and a `cform.pc`. Each target carries its own
dependencies, so `cform::cf_layout` is the layout floor and nothing more:

```cmake
find_package(cform-ui REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE cform::cf_app)   # or cform::cf_layout alone
```

`pkg-config --cflags --libs cform` serves a build that does not use CMake.
`cf_backend_sdl` is deliberately not installed — its interface names `SDL3::SDL3`,
a target the fetch creates inside this tree, and a host that opens a window already
owns an SDL link line. Write the platform's own against
[`docs/backend-abi.md`](docs/backend-abi.md): five function pointers, and
`src/backend/headless/record.c` is a legal two-slot skeleton.

## Testing

Four tiers, all headless — nothing here looks at the screen.

- **unit** — one binary per layer, linking that layer and nothing else, so a reach
  outside its dependencies fails at link. A new rule is usually a row in a data
  table rather than a function.
- **conformance** — 485 of Yoga's flexbox fixtures, ported by `tools/gentest.py`
  and scored against a floor that only moves up.
  [`docs/conformance.md`](docs/conformance.md) publishes the score and the misses.
- **golden** — whole frames through the recorder backend, diffed as a command
  stream. `golden_cases --update` rewrites a snapshot, so a draw that moved is data
  a reviewer reads.
- **bench** — the 10,000-node rigs. They assert geometry and command counts and
  print timing; [`docs/bench.md`](docs/bench.md) publishes the numbers a change
  moves.

The renderer is verified by running `gallery` and looking at it — never by mocking
the backend or asserting pixels. A change that draws differently says what to look
at.

## Not implemented

Writing modes and right-to-left, logical properties, `aspect-ratio`, container
queries, `calc()`, grid named lines and areas, `display: none`, `overflow`,
`box-sizing: content-box`, bidirectional text, variable fonts. None are in the
vocabulary and no field pretends to accept them; anything on this list lands in
`include/cf/` first and in the layout files second.

## Conventions

C17 with no extensions, `snake_case` with the verb first, `cf_function` /
`CF_Type` at the API edge, allocation only through `src/base/`, and comments that
say *why* — an invariant, a lifetime rule, a spec step. The compiler, the standard,
the include paths and the warnings are stated once in `CMakeLists.txt`, and clangd,
CI and your terminal read the same `compile_commands.json`. `.clang-format` and
`.clang-tidy` are not suggestions. Commits are
[Conventional Commits](https://www.conventionalcommits.org/), one logical change
each.

Distributed under the MIT License. See [`LICENSE`](LICENSE).
