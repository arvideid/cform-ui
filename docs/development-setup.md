# Development Setup

This document is for contributors. It covers building, testing and the editor
configuration that makes any machine behave the same. Library **users** only
need the README.

## Core idea: commit project config, not personal config

**CMake is the single source of truth.** The compiler, C standard, include
paths and warning flags are defined once in `CMakeLists.txt`. Everything else -
clangd, the IDE, CI and your terminal - reads the same `compile_commands.json`
that CMake generates, so nobody sees different diagnostics on different
machines.

Committed, shared configuration:

| File | Purpose |
| --- | --- |
| `CMakeLists.txt` | Targets, flags, dependency resolution |
| `CMakePresets.json` | Named `dev` / `release` configurations + test presets |
| `.editorconfig` | Indentation, LF endings, trailing whitespace (every editor) |
| `.clang-format` | Formatting rules (K&R, 4 spaces, 100 cols) |
| `.clang-tidy` | Correctness-focused static analysis |
| `.clangd` | Points the language server at `build/dev` |
| `.vscode/settings.json` | Wires VS Code to the presets + clangd |
| `.vscode/tasks.json` | One-click Configure / Build / Run / Clean |
| `.vscode/extensions.json` | Recommended extensions |

Personal or generated files (`.vscode/launch.json`, `CMakeUserPresets.json`,
`build/`, `compile_commands.json`) are git-ignored.

## Prerequisites

- CMake **3.25+** (for presets) and a C99 compiler (GCC, Clang or MSVC)
- [Ninja](https://ninja-build.org/) - used by the presets
- SDL3, resolved from your system or fetched automatically by CMake

## Build and test from the command line

```sh
cmake --preset dev            # configure (build/dev)
cmake --build --preset dev    # build library, example and tests
ctest --preset dev            # run the test suite
```

The first configure clones and builds SDL3, so it can take a few minutes; later
runs reuse the cached dependency. Run the example from `build/dev/bin/`
(`cform_demo.exe` on Windows).

## Testing

The suite uses **CTest** (CMake's built-in runner) with a tiny, dependency-free
assertion header in [`tests/cform_test.h`](../tests/cform_test.h).

- Tests link against `cform_core` only - never the SDL renderer - so they run
  **headlessly**: no window, no display, no SDL at runtime.
- Build them with `-DCFORM_BUILD_TESTS=ON` (the `dev` preset turns this on).
- Assertions never compile out with `NDEBUG`, so Release runs still check the
  layout math.

Add a case by writing a `static void test_*(void)` that calls `CF_CHECK` /
`CF_NEAR`, register it with `CF_RUN` in that suite's `main`, and add
`cform_add_test(test_name)` in `tests/CMakeLists.txt`. The harness is small on
purpose: if the suite grows, it is the only file to swap for Unity or CMocka.

## VS Code / VSCodium

1. When prompted, install the recommended extensions. The essential one is
   **clangd** (CMake Tools and EditorConfig are also handy). clangd can download
   its own server binary on first launch.
2. Configure once - run the **CMake: Configure (dev)** task, or click
   *Configure* in the CMake Tools status bar. This writes
   `build/dev/compile_commands.json`.
3. clangd reads that database via `.clangd`, so the **Outline** view,
   go-to-definition and inline warnings match the real build. `.vscode/settings.json`
   disables the cpptools IntelliSense engine so it does not fight clangd.

To build or run without leaving the keyboard: **Tasks: Run Build Task**
(`Ctrl+Shift+B`) builds the `dev` preset, and **Run: demo (dev)** builds and
launches the example. Everything is driven through presets, so the IDE and the
command line can never drift apart.

## Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/):
`<type>(<scope>): <short description>`, with types `feat`, `fix`, `test`,
`docs`, `build`, `ci`, `refactor`, `chore`. One commit should be one coherent
change - no "update", "stuff", or unrelated changes squashed together.

To use the bundled template locally (opt-in, not forced on anyone):

```sh
git config commit.template .gitmessage
```

## Formatting and linting

- Format: `clang-format -i path/to/file.c` (the config is committed; VS Code
  formats on save when the clang-format extension is enabled).
- Static analysis: `clang-tidy` reads `.clang-tidy` and uses the same compile
  database. Both are opt-in tooling, not enforced on every build.
