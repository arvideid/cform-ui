# Architecture

cform is a C17 UI engine in ten layers. One frame is one function that returns a
tree of declaration values. The engine reconciles that tree with the slots it
kept from the previous frame, lays the survivors out, turns the resulting boxes
into a command stream, and hands the stream to a backend that knows how to put
pixels somewhere. Nothing in between holds a pointer the caller gave it.

## Layers

Each layer is one static library, and the edge list below is the link line in
`CMakeLists.txt`. A layer may only read what its target links;
[`tools/check_includes.py`](../tools/check_includes.py) checks the includes
themselves, including which layer a public header is allowed to reach.

| Target | Owns | Reaches |
| --- | --- | --- |
| `cf_base` | arena, generational store, interner, hash, log sink | libc |
| `cf_style` | `cf_style`, length and box math, themes and variants | `cf_base` |
| `cf_theme` | the default palette, one `cf_theme` value | `cf_style` |
| `cf_layout` | `cf_tree`, flex, grid, absolute, intrinsic measurement | `cf_style`, `cf_base` |
| `cf_text` | UTF-8 decode, break opportunities, line walking, measurement | `cf_base` |
| `cf_paint` | the command stream: fills, borders, text, images | `cf_layout`, `cf_text` |
| `cf_node` | context, keyed diff, state, input, focus, scroll, tweens, semantics | `cf_layout`, `cf_paint` |
| `cf_widget` | label, button, field, list, scroll and image declarations | `cf_node`, `cf_paint`, `cf_text` |
| `cf_app` | the run loop, the damage rule, the inspector | `cf_widget` |
| `cf_backend_headless` | a recorder that serialises the stream | `cf_paint` |
| `cf_backend_sdl` | window, draw, rasterisation, event pump | `cf_paint`, `cf_text`, SDL3 |

Two edges carry the design. SDL appears in exactly one target, so the engine and
its tests build with no renderer present at all. Display scale is decided inside
that same target: every layer below `src/backend/` counts logical points, and no
device pixel crosses the boundary upward.

`cf_layout`, `cf_style` and `cf_base` link nothing else, which is what makes the
engine shippable on its own — see [`../examples/layout_only.c`](../examples/layout_only.c)
and the single header `tools/amalgamate.py` packs from those four public headers.

## A frame, in order

`cf_frame_run` (`src/node/frame_run.c`) composes the pipeline in the only order
the layers can run in. Each step consumes exactly what the previous one wrote:

1. `cf_frame_begin` clears the frame arena, advances the frame id, drops state
   blocks no live node names any more, and resets every dirty bit.
2. The build function returns a `cf_node` tree that lives on the caller's stack
   for the rest of the frame.
3. `cf_diff` indexes the live keyed records, walks the declaration, claims the
   slot each node matches or allocates a fresh one, and retires what the
   declaration no longer names. A signature comparison decides who is dirty.
4. `cf_frame_resolve` applies the theme's per-state variants to the styles the
   diff stored, so paint reads one settled `cf_style` per node.
5. `cf_frame_layout` allocates one rect per slot from the frame arena and runs
   the layout pass over the tree view.
6. `cf_paint` walks the rects and appends commands to the caller's list.

`cf_app_run` wraps that in the parts a window needs: it polls events until the
backend runs dry, feeds them to `cf_input`, treats F12 as a toggle stored in the
same keyed state a build uses — the shell keeps no globals of its own — and
presents without repainting when nothing woke and the surface did not move. An
undamaged frame emits zero commands and allocates zero bytes, which is the
budget [`bench.md`](bench.md) measures against.

## Identity, slots and lifetime

A node is a slot in a `cf_store`, and a slot is addressed by a `cf_id`: an index
plus the generation stamped on it. Removing a node bumps the generation, so a
stale id reads `NULL` instead of the next occupant of the recycled slot. That is
the whole answer to the class of bug where an old handle silently edits an unrelated
widget.

Identity inside the tree is the `(depth, key, ordinal)` triple, not a pointer.
Depth strictly increases down every parent chain, so a cycle needs a depth that
does not increase and the data structure cannot express one. A declaration's
`n`-th use of a key meets the retained member with ordinal `n`, which is why
duplicate keys keep their state when the tree around them moves.

Memory enters in one of two arenas. The frame arena holds what dies with the
frame: tree views, rects, command lists, the measure cache, the match table. The
persistent arena holds what outlives one frame because it is named: state blocks,
scroll offsets, tweens, interned strings. Both grow by capacity doubling and
never relocate an element one at a time. A page that outgrows the frame arena
degrades inside its own frame — declarations it cannot serve paint nothing —
rather than failing the run, and `cf_error_message` says which refusal happened
after the fact.

## Boundaries

The command stream is the entire interface to a renderer. A backend implements
the slots in [`include/cf/backend.h`](../include/cf/backend.h) and reads
`cf_command` values; it never sees a node, a style or a tree. The contract, the
lifetime rules and the checklist for writing one are in
[`backend-abi.md`](backend-abi.md).

`cf_tree` is a borrowed view the layout pass reads, built by the node layer from
retained records. Layout allocates nothing but rects and its own scratch, calls
no renderer, and can be run head to head against the fixture corpus with no other
layer linked.

A public header states one contract and never the machinery behind it. The opaque
types a caller passes back are handles; everything else is a value struct it may
build on the stack.

## How the boundaries are tested

Every layer has a unit binary beside it, and the engine's own binaries link no
renderer at all — the core is testable headless, which is why it is testable.
The one case that rasterises glyphs through the SDL backend is skipped when the
backend is off. The conformance tier runs Yoga's fixtures as data and publishes
its score in [`conformance.md`](conformance.md). The golden tier runs whole frames
through the recorder backend and diffs the command stream, so a paint regression
is a reviewable data diff rather than a pixel somebody squints at. The bench tier
prints the two 10,000-node rigs on all three operating systems. ASan and UBSan
run the rest of the suite over a Linux clang build.

What no tier checks is what the window draws. That is verified by running
`examples/gallery.c` and looking at it, and a build green across four platforms
does not change who does the looking.
