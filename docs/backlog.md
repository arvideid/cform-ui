# What is left

Ordered by what stops this being usable in an application, not by effort. Every
row names the tier that would prove it closed — a plan nobody can check is a wish.
Scores quoted here are the ones the suites print today; `ctest` is the authority
and this file is the record.

## Blocking a real application

| what is missing | what that means on screen | how it gets closed |
|---|---|---|
| **IME composition** | the committed words reach the field, but the text still being composed is never drawn and the platform is never told where the caret is, so a candidate window lands wherever the desk decides | a composition event pair from the backend and a marked span the field paints differently, plus the text area the platform asks for. Opening the service is done — `set_text_input` — so what is left is the marked span, and it needs a real desktop to test, so it needs a plan before code |
| **No platform accessibility bridge** | the page now says what every box is, but nothing carries that to Narrator, VoiceOver or AT-SPI, so a person on a screen reader still hears nothing | `cf_semantic_visit` is the half the adapter reads, and it is tested: ids that hold across frames, whole UTF-8 names, values, selection, states, and only the asks a frame can serve. `gallery_headless` walks the real page as a bridge would. What is left is one OS-side tree walker per platform with its own event queue, which is a desktop a human qualifies and therefore a plan before code |

## Rough edges that will bite a user

| item | detail |
|---|---|
| A field walks a handful of rows | the widget's own walk of the value — the click that places a caret and the arrows that step rows — keeps a fixed count of the rows the paint lays, so in a value longer than that the caret cannot be pointed at a row past them. `widget_field` pins where the walk stops; closing it wants the walk to hand back the rows it laid, the way the paint pass allocates them from frame scratch |
| Undo is bounded by copying | the ring holds whole images of the buffer, sixteen of them inside four KiB, so a long value walks back through fewer steps and one past about two KiB keeps none — `widget_field` proves both edges. Closing it wants images of the edits rather than the bytes, which needs an offset that stays meaningful across them |
| An announced box knows only its own label | a row of the walk carries `labelled_by` and the preorder it was visited in, but no parent, sibling or `controlled_by` id, so a bridge that wants "the third of nine" or "this button opens that dialog" rebuilds it from the nesting it was handed; and a value that changed is there on the next ask rather than pushed, since live regions have no field. `semantics` and `gallery_headless` cover what is declared today |
| A state block can move | growing a block hands back a new pointer and the old one dangles. The framework no longer reads through the freed pointer itself (the use-after-free in `cf_state_grow` is repaired), but a host that caches a `cf_state` pointer across a growth is still the hazard the API warns about |
| A lost device is not recovered | a refused present now ends the drawing: the backend says so once, submits nothing and takes no further upload, and still answers a release and a teardown. Nothing re-creates the device or re-uploads what was in flight, and no headless tier can make SDL refuse a frame, so recovery stays a desktop case a human qualifies (or a Wasm context-restart fixture) with the host closing and opening a backend |
| A caret is a solid bar | it does not blink, because a blinking caret wants a frame every 500 ms and the idle-frame invariant is the number the bench rig prints. The frame clock exists now (`cf_frame_seconds`, and the loop runs another frame when a build asks for one), so this is a decision to make rather than a wall |
| Which shape arrives under the pointer is unqualified | the shell asks and the SDL port maps the three shapes onto SDL's own, but SDL's dummy driver makes no cursor at all: the headless case only proves that a refused ask leaves the window drawing. Reading the mapping needs a gallery window — the hand over a button, the I-beam in a field, the arrow elsewhere |
| Whether a keyboard appears is unqualified | the shell's ask reaches SDL's `SDL_StartTextInput` and `SDL_StopTextInput`, and the dummy driver takes both because it keeps that state for the window rather than for the hardware — so what no tier can see is a screen keyboard rising on a device that has one to show, which is a touch platform's check |
| Selection by word is the keyboard's | **Ctrl**/**Cmd** with the arrows moves and **Shift** selects over the runs the field reads, but a double-click still selects no word it landed in and a triple-click no line |
| Image loading is BMP | `SDL_LoadBMP` and nothing else, and the web build ships no file dialog to name a path with |

## Text, the largest known gap

None of this is close, and none of it is faked by a field that pretends to take
it. Each needs a design conversation before a line of code:

- Right-to-left and bidirectional text: the walk lays every run left to right, and
  no field names the writing direction of a line — `cf_style.direction` is the
  flexbox axis, not the text's.
- Shaping for Arabic, Devanagari and Thai — one codepoint is not one glyph there.
- Justification, ligatures, `letter-spacing`, `line-height` as a style rather than
  the font's own metric, and variable fonts.
- Grapheme clusters: backspace deletes one scalar and a word key names its run by the
  bytes it reads, so a combining mark or an emoji ZWJ sequence comes apart in more than
  one step, and every scalar above ASCII counts as a letter — an unspaced CJK run is
  one word to the arrow and a non-breaking space ends one.
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

`CF_BACKEND_ABI_VERSION` is 11 and every number after it has been broken on
purpose. A texture release and a backend teardown are what 8 added; 9 added
`size`, a second number at the front of the struct, so a backend compiled against
another revision of the header is refused rather than misread; 10 added the cursor, the
slot `cf_frame_cursor` had been answering into nowhere, and 11 the written-text ask,
which had the same problem on the keyboard's side — `cf_frame_takes_text` knew the
field was focused and had nowhere to say so. A change that adds no slot needs no
number of its own — a font close is one, since the font is not part of the struct, and
so is the clipboard, which went onto the context as a host-owned pair (`cf_clipboard`)
instead of into the backend. Composition events are the row left that would want a
slot, and that row is the one to decide about *now* rather than after a release, because
a marked span cannot be added from a text event that was already dropped. Enum values
are append-only, so the same pressure does not apply to `cf_role`, `cf_key` or
`cf_intrinsic`.

Also worth deciding while the surface is still soft:

- whether `cf_style` is passed by value forever (it is now, and it is getting
  large enough to notice);
- whether a widget may own a `cf_ctx` pointer, which `field_data` does today and a
  node with no context cannot otherwise measure its own text;
- whether `present` without a `submit` in between is a backend's duty or a loop's.
