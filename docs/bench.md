# Layout baseline

Published numbers from `tests/bench/bench_layout.c`, the `bench`-labelled CTest
target. A baseline lives here so a regression is a diff somebody reads instead
of a rumour: run the rig, compare against the table, change the table in the
same pull request that moves it.

```sh
ctest --preset release -L bench   # the numbers below
ctest --preset dev -L bench       # Debug, roughly three times slower
```

## Release, 1280 × 720 offer

Best of nine consecutive runs on a Windows desktop, the `release` preset, GCC
from MSYS2 UCRT64, fastest pass of 50 layouts. Runs spread 10 to 30%: the rig
reports the fastest pass because the slowest one describes the machine rather
than the engine.

| rig                    | nodes  | ms per layout | ns per node |
|------------------------|--------|---------------|-------------|
| deep flex tree         |  1,250 | 1.1           | 920         |
| deep flex tree         |  2,500 | 2.5           | 1,010       |
| deep flex tree         |  5,000 | 5.6           | 1,110       |
| deep flex tree         | 10,000 | 13.0          | 1,300       |
| flat 100-column grid    | 10,000 | 6.0           | 600         |

The deep rig is a complete binary tree of alternating rows and columns with a
third of the branches growing into the leftover space and a seventh claiming half
the width. The root declares the offer it is given, so no branch measures
against a box that is itself still being measured. The flat rig is one grid
container of `fr` columns and content rows, every cell a leaf. Both are 10,000
nodes because that is the size the budget names.

## What the gap means

2 ms at 10,000 nodes is the budget. The flat grid is within a factor of four of
it; the deep flex tree is not, and the reason is structural:

- **Cost per node rises with the tree** - 920 ns at 1,250 nodes against 1,300 ns
  at 10,000, a log factor. Nothing caches a measured size, so a subtree is
  measured once at every level above it: `layout_subtree` resolves a node's own
  border box from its content and then arranges children, each of which measures
  again.

The other structural gap closed with the placement scan: dense auto-placement
used to restart at the first cell of the occupancy map for every item, which
cost a 10,000-cell grid roughly 300 ms - the same rig now costs 6 ms.

## Why no number is asserted

A shared runner's wall clock measures everybody else's processes. The rigs
assert geometry - the tree must still land where the spec says it does - and
print timing, so CI carries the numbers and a reviewer decides whether a change
moved them. Asserting the budget belongs to the node layer, which owns the
layout cache and the dirty bits that invalidate it.

## History

| rig               | before `6e6a53a` | after |
|-------------------|------------------|-------|
| flat grid, 10,000 cells | ~300 ms          | 6.5 ms |

The deep flex tree has not been retuned yet, so its numbers move with the load
on the machine that measured them rather than with any change to the engine.
