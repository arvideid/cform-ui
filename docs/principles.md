# Principles

The README states these as bullets because a person deciding whether to read the
source needs them in ninety seconds. This is the long version: the rule, the failure it
prevents, and the place it is checked. Nothing here is a preference dressed as a rule —
every section names what goes wrong without it.

A rule nobody can violate is a preference, so each one ends with the gate that fails
when it is broken. Where a gate does not exist yet, the row says so.

---

## 1. The layer graph is a link graph

Each layer is its own CMake target, and a target may link only what its row allows. The
header column is the file a reader opens: every public name lives under `include/cf/`, so
the layer is a link edge and the header is its address.

| target | header | links | and nothing else |
| --- | --- | --- | --- |
| — | `types.h` | — | `cf_point`, `cf_size`, `cf_rect`, `cf_color`, `cf_length`, `cf_id`, `cf_string`: the value types every other header states a field with |
| `cf_base` | `base.h`, `version.h` | — | the arena, the intern table, the log, `CF_DEPRECATED`, the version string |
| `cf_style` | `style.h` | `cf_base` | `cf_style`, lengths, the box and track arithmetic |
| `cf_theme` | `theme.h` | `cf_style` | the variant sets and `cf_theme_default()` |
| `cf_layout` | `layout.h` | `cf_style`, `cf_base` | flexbox and grid arithmetic, no text, no drawing |
| `cf_text` | `text.h` | `cf_base` | line breaking and measurement against a `cf_font` |
| `cf_paint` | `paint.h` | `cf_layout`, `cf_text` | the command stream: fills, borders, text, images |
| `cf_node` | `node.h` | `cf_layout`, `cf_paint` | the retained tree, the diff, input routing, state |
| — | `build.h`, `event.h` | the layer each name belongs to | `CF_ROW`/`CF_COL` over `cf_row`/`cf_col`, and the one value a handler receives |
| `cf_widget` | `widget.h` | `cf_node`, `cf_paint`, `cf_text` | label, button, field, list, scroller, image |
| `cf_app` | `app.h` | `cf_widget` | the frame driver and the idle test |
| `cf_backend_headless` | `backend.h` | `cf_paint` | the recorder every test links |
| `cf_backend_sdl` | `backend.h` | `cf_paint`, `cf_text`, SDL3 | the one backend that knows about windows |

`event`, `build` and the two headers a backend is built against are declarations rather
than their own target: `cf_event` is a value, and a layer that only reads one needs nothing
linked to get it.

**Why the link is the rule and not the include.** An include is a request to the
preprocessor and it succeeds wherever the include path reaches — which in this
repository is everywhere, because every public header sits in one directory. So
`cf_layout.h` reaching `cf/text.h` compiles cleanly and shows up as an undefined symbol
in whichever program happened to use the declaration, on the other side of the world, in
a build you did not run. A link edge cannot be accidental: `target_link_libraries` is the
only thing that puts a header directory on a consumer's include path, and the linker
refuses what it has not been given. The CMake graph is therefore the architecture, and
the include list is a claim about it.

**Checked by** `tools/check_includes.py`, which reads both halves: the source tree may
not include a header its layer does not link, and neither may a public header — which is
the half a compiler cannot catch, since a header is compiled in the context of whoever
includes it. It also fails a public header that reaches into `src/` internals, that
includes SDL, or that lets a raw value do a sentinel's job.

---

## 2. Describe the frame, do not manage it

`build` returns a value. It is not told to update anything, it keeps nothing, and it is
thrown away at the end of the frame. Identity is a name rather than an address: a node
slot is an integer, so an out-of-date handle fails safely instead of walking into memory
some later box now owns. Anything a page must survive the rebuild with is asked for by
name from the context — `cf_state(ctx, cf_intern_text(&ctx->names, "clicks"), size)` —
and those bytes go back to the heap the frame after the builds stop asking.

**Why.** The bug class this removes is the one every widget toolkit is famous for: an
object kept past the data that made it, updated in one of the four places that change it
and stale in the other three. If there is no object to keep, there is no keep-in-sync
obligation to miss, and the question "did every path that changes the name also change
the label?" has no paths to answer about.

**What it costs.** You cannot hold a widget across frames, and you cannot ask the tree
what is on screen by walking a pointer you saved. That is deliberate and it is on the
honest-limits list rather than under the rug.

**Checked by** `tests/unit/node_rebuild.c`, which drives random declarations through one
context and fails on the frame that loses a name it should have kept.

---

## 3. No markup language and no stylesheet

There is no parser in this repository, no template syntax, no selector engine, no
cascade, and no code generation step. A screen is a C function; a rule block is a struct.

**Why the omission is the point.** A second language is a second everything: a grammar to
version, an error reporter to write, an editor plugin to make the types work, a debugger
that cannot step into it, and a build that now needs the generator to be the same version
on every machine. Every one of those costs is permanent and every one is paid by people
who did not choose the language.

**What C buys instead.** A misspelled property is a compile error with the correct
spellings in the diagnostic. Refactoring a screen is renaming a function. A style is a
value you can print, compare in a test, and derive from data with an `if`. The public
headers are the documentation because they are the whole interface, which is what lets a
CI gate diff the API as text (§1 of `docs/versioning.md`).

**What it gives up.** No cascade means no "every second button in the app gets a red
border" from one place; the answer is a function that returns a `cf_style`, and the
`cf_theme` variant sets cover the family of cases that a cascade is usually hired for.
This is a real limit and it is listed as one.

---

## 4. A frame that saw nothing moves costs nothing

The idle test lives in `cf_app_run`, not in the pipeline. When the description is
unchanged and no event arrived, the shell does not call the pipeline at all: nothing is
built, laid out, emitted or allocated. Call `cf_frame_run` on an unchanged screen and it
runs whole — build, diff, layout, paint — because it is a function that does a job, not a
cache pretending to be one.

**Why the distinction is stated instead of glossed.** "Nothing costs nothing" is true of
the shell and false of the frame driver, and a reader who takes the strong version to
heart will be surprised by their own profiler. The weak version is still worth having: a
UI sitting in a laptop bag should not be measuring flexbox sixty times a second.

**What wakes it.** A pointer entering or leaving a box, a key, a scroll wheel, a resize,
a timer, and any state a page reports as changed.

**Checked by** the bench suite in `docs/bench.md`, which reports the idle case as a
number beside the animated one, and `tests/unit/app_run.c`.

---

## 5. Points above the backend, pixels below

Every length a caller writes and every rectangle a test asserts is a logical point. The
backend is handed points plus a scale and does the rounding into whatever a real device
owns. A `cf_length` carries the unit in its kind — `CF_PT`, `CF_PX`, `CF_PERCENT`,
`CF_FR`, `CF_AUTO` — rather than being a float with a convention beside it.

**Why.** A DPI of 1.75 exists. So does a window a drag makes 400 points wide, and a
golden test that stores numbers a person can read. If the engine rounded to device pixels
then the answer to "where is this label" would depend on the display attached, and every
screenshot comparison would be a comparison of two machines.

**Why the kind instead of a bare float.** Because `0` is a legitimate length and it is a
terrible way to say "not specified". The same reasoning made colors and borders tagged
(`cf_opt_color` carries a `present` flag) and it is why `check_includes.py` fails a public
header whose comment describes a raw value standing in for absent: that comment is the
first draft of a bug.

**Checked by** the golden tier, whose stored streams are point-valued and therefore
identical on a 1x and a 2x display.

---

## 6. Allocation lives in one layer

Nothing outside `src/base` calls `malloc`, `calloc`, `realloc` or `free`. A frame takes
bytes from `ctx->frame`, which is reset when the frame ends; anything that must outlive
one takes bytes from `ctx->store`, which is an arena that hands out blocks and returns
them by name. Every allocation in the library is therefore one of two lifetimes, and
"who owns this byte" has the same answer everywhere: whoever owns the arena.

**Why.** A leak needs two people: an allocation and a path that can miss the free.
Removing the second is cheaper than auditing it, and it removes the class of bug where a
free runs on one of three error paths. It also makes a frame's cost measurable as a
watermark rather than as a hope, which is what `docs/bench.md` is able to report.

**What it costs.** A pointer into the frame arena is invalid after the next frame. That is
the price and it is paid by the caller, which is why a node is a value you rebuild rather
than an object you hold.

**Checked by** `tools/check_includes.py`, which fails a direct allocation call anywhere in
`src/` outside the base layer. One exception is allowed and named in the script: bytes
the platform allocated and documents itself as freeing, which the framework did not
allocate and therefore must not pretend it did.

---

## 7. Lean, and comments that say why

No templates, no reflection, no generated code, no dependency inside the engine. Each
layer is small enough to finish reading in one sitting, and the whole pipeline from
description to submitted commands fits in the space of a long afternoon.

A comment that restates the line under it is noise, and a comment that is wrong is worse
than none, because it is read as fact. The habit here is that a comment names a
constraint, a failure, or a decision someone would otherwise undo: why an array is
copied rather than borrowed, why a role is cleared, why a bound exists at
`48` bytes. When a rule is enforced by a gate, the comment says so and points at the
gate.

**Why leanness is a goal and not an aesthetic.** Every line in this repository is a line
someone must maintain forever, and a feature that costs a thousand lines of abstraction
also costs a thousand lines of interface that a future change has to work around. The
strongest pull against this is convenience, so the check is external: `docs/backlog.md`
holds the rows that were declined because they add more than they remove.

**Checked by** review, which is the honest answer, plus the layer graph and the
allocation rule above — the two gates that stop growth from quietly becoming structure.

---

## 8. Every public name has a caller

A symbol the shipped code never calls is an interface nobody has tested: its signature is
unproven, its behaviour is unwritten by any real use, and it will be the first thing found
broken by someone with a use for it. So each public name is expected to be reached from
the library itself, from an example, or from the consumer program, and anything that
cannot be — a function the *host* calls, a backend slot filled by an outside
implementation — sits in a written allow-list rather than being quietly exempt.

**Checked by** `tools/check_reachability.py`. It reports for one release before it turns
red, because a gate that fires on a hundred pre-existing rows the day it lands teaches
people to silence it rather than to fix it.

---

## 9. A break is a decision, not an accident

Enum values are appended or nothing changes; the backend struct grows at its tail or the
ABI number moves; a removal waits for a major. The rules, the version semantics and the
ABI axis all live in `docs/versioning.md`, which is their only home — this file links to
it rather than restating it, because a policy written twice stops being true in one of the
two copies the first time either is edited.

**Checked by** `tools/api_surface.py` and `tools/check_api.py`, which classify the diff
against the last release and name the smallest bump that would be legal.

---

## 10. The tests are the specification

The engine is testable without a window because of §1 — nothing above the backend knows
one exists — so the interesting behaviour is checked by running it rather than by looking.
There are four tiers and each is chosen for what it can say:

- **unit** — one layer, one contract, small numbers asserted exactly.
- **headless gallery and site** — a whole real application driven through the frame
  pipeline against the recorder, with the command stream judged as data: geometry finite,
  clips inside the output, radii inside their boxes, sharp fills that arrived cut.
- **golden** — a frame's stream stored as text and reviewed as a diff, which catches the
  change that is correct on every number and wrong on the screen.
- **conformance and bench** — flexbox scored against Yoga's own corpus rather than against
  this repository's opinion of it, and timings recorded so a slowdown cannot arrive
  unnoticed.

**Why a rendering is tested as data.** "It looks right" is not reproducible, and a
framework whose only check is a human opening a tab cannot be changed safely by anyone who
is not the person who opened the tab. The pixels are for a person to judge, once, on
purpose — see `docs/maintenance.md` for where that fits.

**Checked by** CI, on three operating systems, on every push.

---

## Where each rule is checked

| rule | gate |
| --- | --- |
| layer graph, allocation, sentinel comments | `tools/check_includes.py` |
| every public name reachable | `tools/check_reachability.py` |
| API diff matches the version | `tools/check_api.py` |
| the version is one number in three places | `cmake/cform_version.cmake --check` |
| the docs agree with the headers | `tools/check_docs.py` |
| the site carries no borrowed colour | `tools/check_docs.py`, over `examples/site/` |
| a page renders at every width | `tests/unit/site_headless.c` |
| the gallery still draws what it drew | `tests/golden/`, `tests/unit/gallery_headless.c` |
| flexbox is right, not approximately right | `tests/conformance/yoga_cases.c` |

If a rule in this file has no row above, it is a preference and should be argued with.
