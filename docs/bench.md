# How fast it is

A **benchmark** is a program that does the same piece of work many times and
reports how long it took. This file records those numbers, so that when someone
makes the engine slower, the diff shows it instead of it becoming a rumour.

Run them yourself:

```sh
ctest --preset release -L bench   # the numbers below
ctest --preset dev -L bench       # Debug, roughly three times slower
```

Four programs in `tests/bench/` build a big screen and measure the work the
engine does on it. They check their arithmetic — positions, shape counts, bytes
held — but they never fail over timing, because a slow run is not a wrong run.
See [why not](#why-no-number-is-a-failing-test) at the bottom.

## Laying out a screen

`bench_layout` builds a tree and calls `cf_layout`. Best of nine runs on a Windows
desktop, MSYS2 UCRT64 GCC, against a 1280 × 720 space. Each run reports its
fastest of 50 layouts, because on a shared machine most of the clock belongs to
somebody else's process — the fastest pass is the one that describes the engine.
Every number in this file comes from one machine in one session; an absolute that
moves when you change the machine is why the tables below are read as ratios.

| shape of the tree | boxes | per layout | per box |
|---|---|---|---|
| nested rows and columns | 1,250 | 0.43 ms | 346 ns |
| nested rows and columns | 2,500 | 0.82 ms | 328 ns |
| nested rows and columns | 5,000 | 1.86 ms | 372 ns |
| nested rows and columns | 10,000 | 4.88 ms | 488 ns |
| one flat grid of 100 columns | 10,000 | 7.79 ms | 779 ns |

The nested tree is a complete binary tree of alternating rows and columns, a third
of the branches growing into the leftover space and a seventh claiming half the
width.

**The target is 2 ms for 10,000 boxes.** Flex misses it by 2.9 ms and the grid by
5.8 ms, so the goal is still open. Cost per box rises only 1.4× across three
doublings, which is what "it scales" means in practice: four times the boxes costs
a little under four times the time.

## Building a screen and drawing it

`bench_node` reconciles a 10,000-box description through one context.
`bench_paint` turns a laid-out 10,000-box page into the list a device would be
handed.

| step | what happens | time |
|---|---|---|
| cold build | the description is seen for the first time | 6.60 ms |
| idle diff | the same description again, nothing changed | 4.86 ms |
| diff and layout | one whole frame of it | 7.02 ms |
| paint | 12,475 shapes out of 10,000 boxes | 0.28 ms |

Redrawing that page needs 3,321 KiB of scratch, which is the number a shell sizes
its memory pool against. Part of it tracks the deepest path and part tracks the
node count: the content-measurement cache carries a row per node in the
declaration, so the pool does not shrink just because the tree is shallow. See
`cf_layout` in `cf/layout.h` for what `scratch` holds.

## What a page costs, and whether it grows

Milliseconds say how long a frame takes. `bench_memory` answers the two questions
a kiosk and a text editor both have to settle before shipping: how many bytes a
screen holds, and whether a page left open accrues them.

| what is measured | bytes |
|---|---|
| one 10,000-box frame, built and painted | 6,093 KiB of arena |
| the same frame run 1,000 times | 6,093 KiB |
| 200 rounds of 40 state keys never asked before | 5 KiB held at the peak |
| the page stops asking | 0 KiB |

The rig asserts every row: the two arena figures must not move across a thousand
frames, the peak must not exceed what the live rounds asked for, and a page that
stops asking must hold nothing. State is charged to the records that are live, not
to the history of the session — a list that grows and shrinks, a selection that
moves and a wizard with a step per record all allocate under names no earlier
round used, and the bytes come back when the records do.

A state block used to be carved from a persistent arena, and growing one orphaned
the bytes it had already taken. A page left open therefore ratcheted upward and
never came back down; that is the regression this rig exists to catch.

## Where the time goes

A box has to be measured before it can be placed, and measuring is where the grid
pays. A box that states both its size is walked once per axis. A box that states
neither is walked three times: how wide its content wants to be, how narrow it can
be squeezed, then the placing itself.

Changing one decision at a time in the same 10,000-box grid shows it. Only the
*ratios* carry over to another machine.

| the same grid, but | time |
|---|---|
| both sizes left automatic (the rig) | 10.1 ms |
| width stated, height automatic | 6.3 ms |
| both stated | 3.9 ms |
| both stated, and fixed-size columns | 3.6 ms |
| 10 columns instead of 100 | 10.2 ms |
| 1,000 boxes instead of 10,000 | 0.6 ms |
| the same children in a flex column | 2.2 ms |

Doubling the column count changes nothing and quartering the boxes quarters the
time, so the cost is per box and not per column. The 3.9 ms against 2.2 ms is grid
overhead, plain and simple.

## What got faster, and how

| rig | before | after |
|---|---|---|
| flat grid, 10,000 boxes | ~300 ms | 7.8 ms |
| nested flex, 10,000 boxes | 18–20 ms | 4.9 ms |

The grid restarted its search for a free cell at the top of the occupancy map for
every item; it now continues where it left off. The flex tree re-asked every
ancestor for a size a child had already answered; it now remembers. The "before"
column comes from the machines those fixes were made on and the "after" column from
this one, so each gap is wider than the engine actually moved.

## Why no number is a failing test

A shared runner's clock measures everybody else's processes too, so a CI machine
under load would fail builds for the wrong reason. The rigs assert geometry —
the boxes must still land where the specification says — and *print* timing. CI
carries the numbers into the log and a person decides whether a change moved them.

If you change the engine, run the rig and update the table in the same commit.
