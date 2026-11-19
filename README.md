# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A small, declarative layout UI library in C. You describe a tree of frames —
sizes, direction, padding, margin, gap and colors — and `cform-ui` resolves the
layout and renders it through SDL3.

> **Status:** early development (v0.1.0). The layout core and SDL3 renderer
> work, but the public API is not stable yet. Text, buttons and interaction are
> on the roadmap, not implemented.

## Features

- Declarative, tree-based frame layout (`UI_FrameConfig`).
- Horizontal and vertical directions with nested containers.
- Fixed (`UI_FIXED`) and fill (`UI_FILL`) sizing on both axes.
- Box model: padding, margin and gap.
- Renderer-agnostic core with a single SDL3 backend.
- Zero setup: SDL3 is fetched automatically when it is not already installed.

## Requirements

- CMake **3.20**+ and a C99 compiler (GCC, Clang or MSVC)
- [Ninja](https://ninja-build.org/) — used by the local presets
- SDL3, resolved from your system or downloaded via `FetchContent`

## Build and run

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

The SDL3 shared library is placed next to the executable, so it runs directly.

Prefer no presets? Pick a generator that matches a compiler you actually have
(on Windows without MSVC, avoid the `NMake Makefiles` fallback):

```sh
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build
```

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

A minimal, commented example - nested boxes that make padding and margin
visible - lives in [`example/demo.c`](example/demo.c).

## Layout

```
include/cform/   Public header (the API surface)
src/core/        Context, frame and layout logic (no SDL)
src/renderer/    SDL3 backend: window, draw loop
example/         Runnable demo programs
```

## Editor & developer experience

The project is configured so any editor behaves the same, with **CMake as the
single source of truth** for the compiler, standard, includes and warnings.
Shared config committed to the repo:

- `.editorconfig` - indentation, LF endings, trailing-whitespace (every editor).
- `.clang-format` / `.clang-tidy` / `.clangd` - formatting, linting and the
  language server, all reading the same flags CMake uses.
- `CMakePresets.json` - the named `dev` / `release` configurations.
- `.vscode/tasks.json` + `.vscode/extensions.json` - one-click build/run.

### VS Code / VSCodium

1. When prompted, install the recommended extensions. The essential one is
   **clangd** (CMake Tools + EditorConfig are also handy). clangd can fetch its
   own server binary on first launch.
2. Run the **CMake: Configure (dev)** task, or click *Configure* in the CMake
   Tools status bar. This generates `build/dev/compile_commands.json`.
3. clangd reads that database via `.clangd`, so the **Outline**, go-to-definition
   and inline warnings now match the real build. (We disable the cpptools
   IntelliSense engine so it does not fight clangd.)

Build without ever touching the editor:

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/bin/cform_demo      # .exe on Windows
```

Or use the command palette: **Tasks: Run Build Task** (`Ctrl+Shift+B`) builds
the default `dev` preset; **Run: demo (dev)** builds and launches the example.

## Contributing

Pull requests are welcome — open an issue first for anything substantial. Keep
the build warning-free (it compiles with `-Wall -Wextra -Wpedantic` / `/W4`).

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
