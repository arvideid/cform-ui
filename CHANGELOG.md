# Changelog

What changed for people who consume cform: the API in `include/cf/`, the backend
ABI, and the behaviour its guarantees describe. Entries follow
[Semantic Versioning](https://semver.org/); the format is
[Keep a Changelog](https://keepachangelog.com/).

Measured numbers do not live here. Flexbox conformance is scored in
[`docs/conformance.md`](docs/conformance.md), the bench baselines in
[`docs/bench.md`](docs/bench.md), and what a binding may rely on across versions
in [`docs/stability.md`](docs/stability.md). A release entry points at those
files instead of restating them.

## [Unreleased]

The 1.0 framework is in development. `main` still carries the pre-rebuild
framework — the SDL-drawing `ui_`/`UI_` API and its `src/core`/`src/renderer`
tree — and will be frozen and tagged when the first 1.0 pre-release ships.
Until then the version carries an `-alpha` suffix and the items listed as
moving in `docs/stability.md` are still moving.

`CF_BACKEND_ABI_VERSION` is 4 and every backend fills it; `docs/backend-abi.md`
says what a port implements and what a host compares before the first submit.

### Added

- Ten layers, each a CMake target with its own public header: `base` (arena,
  pool-style store, interning, hash, log), `style`, `theme`, `layout`, `text`,
  `paint`, `node`, `widget`, `backend`, `app`. `include/cf/cf.h` pulls in the
  whole set.
- `cf_layout` stands alone: it links `cf_style` and `cf_base` only, receives
  measurement as a callback, and `examples/layout_only.c` builds against it with
  nothing else in the tree.
- The headless layers install as a package: ten archives under the `cform::`
  CMake namespace, `include/cf/`, a `cform-ui-config.cmake` and a `cform.pc`.
  `cf_backend_sdl` is deliberately absent — its interface names a target the SDL
  fetch creates only inside this repository's build tree.
- Flexbox per the spec's freeze loop, wrapping, `order` and baseline alignment;
  an absolute-positioning path; a grid subset of `px`/`%`/`fr`/`auto`/`minmax`
  tracks with `cf_tracks_repeat` for a pattern written once, and 1-based line
  placement and spanning.
- A command stream (`cf_command[]`) with viewport culling, nested clip
  intersection and text runs, consumed by a headless recorder backend that
  goldens compare against, and by an SDL3 GPU backend that draws it — rounded
  fills and borders as triangle strips, glyphs through SDL3_ttf.
- A retained node layer: arena-resident slots, keyed diff, state slots that
  survive a rebuild, pointer and keyboard input with focus and capture,
  framework-owned scroll offsets and wheel range, tweens over dirty rects, and a
  semantic export for assistive technology.
- Widgets: `cf_button`, `cf_label`, `cf_field`, `cf_list` (virtualized),
  `cf_scroller`, `cf_image`. Themes resolve style variants by interaction state,
  so a widget declares a role and the resting box.
- Declaration sugar in `include/cf/build.h`: `CF_COL`/`CF_ROW` over `cf_col` and
  `cf_row`, and `cf_children` for a list a build assembles.
- `cf_app_run` owns the frame loop, skips present when nothing is damaged, and
  toggles a live inspector (dirty bits and command counts) on F12.
- `cf_error_message` reports why the last boundary refused since the frame began.
- `tools/amalgamate.py` packs `types.h`, `base.h`, `style.h` and `layout.h` into
  one `cf_layout.h`. A project that takes it drops the file beside its own
  source, links the `cf_layout` objects and configures no include path; a case
  in `ctest` compiles against the generated header alone, which is what keeps
  that promise checked.
- Four test tiers in `ctest`: unit, a ported Yoga conformance corpus, golden
  command-stream snapshots, and bench rigs with published baselines.
- CI: three operating systems in Debug and Release, a sanitizer build, a wasm
  gallery, clang-format, clang-tidy, and the include-graph check
  (`tools/check_includes.py`).
- `AGENTS.md` as the tracked repository contract, with a thin machine-local
  pointer rule, `SECURITY.md`, issue templates and a pull-request template.
- `docs/architecture.md`, `docs/layout-spec.md` and `docs/porting.md`: the layer
  graph and the order a frame runs in, the CSS the engine implements with the
  spec step each layout file follows and the grid subset as the API exposes it,
  and the route in for a host or a platform the SDL backend does not cover.

### Changed

- Public symbols are prefixed `cf_` and types are `cf_*`; the `ui_`/`UI_`
  generation is gone.
- Nodes are handles into parallel arrays. A caller holds no node pointer and
  rebuilds a declaration instead of mutating a tree in place.
- Sizing is tagged: `CF_AUTO` (the default, hugging content), `CF_PX`,
  `CF_PERCENT`, `CF_FR`. `CF_FILL` becomes `CF_FR(1)`, and the zero-means-unset
  conventions the old header carried are replaced by `cf_opt_*` pairs.
- Interaction colours are theme variants resolved per state rather than
  `hover_color`/`pressed_color` fields on a style.
- Scroll geometry and the wheel range are core math; the backend only consumes
  commands and presents.
- Logical points are the unit everywhere above the backend, which owns display
  scale.
- `cf_app_run` compares `backend.abi_version` with `CF_BACKEND_ABI_VERSION`
  before the first submit and sits idle on a mismatch, reported once on the
  backend log channel. A backend reading a command layout it does not have drew
  wrong rather than failing, and `cf/backend.h` already told the caller to check.

### Removed

- `include/cform/cform.h`, `src/internal.h`, `src/core/`, `src/renderer/` and its
  `http_loader_*` variants. Pixel bytes now reach the tree through the backend's
  `create_texture`, which the application calls with what it has fetched.
- The 440-line single-file demo, replaced by `examples/gallery.c` in under 100
  lines and `examples/scenes.c` for the backend ABI.
- A committed font binary; examples name a URL or generate what they draw.
