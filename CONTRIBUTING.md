# Contributing

cform is a retained-mode, data-oriented UI framework in C17. The tree is data,
work is systems, and a low-level command stream sits beneath the convenience
tree. Plain C — no DSL, no compiler, no C++ idioms wearing C clothing.

## Build and run

CMake is the single source of truth; the presets carry the flags so the IDE, the
command line and CI never drift.

```sh
cmake --preset dev            # Ninja, Debug, tests on, compile_commands.json
cmake --build --preset dev
ctest --preset dev
./build/dev/bin/cform_demo    # the native window
```

SDL3, SDL3_ttf and SDL3_image are found locally when installed and fetched from
pinned release tags otherwise. Native remote images need libcurl; without it the
build configures and `http(s)` image sources report as unloadable.

The WebAssembly port needs emsdk activated first (`source ./emsdk_env.sh`), then
`cmake --preset web && cmake --build --preset web`.

`-DCFORM_WITH_SANITIZERS=ON` builds `cf_base` and its tests under AddressSanitizer
and UndefinedBehaviorSanitizer. Configure fails where the toolchain has no
sanitizer runtime to link, rather than producing a build that checks nothing.

## Layers

Dependency runs one way, from the top down. Each layer is a CMake target, and
the target graph is what enforces the edge — a link that the graph forbids does
not configure. `tools/check_includes.py` catches the mirror-image mistake: an
`#include` that assumes an edge the build never granted.

    app → widget, backend
    widget → node, theme
    node → layout, paint, event
    paint → layout, style, text
    layout → style, base        (measure arrives as a callback, never a link)
    text → base
    style → base
    backend → paint, text, base
    base → libc

- `include/cf/` is the public contract. Changes there are breaking and need a
  version note.
- Only `src/app/` and `src/backend/*/` may include SDL or platform headers.
  Platform differences live in build-selected files, never in `#ifdef` inside a
  shared source.
- Everything above the backend speaks logical points. Device pixels stop at the
  backend.
- `cf_layout` must compile and pass conformance with no other layer present.
- One job per file, roughly 300 lines, 400 the ceiling. Split when a file starts
  changing for two unrelated reasons. Moving code across a layer boundary is
  architectural — propose it before doing it.

The v0.2 tree (`include/cform/`, `src/core/`, `src/renderer/`, `example/`) is
grandfathered until the 1.0 layers reach parity, then it goes in one commit. Its
`ui_`/`UI_` prefix and its SDL-owning renderer directory do not extend to new
files.

## Data rules

- Nodes are `cf_id` handles into parallel component arrays. No node owns a
  pointer to another node, no child pointer lists.
- Allocation goes through `src/base/`. `malloc`/`free` outside it is a review
  rejection; arrays grow by capacity doubling, never per element.
- Every cache is derived and invalidated by a dirty bit. Nothing is hand-synced.
  Cache and intern keys are owned values, never borrowed `const char *`.
- No mutable globals in library code. The log sink in `base/log.c` and the
  per-binary failure counter in the test harness are the only exceptions, each
  confined to one file behind an install function.
- A struct's zero state must be valid, so `calloc` and `(T){0}` are free to use.
- Optional values are tagged. No sentinel that means "unset" for one field and a
  real value for another. Every pointer that is only read is `const *`.

## API and style

Functions and struct tags are `cf_snake_case`, verbs first; enum variants and
macros are `CF_SCREAMING_SNAKE`; no `_t` suffixes. One failure mode per function:
`NULL` return, silent no-op, or cannot-fail — no error enums, no `exit`, no
shipped `assert`. Configs pass by value, enums are append-only because the
numeric form is ABI, and every macro has a plain-function escape hatch.

C17 with no compiler extensions, braces attached, guard clauses first, locals at
first use, no VLAs, no `alloca`, fixed-width types at the ABI boundary. Include
guards follow the file path in caps. Trust `.clang-format` and `.clang-tidy`;
neither is a suggestion, and both run in CI.

Comments are rare and only ever say *why*: an invariant, the reason behind layout
math, an ownership or lifetime rule, a spec citation (`/* flexbox §9.7 freeze
loop */`). A good name beats a good comment. No banners, no file headers, no
commented-out code, no TODO without an issue link, no license repetition.

## Tests

Four tiers: unit (per layer, headless), conformance (fixtures plus expected
rects as data, generated once against a reference browser by `tools/gentest`),
golden (command-stream snapshots, updated only through `tools/golden --update`
and reviewed as data), and bench (published baselines). Test names describe the
scenario — `store_rejects_a_stale_id`, not `test_store_3`. Add a failing test
before fixing a bug and keep it afterwards.

Nothing in the suite verifies a pixel. Screenshots and window behaviour are the
maintainer's check, so a PR that changes rendering says what to look at.

Performance budgets: an idle frame does zero layouts, emits zero commands and
performs zero allocations; a 10 000-node layout stays under 2 ms in release;
`cf_layout` and `cf_paint` never allocate, log or touch the platform.

## Git

Conventional Commits — `<type>(<scope>): <subject>`, subject under 72 characters,
types `feat fix test docs build ci refactor chore`, scopes named after the
layers. One logical change and one kind of edit per commit; a pure refactor is
its own commit, separate from the fix that noticed it. Messages describe the code
and why, never the process that produced it. Breaking changes carry a
`BREAKING CHANGE:` footer naming the migration.

Trunk is `main`. Small single-commit changes can land straight through a PR;
anything larger goes to a branch named `<type>/<slug>` (`feat/box-completion`),
rebased onto `main` before opening the PR and again before merge. Every
non-trivial change is reviewed by a human. Never force-push `main` or rewrite a
branch that is already pushed.

Concurrent work uses one worktree per task inside the repo, under `.worktrees/`
(already ignored):

```sh
git worktree add .worktrees/<slug> -b feat/<slug> main
```

Each worktree owns one branch and one layer scope, so two tasks never edit the
same file. Cross-boundary work goes to a single branch.

Before committing, read `git diff --cached` and confirm the branch; stage by path
or hunk, never `git add .`. Before amending or rebasing, check `git reflog -5` —
if HEAD moved underneath you, stop.

## Security

Report vulnerabilities as described in [`SECURITY.md`](SECURITY.md), not in a
public issue.

## License

By contributing you agree that your contributions are licensed under the MIT
license in [`LICENSE`](LICENSE).
