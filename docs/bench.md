# Bench baselines

Published numbers from the `bench`-labelled CTest targets in `tests/bench/`. A
baseline lives here so a regression is a diff somebody reads instead of a
rumour: run the rig, compare against the table, change the table in the same
pull request that moves it.

```sh
ctest --preset release -L bench   # the numbers below
ctest --preset dev -L bench       # Debug, roughly three times slower
```

## Release, 1280 × 720 offer

Best of nine consecutive runs on a Windows desktop, the `release` preset, GCC
from MSYS2 UCRT64. Each run already reports its fastest pass of 50 layouts,
because a shared machine spends its wall time on somebody else's process. Passes
spread 20 to 30% on an idle machine and about double on a busy one: the table
holds the fastest pass observed while nothing else was running, and the low end
is what describes the engine.

| rig                  | nodes  | ms per layout | ns per node |
|----------------------|--------|---------------|-------------|
| deep flex tree       |  1,250 | 0.29          | 230         |
| deep flex tree       |  2,500 | 0.59          | 237         |
| deep flex tree       |  5,000 | 1.30          | 261         |
| deep flex tree       | 10,000 | 2.84          | 284         |
| flat 100-column grid | 10,000 | 5.31          | 531         |

The deep rig is a complete binary tree of alternating rows and columns with a
third of the branches growing into the leftover space and a seventh claiming half
the width. The root declares the offer it is given, so no branch measures
against a box that is itself still being measured. The flat rig is one grid
container of `fr` columns and content rows, every cell a leaf. Both are 10,000
nodes because that is the size the budget names.

## The frame and the stream

`bench_node` reconciles one 10,000-node declaration through a single context;
`bench_paint` turns a laid-out 10,000-node page into the command stream a device
would be handed. The paint rig's 99 rows of 100 cells stay inside the surface,
which is what lets its command count be an exact number rather than a range.

| rig              | what                                                    |    ms |
|------------------|---------------------------------------------------------|-------|
| cold build       | first diff of the declaration                            |  5.98 |
| idle diff        | the same declaration again, nothing changed              |  4.46 |
| diff and layout  | one whole frame of it                                    |  6.46 |
| paint            | 12,475 commands out of 10,000 boxes                     |  0.20 |

The paint rig asserts its stream: one fill per box, a border over every fourth
cell, the page's own fill first, and nothing at all for a viewport that reaches
nothing. A repaint of that page costs 2,810 KiB of frame scratch, which is what
a shell sizes its arena against.

## What the budget says

2 ms at 10,000 nodes is the target. The deep tree reaches 2.8 ms and the flat
grid 5.3 ms, so the budget is still open and the grid is the rig that misses by
more. The idle half of the same line is met — an unchanged frame dirties
nothing, lays out nothing and hands the backend zero commands, held by the node
rig's own assertions, the golden `idle_frame` scene and the shell's
undamaged-frame case.

Cost per node rises 1.2× over three doublings of the tree, 230 ns at 1,250 nodes
against 284 ns at 10,000, where it rose 1.7× while every ask still paid for a
leaf cell and a freshly resolved frame. The measured answer still lives only as
long as the layout that found it: each of the 50 passes rebuilds the table from
nothing.

The flat grid is not paying for measurement: it cost 9.3 to 10.6 ms before
measures were cached and 8.8 to 12.8 ms after, inside the spread either way.
What it pays for is the number of full track-sizing runs one layout performs,
measured by rebuilding the rig's tree with one decision changed at a time:

| the same 10,000-cell grid      |    ms |
|---------------------------------|-------|
| both axes auto (the rig)        | 10.1  |
| width stated, height auto       |  6.3  |
| both axes stated                 |  3.9  |
| both axes stated, `px` tracks   |  3.6  |
| 10 columns instead of 100        | 10.2  |
| 1,000 nodes instead of 10,000    |  0.6  |
| same children, one flex column   |  2.2  |

Every row belongs to the build that was measured before an ask got cheaper, and
that day's machine: the same rig in the row above the table now reads 5.3 ms.
Only the ratios between rows carry over.

A sizing run walks the child list once per axis per intrinsic mode. A box that
states both sizes needs one run, when it places; a box that states neither needs
three — max-content to guess the width, min-content to keep that guess from
falling below what cannot break, and the run that places. Doubling the column
count moves the number barely at all and halving the nodes halves the time, so
the cost is per item and not per track, and 3.9 ms for one run against 2.2 ms for
the same children in a flex column is the grid's own overhead above that.

The reduction §12.5 points at has been taken: a track's floor comes from its
items' min-content contributions and its ceiling from their max-content
contributions, so one walk already holds both answers for an item with no
children and no measure callback - its border box is the same number at either
intrinsic kind. The floor pass records each item's weight and the ceiling pass
reads it back. The flat grid went 6.6 to 5.3 ms with both binaries run back to
back on one machine, three times each. What is left of the doubling is the
sizing *run*, not the walk: an auto-sized box still runs the solver three times.
None of this is what the flat grid pays: a tree two levels deep is refused a
table at all, because nothing in it re-asks a question.

## What one deep layout actually does

Counters bolted onto the memo and the placing walk, the deep rig at 10,000 nodes,
as first measured — a childless node still asked for a cell then, and a frame was
still resolved on every ask:

| counted per layout        |       |
|---------------------------|-------|
| nodes placed              | 10,000 |
| extent asks               | 32,245 |
| answered from the table   | 12,247 |
| recomputed                | 19,998 |
| cells probed per ask      | ~1 |

Placing is optimal — exactly one visit per node, so nothing re-walks the tree to
position it. The time goes into the 20,000 recomputed extents, two per node, each
of which walks that node's children again. The table itself is not slow: at
32,768 cells holding 20,000 entries it answers in about one probe, and
relieving it measured as a regression rather than a win — recycling the table when
full changes nothing at this size, because the table never reaches capacity, and
recycling it at half load threw answers away faster than it saved probes (asks
32,245 → 66,376, recomputes 19,998 → 54,297, and the rig went 15.0 → 23.6 ms
with the counters compiled in).

What is left is the shape of the key. An ask carries the room the node was
offered, and a subtree answers once for every offer only if it is free of
percentages, wrapping, measure callbacks and grids all the way down — which a
seventh of the branches declaring `50%` defeats for every level above them.
Per-axis freedom instead of per-subtree is the lever; like the grid's doubled
sizing run, it changes the measure contract rather than a constant, and belongs
in its own pull request with the conformance tier as the net.

The two costs this table could act on have gone: a leaf with no callback now
answers before the key is built, and a frame with no percentage side is resolved
once per layout. The deep rig went 4.03 to 2.86 ms with both binaries run back to
back on one machine, three times each. The counts above are what the memo saw
before that, and the two cache experiments stand — the table was never the slow
part, and the key is still what is left.

## History

| rig                     | before              | after |
|-------------------------|---------------------|-------|
| flat grid, 10,000 cells | ~300 ms             | 8.8 ms |
| deep flex, 10,000 nodes | 18 to 20 ms         | 5.8 ms |
| flat grid, 10,000 cells | two walks per axis  | one   |

The first row is the dense auto-placement scan, which restarted at the first cell
of the occupancy map for every item; the second is the intrinsic-measure
memoisation, which stopped re-asking a subtree for the same extent at every level
above it; the third is the floor pass's item weights, which the ceiling pass now
reads instead of recomputing (6.6 to 5.3 ms, measured back to back here). The
before columns come from the sessions that made those changes and the after
columns from this machine, so a row's gap is wider than the engine moved.

## Why no number is asserted

A shared runner's wall clock measures everybody else's processes. The rigs
assert geometry and command counts — the tree must still land where the spec
says it does — and print timing, so CI carries the numbers and a reviewer decides
whether a change moved them.
