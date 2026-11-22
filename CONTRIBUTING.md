# Contributing

[`AGENTS.md`](AGENTS.md) is the contract: the layer graph, the data rules,
naming, comments, test tiers and the git conventions. This file is the route
from an idea to a merged pull request.

## Get a build running

```sh
cmake --preset dev                      # Ninja, Debug, tests on
cmake --build --preset dev
ctest --preset dev
```

Add `-DCFORM_WITH_SDL_BACKEND=ON` to configure the SDL3 backend and the examples,
`-DCFORM_WITH_SANITIZERS=ON` on Linux to build the layers and their tests under
AddressSanitizer and UndefinedBehaviorSanitizer. CMake is the single source of
truth; do not add a second build path or a shell script that repeats it.

## Before writing code

Open an issue for anything substantial — a new layer edge, a public header
change, a dependency. A small fix can go straight to a branch. If the change
touches `include/cf/`, assume it is breaking and say so in the issue.

## Branches and commits

Branch from `main` as `<type>/<slug>` (`feat/grid-auto-rows`,
`fix/scroll-clamp-overflow`), one logical change per commit, Conventional
Commits with a layer as the scope:

```
feat(layout): place items in implicit auto-sized rows
```

Types are `feat fix test docs build ci refactor chore`. A pure refactor is its
own commit, separate from the fix that noticed it. A breaking change carries a
`BREAKING CHANGE:` footer naming the migration, and `include/cf/` changes note
the version they land in.

Keep concurrent work in its own worktree so two changes never edit the same
file:

```sh
git worktree add .worktrees/<slug> -b feat/<slug> main
```

## Tests

Every rule that is not a one-line typo gets a case in the tier that can see it.
Layout, node, paint, text, style and base behavior is headless and belongs in
`tests/unit/` (or `tests/conformance/` when the row belongs to the Yoga corpus);
a change to the command stream belongs in `tests/golden/`; something that could
regress a budget belongs in `tests/bench/`.

Test names state the scenario — `scroll_clamps_past_the_content`, not
`test_scroll_3`. Add the failing test before the fix and keep it afterwards.
A golden snapshot changes only through `golden_cases --update`, and the diff is
reviewed as data.

No test looks at the window. CI proves compilation and headless behavior; what
the demo draws is the maintainer's check, so a pull request that changes
rendering says what to look at and in which example.

## Gates

CI runs these; run them locally before you push.

- `cmake --build --preset dev` and `ctest --preset dev`, zero warnings, on Linux,
  Windows and macOS, in Debug and Release.
- `clang-format` and `clang-tidy` clean for the files you touched.
- `python tools/check_includes.py` — no `#include` that assumes a layer edge the
  target graph does not grant, no SDL outside `src/app/` and `src/backend/`, no
  new mutable global, no "0 means unset" phrasing in `include/cf/`.
- The sanitizer build, and the WebAssembly compile of the gallery.

## Pull requests

Rebase onto `main` before opening, and again before merge. Use the template: what
changed and why, how it is tested, and what a reviewer should look at in the
demo. Stage by path or hunk, never `git add .`, and read `git diff --cached`
before you commit.

Update [`CHANGELOG.md`](CHANGELOG.md) when a change is worth telling consumers
about, and the numbers in `docs/` when a change moves one — a benchmark or a
conformance score lives in `docs/` and moves in the diff that moves it.

## Security

Report vulnerabilities as described in [`SECURITY.md`](SECURITY.md), not in a
public issue.

## License

By contributing you agree that your contributions are licensed under the MIT
license in [`LICENSE`](LICENSE).
