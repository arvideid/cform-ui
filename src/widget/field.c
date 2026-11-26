#include <string.h>

#include "cf/widget.h"

/*
 * The lines a click can land on. A field's value refuses to wrap, so one row
 * is what a typed run lays; the rest is the room a buffer holds between the
 * breaks a build put in itself. A point below them all reads the last line
 * rather than nothing.
 */
#define FIELD_VISIBLE_LINES 8u

/*
 * What a handler needs and the declaration does not carry: the context to read
 * the box's rect and the interaction off, the buffer to edit, the name the
 * history is keyed under, and the fonts to lay the bytes in - the stack the build
 * handed the field, borrowed for as long as the field is declared, which is the
 * rule a label's stack already follows.
 */
typedef struct field_data {
    cf_ctx *ctx;
    cf_text_edit *text;
    cf_name key;
    cf_text_stack stack;
} field_data;

static uint32_t sel_from(const cf_text_edit *text) {
    return text->mark < text->caret ? text->mark : text->caret;
}

static uint32_t sel_to(const cf_text_edit *text) {
    return text->mark > text->caret ? text->mark : text->caret;
}

/* A build that wrote the bytes itself can leave the marks past them, and no
 * edit below may read an offset the buffer does not hold. */
static void clamp_marks(cf_text_edit *text) {
    if (text->caret > text->length) {
        text->caret = text->length;
    }

    if (text->mark > text->length) {
        text->mark = text->length;
    }
}

/*
 * The history a field keeps of its own buffer: whole images of the bytes and of the
 * two marks, in a ring the field's name keys and cf_state keeps across rebuilds.
 * Images rather than the edits, because what one keystroke moved here is nobody's
 * guess - a paste over a selection, a backspace off the lead byte of a four-byte
 * rune - and an undo that has to know what the edit did is an undo that can get it
 * wrong.
 *
 * The cost is bounded twice over: FIELD_UNDO_IMAGES images at most, and
 * FIELD_UNDO_BYTES for the block however big the buffer is. A field over a long value
 * keeps fewer steps; one over a buffer too large to copy twice keeps none, and its
 * chord falls through to whoever is above - the rule a field handed no clipboard
 * already keeps for the keys it cannot serve.
 */
#define FIELD_UNDO_IMAGES 16u
#define FIELD_UNDO_BYTES 4096u

/* One image: what the buffer held and where the marks stood. Its bytes follow the
 * header, so the room an image takes is a property of the buffer, not of the struct. */
typedef struct undo_image {
    uint32_t length;
    uint32_t caret;
    uint32_t mark;
} undo_image;

/* The ring's front, at the head of the keyed block. Only `capacity` is a fact the
 * images depend on: the depth and the stride come out of it. */
typedef struct undo_ring {
    uint32_t capacity; /* buffer bytes one image copies */
    uint32_t head;     /* ring slot of the oldest image */
    uint32_t count;    /* images held */
    uint32_t pos;      /* steps from head to the image the buffer matches */
} undo_ring;

/* One walk of a field's ring: the block, the buffer it copies, and the layout the
 * block was paid for, so no helper recomputes them and none can disagree with the
 * size cf_state was asked for. */
typedef struct undo_book {
    cf_text_edit *text;
    undo_ring *ring;
    uint32_t capacity;
    uint32_t depth;
} undo_book;

/* The bytes an image copies, padded to whole words: the ring's headers follow one
 * another through the block, and an unaligned uint32_t traps on the targets this
 * library is built for. */
static size_t undo_data(uint32_t capacity) {
    return ((size_t)capacity + 3u) & ~(size_t)3u;
}

static size_t undo_stride(uint32_t capacity) {
    return sizeof(undo_image) + undo_data(capacity);
}

/*
 * Two images is the fewest worth asking the heap for: one holds where the typing
 * started and the next where it got to, and a ring of one can only ever answer that
 * there is nothing to undo.
 */
static uint32_t undo_depth(uint32_t capacity) {
    const uint32_t steps =
        (uint32_t)(((size_t)FIELD_UNDO_BYTES - sizeof(undo_ring)) / undo_stride(capacity));

    if (steps < 2u) {
        return 0u;
    }

    return steps < FIELD_UNDO_IMAGES ? steps : FIELD_UNDO_IMAGES;
}

static size_t undo_bytes(uint32_t depth, uint32_t capacity) {
    return sizeof(undo_ring) + (size_t)depth * undo_stride(capacity);
}

/*
 * The block under the field's own name, laid out for the buffer it rides beside. False
 * is the answer for a field with no name to key it under, no budget that pays for two
 * images, or a heap that said no - and a field with no history serves no undo.
 */
static bool undo_open(field_data *field, undo_book *book) {
    const uint32_t capacity = field->text->capacity;
    const uint32_t depth = undo_depth(capacity);

    *book = (undo_book){.text = field->text, .ring = NULL, .capacity = capacity, .depth = depth};

    if (field->key == CF_NO_NAME || depth == 0u) {
        return false;
    }

    undo_ring *ring = cf_state(field->ctx, field->key, undo_bytes(depth, capacity));

    if (ring == NULL) {
        return false;
    }

    if (ring->capacity != capacity) {
        /* The buffer the field edits is a different size than it was: the images the
         * block holds were laid out in another stride, and their lengths belong to that
         * layout rather than to the buffer now asking to be filled from them. */
        *ring = (undo_ring){.capacity = capacity, .head = 0u, .count = 0u, .pos = 0u};
    }

    book->ring = ring;
    return true;
}

static char *undo_slot(const undo_book *book, uint32_t index) {
    const uint32_t slot = (book->ring->head + index) % book->depth;

    return (char *)book->ring + sizeof(undo_ring) + (size_t)slot * undo_stride(book->capacity);
}

static void undo_store(const undo_book *book, uint32_t index) {
    char *slot = undo_slot(book, index);
    const cf_text_edit *text = book->text;

    *(undo_image *)slot =
        (undo_image){.length = text->length, .caret = text->caret, .mark = text->mark};
    memcpy(slot + sizeof(undo_image), text->chars, text->length);
}

static void undo_load(const undo_book *book, uint32_t index) {
    const char *slot = undo_slot(book, index);
    const undo_image *image = (const undo_image *)slot;
    cf_text_edit *text = book->text;

    memcpy(text->chars, slot + sizeof *image, image->length);
    text->length = image->length;
    text->caret = image->caret;
    text->mark = image->mark;
}

/*
 * Makes the newest image the buffer's own state. Anything the ring held past it goes:
 * a keystroke after a step back is a new branch, and the redo it cancels is not
 * reachable again. A change that moved no bytes writes the marks into the image the
 * buffer already matches instead of costing a step, so nobody has to undo their way
 * back through the caret - which is why a caret move needs no note of its own: the next
 * note, wherever it is taken, folds the marks into the state a step forward returns to.
 */
static void undo_note(const undo_book *book) {
    undo_ring *ring = book->ring;
    const cf_text_edit *text = book->text;

    if (ring->count == 0u) {
        ring->count = 1u;
        ring->pos = 0u;
        undo_store(book, 0u);
        return;
    }

    /* A buffer whose bytes are still the ones the image at `pos` put back has not gone
     * anywhere new, whatever its caret is doing: the walk forward from here is intact,
     * because only an edit can end it. */
    char *top = undo_slot(book, ring->pos);

    if (((undo_image *)top)->length == text->length &&
        memcmp(top + sizeof(undo_image), text->chars, text->length) == 0) {
        ((undo_image *)top)->caret = text->caret;
        ((undo_image *)top)->mark = text->mark;
        return;
    }

    if (ring->pos + 1u < ring->count) {
        ring->count = ring->pos + 1u;
    }

    if (ring->count < book->depth) {
        ring->count++;
    } else {
        ring->head = (ring->head + 1u) % book->depth;
    }

    ring->pos = ring->count - 1u;
    undo_store(book, ring->pos);
}

/*
 * One step back, or forward through a step just taken. False leaves the buffer alone:
 * the ring holds the states this field's keys produced and invents none, so a key at
 * either edge is simply spent, the way an arrow at the end of the value is.
 */
static bool undo_step(const undo_book *book, bool back) {
    if (!back) {
        if (book->ring->pos + 1u >= book->ring->count) {
            return false;
        }

        book->ring->pos++;
        undo_load(book, book->ring->pos);
        return true;
    }

    /* Where the build has written the bytes itself, the state the user stands in is not
     * in the ring yet: note it, so the step back has somewhere to come forward to. */
    undo_note(book);

    if (book->ring->pos == 0u) {
        return false;
    }

    book->ring->pos--;
    undo_load(book, book->ring->pos);
    return true;
}

/*
 * The walk the box paints and the click reads, built from the same buffer both
 * times: what the paint drew is what an offset is measured along, so a caret a
 * click places stands under the pointer that placed it.
 */
static cf_text_content content_of(const field_data *field) {
    const cf_text_edit *text = field->text;

    return (cf_text_content){.stack = field->stack,
                             .text = {text->chars, text->length},
                             .caret = {.present = true, .value = text->caret},
                             .mark = {.present = true, .value = text->mark},
                             .nowrap = true};
}

/* One scalar back: over the continuation bytes the scalar owns, then off its
 * lead byte. */
static uint32_t back_one(const cf_text_edit *text, uint32_t at) {
    while (at > 0u && ((unsigned char)text->chars[at - 1u] & 0xC0u) == 0x80u) {
        at--;
    }

    return at > 0u ? at - 1u : 0u;
}

/* One scalar on: past the lead byte and every byte that continues it, stopping
 * at the end of what the buffer holds. */
static uint32_t forward_one(const cf_text_edit *text, uint32_t at) {
    if (at >= text->length) {
        return text->length;
    }

    at++;

    while (at < text->length && ((unsigned char)text->chars[at] & 0xC0u) == 0x80u) {
        at++;
    }

    return at;
}

/*
 * The classes a word key moves between, read off the byte the scalar walk above
 * already lands on: a blank, an ASCII letter or digit, and anything else. The last
 * class holds every scalar above ASCII, because naming a codepoint's category is the
 * text layer's work and its decoder is private to it - the same limit that makes a
 * combining mark two deletes instead of one, which docs/backlog.md keeps. So an
 * unspaced CJK run is one jump rather than one glyph, and a pasted non-breaking
 * space ends a word instead of being one.
 */
static bool blank_lead(const cf_text_edit *text, uint32_t at) {
    if (at >= text->length) {
        return false;
    }

    const unsigned char lead = (unsigned char)text->chars[at];

    return lead == 0x20u || (lead >= 0x09u && lead <= 0x0Du);
}

static bool letter_lead(const cf_text_edit *text, uint32_t at) {
    if (at >= text->length) {
        return false;
    }

    const unsigned char lead = (unsigned char)text->chars[at];

    return (lead >= (unsigned char)'a' && lead <= (unsigned char)'z') ||
           (lead >= (unsigned char)'A' && lead <= (unsigned char)'Z') ||
           (lead >= (unsigned char)'0' && lead <= (unsigned char)'9');
}

/* The rest of what a buffer can hold: a comma, an ideograph, a byte no sequence
 * began. A word key owns it as a run of its own, so punctuation between two words
 * costs one jump rather than being crossed in silence. */
static bool other_lead(const cf_text_edit *text, uint32_t at) {
    return at < text->length && !blank_lead(text, at) && !letter_lead(text, at);
}

/* As far as the run reaching forward from `at` goes. A caret standing where the run
 * does not answer ends where it stands, which is what spends an arrow at the edge of
 * the buffer instead of walking off it. */
static uint32_t run_end(const cf_text_edit *text, uint32_t at,
                        bool (*kind)(const cf_text_edit *, uint32_t)) {
    while (kind(text, at)) {
        at = forward_one(text, at);
    }

    return at;
}

/* The same walk backwards: a caret sits between two scalars, and the one a back
 * arrow owns is the one behind it. */
static uint32_t run_start(const cf_text_edit *text, uint32_t at,
                          bool (*kind)(const cf_text_edit *, uint32_t)) {
    while (at > 0u && kind(text, back_one(text, at))) {
        at = back_one(text, at);
    }

    return at;
}

static uint32_t forward_word(const cf_text_edit *text, uint32_t at) {
    if (letter_lead(text, at)) {
        return run_end(text, at, letter_lead);
    }

    if (other_lead(text, at)) {
        return run_end(text, at, other_lead);
    }

    if (!blank_lead(text, at)) {
        return at;
    }

    /* Standing in the gap, the reader is asking for the word beyond it: the blanks
     * are spent and the run after them finished, so one keypress crosses both. */
    at = run_end(text, at, blank_lead);

    return letter_lead(text, at) ? run_end(text, at, letter_lead) : run_end(text, at, other_lead);
}

static uint32_t back_word(const cf_text_edit *text, uint32_t at) {
    if (at == 0u) {
        return 0u;
    }

    const uint32_t before = back_one(text, at);

    if (letter_lead(text, before)) {
        return run_start(text, at, letter_lead);
    }

    if (other_lead(text, before)) {
        return run_start(text, at, other_lead);
    }

    if (!blank_lead(text, before)) {
        return at;
    }

    at = run_start(text, at, blank_lead);

    if (at == 0u) {
        return 0u;
    }

    const uint32_t prior = back_one(text, at);

    return letter_lead(text, prior) ? run_start(text, at, letter_lead)
                                    : run_start(text, at, other_lead);
}

/* A run carrying a line break is refused whole: the field shows one run of
 * bytes, and a break in the middle would put the caret on a row no key here
 * can reach back to. */
static bool holds_break(cf_string run) {
    for (uint32_t i = 0u; i < run.length; ++i) {
        if (run.chars[i] == '\n' || run.chars[i] == '\r') {
            return true;
        }
    }

    return false;
}

/*
 * What the keyboard wrote, over the selection: the range goes, the bytes come,
 * and the caret lands after them with nothing left selected. A run longer than
 * the room the buffer has left is refused whole rather than cut, because half a
 * UTF-8 sequence is bytes nothing can read back.
 *
 * Both sides of the change are noted, so the state before it is a step the user can
 * come back from and the state after it is where a step back returns to.
 */
static void replace(field_data *field, cf_string run) {
    cf_text_edit *text = field->text;

    if (run.chars == NULL || run.length == 0u || holds_break(run)) {
        return;
    }

    const uint32_t from = sel_from(text);
    const uint32_t to = sel_to(text);
    const uint32_t kept = text->length - (to - from);

    if (kept + run.length > text->capacity) {
        /* Said so in the buffer the page reads, because a key that lands nowhere is
         * otherwise indistinguishable from a key that was never sent. */
        text->refusals++;
        return;
    }

    undo_book book;
    const bool held = undo_open(field, &book);

    if (held) {
        undo_note(&book);
    }

    memmove(text->chars + from + run.length, text->chars + to, text->length - to);
    memcpy(text->chars + from, run.chars, run.length);
    text->length = kept + run.length;
    text->caret = from + run.length;
    text->mark = text->caret;

    if (held) {
        undo_note(&book);
    }
}

/* The bytes between two offsets go, and the caret where they were. */
static void cut(field_data *field, uint32_t from, uint32_t to) {
    cf_text_edit *text = field->text;
    undo_book book;
    const bool held = undo_open(field, &book);

    if (held) {
        undo_note(&book);
    }

    memmove(text->chars + from, text->chars + to, text->length - to);
    text->length -= to - from;
    text->caret = from;
    text->mark = from;

    if (held) {
        undo_note(&book);
    }
}

/* The bytes between the marks, or none where the field has selected nothing. */
static cf_string selection(const cf_text_edit *text) {
    const uint32_t from = sel_from(text);
    const uint32_t to = sel_to(text);

    return (cf_string){.chars = text->chars + from, .length = to - from};
}

/*
 * The key's letter, lower case, or nothing for a key that is not a Latin letter. A
 * backend hands over the ASCII number - event.h states that agreement - so naming
 * the letter here reads no device's code for the key beside it.
 */
static char letter_of(uint32_t key) {
    if (key >= (uint32_t)'A' && key <= (uint32_t)'Z') {
        return (char)(key + ((uint32_t)'a' - (uint32_t)'A'));
    }

    return key >= (uint32_t)'a' && key <= (uint32_t)'z' ? (char)key : '\0';
}

/*
 * The selection, offered to the host. Cutting carries the same bytes and drops them
 * only once the host has taken them: a refused write leaves the bytes selected,
 * which is the state the user can still do something about.
 */
static void carry_out(field_data *field, bool remove) {
    const cf_clipboard *board = field->ctx->clipboard;
    cf_text_edit *text = field->text;
    const cf_string run = selection(text);

    if (board == NULL || board->write == NULL || run.length == 0u ||
        !board->write(board->user_data, run)) {
        return;
    }

    if (remove) {
        cut(field, sel_from(text), sel_to(text));
    }
}

/*
 * What the host holds, put over the selection. The length is asked for first so the
 * run crosses once and the frame arena holds it exactly once: a clipboard the frame
 * cannot pay for, like one the buffer cannot hold, is refused whole rather than
 * half-inserted, which is the rule this field already keeps for bytes that will not
 * fit.
 */
static void bring_in(field_data *field) {
    const cf_clipboard *board = field->ctx->clipboard;

    if (board == NULL || board->read == NULL) {
        return;
    }

    const uint32_t wanted = board->read(board->user_data, NULL, 0u).length;
    char *scratch = wanted > 0u ? cf_alloc(&field->ctx->frame, wanted) : NULL;

    if (scratch == NULL) {
        return;
    }

    replace(field, board->read(board->user_data, scratch, wanted));
}

/*
 * The chords a field serves itself, on either of the two bits a platform puts its main
 * modifier on: which of Control and a command key carries a shortcut is the platform's
 * decision, and a field that reads either is right on each rather than wrong on one.
 * Alt stays out of them - a chord with Alt in it is the desk or the host's to keep -
 * and a chord the field cannot serve is left to whoever is above it, because the keys
 * it cannot serve are not its to swallow. That is why the trio that trades bytes needs a
 * clipboard and the pair that walks the history needs only the field's own ring: a field
 * with no name to key it under, or a buffer too large to copy, has no history to serve.
 */
static bool serve_shortcut(field_data *field, cf_event *event) {
    const char letter = letter_of(event->key);
    const uint32_t primary = (uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_META;
    const bool trades = field->ctx->clipboard != NULL;

    if (letter == '\0' || (event->mods & (uint32_t)CF_MOD_ALT) != 0u ||
        (event->mods & primary) == 0u) {
        return false;
    }

    if (trades && letter == 'c') {
        carry_out(field, false);
    } else if (trades && letter == 'x') {
        carry_out(field, true);
    } else if (trades && letter == 'v') {
        bring_in(field);
    } else if (letter == 'a') {
        /* Both marks at the buffer's ends, and nothing else: the ask is to hold the
         * value, not to change it, so the history is left out of it the way a caret
         * move is. An empty buffer selects the empty run it holds, which is where the
         * next key types. */
        field->text->mark = 0u;
        field->text->caret = field->text->length;
    } else if (letter == 'z' || letter == 'y') {
        undo_book book;

        if (!undo_open(field, &book)) {
            return false;
        }

        /* Shift turns the ask around, and the Windows spelling of redo rides the key
         * beside it rather than a modifier this field reads. */
        const bool back = letter == 'z' && (event->mods & (uint32_t)CF_MOD_SHIFT) == 0u;

        undo_step(&book, back);
    } else {
        return false;
    }

    return true;
}

static void field_key(cf_event *event, void *user_data) {
    field_data *field = user_data;
    cf_text_edit *text = field->text;

    clamp_marks(text);

    if (event->type == CF_EVENT_TEXT) {
        replace(field, event->text);
        cf_stop(event);
        return;
    }

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    /* The chords the field serves itself come before the keys that move the caret:
     * they are the ones the user asks the selection or the history to do something
     * about. */
    if (serve_shortcut(field, event)) {
        cf_stop(event);
        return;
    }

    /* Whether a shift makes a capital is the device's, already settled inside
     * the text it sent; whether it means "extend what is selected" is the
     * field's, and this is the only place it can read it. */
    const bool shift = (event->mods & (uint32_t)CF_MOD_SHIFT) != 0u;
    /* Either of the two main modifiers, the same pair the chords read: which one a
     * desk puts a word key on is its decision, and honouring both is right on each.
     * An alt is left out the way the chords leave it, so a desk binding its own arrow
     * chord still finds the arrow doing what it did before this key existed. */
    const bool by_word = (event->mods & ((uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_META)) != 0u &&
                         (event->mods & (uint32_t)CF_MOD_ALT) == 0u;

    switch ((cf_key)event->key) {
    case CF_KEY_LEFT:
    case CF_KEY_RIGHT: {
        const bool back = event->key == (uint32_t)CF_KEY_LEFT;

        /* Nothing held, an arrow collapses a selection to the end it points at
         * rather than stepping away from where the caret already stands. The
         * modifier decides how far a step reaches, not whether this one happens. */
        if (!shift && sel_from(text) != sel_to(text)) {
            text->caret = back ? sel_from(text) : sel_to(text);
            text->mark = text->caret;
            break;
        }

        if (back) {
            text->caret = by_word ? back_word(text, text->caret) : back_one(text, text->caret);
        } else {
            text->caret =
                by_word ? forward_word(text, text->caret) : forward_one(text, text->caret);
        }

        if (!shift) {
            text->mark = text->caret;
        }
        break;
    }

    case CF_KEY_HOME:
    case CF_KEY_END:
        text->caret = event->key == (uint32_t)CF_KEY_HOME ? 0u : text->length;

        if (!shift) {
            text->mark = text->caret;
        }
        break;

    case CF_KEY_BACKSPACE:
        if (sel_from(text) != sel_to(text)) {
            cut(field, sel_from(text), sel_to(text));
        } else if (text->caret > 0u) {
            cut(field, back_one(text, text->caret), text->caret);
        }
        break;

    case CF_KEY_DELETE:
        if (sel_from(text) != sel_to(text)) {
            cut(field, sel_from(text), sel_to(text));
        } else if (text->caret < text->length) {
            cut(field, text->caret, forward_one(text, text->caret));
        }
        break;

    default:
        /* A key the field has no word for writes nothing and stops nothing:
         * the number a modifier carries is not a character, and an arrow the
         * field does not name belongs to whoever holds it. */
        return;
    }

    cf_stop(event);
}

/*
 * The byte offset a point in the box lands on, read back along the lines the
 * box paints: the same insets, the same wrap and the same slide, so the caret
 * goes where the glyph is. False when the box has nothing to place a caret in.
 */
static bool offset_at(const cf_ctx *ctx, const field_data *field, uint32_t slot, cf_point where,
                      uint32_t *out) {
    const cf_tree *view = cf_frame_tree(ctx);
    const cf_text_edit *text = field->text;

    if (view == NULL || view->styles == NULL || ctx->rects == NULL || field->stack.count == 0u ||
        text->length == 0u) {
        return false;
    }

    const cf_style *style = &view->styles[slot];
    const cf_rect box = ctx->rects[slot];
    const cf_edges insets = cf_box_resolve(style->padding, box.width);
    const cf_edges stroke = cf_box_resolve(style->border, box.width);
    const float left = insets.left + stroke.left;
    const float top = insets.top + stroke.top;
    const float wrap = box.width - left - insets.right - stroke.right;
    const cf_font *primary = &field->stack.fonts[0];
    const float line_height = primary->ops->metrics(primary->handle).line_height;
    const float visible = box.height - top - insets.bottom - stroke.bottom;
    cf_text_content content = content_of(field);
    cf_text_line lines[FIELD_VISIBLE_LINES];
    const uint32_t laid = cf_text_lines(&content, wrap, lines, FIELD_VISIBLE_LINES);

    if (laid == 0u || wrap <= 0.0f) {
        return false;
    }

    const cf_point slide = cf_text_scroll(&content, lines, laid, (cf_size){wrap, visible});
    const float y = where.y - (box.y + top - slide.y);
    uint32_t row =
        line_height > 0.0f && y > 0.0f ? (uint32_t)(y / line_height) : 0u; /* below the last row */

    if (row >= laid) {
        row = laid - 1u;
    }

    *out = cf_text_offset_at(&content, &lines[row], where.x - (box.x + left - slide.x));
    return true;
}

/* Where a pointer puts the caret: the mark rides with it, or stays holding the
 * end of the selection the drag is stretching. */
static void place_at(cf_ctx *ctx, field_data *field, uint32_t slot, cf_point where,
                     bool keep_mark) {
    uint32_t offset = 0u;

    if (!offset_at(ctx, field, slot, where, &offset)) {
        return;
    }

    if (!keep_mark) {
        field->text->mark = offset;
    }

    field->text->caret = offset;
}

static void field_pointer(cf_event *event, void *user_data) {
    field_data *field = user_data;
    cf_ctx *ctx = field->ctx;
    const bool shift = (event->mods & (uint32_t)CF_MOD_SHIFT) != 0u;

    if (event->type == CF_EVENT_POINTER_DOWN) {
        /* The press holds the pointer, so a drag that strays past the box keeps
         * selecting to wherever the hand goes. */
        cf_capture(event);
        place_at(ctx, field, event->target, event->position, shift);
        cf_stop(event);
        return;
    }

    /* Only a move that comes out of a press on this field selects: hovering a
     * field must not move anyone's caret. */
    if (event->type == CF_EVENT_POINTER_MOVE && cf_pressed(ctx, event->target)) {
        place_at(ctx, field, event->target, event->position, true);
        cf_stop(event);
    }
}

cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack *stack) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || text == NULL || text->chars == NULL) {
        return node;
    }

    /* The handler's block lives in the frame arena beside the frame that declared
     * it: both are read by this frame's dispatch alone. */
    field_data *data = cf_alloc(&ctx->frame, sizeof *data);

    if (data == NULL) {
        return node;
    }

    node.key = cf_intern_text(&ctx->names, name);

    *data = (field_data){.ctx = ctx,
                         .text = text,
                         .key = node.key,
                         .stack = stack != NULL ? *stack : (cf_text_stack){0}};

    clamp_marks(text);

    /* Asking for the block is the whole point of the call: a name the builds stop
     * requesting loses its state the frame after, so declaring the field is what keeps
     * its history alive. Nothing is noted here - the states the ring holds are the ones
     * the field's own keys produced, not the ones a build wrote behind them. */
    undo_book history;
    (void)undo_open(data, &history);

    node.role = CF_ROLE_FIELD;
    /* A field is the control a keyboard exists for, so it has to be reachable by
     * one: the handler answers the keys, and taking the focus is what gets them
     * here in the first place. */
    node.focusable = true;
    node.handlers.pointer = field_pointer;
    node.handlers.pointer_data = data;
    node.handlers.key = field_key;
    node.handlers.key_data = data;

    /* The field paints its own bytes rather than passing them to a label: the
     * box that holds the value is the box the theme styles by focus, and a caret
     * the theme hands out has to reach the lines it stands among. */
    node.style = ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_FIELD].base : cf_style_default();
    node.style.color = (cf_opt_color){.present = true,
                                      .value = ctx->theme != NULL ? ctx->theme->ink
                                                                  : (cf_color){0u, 0u, 0u, 255u}};

    if (stack != NULL && stack->fonts != NULL && stack->count > 0u) {
        cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

        if (content != NULL) {
            *content = content_of(data);
            node.measure = cf_text_measure;
            node.measure_data = content;
        }
    }

    return node;
}
