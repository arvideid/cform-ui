# Flexbox conformance

Published numbers from `tests/conformance/yoga_cases.c`. The gate this tier
exists to meet is at least 95% of the flexbox suite, so the score lives here
and moves in the diff that moves it: run the tier, compare against the table,
change the table in the same pull request.

```sh
ctest --preset dev              # unit and conformance tiers
python tools/gentest.py         # rewrite the corpus from .vendor/yoga
clang-format -i tests/conformance/yoga_cases_data.h
```

## Where the expectations come from

[facebook/yoga](https://github.com/facebook/yoga) at `503bec3` (MIT, Copyright
(c) Facebook, Inc. and its affiliates) ships `tests/generated/*.cpp`: the
output of its `gentest` driver, which loads each HTML fixture in headless
Chrome at device-scale-factor 1 and writes a test from what it measured - the
calls that rebuild the tree in Yoga's API, and per node the box the browser
gave it as an offset from its parent element. The fixtures themselves are plain
CSS flexbox, and `gentest/test-template.html` styles every `div` with the
defaults Yoga's own API starts from - `display: flex`, `position: relative`,
`flex-direction: column`, `box-sizing: border-box`, `flex-shrink: 0`,
`align-content: flex-start` - so those are the values a ported row states
rather than the ones CSS would infer.

Nothing at test time reads Yoga or a browser: `tools/gentest.py` turns the
recorded calls into `cf_style` rows and the corpus is checked-in C data. The
generated header is reproducible - regenerating it and running `clang-format`
replays the same bytes.

`YGNodeLayoutGetLeft` and `YGNodeLayoutGetTop` answer relative to the parent
node, and the recorded offsets are too, while a corpus row places its box in
the root's space - so the generator carries the pair down the tree before
writing a row. Without that step a fixture whose ancestors are not at the
origin asks for a box no absolute rect can hold: the parent-relative `-50`
where the CSS answer is `50`.

## What is ported

The reference's number is only a target when `cf_style` can state the request.
485 of 543 fixtures can; the rest are counted by what they need, so the
denominator stays the whole suite rather than the convenient part of it.

| skipped | needs |
|---------|-------|
| 23 | `box-sizing: content-box`, which `cf_style` has no field for |
| 15 | `display: none` |
| 12 | a measure callback, so the number depends on text metrics |
| 2 | `overflow` |
| 1 | `aspect-ratio` |
| 5 | an intrinsic or stretch keyword (`max-content`, `stretch`) |

## Score

448 of the 485 ported fixtures match, 92% of what was ported and 82% of the
whole corpus. Measured at `572c8dd`. The table is the tier's own output, so a
run is the check and this page is the record of it.

The grade is half a pixel. The reference snapped every fixture edge to the
device grid before it was recorded; `cf_layout` returns exact fractions and
leaves rounding to whoever paints, so a correct box can sit at most half a
pixel from a snapped edge. A finer grade would score that divergence instead of
the rules.

| suite | ported | match | miss |
|-------|--------|-------|------|
| absoluteposition | 33 | 33 | 0 |
| aligncontent | 64 | 63 | 1 |
| alignitems | 29 | 13 | 16 |
| alignself | 5 | 4 | 1 |
| androidnewsfeed | 1 | 1 | 0 |
| auto | 5 | 5 | 0 |
| border | 5 | 5 | 0 |
| boxsizing | 23 | 23 | 0 |
| dimension | 2 | 2 | 0 |
| flex | 10 | 9 | 1 |
| flexbasisfitcontent | 5 | 4 | 1 |
| flexdirection | 53 | 53 | 0 |
| flexwrap | 23 | 22 | 1 |
| gap | 32 | 30 | 2 |
| justifycontent | 28 | 28 | 0 |
| margin | 34 | 33 | 1 |
| minmaxdimension | 24 | 24 | 0 |
| padding | 7 | 7 | 0 |
| percentage | 24 | 22 | 2 |
| rounding | 13 | 3 | 10 |
| sizeoverflow | 3 | 3 | 0 |
| staticposition | 62 | 61 | 1 |
| **total** | **485** | **448** | **37** |

92 node assertions fail across those 37 cases. The grade is not what is
missing - the rules are.

## What the misses need

- **`align-items: baseline`** and the self variant, which need a baseline the
  engine can only get from a measure callback. 16 of `alignitems` and the one
  `alignself` miss: a row whose items sit on the text baseline the fixtures
  recorded but the ported tree never carries.
- **Growth and fit-content bases.** `percentage_flex_basis_main_min_width`
  keeps growing items whose declared minimum should have frozen one, and
  `container_child_overflows_definite_parent_row` caps an auto main basis at
  the definite parent while the fixture's unported `fit-content` keyword
  expects it to overflow. The rest of the old percentage gap is closed: a
  percentage size reads as `auto` while the pass is still measuring its axis
  and resolves against the settled box, which returns
  `percent_within_flex_grow`'s 150, and an out-of-flow shrink-wrap keeps the
  minimum its content cannot break, which returns the two `staticposition`
  amalgamations' 133.
- **Edge rounding.** Rounding a box means rounding both its edges, so its size
  can land a whole pixel from the exact one - `33.333` wide starting at `33.333`
  becomes `34` when the far edge snaps to `67`. That is more than the grade
  forgives, and it is why 10 of the 13 `rounding` cases still miss: the snap is
  a renderer decision that `cf_layout` deliberately does not make, so this gap
  stays a published divergence rather than a rule to implement. The same snap
  accounts for the surviving `staticposition` case and one `gap` case: boxes
  whose recorded edges were whole pixels and whose exact answers land a
  fraction away.
- **Scattered single misses.** Line breaking, `align-content` packing and
  negative free space land, so `flexwrap`, `aligncontent`, `margin`, `flex`,
  `gap` and `percentage` each keep a case the suites above do not account for -
  the outer corner of each rule rather than a cluster left to size.

## The floor

`CONFORMANCE_FLOOR` in `tests/conformance/yoga_cases.c` is 448 and only moves
up. The tier prints every mismatch and asserts the floor rather than a verdict,
so a fixture that regresses fails `ctest` while the ones that have never passed
stay visible here instead of being deleted to make the run green. Closing a gap
means raising the floor, the table and the score in one diff, with a failing
fixture named in the commit as the thing that was fixed.
