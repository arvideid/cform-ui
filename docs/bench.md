# Bench baselines

Published numbers from the `bench`-labelled CTest targets in `tests/bench/`. A
baseline lives here so a regression is a diff somebody reads instead of a rumour:
run the rig, compare against the table, change the table in the same commit that
moves it.

```sh
ctest --preset release -L bench   # the numbers below
ctest --preset dev -L bench       # Debug, roughly three times slower
```

## Layout, release, 1280 × 720 offer

Best of nine runs on a Windows desktop, MSYS2 UCRT64 GCC. Each run already
reports its fastest of 50 layouts, because a shared machine spends its wall time
on somebody else's process; the spread is 20–30% idle and about double busy, so
the fastest pass is what describes the engine.

| rig                  | nodes  | ms per layout | ns per node |
|----------------------|--------|---------------|-------------|
| deep flex tree       |  1,250 | 0.29          | 230         |
| deep flex tree       |  2,500 | 0.59          | 237         |
| deep flex tree       |  5,000 | 1.30          | 261         |
| deep flex tree       | 10,000 | 2.84          | 284         |
| flat 100-column grid | 10,000 | 5.31          | 531         |

The deep rig is a complete binary tree of alternating rows and columns, a third
of the branches growing into the leftover space and a seventh claiming half the
width; the root declares the offer it is given, so no branch measures against a
box still being measured. The flat rig is one `fr`-column grid of leaf cells.

## The frame and the stream

`bench_node` reconciles one 10,000-node declaration through a single context;
`bench_paint` turns a laid-out 10,000-node page into the stream a device would be
handed. Both assert their output — geometry, one fill per box, a border over every
fourth cell, nothing for a viewport that reaches nothing — so the timing is the
only thing they print rather than check.

| rig              | what                                        |    ms |
|------------------|---------------------------------------------|-------|
| cold build       | first diff of the declaration                |  5.98 |
| idle diff        | the same declaration again, nothing changed  |  4.46 |
| diff and layout  | one whole frame of it                        |  6.46 |
| paint            | 12,475 commands out of 10,000 boxes          |  0.20 |

A repaint of that page costs 2,810 KiB of frame scratch, which is what a shell
sizes its arena against.

## Where the budget stands

2 ms at 10,000 nodes is the target. The deep tree reaches 2.8 ms and the flat grid
5.3 ms, so the budget is open and the grid misses by more. The idle half of the
line is met: an unchanged frame dirties nothing, lays out nothing and hands the
backend zero commands.

Cost per node rises 1.2× over three doublings (230 ns at 1,250 nodes against
284 ns at 10,000). What the grid pays for is the number of full track-sizing runs
one layout performs, measured by rebuilding the rig with one decision changed at a
time — only the ratios between these rows carry over to another machine:

| the same 10,000-cell grid     |    ms |
|-------------------------------|-------|
| both axes auto (the rig)      | 10.1  |
| width stated, height auto     |  6.3  |
| both axes stated              |  3.9  |
| both axes stated, `px` tracks |  3.6  |
| 10 columns instead of 100     | 10.2  |
| 1,000 nodes instead of 10,000 |  0.6  |
| same children, one flex column|  2.2  |

A sizing run walks the child list once per axis per intrinsic mode: a box stating
both sizes needs one run, a box stating neither needs three (max-content to guess,
min-content to bound the guess, the run that places). Doubling the column count
moves nothing and halving the nodes halves the time, so the cost is per item and
not per track, and 3.9 ms against 2.2 ms for the same children in a flex column is
the grid's own overhead above that.

The remaining lever is the shape of the measure key: an ask carries the room the
node was offered, so a subtree answers once per offer unless it is free of
percentages, wrapping, measure callbacks and grids all the way down. Per-axis
freedom instead of per-subtree changes the measure contract rather than a constant,
and belongs in its own commit with the conformance tier as the net.

## History

| rig                     | before             | after |
|-------------------------|--------------------|-------|
| flat grid, 10,000 cells | ~300 ms            | 5.3 ms |
| deep flex, 10,000 nodes | 18 to 20 ms        | 2.8 ms |

The first was the dense auto-placement scan, which restarted at the first cell of
the occupancy map for every item. The second was intrinsic measurement, which
stopped re-asking a subtree for the same extent at every level above it; the floor
pass now also records each item's weight for the ceiling pass to read back instead
of recomputing. The before columns come from the sessions that made those changes
and the after columns from this machine, so a row's gap is wider than the engine
moved.

## Why no number is asserted

A shared runner's wall clock measures everybody else's processes. The rigs assert
geometry and command counts — the tree must still land where the spec says — and
print timing, so CI carries the numbers and a reviewer decides whether a change
moved them.
