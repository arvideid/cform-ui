# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A retained-mode, data-oriented UI framework in C17. You declare a tree of
nodes; systems sweep parallel arrays to resolve styles, lay the tree out and
emit draw commands. No DSL, no C++, no per-node vtables.

> **Status:** `1.0.0` in development. The arena, the style vocabulary and the
> layout engine are built and conformance-tested; text, paint, the node tree,
> widgets and the backend do not exist yet. The 0.2 frame API on `main` is not
> compatible and has no aliases — `cf_` names are the contract from here.

## What is usable today

`cf_layout` is the standalone engine: flexbox as CSS Fragment 3 §9 specifies it
— direction, wrap, order, grow, shrink, basis, auto margins, `min`/`max`
clamps, percent resolution, cross alignment, align-content, and children placed
out of the flow with `position: absolute`. Content the engine cannot see into —
text, an image, a foreign subtree — arrives through a `cf_measure_fn` callback,
so the library links nothing but libc.

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
          &(cf_tree){
              .styles = styles,
              .child_start = child_start,
              .child_slots = child_slots,
              .node_count = 3u,
          },
          0u, (cf_available){.width = CF_PX(400.0f), .height = CF_PX(200.0f)},
          (cf_point){0.0f, 0.0f}, rects);

/* rects[1] is 0, 0, 120, 200 - rects[2] starts at 132 and fills the rest. */
```

The tree is slot-indexed data: styles, measures and children are parallel
arrays, so a subtree can be moved, serialized or replayed without chasing
pointers. Scratch is a caller-owned arena that each node hands back before
descending, which keeps layout allocation-free across a frame.

## Requirements

- CMake **3.25**+, a **C17** compiler (GCC, Clang or MSVC) and
  [Ninja](https://ninja-build.org/), which the presets use
- Nothing else. There are no third-party dependencies to fetch.

## Build and test

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`dev` is a Debug build with a compile database and the test suite on;
`release` is the same graph in Release. Sanitizers are opt-in and need a
toolchain that ships the ASan/UBSan runtime:

```sh
cmake --preset dev -DCFORM_WITH_SANITIZERS=ON
```

## Layout

```
include/cf/     Public headers — the contract; changes here are breaking
src/base/       Arena, pool, handle, intern, hash, log (libc only)
src/style/      Length and box resolution, style defaults
src/layout/     Flex, grid, absolute, intrinsic measurement, measure callback
src/*/internal.h  Types shared across src/ but not published
tests/unit/     One file per layer, scenario-named cases
tools/          The include-graph check CI runs
```

Dependency direction is enforced by one CMake target per layer and by
`tools/check_includes.py`; a layer may only reach what its target links.
`cf_layout` plus `cf_style` and `cf_base` compile and pass conformance with no
other layer present — that is the standalone-engine product, and it is the part
you can use today.

## Testing

Layout math is testable without a window, so it is tested that way.
[`tests/unit/layout_cases.c`](tests/unit/layout_cases.c) runs a corpus of
scenarios kept as data — a tree of styles, the offer the pass starts from, and
the border box every node must land in — and prints one line per case, so a
failure names the node and shows both rects. Add a row to
[`tests/unit/layout_cases_data.h`](tests/unit/layout_cases_data.h) rather than
a function when a rule needs covering. Assertions come from the tiny,
dependency-free [`tests/cform_test.h`](tests/cform_test.h); the harness is
small on purpose.

Renderer behaviour is verified by running the example and looking at it, never
by mocking the backend or asserting pixels.

## Editor and developer experience

**CMake is the single source of truth.** The compiler, C standard, include
paths and warning flags are defined once in `CMakeLists.txt`; clangd, the IDE,
CI and your terminal all read the same `compile_commands.json` it produces, so
nobody sees different diagnostics on different machines.

Committed, shared configuration — `.editorconfig`, `.clang-format`,
`.clang-tidy`, `.clangd`, `CMakePresets.json`, `.vscode/settings.json`,
`.vscode/tasks.json` and `.vscode/extensions.json`. Personal and generated
files (`build/`, `compile_commands.json`, `.vscode/launch.json`,
`CMakeUserPresets.json`) are git-ignored.

1. Install the recommended extensions when prompted; the essential one is
   **clangd**, which can fetch its own server binary on first launch.
2. Run **CMake: Configure (dev)** once. This writes
   `build/dev/compile_commands.json`.
3. clangd reads that database through `.clangd`, so the Outline view,
   go-to-definition and inline warnings match the real build.
   `.vscode/settings.json` disables the cpptools IntelliSense engine so it does
   not fight clangd.

`Ctrl+Shift+B` builds; **Test: ctest (dev)** builds and runs the suite.
Everything goes through presets, so the IDE and the command line never drift
apart.

## Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/):
`<type>(<scope>): <short description>`, with types `feat`, `fix`, `test`,
`docs`, `build`, `ci`, `refactor`, `chore`. One commit is one coherent change —
no "update", "stuff", or unrelated changes squashed together. To use the
bundled template locally (opt-in, not forced on anyone):

```sh
git config commit.template .gitmessage
```

## Contributing

Pull requests are welcome; open an issue first for anything substantial. CI
gates the formatting and static-analysis checks over every layer, the
include-graph rule, a sanitizer build, and Debug and Release compiles on Linux,
Windows and macOS — keep those green and `ctest` passing. Report security
problems as described in [`SECURITY.md`](SECURITY.md). A PR template is
provided.

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
