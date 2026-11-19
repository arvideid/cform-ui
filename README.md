# cform-ui

[![Build](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml/badge.svg)](https://github.com/Arvideid/cform-ui/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A small, declarative layout UI library in C. You describe a tree of frames —
sizes, direction, padding, margin, gap and colors — and `cform-ui` resolves the
layout and renders it through SDL3.

> **Status:** early development. The layout core and SDL3 renderer work and the
> example runs, but the public API is experimental and may change between
> releases. Text, buttons and interaction are on the roadmap, not implemented.

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

A minimal, commented example - nested frames that make padding and margin
visible - lives in [`example/demo.c`](example/demo.c).

## Layout

```
include/cform/   Public header (the API surface)
src/core/        Context, frame and layout logic (no SDL)
src/renderer/    SDL3 backend: window, draw loop
example/         Runnable demo programs
tests/           Headless unit tests (CTest)
docs/            Contributor guides
```

## Development

Shared formatting, compiler and editor configuration is committed so every
machine behaves the same, and a small headless test suite runs under CTest.

```sh
cmake --build --preset dev   # library, example and tests
ctest --preset dev           # run the test suite
```

See [Development Setup](docs/development-setup.md) for the VS Code / VSCodium,
clangd, CMake presets and Conventional Commits details.

## Contributing

Pull requests are welcome; open an issue first for anything substantial. Use
[Conventional Commits](https://www.conventionalcommits.org/), keep the build
warning-free, and make `ctest` pass - CI enforces all of it. A PR template is
provided.

## License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for details.
