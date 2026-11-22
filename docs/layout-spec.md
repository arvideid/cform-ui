# The Layout Spec

What this engine implements of CSS layout, which parts of the spec each file
follows, and where it deliberately stops. The measured results — the flexbox
score and the throughput numbers — are published in [conformance.md](conformance.md)
and [bench.md](bench.md); this document is about the rules, not the score.

## The call

`cf_layout(scratch, tree, root, available, origin, rects)` is a pure function of
its tree and its offer. The tree is parallel arrays indexed by slot — styles,
measure callbacks, child ranges — and the only thing the pass writes is one
absolute border-box rect per node it reaches. It allocates from `scratch` and
nowhere else, calls nothing outside `cf_style` and `cf_base`, and never reads a
renderer. Each node hands its scratch back before descending, so the high-water
mark follows the deepest path rather than the node count, and an arena cleared
once per frame leaves the whole pass allocation-free.

The same call answers both questions the engine asks a container: a definite
offer arranges and writes rects, an `auto` axis reports the extent the children
need and writes nothing.

When `scratch` cannot serve, the node that asked is laid out as a leaf — its own
rect is still written, its subtree keeps the rects it had — and the exhaustion is
reported once on the layout log channel. Layout does not fail a frame.

## Lengths

`cf_length` is a tagged unit: `CF_AUTO`, `CF_PX`, `CF_PERCENT`, `CF_FR`. A
percentage resolves against the reference the caller passes, and an axis that is
still being measured has no reference to give — `cf_length_of` then returns false
rather than zero, which is the engine's signal to measure the content instead.
`CF_FR` holds nothing outside a grid track. Percent padding, percent border and
percent column gaps resolve against the containing block's width, as CSS does,
whatever axis they sit on.

A declared `width` or `height` is the **border box**: padding and border sit
inside it, and `min`/`max` clamp it with them. CSS's `content-box` default is not
inherited on purpose — a box that has to be measured twice to learn its own
padding is not worth the citation. Negative extents read as zero, `min` keeps the
last word when `min` and `max` contradict each other, and a percentage bound
against an indefinite reference bounds nothing.

## Flexbox

The pass runs CSS Flexible Box Layout §9.2 through §9.7 per container, with §9.1
item 4 and §9.8 beside them. What that means in practice:

- **Direction and reversal.** All four directions. §9.3 moves main-start to the
  far physical edge for a reversed axis; §9.4 and `wrap-reverse` do the same for
  cross-start.
- **Basis, grow, shrink.** `flex-basis` of `auto` reads the declared size and
  falls back to a content measure. The freeze loop resolves violated `min` and
  `max` and repeats until nothing more can be frozen, so `flex-shrink` below a
  floor and `flex-basis` above a ceiling land where the clamps say.
- **The automatic minimum size.** §9.9.1: with no declared minimum an item cannot
  shrink below the size its content needs on the main axis.
- **Wrapping.** `nowrap`, `wrap` and `wrap-reverse`, with lines broken between
  items and never inside one (§9.2.2). A measure has no main size to break
  against, so max-content keeps every item on one line and min-content gives each
  a line of its own — which is how the narrowest a wrapped container can go is
  found. A container that declared its main size breaks at that size in a measure
  exactly as it does when arranging.
- **Distribution.** All six `justify-content` values, all seven `align-content`
  values including `auto`, `align-items` with `stretch` as the container's own
  `auto`, and `align-self` overriding it per item. Auto margins take the free
  space before `justify-content` sees any of it (§9.5.2).
- **Baselines.** `align-items: baseline` and `align-self: baseline` place a
  baseline group on the tallest ascent among the items that share it (§9.8),
  including the container's own first baseline handed upward — a row's first line
  carries it, a column counts its first item down its main axis.
- **Re-measurement.** §9.2.2 steps 16–18 and §9.6.1: an item whose main size
  changed since it was measured is measured again, because its content can wrap
  differently at the size it ends up with.
- **Order.** `order` sorts before packing, stably, with declaration order
  breaking ties; the sort is skipped when every item declares zero.

## Grid

`display: CF_DISPLAY_GRID` places children into tracks instead of lines. CSS Grid
§12.4 through §12.7 runs on both axes: item floors and ceilings, then the
fractional shares of whatever the fixed and content-sized tracks leave over
(§12.7), with a track stopping at its ceiling as soon as there is room to say so
(§12.6 step 1). Leftover space is only shared out when the box is already sized;
while an axis is still being measured the grid hugs its content.

The supported subset, as the API exposes it:

- Tracks are `cf_track { min; max; }`, which is `minmax()` and nothing else.
  `CF_AUTO` as a floor collapses, as a ceiling hugs the items that fill the track
  alone; `CF_FR(v)` claims `v` shares; `CF_PX` and `CF_PERCENT` fix. `auto` is
  `{CF_AUTO, CF_AUTO}` and a fixed track repeats its length in both slots.
- The explicit grid is two borrowed arrays with counts. `cf_tracks_repeat` writes
  a pattern end to end `times` so a `repeat()` can be expressed; the engine reads
  a flat array and never sees the pattern.
- A container with no declared columns gets one content-sized column. Rows the
  items overflow into are added as content-sized tracks, and a span that reaches
  past the declared grid cannot ask for tracks nobody declared.
- Placement is 1-based `start` plus `span` on each axis. Zero on either is `auto`:
  the first free cell the row-major scan reaches, dense, one track wide. Out of
  flow children get no cell.
- `column_gap` and `row_gap` are lengths; percent resolves against the width.

Grid does not distribute free space inside a cell, so `justify-content`,
`align-content` and `align-self` belong to flex lines and change nothing here: an
item with a definite size takes its cell's start edge plus its own margin, and an
item with an `auto` size stretches to the cell. Named lines, named areas and
`grid-template-areas` are not in the vocabulary.

## Out of flow

`CF_POSITION_ABSOLUTE` leaves the container's flow and is measured and placed
against its containing block: the padding box of the nearest ancestor that is not
`static`, or the root's own box when the chain never leaves `static`. Its static
position still comes from the parent that would have held it, so all-`auto`
insets put the child where the flow would have put it, with that flow's own
packing (§4 of the box model as Flexible Box Layout asks of it). `CF_POSITION_RELATIVE`
stays in the flow and moves by its insets. Out-of-flow children add nothing to
their container's measured extent, which is why a box holding only them measures
as empty. Whether a box's children paint inside its rect is `clips_content`, and
paint enforces it; layout never clips.

## Measuring content

A node with a `cf_measure_fn` answers for its own content instead of deriving it
from children: text and images get their size this way, and a widget that owns a
box installs one. The contract is a cache's contract —

> the answer must be a pure function of the node, the intrinsic kind and the
> content offer, because within one layout the engine serves any repeat of that
> triple from the table it filled the first time.

Subtrees whose answer cannot depend on the offer — leaves, mostly — share one key
per slot rather than one per (slot, offer), which is what keeps a deep hug chain
from asking the same question at every level above it. An axis still being
measured arrives at the callback as `CF_AUTO`, so a callback with a percentage of
its own has no reference to work from and has to answer from content. A missing
callback is measured from the node's children.

## Not implemented

Writing modes and right-to-left direction, logical properties, `aspect-ratio`,
container queries and `calc()`: none of them are in the vocabulary, and no field
pretends to accept them. Bidirectional text and variable fonts are out of scope
for the text layer too, and are documented as out of scope rather than left to
surprise someone. Anything in this list would be a public API change, so it lands
in `include/cf/` first and in the layout files second.
