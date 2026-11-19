# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A small, declarative layout UI library in C. You describe a tree of frames -
sizes, direction, padding, margin, gap and colors - and `cform-ui` resolves the
layout and renders it through SDL3, either in a native window or in a browser.

> **Status:** early development. The layout core, the SDL3 renderer and the
> WebAssembly port all build and run, but the public API is experimental and
> may change between releases. Text, buttons and interaction are on the
> roadmap, not implemented.

## Features

- Declarative, tree-based frame layout (`UI_FrameConfig`).
- Horizontal and vertical directions with nested containers.
- Fixed (`UI_FIXED`) and fill (`UI_FILL`) sizing on both axes.
- Box model: padding, margin and gap.
- One layout implementation, two backends: native SDL3 and WebAssembly.
- Renderer-agnostic core, verified by a headless test suite.
- Zero setup: SDL3 is fetched automatically when it is not already installed.

## Requirements

- CMake **3.25**+ and a C99 compiler (GCC, Clang or MSVC)
- [Ninja](https://ninja-build.org/) - used by the presets
- SDL3, resolved from your system or downloaded via `FetchContent`
- For the web build only: [Emscripten](https://emscripten.org/) (`emsdk`)

## Build and run - native

```sh
cmake --preset dev
cmake --build --preset dev
```

The first configure clones and builds SDL3, so it can take several minutes;
later runs reuse the cached dependency. The example ends up in
`build/dev/bin/`:

```sh
# Windows
build/dev/bin/cform_demo.exe
# Linux / macOS
./build/dev/bin/cform_demo
```

## Run in the browser - WebAssembly

Activate `emsdk` first so the `EMSDK` environment variable is set (the `web`
preset reads it), then build the same `example/demo.c` to WebAssembly:

```sh
source ./emsdk_env.sh          # or emsdk_env.bat / .ps1 on Windows
cmake --preset web
cmake --build --preset web
```

This produces `cform_demo.{html,js,wasm}` in `build/web/bin/`. Browsers refuse
to load `.wasm` from `file://`, so serve that folder:

```sh
python -m http.server -d build/web/bin 8080
```

Open <http://localhost:8080/cform_demo.html>. The page shows the same nested
layout and colors as the native window.

## Usage

```c
#include <cform/cform.h>

UI_Context *ctx = ui_create();

UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
    .width      = UI_FILL,
    .height     = UI_FILL,
    .direction  = UI_HORIZONTAL,
    .padding    = UI_ALL(20),
    .gap        = 12,
    .background = UI_COLOR(28, 30, 38),
});

UI_Frame *sidebar = ui_frame(ctx, (UI_FrameConfig){
    .width      = UI_FIXED(220),
    .height     = UI_FILL,
    .background = UI_COLOR(38, 40, 51),
});

ui_add(root, sidebar);
ui_render(ctx, root);
ui_destroy(ctx);
```

A minimal example - nested frames that make padding and margin visible - lives
in [`example/demo.c`](example/demo.c).

## Layout

```
include/cform/   Public header (the API surface)
src/core/        Context, frame and layout logic (SDL-free)
src/renderer/    SDL3 backend: window, draw loop
example/         Runnable demo programs (web/ holds the browser shell)
tests/           Headless unit tests (CTest)
```

## Testing

The suite uses **CTest** (CMake's built-in runner) with a tiny, dependency-free
assertion header in [`tests/cform_test.h`](tests/cform_test.h). Tests link
against `cform_core` only - never the SDL renderer - so they run headlessly:
no window, no display, no SDL at runtime.

```sh
cmake --build --preset dev   # library, example and tests
ctest --preset dev           # run the test suite
```

Add a case by writing a `static void <scenario>(void)` that calls `CF_CHECK` /
`CF_NEAR`, register it with `CF_RUN` in that suite's `main`, and add
`cform_add_test(<suite>)` in `tests/CMakeLists.txt`. Give the case a name that
describes the scenario. The harness is small on purpose: if the suite grows, it
is the only file to swap for Unity or CMocka.

## Editor & developer experience

**CMake is the single source of truth.** The compiler, C standard, include
paths and warning flags are defined once in `CMakeLists.txt`; clangd, the IDE,
CI and your terminal all read the same `compile_commands.json` it produces, so
nobody sees different diagnostics on different machines.

Committed, shared configuration - `.editorconfig`, `.clang-format`,
`.clang-tidy`, `.clangd`, `CMakePresets.json`, `.vscode/settings.json`,
`.vscode/tasks.json` and `.vscode/extensions.json`. Personal and generated
files (`build/`, `compile_commands.json`, `.vscode/launch.json`,
`CMakeUserPresets.json`) are git-ignored.

To set up VS Code or VSCodium:

1. Install the recommended extensions when prompted - the essential one is
   **clangd** (it can fetch its own server binary on first launch).
2. Configure once: run the **CMake: Configure (dev)** task, or *Configure* in
   the CMake Tools status bar. This writes `build/dev/compile_commands.json`.
3. clangd reads that database via `.clangd`, so the Outline view, go-to-definition
   and inline warnings match the real build. `.vscode/settings.json` disables the
   cpptools IntelliSense engine so it does not fight clangd.

`Ctrl+Shift+B` runs the default build task; **Run: demo (dev)** and **Run: demo
(web)** build and launch the native window or the browser build. Everything is
driven through presets, so the IDE and the command line never drift apart.

## Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/):
`<type>(<scope>): <short description>`, with types `feat`, `fix`, `test`,
`docs`, `build`, `ci`, `refactor`, `chore`. One commit is one coherent change -
no "update", "stuff", or unrelated changes squashed together. To use the
bundled template locally (opt-in, not forced on anyone):

```sh
git config commit.template .gitmessage
```

## Contributing

Pull requests are welcome; open an issue first for anything substantial. Keep
the build warning-free (`-Wall -Wextra -Wpedantic` / `/W4`) and make `ctest`
pass - CI enforces both, plus the WebAssembly compile. A PR template is
provided.

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
