# Layout baseline

Published numbers from `tests/bench/bench_layout.c`, the `bench`-labelled CTest
target. A baseline lives here so a regression is a diff somebody reads instead
of a rumour: run the rig, compare against the table, change the table in the
same pull request that moves it.

```sh
ctest --preset release -L bench   # the numbers below
ctest --preset dev -L bench       # Debug, roughly 3x slower and noisier
```

## Release, 1280 × 720 offer

A Windows desktop, the `release` preset, GCC from MSYS2 UCRT64, fastest pass of
the rig. Three consecutive runs of the flex tree agreed within 5%; the flat grid
spread 240 to 330 ms, which is what a quadratic scan shows when the machine is
not idle.

| rig                    | nodes  | ms per layout | ns per node |
|------------------------|--------|---------------|-------------|
| deep flex tree         |  1,250 | 1.0           | 780         |
| deep flex tree         |  2,500 | 2.3           | 900         |
| deep flex tree         |  5,000 | 4.9           | 980         |
| deep flex tree         | 10,000 | 10.5          | 1,050       |
| flat 100-column grid    | 10,000 | ~300          | 30,000      |

The deep rig is a complete binary tree of alternating rows and columns with a
third of the branches growing into the leftover space and a seventh claiming half
the width. The flat rig is one grid container of `fr` columns and content rows,
every cell a leaf. Both are 10,000 nodes because that is the size the budget
names.

## What the gap means

2 ms at 10,000 nodes is the budget, and neither rig meets it. The two gaps are
structural, not tuning:

- **Cost per node rises with the tree** - 780 ns at 1,250 nodes against 1,050 ns
  at 10,000, a log factor. Nothing caches a measured size, so a subtree is
  measured once at every level above it: `layout_subtree` resolves a node's own
  border box from its content and then arranges children, each of which measures
  again.
- **The flat grid is quadratic in the cells** - dense auto-placement restarts its
  row-major scan of the occupancy map at the first cell for every item, so
  10,000 cells cost on the order of 5e7 cell tests before any track is sized.

## Why no number is asserted

A shared runner's wall clock measures everybody else's processes. The rigs
assert geometry - the tree must still land where the spec says it does - and
print timing, so CI carries the numbers and a reviewer decides whether a change
moved them. Asserting the budget belongs to the node layer, which owns the
layout cache and the dirty bits that invalidate it.

## History

| commit    | deep 10,000 | flat grid 10,000 |
|-----------|-------------|------------------|
| `3febb17` | 10.5 ms     | ~300 ms          |
