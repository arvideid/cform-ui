# Security Policy

## Supported versions

cform has no tagged releases yet. The maintained line is `main`; anything built
from an older commit is unsupported until the first tagged release adds a row to
the table below.

| Version | Supported          |
| ------- | ------------------ |
| 1.x     | to be declared once tagged |
| < 1.0   | not supported      |

## Reporting a vulnerability

Report suspected vulnerabilities privately instead of opening a public issue:
use [Report a vulnerability](https://github.com/arvideid/cform-ui/security/advisories/new)
to file a private advisory, or open a draft PR only if you would rather not
describe the flaw in prose.

Expect an acknowledgement within five working days. The response is a
reproduction and a plan, not a promise about a date; fixes land on `main` and are
announced through a release note or a security advisory once they are public.

## What counts as a vulnerability

Memory-safety defects reachable from untrusted input, since the framework is C
and the input is data written by someone else:

- A malformed asset (SVG, PNG, JPEG, font) or a hostile `http(s)` response that
  corrupts memory, rather than failing to load.
- A crafted string, style or layout value that overflows a fixed capacity or
  drives the layout loop unbounded.
- Missing bounds or privilege checks around remote image fetching, including
  following redirects to a host the caller did not authorise.
- Certificate validation that can be bypassed.

## What does not

- Crashes, hangs or leaks from a program that calls the API in a way the
  documentation forbids. The API's contracts are not hardened against callers
  who break them.
- Memory usage growth in a long-running UI that the arena or cache limits already
  describe.
- Vulnerabilities in the fetched dependencies themselves (SDL3, SDL3_ttf,
  SDL3_image, libcurl, FreeType, HarfBuzz). Report those upstream; this project
  tracks their releases and pins them in `CMakeLists.txt`.

## Hardening in place

All allocation happens in `src/base/`, which is where a bounds mistake has to be
found; the CI builds that layer and its tests with AddressSanitizer and
UndefinedBehaviorSanitizer enabled (`-DCFORM_WITH_SANITIZERS=ON`).
`tools/check_includes.py` keeps the layers that must stay headless free of SDL
and platform headers, and remote image fetching runs through one pinned file per
platform rather than a branch inside the renderer.
