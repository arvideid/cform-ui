# Stability, for bindings and embedders

The C headers in `include/cf/` are the ABI. A binding in another language wraps
them; nothing else is a contract, and the layout of `src/` is not.

## What a binding may rely on

- **Symbols** are prefixed `cf_` for functions, `CF_` for types, enums and
  macros. A name outside `include/cf/` is not exported policy: `src/*/internal.h`
  is shared engine code and can change without notice.
- **Handles, not pointers.** `cf_id` and `cf_name` are `uint32_t`. A binding
  stores and passes them freely, and never dereferences them: an id is only
  meaningful against the store or interner that issued it, and a slot's memory
  moves when the store grows. `CF_NO_ID` and `CF_NO_NAME` are the empty values.
- **`cf_*` structs are plain data**: `cf_rect`, `cf_size`, `cf_point`,
  `cf_color`, `cf_edges`, `cf_style`, `cf_node`, `cf_command`, `cf_event`. They
  have no constructors, no vtables, no hidden padding logic, and their zero
  state is valid — a binding can allocate one with `calloc`, `memset` or its
  language's default and read it safely.
- **Configs pass by value** (`cf_app_config`, `cf_sdl_config`). A binding may
  build one on the stack; the engine copies what it needs to keep.
- **A `cf_string` is `{chars, length}` and borrowed.** It is not NUL-terminated,
  so a binding that hands one to a C string API must copy. Nothing keeps a
  `cf_string` as a key: interning copies the bytes.
- **Enums are append-only.** `cf_command_kind`, `cf_event_type`, `cf_role`,
  `cf_length_kind`, `cf_ease`, `cf_cursor` keep their existing numbers forever;
  a new variant is added at the end. A binding may hardcode a value it has
  already seen. Nothing may be renumbered or reordered — that is a breaking
  change, not a patch.

## What a binding must model

**Two lifetimes, not one.** The context owns two arenas: a frame arena that is
cleared at the start of every frame, and persistent memory that survives across
them.

| Produced by | Lives until | A binding must |
|---|---|---|
| `cf_alloc(&ctx->frame, …)` | next `cf_frame_begin` | not store the pointer |
| `cf_state`, `cf_scroll_of`, `cf_tween_of` | `cf_ctx_free`, unless the arena fills | check for `NULL` at creation |
| `submit`'s command array | end of the call | copy to draw later |
| `cf_interned` | `cf_ctx_free` | treat as read-only |
| `cf_semantic_export`'s output | the caller's own buffer | size the buffer first |

A node tree a `cf_build_fn` returns is frame memory, root and children arrays
alike: it is read during the build, and it is dead by the next one. That is why
the build function returns `const cf_node *` rather than owning anything.

**One thread.** The engine takes no locks and expects none. A frame is
`cf_frame_begin` → build → `cf_diff` → `cf_frame_resolve` → `cf_frame_layout` →
`cf_paint`, and no callback (`cf_build_fn`, `cf_measure_fn`, `cf_handlers`,
`cf_text_ops`, the backend slots) may be entered from another thread while a
frame is in flight. A binding with a garbage collector or an async runtime
marshals frames to one thread.

**Exactly one failure mode per function**: a `NULL` return, a silent no-op for
`void`, or cannot-fail. There are no error codes, no `errno`, no exceptions and
no `assert` in shipped headers. A binding maps `NULL` to its optional type and
does not invent an error path the C side does not have. Logging is a separate
channel (`cf_logf` on a named channel) and never a return value.

## Versioning

The library version is `CMakeLists.txt`'s `project(cform-ui VERSION …)`.

| Change | Version | Commit |
|---|---|---|
| New function, new enum variant at the end, new example | minor | `feat(...)` |
| Fix that keeps every header intact | patch | `fix(...)` |
| Any change to a struct's fields, an enum's existing number, a function's signature or failure mode | major | `feat!`/`fix!` with a `BREAKING CHANGE:` footer naming the migration |

`CF_BACKEND_ABI_VERSION` in `cf/backend.h` is narrower and moves on its own: it
names the command-stream layout a compiled backend was built against, because a
backend that misreads a stream draws wrong instead of failing. It is bumped
whenever `cf_command`'s layout changes, and a backend checks it before its first
submit.

## What is still moving in the alpha

While the version carries an `-alpha` suffix these are documented as data, not
as contracts, and may change without a major bump:

- the inspector panel's contents and its state key (`cf/inspector`),
- the text format `cf_semantic_export` writes — the shape of the tree and the
  role names are stable, the exact line layout is not,
- which examples exist and what each demonstrates,
- the golden snapshots under `tests/golden/snapshots/`, which are rewritten by
  an update tool and reviewed as data.

`include/cf/` itself is not on that list: it has the same breaking-change rules
during the alpha as after it.
