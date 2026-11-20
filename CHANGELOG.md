# Changelog

All notable changes to cform are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project adheres
to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Versions apply to the public API in `include/cf/`. The backend ABI is versioned
separately through `cf_backend.abi_version` and bumps with any change to the
command stream. Everything before the first tagged release is history in
`git log`, not a changelog entry.

## [Unreleased]

### Added

- `cf_base` — the allocation and utility layer every later layer builds on: a
  bump arena that frames clear instead of free, a generation-checked handle
  store over parallel component arrays that grows by capacity doubling, a string
  intern table whose keys survive the caller rebuilding the text, and a log sink
  the application installs or silences.
- `tools/check_includes.py` — CI check that an `#include` never assumes a layer
  edge the target graph does not grant, and that SDL, platform headers and direct
  allocation stay where they belong.
- `CFORM_WITH_SANITIZERS` — build `cf_base` and its tests with AddressSanitizer
  and UndefinedBehaviorSanitizer.
- CI jobs for formatting, clang-tidy, the include graph and the sanitizer build,
  alongside the existing platform matrix and WebAssembly compile.
- `CONTRIBUTING.md`, `SECURITY.md`, issue templates.

### Changed

- The C dialect is C17 (`CMAKE_C_STANDARD 17`), the documented CMake minimum is
  3.25 in both `CMakeLists.txt` and `CMakePresets.json`, and `.clang-format`
  declares `Standard: Latest` because clang-format 15 dropped the C-specific
  values.

[Unreleased]: https://github.com/arvideid/cform-ui/commits/main
