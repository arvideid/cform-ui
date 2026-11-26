# Releases

How a number becomes something a stranger downloads.

Two things make this short. The version lives in one header, so there is no
list of places to edit it; and the commits are Conventional, so the notes write
themselves from the log rather than from someone's memory of the sprint.

## Step 0 — once, in the repository settings

GitHub Pages has to be told that this workflow is its source, and no workflow
can change the setting it needs:

**Settings → Pages → Build and deployment → Source → Deploy from a branch**,
then switch it to **GitHub Actions**.

The first choice exists only to create the Pages site; the second is what the
`site` job deploys into. Without it the deploy step fails with a 409 about the
build context, and the job says so in words instead of leaving an Actions API
error to be decoded.

## The release

### 1. Cut the branch off a green `main`

```sh
git switch main && git pull
git switch -c chore/release-0.2.0
```

Green means the CI run on `main`'s tip passed. A release branch is not a place
to fix a broken build; it is a place to write down a number.

### 2. State the number

Edit [`../include/cf/version.h`](../include/cf/version.h): the three macros and
`CF_VERSION_STRING` together, in one commit.

```sh
git commit --only -m "chore(release): prepare 0.2.0" -- include/cf/version.h
```

Nothing else carries the version. `project()` reads the header through
`cmake/cform_version.cmake`, `cform.pc` and the package config file take it from
`project()`, and the top bar of the site prints `CF_VERSION_STRING`. A commit
that edits two of those three has found a bug in this file rather than a step to
perform.

Which of the three numbers moves is [`docs/versioning.md`](versioning.md)'s
question, not this one. Read it before you type.

### 3. Refresh the records the release ships with

All three are generated or checked in the same build, from the release
configuration:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix build/stage --config Release
```

Then:

- **the API baseline** — the surface this version ships, committed as data:

  ```sh
  python3 tools/api_surface.py -o tests/api/0.2.0.txt
  ```

  The release workflow refuses a tag whose `tests/api/<version>.txt` is missing
  or does not match the tree, so this is the step that unblocks the tag rather
  than one that documents it.

- **the versioning contract** — `docs/versioning.md`'s backend ABI table gains a
  row for this version naming what `CF_BACKEND_ABI_VERSION` added. If the number
  did not move, the table still needs the row saying the number it kept;
  `tools/check_api.py` refuses an ABI move that `docs/backend-abi.md` does not
  describe, and this table is what a driver author reads first.

- **conformance and bench** — re-record
  [`docs/conformance.md`](conformance.md) and [`docs/bench.md`](bench.md) from
  this build, each with the commit it was recorded at, so a number cannot
  outlive the code that made it.

```sh
git commit --only -m "chore(release): record the surface and the numbers for 0.2.0" \
  -- tests/api docs/conformance.md docs/bench.md docs/versioning.md
```

### 4. Open the pull request and let the gates answer

```sh
git push -u origin chore/release-0.2.0
gh pr create --fill
```

Every gate in `.github/workflows/ci.yml` runs against the branch, including the
API-surface comparison. Expect that one to be the interesting step: it is the
gate that says whether the number in step 2 is the number the diff earns.

Merge by rebase, so `main` stays one line of commits and the tag lands on a
commit CI actually gated.

### 5. Tag `main`

```sh
git switch main && git pull
git tag -a v0.2.0 -m "cform-ui 0.2.0"
git push origin v0.2.0
```

An annotated tag, so `git describe` names the release rather than a bare commit,
and on `main` — never on the branch. A tag on an unmerged commit produces a
release nobody can build from the repository's own history.

### 6. Let the workflow run, then check what it published

`.github/workflows/release.yml` verifies the tag, builds the three package
archives, builds the WebAssembly pages and the amalgamated header, writes the
notes from the commits since the last tag, publishes the release and deploys the
site. Read the notes; the grouping is only as good as the messages.

```sh
gh release view v0.2.0
gh run watch                            # the release workflow, if you are watching
```

Confirm: the three `.tar.gz` files, `cf_layout-0.2.0.h`, the release body, and
the site answering at its Pages URL. Then delete the branch — it has no further
use, and a release branch that survives becomes somewhere fixes go instead of
`main`.

```sh
git push origin --delete chore/release-0.2.0
git branch -d chore/release-0.2.0
```

## What a release contains

| Artefact | Where it comes from | What it is for |
|---|---|---|
| `cform-ui-<ver>-<os>.tar.gz` × 3 | `cmake --install` of a Release build, on Linux, Windows and macOS | a project that takes the library as an installed package through `find_package` or `pkg-config` |
| the amalgamated `cf_layout-<ver>.h` | `cmake --build --target cf_amalgamate` | a project that wants one header and no build system |
| `site.html`, `gallery.html` | the `web` preset, one Emscripten compile | the Pages site and the live playground |
| `releases.json` | `tools/release_notes.py` over every tag | the `/releases` data, and anything outside this repository that wants the list |
| the source tarball | GitHub, at `/archive/<tag>.tar.gz` | anyone building from source. Not rebuilt here — it would only differ in what it chose to leave out |

There are **no prebuilt compiler-specific binaries** beyond those archives, and
that is deliberate. A `.lib` shipped for one MSVC minor is a promise about a
toolchain this repository does not choose, and static archives carry a C
runtime's assumptions with them. `docs/versioning.md` states a source and rebuild
promise; shipping binaries would imply a stronger one that no toolchain keeps.

## What a release does not contain

No `CHANGELOG.md`. The tags and the commit messages between them are the record,
and a committed rendering of them is build output that rots the first time
someone amends a message. `tools/release_notes.py` regenerates the body for any
tag, at any time, from what the repository already holds.

## Support windows

Pre-1.0, there is one line: `main`. Fixes land there and go out with the next
`0.MINOR` or `0.PATCH`. Nothing is backported, because nothing is old enough to
have anyone on it, and a branch that only the maintainer remembers is a branch
that only the maintainer tests.

Post-1.0, the latest `MAJOR.MINOR` gets fixes and the previous `MINOR` stays
open until the next one ships. An LTS line opens when a named adopter needs one
and says what they would run on it — not on a calendar.

## Yanking

A published tag is never deleted or moved. A ref that changes under a
`git fetch` is a supply chain break, and an adopter who pinned it deserves to
find out another way.

A bad release is superseded: cut the next one, and mark the bad one on its
release page.

```sh
gh release edit v0.2.1 --notes "Do not use: <what and which version to take instead>.

<the original body>"
```

If the break is security-relevant, the disclosure path is
[`docs/maintenance.md`](maintenance.md), and the fix still ships as an ordinary
release with the version that says how much it cost.

## Prereleases

A tag with a suffix — `v0.3.0-rc1` — publishes a GitHub **prerelease** and does
**not** deploy the site. Everything else is the same run: the gates, the
archives, the notes.

```sh
git tag -a v0.3.0-rc1 -m "cform-ui 0.3.0 release candidate 1"
git push origin v0.3.0-rc1
```

The suffix is what makes an `rc` not a version anyone depends on:
`include/cf/version.h` still states three plain numbers (`0.3.0`), so an adopter
who builds the candidate and one who builds the final get the same
`cf_version_string()`. What differs is only the published object. Do not put the
suffix in the header — a library that reports `0.3.0-rc1` and a package that
says `0.3.0` are two numbers, and this repository keeps one.

An `rc` earns its final tag by passing the same checks the release branch
passed. It is a dress rehearsal, not a channel.

## Trying the pieces before the tag

Every step of the workflow is a command that runs from a checkout:

```sh
cmake -DCFORM_EXPECTED_VERSION=0.2.0 -P cmake/cform_version.cmake --check
python3 tools/check_api.py --baseline tests/api/0.1.0.txt
python3 tools/release_notes.py --tag v0.2.0 --range origin/main.. \
  --notes /tmp/release.md --index /tmp/releases.json
cmake --build --preset web --target cf_amalgamate
```

The `--range` form is for the first release and for dry runs: with no previous
tag to describe, the commits a release adds are the ones you name.
