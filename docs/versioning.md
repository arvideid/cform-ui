# Versioning

Two numbers move in this library, and they mean different things.

One is the version of the C API: `0.2.0`, written once in
[`include/cf/version.h`](../include/cf/version.h) and read from there by CMake,
by the package files and by `cf_version_string()`. The other is the backend ABI
version: `CF_BACKEND_ABI_VERSION` in [`include/cf/backend.h`](../include/cf/backend.h),
the number a device driver and a build of the library agree on before either
touches a screen.

A caller of the library cares about the first. Whoever writes a backend cares
about both.

## What is promised

cform-ui ships as **source and static archives**. You compile it into your
program, so the promise a version makes is about *your source* and *your
rebuild*, not about a binary someone else linked:

> Within one MAJOR, code that compiled and ran against `0.MINOR.PATCH` compiles
> and runs the same way against a higher `MINOR` or `PATCH` of the same MAJOR,
> with nothing for you to edit.

That is a **source** and **rebuild** compatibility promise. It is deliberately
not a claim that `libcf_app.a` built by one compiler links against headers
installed by another. Static archives carry a compiler's and a C runtime's
assumptions, and a library that promised otherwise would be promising something
no toolchain keeps. If you distribute prebuilt binaries of your own, you rebuild
them against the version you ship — the same way this project rebuilds its own.

Everything above the backend speaks logical points rather than device pixels, and
every type a caller can name is either a POD struct or an opaque handle. That is
what makes the promise checkable at all: there is no hidden state, no
constructor that runs before `main`, no allocation crossing a CRT boundary.

## MAJOR

A MAJOR says: **your source will need editing, and the release notes name every
edit.** It happens when

- a public symbol is removed or renamed,
- a function's signature or a struct's field order changes,
- an enum member's number changes,
- the meaning of a command in the paint stream changes,
- a default in `cf_theme_default()` changes in a way a page cannot override.

The first three are mechanical and CI can read them off a header diff. The last
two are judgement, and they are the reason a MAJOR is cut by a person rather than
by a tool.

## MINOR

A MINOR says: **nothing was taken away.** Only appends are allowed —

- new functions,
- new optional `cf_backend` slots at the tail of the struct,
- new enum members at the end of their enum,
- new fields on `cf_style`, `cf_theme` or any struct a caller reads.

Code written against `0.2.0` compiles and runs unchanged against `0.3.0`. The
word "unchanged" is the whole contract; `tools/check_api.py` is the thing that
holds it to the word.

## PATCH

A PATCH says: **fixes only.** No new public symbol, and no change in behaviour a
person would notice in a screenshot. If a fix would change what a screen looks
like, it is a MINOR or it waits.

## Before 1.0

While the major number is `0`, a `MINOR` bump is where a break is allowed to
live: `0.2.0` to `0.3.0` may remove, rename or reorder something. `0.2.0` to
`0.2.1` may not.

That is not a licence to break casually. Every break goes in the release notes
with the call-site edit that migrates it, and `docs/releases.md` is the checklist
that makes sure the notes get written. It is also why the package version file
uses `SameMinorVersion` while the major is zero: an install of `0.2` must not
answer a `find_package` that asked for `0.9`.

## The backend ABI is a second axis

`CF_BACKEND_ABI_VERSION` moves when the `cf_backend` struct moves or the
command and event layouts move — the things a compiled driver and the engine
must agree on byte for byte. It does not move when a function is added to
`cf_widget.h`, and it is not a re-encoding of the library version.

A backend is not asked to be compatible with a range of library versions. It
states the number it was built against and the size of its struct:

```c
cf_backend backend = {
    .abi_version = CF_BACKEND_ABI_VERSION,
    .size = sizeof backend,
    /* ... */
};
```

`cf_app_run` refuses the pair before frame one when either does not match what
the engine was compiled with. That check is the whole compatibility policy for
the backend boundary: no version table, no negotiation, no partially working
driver. A backend built against a number this library no longer accepts is a
rebuild, and it fails loudly rather than drawing the wrong thing.

Slots are appended at the struct tail and never reordered or removed, so an
older driver still finds `submit` where it expects it — and an engine that needs
a slot the driver left NULL asks for it by checking, the way it checks the ones
that may be absent.

| Library | Backend ABI | Also accepts | What the number added |
|---|---|---|---|
| `0.1.0` | 10 | — | `set_cursor`, so a pointer can say a field is ahead |
| `0.2.0` | 11 | 10 is not accepted | `set_text_input`, so a keyboard can be asked for |

One row per release, and `tools/check_api.py` refuses to let the number move
while [`docs/backend-abi.md`](backend-abi.md) says nothing about what it added.
The table is generated by nothing, so a release checklist item is what keeps it
true; see `docs/releases.md`.

## Deprecation

`CF_DEPRECATED` in [`include/cf/base.h`](../include/cf/base.h) marks a public
symbol that is on its way out, with a message naming what to call instead.

A deprecated symbol stays for **one full MAJOR**. It keeps working, it keeps
being tested, and it warns. Removal happens at a MAJOR and is listed in that
release's notes. Nothing is deprecated in `0.2.0`.

`CF_PX` is the one symbol carrying the mark today in spirit rather than in
source: `CF_PT` is the spelling that says what the number is, and `CF_PX` builds
the identical tagged length so existing source keeps compiling. It will be marked
when the call sites in this repository are moved over, and removed at 1.0.

## What CI enforces

| Check | Where | What it refuses |
|---|---|---|
| One version, one place | `cmake -P cmake/cform_version.cmake --check` | a `CMakeLists.txt` with a literal version, or a `CF_VERSION_STRING` that disagrees with the macros beside it |
| The surface only appended | `python3 tools/check_api.py` | a removal, a rename or a signature change that the version in `include/cf/version.h` does not cover |
| Enum numbers frozen | the same script | a member whose number moved, at any bump level, with no version that makes it legal |
| A moved ABI is written down | the same script | `CF_BACKEND_ABI_VERSION` changing while `docs/backend-abi.md` names nothing about it |
| The package keeps its word | the `package` job | an installed package that answers a version request it should refuse |

The baselines live in [`tests/api/`](../tests/api/) as generated text and are
reviewed as data, the way `tests/golden/snapshots/` is. To refresh one after a
release, run

```sh
python3 tools/api_surface.py -o tests/api/0.2.0.txt
```

and commit the file. A baseline that was not regenerated for the release it
names is a gate pointed at the wrong year, which is why the file is named for a
version rather than for a date.

## The gate for 1.0

1.0 is a list, not a date. It happens when every one of these is true:

1. The API surface has been frozen for one full MINOR with no break found by
   someone building real software on it.
2. `cf_ctx` is opaque to a caller — a widget reaches the engine through the
   functions it is given, not through a struct layout.
3. The backend ABI has held across one release without a driver needing a rebuild
   for a reason the notes could not name.
4. A conformance floor is published and CI refuses a release under it, per
   category rather than in aggregate.
5. The honest-limits list in `README.md` has shrunk to things a user can work
   around rather than things a user must route around.

Until then the major number stays `0`, which is the honest way to say *the
MINOR is where the breaking changes still live*.
