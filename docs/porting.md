# Porting and Embedding

Two different jobs get confused with each other, so this document separates them:
putting the engine inside a program you already own, and taking the drawing onto
a platform the SDL backend does not cover. The renderer contract itself is
[`backend-abi.md`](backend-abi.md); this is the route in from either side.

## Choose the floor you link

**Layout only.** `cf_layout`, `cf_style`, `cf_base` — three static libraries and
`include/cf/layout.h`. Nothing else is required, nothing else is linked, and no
renderer exists at this floor. [`../examples/layout_only.c`](../examples/layout_only.c)
is the proof: it owns an arena, builds a `cf_tree` by hand and reads the rects
back. If a consumer wants one file instead of three libraries and four headers,
`tools/amalgamate.py` packs `types.h`, `base.h`, `style.h` and `layout.h` into a
single `cf_layout.h`:

```sh
cmake --build --preset dev --target cf_amalgamate   # -> build/amalgam/cf_layout.h
python3 tools/amalgamate.py --out cf_layout.h       # or straight from the source tree
```

The packer fails by name if a packed header starts reaching a layer outside the
set, so the generated file cannot silently grow a dependency.
[`../tests/unit/layout_amalgamated.c`](../tests/unit/layout_amalgamated.c)
compiles against the generated header and nothing else, which is what keeps that
promise checked rather than claimed.

**The engine, no window.** Add `cf_text`, `cf_paint`, `cf_theme`, `cf_node` and
`cf_widget`, and drive the frame yourself: `cf_ctx_init` with the two arena
sizes your page needs, then `cf_frame_run` per frame against the size your host
has, and hand the resulting command list to whatever consumes it. The headless
recorder (`cf_backend_recorder`) is the consumer tests use — it keeps the last
stream in an arena so a case can read the draws instead of looking for a window.

**A whole program.** `cf_app` plus a backend. `cf_app_run` takes a build
function, a backend by value, a borrowed theme and a borrowed font stack, and
runs until the backend reports quit. It does nothing at all — silently, which is
the shell's only failure mode — when the build is missing, when the backend
cannot submit, when the backend reads a command layout this build does not emit,
or when its arenas cannot start.

### Taking the floors from an installed package

`cmake --install build --prefix <prefix>` lays down the ten headless archives,
`include/cf/`, a CMake package config and a `cform.pc`. A CMake consumer resolves
the same layer graph by name:

```cmake
find_package(cform-ui REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE cform::cf_app)
```

Every installed target carries its own dependencies, so `cform::cf_layout` is the
layout floor and nothing more. Without CMake:

```sh
pkg-config --cflags --libs cform   # deepest layer last; drop the floors above the one you call
```

The installed set stops at the headless layers on purpose. `cf_backend_sdl`
references `SDL3::SDL3`, a target the fetch creates inside this repository's
build tree, and a host that wants a window already has its own SDL link line —
build the backend from source with `CFORM_WITH_SDL_BACKEND=ON`, or write the
platform's own against [`backend-abi.md`](backend-abi.md).

## Build knobs

CMake is the single source of truth: the compiler, the C standard, the include
paths and the warning flags are set once in `CMakeLists.txt`, and CI, clangd and
your terminal read the same `compile_commands.json` it writes. One minimum,
3.25, in the `cmake_minimum_required` call and in `CMakePresets.json`.

| Option | Default | Turns on |
| --- | --- | --- |
| `CFORM_BUILD_TESTS` | `OFF` | the unit, conformance, golden and bench tiers |
| `CFORM_BUILD_EXAMPLES` | `OFF` | the standalone programs under `examples/` |
| `CFORM_WITH_SDL_BACKEND` | `OFF` | `cf_backend_sdl`, and the SDL fetch below |
| `CFORM_WITH_SANITIZERS` | `OFF` | ASan and UBSan over every layer; fatal when the compiler cannot link them, so a green instrumented build is never an accident |

`cmake --preset dev` configures Ninja, Debug, with tests, examples and a compile
database; `--preset release` is the build the bench numbers come from.

When `CFORM_WITH_SDL_BACKEND` is on, the build fetches SDL3 at `release-3.2.20`
and SDL3_ttf at `release-3.2.2` and builds them in the tree. Those two are the
entire third-party surface: the engine links no image decoder, no SVG parser and
no HTTP client, and nothing outside `src/backend/` includes any of them. The one
asset the examples read — the Lato face used for labels and for the inspector —
is downloaded at configure time rather than committed, with its status checked,
so an offline machine gets a page without labels instead of a failed configure.

## What a platform supplies

A backend is one struct of five function pointers plus a `user_data`: submit,
present, create texture, poll event, surface. What each owes the engine, what is
borrowed and for how long, and how to test a port without looking at a screen is
[`backend-abi.md`](backend-abi.md). Three of those seams are where a platform
actually shows through:

- **Scale and vsync are the backend's.** The engine lays out in logical points
  and asks `surface` for the extent again every frame, so a resize needs no
  callback. Pixel density is applied on the way to the device and never travels
  upward.
- **Input is a queue you drain.** `poll_event` moves one device event into the
  engine's normalised `cf_event` or answers false. A port with no input leaves it
  `NULL` and runs without events; a port with a callback-driven event loop pushes
  into a queue of its own and lets the shell drain it once per frame.
- **Pixels enter as RGBA8888 and nowhere else.** `create_texture` uploads a block
  of bytes and returns an id the image commands name; zero means nothing was
  uploaded and nothing will draw. Decoding is the caller's: a port that wants
  PNGs, SVGs or downloaded assets brings a decoder and hands over finished pixels.
  The engine never fetches.

Fonts are the one place a port writes more than a struct. `cf_font` is a handle
plus three operations — metrics, advance, glyph coverage — and measuring never
rasterises, so a platform that can report a face's metrics can drive the text
layer without a GPU present. `cf_font_sdl` is the SDL3_ttf implementation of that
table and the model to copy.

## Threads, and the one global

`cf_ctx` owns everything a page keeps and takes no lock: one context per thread
is the whole concurrency story, and sharing one between threads is not supported.
The single exception in the library is the installed log sink, which is
process-wide because logging has no context to hang off. A port that installs a
sink from more than one thread has to serialise the install itself; nothing else
in `src/` changes a file scope after startup.

## WebAssembly

`examples/gallery.c` compiled under Emscripten *is* the playground: the same
source, the same backend, a `--shell-file` page and the font preloaded into the
data filesystem. CI treats it as a gate rather than a local step, pinned to the
SDK version named in `.github/workflows/ci.yml`, because an SDK that moves under
the build reports divergence nobody in the diff caused:

```sh
emcmake cmake -S . -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCFORM_WITH_SDL_BACKEND=ON -DCFORM_BUILD_EXAMPLES=ON
cmake --build build-wasm --parallel
```

Under Emscripten the example is linked to `.html`, with the `.js` and `.wasm`
beside it, and the browser is the only thing that presents frames. Sanitizers are
a clang-on-Linux job; the wasm build is a compile-and-link gate, and the headless
tiers still run on the host.

## Verifying a port

Nothing here needs a screen, which is the point. `ctest --preset dev` runs the
unit, conformance and golden tiers; `ctest --preset dev -L bench` prints the
throughput rigs. A port that passes `backend_record` and the golden cases emits
exactly the stream the reference backend does. What a port has to be *seen*
doing is the window: the demo, run by a person, and the console output when the
result cannot be read from the code.
