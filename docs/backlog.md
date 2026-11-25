# What is left

Ordered by what stops this being usable in an application, not by effort. Every
row names the tier that would prove it closed — a plan nobody can check is a wish.
Scores quoted here are the ones the suites print today; `ctest` is the authority
and this file is the record.

## Blocking a real application

| what is missing | what that means on screen | how it gets closed |
|---|---|---|
| **Tab order** | focus follows the pointer. A keyboard-only user cannot reach a field at all | a focus walk over the declared tree in order, shift reversing it, skipping boxes that take no focus and retired nodes. Cases in `node_input`; a golden of the focused box's paint |
| **The clipboard** | a field cannot copy, cut or paste | one backend slot for reading and writing UTF-8 text, an answer from the headless recorder, and `CF_MOD_CONTROL`/`CF_MOD_META` paths in the field |
| **`display: none`** | 15 Yoga fixtures cannot be expressed, and a build has no way to hide a box without building it differently | a style field that takes the box out of layout and out of the paint walk; the fixtures move from the skip table into the score |
| **`box-sizing: content-box`** | 23 fixtures, and every percentage width a web developer writes means the other thing | resolve padding and border outside the declared width where the box asks for it; the fixtures plus one unit case per axis |
| **`overflow`** | 2 fixtures. A box cannot say *clip*, *scroll* or *visible*: clipping is a property of the scroller widget, not of the style | `cf_style.overflow` naming both axes, with the scroller's clip becoming one answer of the general rule. The clip contract already has a golden (`clipped_content`) to extend |
| **A multiline field** | a value is one row: Enter types nothing and Up/Down do nothing | the field drops `nowrap` when its box is taller than one row, keeps a vertical slide beside the horizontal one, and names the rows its arrows move between. A golden of a caret below the fold |
| **Undo** | a mistyped run is gone for good | a ring of (bytes, caret, mark) in the field's own state block. Unit cases: type, undo, redo, and an undo that stops at the buffer's edge |
| **IME composition** | typing Japanese, Chinese or accented-by-composition characters sends nothing | a composition event pair from the backend and a marked span the field paints differently. This one needs a real desktop to test, so it needs a plan before code |

## Rough edges that will bite a user

| item | detail |
|---|---|
| A full field ignores the keystroke | the buffer's capacity is the caller's and a run that will not fit whole is refused, so typing into a full field looks dead. Either truncate at a scalar boundary or show the refusal; today neither is stated |
| A state block can move | growing a block hands back a new pointer and the old one dangles. The framework no longer reads through the freed pointer itself (the use-after-free in `cf_state_grow` is repaired), but a host that caches a `cf_state` pointer across a growth is still the hazard the API warns about |
| Textures are never reclaimed | `create_texture` hands back an id and nothing frees it. A screen that loads images as it goes grows until the device stops it |
| The cursor is dead API | `cf_frame_cursor` answers an I-beam over a field and no backend can act on it, because the surface contract has no slot for the ask |
| A caret is a solid bar | it does not blink, because a blinking caret wants a frame every 500 ms and the idle-frame invariant is the number the bench rig prints. The frame clock exists now (`cf_frame_seconds`, and the loop runs another frame when a build asks for one), so this is a decision to make rather than a wall |
| Selection is by character | no double-click to select a word and no triple-click for the line, though a drag that strays past the box keeps selecting because the press holds the pointer |
| Image loading is BMP | `SDL_LoadBMP` and nothing else, and the web build ships no file dialog to name a path with |
| The gallery is the least tested file in the tree | the three defects a person reported from looking at it lived in `examples/gallery.c` (a track array on the stack, so the grid read freed memory), in `src/theme/default.c` (no shrink, so items fell off the edge instead of giving ground) and in `src/paint/emit.c` (a rounded fill cut to a clip rounds the cut box, so its corner stops matching its own border). Only the last one has a golden. A tier that builds the gallery through the headless recorder across a range of surfaces, asserting no command lands off-screen, outside the clip it names, or with a radius past its own box, closes all three at once |

## Text, the largest known gap

None of this is close, and none of it is faked by a field that pretends to take
it. Each needs a design conversation before a line of code:

- Right-to-left and bidirectional text: the walk lays every run left to right, and
  no field names the writing direction of a line — `cf_style.direction` is the
  flexbox axis, not the text's.
- Shaping for Arabic, Devanagari and Thai — one codepoint is not one glyph there.
- Justification, ligatures, `letter-spacing`, `line-height` as a style rather than
  the font's own metric, and variable fonts.
- Grapheme clusters: backspace deletes one scalar, so a combining mark or an emoji
  ZWJ sequence comes apart in more than one step.
- Fonts from memory, and a font stack chosen per script rather than tried in order.

## Layout and performance

- **The 2 ms target at 10,000 boxes is open.** The rig prints 4.9 ms for nested
  flex and 7.8 ms for the flat grid. The published table's earlier numbers came
  from a different machine, so read the ratios, not the absolutes.
- The 12 failing fixtures are one rounding story, not twelve layout bugs: Chrome
  snapped every edge to a whole pixel before recording it and `cf_layout` returns
  exact fractions. Closing them means deciding whether a layout engine owns
  pixel-snapping or the renderer does. Until that is settled the floor stays at
  473.
- 5 fixtures need `max-content`/`stretch` as sizing keywords, and 12 need a measure
  callback whose answer depends on real font metrics — that one belongs to a
  text-backend test rather than to this tier.
- Grid has no named lines or areas and no subgrid.
- Container queries and `aspect-ratio` have no field.

## Before a 1.0 freezes the ABI

`CF_BACKEND_ABI_VERSION` is 7 and every number after it has been broken on
purpose. Three slots are already wanted by rows above — clipboard, texture
release, cursor — and each one is a reason to say *now* rather than after a
release. Enum values are append-only, so the same pressure does not apply to
`cf_role`, `cf_key` or `cf_intrinsic`.

Also worth deciding while the surface is still soft:

- whether `cf_style` is passed by value forever (it is now, and it is getting
  large enough to notice);
- whether a widget may own a `cf_ctx` pointer, which `field_data` does today and a
  node with no context cannot otherwise measure its own text;
- whether `present` without a `submit` in between is a backend's duty or a loop's.
