# What is left

Ordered by what stops this being usable in an application, not by effort. Every
row names the tier that would prove it closed — a plan nobody can check is a wish.
Scores quoted here are the ones the suites print today; `ctest` is the authority
and this file is the record.

*Recorded at* `0.2.0`, commit `1de38b7`. A row that closes gets deleted rather than
crossed out, and the score beside it moves with the release that moved it — step 3 of
[`releases.md`](releases.md).

## Blocking a real application

| what is missing | what that means on screen | how it gets closed |
|---|---|---|
| **No platform accessibility bridge** | the page now says what every box is, but nothing carries that to Narrator, VoiceOver or AT-SPI, so a person on a screen reader still hears nothing | `cf_semantic_visit` is the half the adapter reads, and it is tested: ids that hold across frames, whole UTF-8 names, values, selection, states, and only the asks a frame can serve. `gallery_headless` walks the real page as a bridge would. What is left is one OS-side tree walker per platform with its own event queue, which is a desktop a human qualifies and therefore a plan before code |

## Rough edges that will bite a user

| item | detail |
|---|---|
| A switch says on and off in tone alone | `cf_checkbox` flips the bit the build handed it and the theme's `checked` variant paints the difference, but the default theme's difference is one fill tone — 250 off against 23 on — with no tick drawn and no mark a reader who cannot tell the two tones apart could read. Closing it wants a theme that carries a mark and the tier that checks it: a golden case per state asking that the two differ in shape as well as colour |
| A theme's numbers go unchecked | the gallery now ships a second design system and chapter seventeen swaps to it on one bit (`theme_resolve`, `gallery_headless`), but nothing asks whether a stated pair clears the ratio its comment claims — the tables quote 19:1, 12:1 and 3:1 readings and no tier computes one. Closing it wants the contrast arithmetic at the style boundary and a case that walks every fill-and-ink pair the default table states, which is also the rule a high-contrast theme and a `prefers-reduced-motion` answer would have to be written against |
| A field walks a handful of rows | the widget's own walk of the value — the click that places a caret and the arrows that step rows — keeps a fixed count of the rows the paint lays, so in a value longer than that the caret cannot be pointed at a row past them. `widget_field` pins where the walk stops; closing it wants the walk to hand back the rows it laid, the way the paint pass allocates them from frame scratch |
| Undo is bounded by copying | the ring holds whole images of the buffer, sixteen of them inside four KiB, so a long value walks back through fewer steps and one past about two KiB keeps none — `widget_field` proves both edges. Closing it wants images of the edits rather than the bytes, which needs an offset that stays meaningful across them |
| An announced box carries no parent | a row of the walk carries `labelled_by` — the id of the label it took its name from — the live-region level covering it and the two boxes the build named beside it, but no parent id and no place in a run, so a bridge that wants "the third of nine" rebuilds that from the nesting it was handed. The named boxes are keys resolved when the walk reads the frame (`cf_node.described_by` and `controls`, reported as `cf_semantic.described_by` and `controls`), so a relation to a dialog that is closed answers `CF_NO_ID` instead of the box that last carried the key, and a key carried only by a box the page announces nowhere is passed over. News still arrives by being asked for: the level says which rows to watch and the page asks for the frame that carries the changed words, so nothing is pushed at a bridge between frames. `semantics` and `gallery_headless` cover both edges |
| A state block can move | growing a block hands back a new pointer and the old one dangles. The framework no longer reads through the freed pointer itself (the use-after-free in `cf_state_grow` is repaired), but a host that caches a `cf_state` pointer across a growth is still the hazard the API warns about |
| A lost device is not recovered | a refused present now ends the drawing: the backend says so once, submits nothing and takes no further upload, and still answers a release and a teardown. Nothing re-creates the device or re-uploads what was in flight, and no headless tier can make SDL refuse a frame, so recovery stays a desktop case a human qualifies (or a Wasm context-restart fixture) with the host closing and opening a backend |
| A caret is a solid bar | it does not blink, because a blinking caret wants a frame every 500 ms and the idle-frame invariant is the number the bench rig prints. The frame clock exists now (`cf_frame_seconds`, and the loop runs another frame when a build asks for one), so this is a decision to make rather than a wall |
| Which shape arrives under the pointer is unqualified | the shell asks and the SDL port maps the three shapes onto SDL's own, but SDL's dummy driver makes no cursor at all: the headless case only proves that a refused ask leaves the window drawing. Reading the mapping needs a gallery window — the hand over a button, the I-beam in a field, the arrow elsewhere |
| Whether a keyboard appears is unqualified | the shell's ask reaches SDL's `SDL_StartTextInput` and `SDL_StopTextInput`, and the dummy driver takes both because it keeps that state for the window rather than for the hardware — so what no tier can see is a screen keyboard rising on a device that has one to show, which is a touch platform's check |
| Where the platform stands its answers is unqualified | the run an input method is deciding is painted and measured in the page (the protocol is replayed headlessly over the gallery's own field: `widget_field`, `app_run`, `gallery_headless`), the shell hands the same box to SDL's `SDL_SetTextInputArea`, and chapter fifteen lays a strip of its own from those numbers so a tier can check that a page moving under a standing run moves the strip along with the answer. What no tier can show is a candidate list rising under those letters, because the dummy driver has no list to place: reading it needs a gallery window, a field in it and a real input method — typing pinyin into a box on the page and watching the run underline, the strip and the platform's list follow it |
| What a border covers is unqualified | the SDL port strokes a border as a strip between two perimeters, and a piece of that strip lying over an edge of no width covers nothing while still being handed to an antialiased raster — which is how a box stroked on one edge leaked a hairline along the other three. Those pieces are skipped now, and what no headless tier can qualify is the raster itself: it wants a backend that draws to bytes a test can diff |
| Selection by word is the keyboard's | **Ctrl**/**Cmd** with the arrows moves and **Shift** selects over the runs the field reads, but a double-click still selects no word it landed in and a triple-click no line |
| A sleeping page does not follow the back button | the documentation site reads the address bar on every frame it builds, and the idle invariant means it builds no frame when nothing moved — so a reader who presses Back without touching the canvas keeps the page they left until the next event. Closing it wants what the loop lacks: one way for a host to say "something outside the window changed", which is the same missing door a network response or a file watch would knock on. `cf_frame_requested` is the build's own door and the honest answer is that it should not be the only one |
| The masthead's GitHub item is a word, not a mark | a drawn octocat is an image, an image wants a decoder, and a decoder is the `load_image` slot — a backend-ABI conversation rather than a stylesheet change. The site states the destination in text until the slot has an owner |
| The site's list of pages scrolls away with the prose | the frame is one viewport, so a reader deep in a long page has to walk back to the top to change page. `cf_position` has no sticky value: a box that stays where it is until its container's edge passes it and then travels is a rule the flex pass would have to run per line, and the tier that would prove it is the site's own — a case that scrolls and asks where the first row ended up |
| Image loading is BMP | `SDL_LoadBMP` and nothing else, and the web build ships no file dialog to name a path with |
| A line is as tall as its primary says | a line is drawn in the fonts it was measured in — `cf_text_each_run` splits it where the stack changed hands and the stream carries one command per face it needed (`text_measure`, `paint_emit`, `app_run`) — but the row's height still comes from the first font alone, so a fallback glyph taller than the latin face is cut by a line the layout swears fits. Closing it wants the walk to report the tallest face a line used, and the measure to charge that |

## Text, the largest known gap

None of what is left here is close, and none of it is faked by a field that
pretends to take it. Each needs a design conversation before a line of code:

- Right-to-left and bidirectional text: the walk lays every run left to right, and
  no field names the writing direction of a line — `cf_style.direction` is the
  flexbox axis, not the text's.
- Shaping for Arabic, Devanagari and Thai — one codepoint is not one glyph there.
- Justification, ligatures, `letter-spacing`, `line-height` as a style rather than
  the font's own metric, and variable fonts.
- Grapheme boundaries, the part that is left: the editor already steps and deletes whole
  characters (`cf_text_next_cluster`, `cf_text_prev_cluster`, covered by `text_cluster`,
  `widget_field` and the gallery page), but on a subset of UAX #29 — a Hangul syllable
  spelled with separate jamo and the few marks that attach forwards still come apart in
  more than one step, and the whole property table is not carried.
- Wrapping does not use those boundaries. A run too long for its box breaks at a scalar,
  so a mark can be left behind on the row above its letter; the break opportunities are
  still the spaces, hyphens and ideographs the wrapper knows.
- The word key names its run by the leading byte, so anything above ASCII is neither
  blank nor letter — an unspaced CJK run is one jump and a pasted non-breaking space
  ends one. Naming a codepoint's category is the text layer's unbuilt work.
- Fonts from memory, and a font stack chosen per script rather than tried in order.

## Layout and performance

- **The 2 ms target at 10,000 boxes is open.** The rig prints 4.9 ms for nested
  flex and 7.8 ms for the flat grid. The published table's earlier numbers came
  from a different machine, so read the ratios, not the absolutes.
- The 12 failing fixtures are one rounding story, not twelve layout bugs: Chrome
  snapped every edge to a whole pixel before recording it and `cf_layout` returns
  exact fractions. Closing them means deciding whether a layout engine owns
  pixel-snapping or the renderer does. Until that is settled the floor stays at
  510.
- 12 fixtures need a measure callback whose answer depends on real font metrics;
  that belongs to a text-backend test rather than to this tier.
- Grid has no named lines or areas and no subgrid.
- Container queries have no field.

## Before a 1.0 freezes the ABI

`CF_BACKEND_ABI_VERSION` is 12 and every number after it has been broken on
purpose. A texture release and a backend teardown are what 8 added; 9 added
`size`, a second number at the front of the struct, so a backend compiled against
another revision of the header is refused rather than misread; 10 added the cursor, the
slot `cf_frame_cursor` had been answering into nowhere, and 11 the written-text ask,
which had the same problem on the keyboard's side — `cf_frame_takes_text` knew the
field was focused and had nowhere to say so. 12 added `set_candidate_rect`, the last of
the three asks that a frame already answered and a device never heard: an input method
with nowhere to stand its list is the reason a page has to draw the run itself. A change
that adds no slot needs no number of its own — a font close is one, since the font is not
part of the struct, and so is the clipboard, which went onto the context as a host-owned
pair (`cf_clipboard`) instead of into the backend. Enum values
are append-only, so the same pressure does not apply to `cf_role`, `cf_key` or
`cf_intrinsic`.

Also worth deciding while the surface is still soft:

- whether `cf_style` is passed by value forever (it is now, and it is getting
  large enough to notice);
- whether a widget may own a `cf_ctx` pointer, which `field_data` does today and a
  node with no context cannot otherwise measure its own text.
