# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A retained-mode, data-oriented UI framework in C17. You declare a tree of
nodes; systems sweep parallel arrays to resolve styles, lay the tree out and
emit draw commands. No DSL, no C++, no per-node vtables.

> **Status:** `1.0.0` in development. Every layer is built: the arena and pool,
> the style vocabulary, flexbox and grid layout, text measurement, the paint
> command stream, the node tree with diffing, input, scroll and tweens, the
> widgets, themes and the app shell over an SDL3 backend. The 0.2 frame API on
> `main` is not compatible and has no aliases — `cf_` names are the contract
> from here.

## What is usable today

`cf_layout` is the standalone engine: flexbox as CSS Fragment 3 §9 specifies it
— direction, wrap, order, grow, shrink, basis, auto margins, `min`/`max`
clamps, percent resolution, cross alignment, align-content, and children placed
out of the flow with `position: absolute`. Content the engine cannot see into —
text, an image, a foreign subtree — arrives through a `cf_measure_fn` callback,
so the library links nothing but libc.

Grid is a subset, and the subset is the contract: explicit tracks written as
`px`, `%`, `fr`, `auto`, `minmax()` and `repeat()`, items placed by a 1-based
start line and a span on either axis, and dense auto-placement for the items
that name no lines. A container with no column tracks gets one content-sized
column, and a row an item overflows into becomes a content-sized track of its
own. The grid reads none of the alignment enums: leftover space in a settled box
always goes to the tracks that named no ceiling, which is CSS's `normal` on both
axes. Named lines, `span all` and `grid-auto-flow: column` are out of scope.

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

[`examples/layout_only.c`](examples/layout_only.c) is the same idea as a
program: it links `cf_layout` and nothing else, lays a grid inside a column that
hugs it, and measures a label through a callback. Building it is the check that
the engine still stands alone.

[`tools/amalgamate.py`](tools/amalgamate.py) is the same engine as one file. It
packs `types.h`, `base.h`, `style.h` and `layout.h` into a generated
`cf_layout.h`, which a project drops beside its own source and compiles against
with no include path configured — `ctest` builds a case that includes the
packed header and nothing from `include/cf`, so the promise stays checked:

```sh
cmake --build build/dev --target cf_amalgamate   # writes build/amalgam/cf_layout.h
```

## The whole stack

Above `cf_layout`, a page is a function that returns a tree. The shell owns the
window, the loop and the frame arenas; the backend owns pixels and density.

```c
#include "cf/app.h"
#include "cf/widget.h"

static const cf_node *build(cf_ctx *ctx) {
    cf_node *page = cf_alloc(&ctx->frame, sizeof *page);
    cf_node *children = cf_alloc(&ctx->frame, sizeof *children);

    if (page == NULL || children == NULL) {
        return NULL; /* a page that paints nothing beats a half-built tree */
    }

    children[0] = cf_button(ctx, "press", on_press, NULL, NULL, 0u);
    *page = (cf_node){.style = theme->page, .children = children, .child_count = 1u};

    return page;
}

cf_app_run(&(cf_app_config){.build = build, .backend = cf_backend_sdl(&(cf_sdl_config){
                                .title = "cform", .width = 480u, .height = 320u}),
                            .theme = theme, .fonts = &fonts});
```

`cf_app_run` polls the device, dispatches into the tree, rebuilds only when
something changed, and submits the command stream the golden tier snapshots.
`F12` opens the inspector: node count, dirty count, commands, the page's rect
and the frame's time.
[`examples/gallery.c`](examples/gallery.c) is every widget on one page.

## Requirements

- CMake **3.25**+, a **C17** compiler (GCC, Clang or MSVC) and
  [Ninja](https://ninja-build.org/), which the presets use
- Nothing else for the layers: they link libc alone. The SDL backend fetches
  SDL3 and SDL3_ttf when it is turned on

## Build and test

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`dev` is a Debug build with a compile database and the test suite on;
`release` is the same graph in Release. The renderer backend and the examples
that open a window are off until asked for, because fetching SDL is a network
operation a headless machine should not pay for:

```sh
cmake --preset dev -DCFORM_WITH_SDL_BACKEND=ON -DCFORM_BUILD_EXAMPLES=ON
```

Sanitizers are opt-in and need a toolchain that ships the ASan/UBSan runtime:

```sh
cmake --preset dev -DCFORM_WITH_SANITIZERS=ON
```

The headless layers install as a package, with a CMake config under the `cform::`
namespace and a `cform.pc` for builds that do not use CMake:

```sh
cmake --install build/dev --prefix /usr/local
```

```cmake
find_package(cform-ui REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE cform::cf_app)  # cform::cf_layout for the engine alone
```

The SDL backend is not installed: a host that opens a window already owns an SDL
link line. [`docs/porting.md`](docs/porting.md) is the route in either way.

### In the browser

The gallery runs on WebAssembly through the same SDL backend. [Emscripten](https://emscripten.org/)
is the only extra requirement, and the output needs a static file server because
a tab will not fetch a `.wasm` over `file://`:

```sh
cmake -S . -B build/wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCFORM_WITH_SDL_BACKEND=ON -DCFORM_BUILD_EXAMPLES=ON
cmake --build build/wasm
python -m http.server --directory build/wasm/bin 8000   # then open gallery.html
```

Nothing in the layers changes for the browser: `cf_layout`, `cf_text`, `cf_paint`
and `cf_node` compile to the same objects, and the backend is the one directory
that knows a tab exists. The font is preloaded into the data filesystem, and
`ASYNCIFY` lets the shell's loop yield to the browser's frame clock instead of
freezing the tab.

## Layout

```
include/cf/     Public headers — the contract; changes here are breaking
src/base/       Arena, pool, handle, intern, hash, log (libc only)
src/style/      Length and box resolution, style defaults, roles
src/theme/      The default palette: what each role rests on and reacts in
src/layout/     Flex, grid, absolute, intrinsic measurement, measure callback
src/text/       Decoding, break opportunities, line walking, measurement
src/paint/      The command stream: fills, borders, lines, images
src/node/       Slot tree, keyed diff, state, input, focus, scroll, tweens
src/widget/     Label, button, field, list, scroll and image builders
src/backend/    The ABI's implementations: headless recorder, SDL3 renderer
src/app/        The shell: loop, damage rule, inspector
src/*/internal.h  Types shared across src/ but not published
examples/       Standalone programs, off unless CFORM_BUILD_EXAMPLES is on
tests/unit/     One file per layer, scenario-named cases
tests/conformance/  Yoga's fixtures as data, scored against a published number
tests/golden/   Command-stream snapshots, reviewed as data
tests/bench/    Throughput rigs at the size the budget names
tools/          The include-graph check CI runs, the corpus generator, the
                layout packer
docs/           The architecture, the layout spec, the backend ABI, the porting
                route, the stability contract and the published numbers
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
the border box every node must land in — and prints only what missed: the node,
the rect it got, the rect it was owed. Add a row to
[`tests/unit/layout_cases_data.h`](tests/unit/layout_cases_data.h) rather than
a function when a rule needs covering. Assertions come from the tiny,
dependency-free [`tests/cform_test.h`](tests/cform_test.h); the harness is
small on purpose.

[`tests/conformance/yoga_cases.c`](tests/conformance/yoga_cases.c) runs the
other tier: the same row format filled with 485 of Yoga's flexbox fixtures,
which are HTML as a reference browser measured it, ported to this engine's
style by [`tools/gentest.py`](tools/gentest.py). It reports a score and asserts
a floor that only moves up, so a fixture that regresses goes red while the ones
that have never passed stay counted rather than deleted.

[`tests/bench/bench_layout.c`](tests/bench/bench_layout.c) and
[`tests/bench/bench_node.c`](tests/bench/bench_node.c) are the bench tier:
two 10,000-node rigs that print milliseconds per layout and assert only that the
tree still lands where it should. A shared runner cannot be trusted for a
wall-clock number, so a regression is a diff in a published baseline a reviewer
reads rather than a build that goes red on a busy machine. They carry the
`bench` label, which keeps them out of an everyday run:

```sh
ctest --preset dev           # unit and conformance cases
ctest --preset dev -L bench  # the throughput rigs, printing the baseline
```

The golden tier, [`tests/golden/golden_cases.c`](tests/golden/golden_cases.c),
sits between the two: it runs whole frames through the recorder backend and
compares the command stream against a checked-in snapshot. A snapshot changes
only through `golden_cases --update`, so a draw that moved is a diff somebody
reads as data rather than a pixel somebody squints at.

The renderer is verified by running the example and looking at it, never by
mocking the backend or asserting pixels. `ctest` says the engine is right; the
window is the maintainer's check.

[`docs/bench.md`](docs/bench.md) publishes the baselines: what a layout, a
re-reconciled 10,000-node frame and a page's command stream cost, how the cost
grows with the tree, and why no number is asserted in CI.

[`docs/conformance.md`](docs/conformance.md) publishes the flexbox score: which
fixtures were ported and why the rest were not, how many match per suite, and
the behaviour each miss needs.

[`docs/backend-abi.md`](docs/backend-abi.md) is the contract a renderer backend
has to satisfy, and the checklist for writing one: the five slots, what is
borrowed and what is owned, where logical points stop being points, and how to
test a port without looking at it.

[`docs/stability.md`](docs/stability.md) says what a binding may rely on across
versions: handle types, append-only enums, the two arena lifetimes, the one
failure mode, and what still moves while the version carries `-alpha`.

[`docs/architecture.md`](docs/architecture.md) is the shape of the thing: the
ten layers and the edges between them, the order a frame runs in, why a node is
a slot and an id rather than a pointer, and which boundary each test tier
protects.

[`docs/layout-spec.md`](docs/layout-spec.md) is the layout reader's reference:
which CSS the engine implements, the spec step each layout file follows, the
grid subset as the API exposes it, the contract a measure callback has to keep,
and the list of what is deliberately not there.

[`docs/porting.md`](docs/porting.md) is the way in: which floor to link, what
the four build options change, what a platform owes the engine for input, scale
and pixels, how the single packed header is produced, and how to compile the
gallery for a browser tab.

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

[`AGENTS.md`](AGENTS.md) is the repository contract: the layer graph, the data
rules, naming, comments, test tiers and the git conventions. Read it before the
first edit. [`CONTRIBUTING.md`](CONTRIBUTING.md) is the route from an idea to a
merged pull request — branches, the test tier a rule belongs in, and the gates CI
runs. [`CHANGELOG.md`](CHANGELOG.md) records what changed for consumers.
Pull requests are welcome; open an issue first for anything
substantial. CI gates the formatting and static-analysis checks over every
layer, the include-graph rule, a sanitizer build, the WebAssembly compile of the
gallery, and Debug and Release compiles on Linux, Windows and macOS — keep those
green and `ctest` passing. Report
security problems as described in [`SECURITY.md`](SECURITY.md). A PR template is
provided.

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
