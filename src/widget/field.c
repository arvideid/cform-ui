#include <string.h>

#include "cf/widget.h"

/*
 * The lines a click can land on and the rows an arrow steps between. A single-line
 * field's value refuses to wrap, so one row is what a typed run lays; the rest is the
 * room a buffer holds between the breaks the field's own Enter wrote. A point below them
 * all, or a step past them, reads the last line rather than nothing.
 */
#define FIELD_VISIBLE_LINES 8u

/*
 * The room a field keeps for a run an input method has not settled on. A run is kept whole
 * or not kept: half a sequence painted as preedit is bytes the platform no longer
 * recognises and a rule drawn under nothing an offset answers, so a longer run is refused
 * the way a run too long for the value buffer is, and said so through the same count.
 */
#define FIELD_MARKED_BYTES 64u

/*
 * The run as one frame sees it: the bytes, and the pair of value offsets they were written
 * between. Those two are the selection the composition started on, kept so that a cancel
 * leaves the box holding what it held and a commit replaces exactly that range.
 */
typedef struct field_marked {
    uint32_t length; /* none is a field deciding nothing */
    uint32_t from;
    uint32_t to;
    char bytes[FIELD_MARKED_BYTES];
} field_marked;

/*
 * What a handler needs and the declaration does not carry: the context to read
 * the box's rect and the interaction off, the buffer to edit, the name the
 * history is keyed under, the fonts to lay the bytes in - the stack the build
 * handed the field, borrowed for as long as the field is declared, which is the
 * rule a label's stack already follows - and the mode that decides what Enter and
 * the vertical arrows are.
 *
 * `edited` is a fact about the key being served rather than about the box: the value
 * moves once per event however many runs the chord put in, so the edit sites note it and
 * the handler reports once on the way out.
 *
 * `marked` is read out of the keyed block once, at the build, so that every walk of the
 * frame - the paint's, a click's, an arrow's - answers from one copy rather than reaching
 * into build state each time it wants to know what the input method is holding.
 */
typedef struct field_data {
    cf_ctx *ctx;
    cf_text_edit *text;
    cf_name key;
    cf_text_stack stack;
    cf_field_mode mode;
    field_marked marked;
    bool edited;
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

/*
 * What a field keeps under its own name across frames: the run an input method is
 * deciding, then the history ring whose images follow its header. `cf_state` serves one
 * block per name, so the two halves share it, and the marked part is a fixed size and comes
 * first - it sits at the same address whatever a change of the buffer's capacity did to the
 * ring's layout, and the ring's own walk keeps reading `(char *)ring + sizeof(undo_ring)`.
 */
typedef struct field_state {
    uint32_t marked; /* bytes held; none is a field deciding nothing */
    uint32_t from;
    uint32_t to;
    char bytes[FIELD_MARKED_BYTES];
    undo_ring ring;
} field_state;

/*
 * The block's head - the marked part that comes before the ring. Every member is word-sized
 * and the ring is last, so the difference of the two sizes is where the ring begins and the
 * layout does not depend on a compiler's padding.
 */
static size_t field_head(void) {
    return sizeof(field_state) - sizeof(undo_ring);
}

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
 * there is nothing to undo. The preedit's head is charged to the same budget, so the block
 * a field holds stays bounded by FIELD_UNDO_BYTES however the buffer changes.
 */
static uint32_t undo_depth(uint32_t capacity) {
    const uint32_t steps =
        (uint32_t)(((size_t)FIELD_UNDO_BYTES - field_head() - sizeof(undo_ring)) /
                   undo_stride(capacity));

    if (steps < 2u) {
        return 0u;
    }

    return steps < FIELD_UNDO_IMAGES ? steps : FIELD_UNDO_IMAGES;
}

static size_t undo_bytes(uint32_t depth, uint32_t capacity) {
    return sizeof(undo_ring) + (size_t)depth * undo_stride(capacity);
}

/*
 * The field's keyed block, sized for the buffer it rides beside, or NULL for a box with nothing to
 * keep. Asking for it is what keeps both halves alive: a name the builds stop requesting loses its
 * history and its preedit together, one frame later. `holding` is that word - whether this box is
 * the one a method is deciding a run for, or has a history at all. A buffer too large for even two
 * history images asks for nothing while it is also typing no run, so a page of wide read-only boxes
 * costs no heap beside its bytes, which is what it cost before a preedit existed.
 */
static field_state *field_hold(field_data *field, bool holding) {
    const uint32_t capacity = field->text->capacity;

    if (field->key == CF_NO_NAME || (!holding && undo_depth(capacity) == 0u)) {
        return NULL;
    }

    return cf_state(field->ctx, field->key,
                    field_head() + undo_bytes(undo_depth(capacity), capacity));
}

/*
 * The run as this frame's buffer can show it. Offsets the value no longer reaches mean the
 * bytes were written behind the composition's back - a build that replaced the value, a
 * buffer that changed size - and a run nobody can place is dropped rather than painted
 * between letters it no longer belongs to.
 */
static void marked_read(field_data *field, field_state *state) {
    field->marked = (field_marked){0};

    if (state == NULL) {
        return;
    }

    if (state->marked == 0u || state->marked > FIELD_MARKED_BYTES || state->from > state->to ||
        state->to > field->text->length) {
        state->marked = 0u;
        return;
    }

    field->marked.length = state->marked;
    field->marked.from = state->from;
    field->marked.to = state->to;
    memcpy(field->marked.bytes, state->bytes, state->marked);
}

/*
 * The block under the field's own name, laid out for the buffer it rides beside. False is the
 * answer for a field with no name to key it under, no budget that pays for two images, or a
 * heap that said no - and a field with no history serves no undo.
 */
static bool undo_open(field_data *field, undo_book *book) {
    const uint32_t capacity = field->text->capacity;
    const uint32_t depth = undo_depth(capacity);

    *book = (undo_book){.text = field->text, .ring = NULL, .capacity = capacity, .depth = depth};

    if (depth == 0u) {
        return false;
    }

    field_state *state = field_hold(field, true);

    if (state == NULL) {
        return false;
    }

    if (state->ring.capacity != capacity) {
        /* The buffer the field edits is a different size than it was: the images the block
         * holds were laid out in another stride, and their lengths belong to that layout
         * rather than to the buffer now asking to be filled from them. */
        state->ring = (undo_ring){.capacity = capacity, .head = 0u, .count = 0u, .pos = 0u};
    }

    book->ring = &state->ring;
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
 * The walk the box paints, the click reads and an arrow steps along, built from the same
 * buffer every time: what the paint drew is what an offset is measured along, so a caret
 * a click places stands under the pointer that placed it and a caret an arrow moves
 * lands on the row the eye sees. The mode is the only difference: a single-line field
 * refuses to wrap, so only the breaks it holds make rows.
 *
 * The run an input method is holding is spliced in between the value offsets it covers, so
 * the rows lay what a person reads and the caret stands where they see it. It lands in a
 * display buffer of its own rather than in the value because the value the page owns holds
 * only what the person settled on: ending a run the person rejected costs a frame instead of
 * an undo step, and the change door never hears about bytes that were never the page's.
 * A buffer the frame cannot pay for lays the value alone - the preedit is missing for a
 * frame rather than half-drawn - which is the answer this field already gives a run the
 * room cannot hold.
 */
static cf_text_content content_of(const field_data *field) {
    const cf_text_edit *text = field->text;
    cf_text_content content = (cf_text_content){.stack = field->stack,
                                                .text = {text->chars, text->length},
                                                .caret = {.present = true, .value = text->caret},
                                                .mark = {.present = true, .value = text->mark},
                                                .nowrap = field->mode == CF_FIELD_SINGLE};

    if (field->marked.length == 0u) {
        return content;
    }

    const uint32_t tail = text->length - field->marked.to;
    const uint32_t across = field->marked.from + field->marked.length;
    char *display = cf_alloc(&field->ctx->frame, (size_t)across + tail);

    if (display == NULL) {
        return content;
    }

    memcpy(display, text->chars, field->marked.from);
    memcpy(display + field->marked.from, field->marked.bytes, field->marked.length);
    memcpy(display + across, text->chars + field->marked.to, tail);

    content.text = (cf_string){display, across + tail};
    /* The caret follows the run the person is typing, and the mark rides with it: a run
     * covers the selection it began on rather than adding one of its own. */
    content.caret = (cf_opt_u32){.present = true, .value = across};
    content.mark = (cf_opt_u32){.present = true, .value = across};
    content.marked_from = (cf_opt_u32){.present = true, .value = field->marked.from};
    content.marked_to = (cf_opt_u32){.present = true, .value = across};
    return content;
}

/*
 * One character back, one character on - a character being what a reader sees, so an
 * accent written onto a letter is crossed with it and an emoji built from joined
 * pictures comes out on one keypress. The rule is the text layer's (cf/text.h), which
 * is also where its boundaries are read from: a field that counted its own bytes
 * would disagree with the caret a click places about where a character begins.
 */
static uint32_t back_cluster(const cf_text_edit *text, uint32_t at) {
    return cf_text_prev_cluster((cf_string){text->chars, text->length}, at);
}

static uint32_t forward_cluster(const cf_text_edit *text, uint32_t at) {
    return cf_text_next_cluster((cf_string){text->chars, text->length}, at);
}

/*
 * The classes a word key moves between, read off the scalar that opens the cluster the
 * walk above lands on: a blank, an ASCII letter or digit, and anything else. The last
 * class holds every non-ASCII letter, because naming a codepoint's category is the
 * text layer's work and its tables carry the marks rather than the letters - so an
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
        at = forward_cluster(text, at);
    }

    return at;
}

/* The same walk backwards: a caret sits between two characters, and the one a back
 * arrow owns is the one behind it. */
static uint32_t run_start(const cf_text_edit *text, uint32_t at,
                          bool (*kind)(const cf_text_edit *, uint32_t)) {
    while (at > 0u && kind(text, back_cluster(text, at))) {
        at = back_cluster(text, at);
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

    const uint32_t before = back_cluster(text, at);

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

    const uint32_t prior = back_cluster(text, at);

    return letter_lead(text, prior) ? run_start(text, at, letter_lead)
                                    : run_start(text, at, other_lead);
}

/* A break the mode cannot show is refused whole: a single-line field would put the caret
 * on a row no key here reaches back to, and a carriage return lays no row in either mode
 * - the walk breaks a line at the newline a device sends, and the byte beside it would
 * hold a place in the value while naming no row. */
static bool holds_refused_break(const field_data *field, cf_string run) {
    for (uint32_t i = 0u; i < run.length; ++i) {
        const char byte = run.chars[i];

        if (byte == '\r' || (byte == '\n' && field->mode == CF_FIELD_SINGLE)) {
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

    if (run.chars == NULL || run.length == 0u || holds_refused_break(field, run)) {
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

    field->edited = true;
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

    field->edited = true;
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
         * beside it rather than a modifier this field reads. A step that found nowhere to
         * go wrote no bytes, so it is not a change the page has to look at. */
        const bool back = letter == 'z' && (event->mods & (uint32_t)CF_MOD_SHIFT) == 0u;

        if (undo_step(&book, back)) {
            field->edited = true;
        }
    } else {
        return false;
    }

    return true;
}

/*
 * The event is the field's, and what it moved is the page's to hear about: the door runs
 * once, on the event the bytes came in on, so a form validating a value reads its own
 * buffer here instead of waiting for the next build to notice the caret is somewhere new.
 */
static void claim(field_data *field, cf_event *event) {
    if (field->edited) {
        cf_report_change(field->ctx, event);
    }

    cf_stop(event);
}

/*
 * One read of the box: the rows the value lays and the numbers around them, so a click
 * and an arrow measure along the same lines the paint painted and neither of them
 * re-derives the insets on its own. `rows` is how many `lines` holds, which is never more
 * than the array - a walk longer than the room stops at its last row rather than reading
 * past it. False says there is nothing to place a caret in: no frame drawn yet, no font
 * to lay the bytes with, no value to lay, or a box too narrow to hold a row.
 */
typedef struct field_walk {
    cf_text_content content;
    cf_text_line lines[FIELD_VISIBLE_LINES];
    cf_rect box;
    cf_point origin; /* where a row's left edge and the first row's top sit */
    uint32_t rows;
    float wrap;
    float visible; /* the height the rows are slid within */
    float line_height;
} field_walk;

static bool walk_of(const cf_ctx *ctx, const field_data *field, uint32_t slot, field_walk *walk) {
    const cf_tree *view = cf_frame_tree(ctx);

    if (view == NULL || view->styles == NULL || ctx->rects == NULL || slot >= view->node_count ||
        field->stack.count == 0u || (field->text->length == 0u && field->marked.length == 0u)) {
        return false;
    }

    const cf_style *style = &view->styles[slot];
    const cf_font *primary = &field->stack.fonts[0];

    walk->box = ctx->rects[slot];

    const cf_edges insets = cf_box_resolve(style->padding, walk->box.width);
    const cf_edges stroke = cf_box_resolve(style->border, walk->box.width);

    walk->origin =
        (cf_point){walk->box.x + insets.left + stroke.left, walk->box.y + insets.top + stroke.top};
    walk->line_height = primary->ops->metrics(primary->handle).line_height;
    walk->wrap = walk->box.width - insets.left - stroke.left - insets.right - stroke.right;
    walk->visible = walk->box.height - insets.top - stroke.top - insets.bottom - stroke.bottom;
    walk->content = content_of(field);

    if (walk->wrap <= 0.0f) {
        return false;
    }

    const uint32_t laid =
        cf_text_lines(&walk->content, walk->wrap, walk->lines, FIELD_VISIBLE_LINES);

    walk->rows = laid < FIELD_VISIBLE_LINES ? laid : FIELD_VISIBLE_LINES;
    return walk->rows > 0u;
}

/*
 * One arrow between the rows: the caret keeps the distance it had travelled across the row
 * it stands on, which is the same question a click answers in reverse and the reason a
 * walk down a column of words lands under the word the eye was reading rather than at the
 * row's edge. Past the first or the last row the caret stays there, because a row the box
 * does not lay is not a row an arrow can reach.
 */
static void step_row(field_data *field, uint32_t slot, bool back, bool shift) {
    cf_text_edit *text = field->text;
    field_walk walk;

    if (!walk_of(field->ctx, field, slot, &walk)) {
        return;
    }

    const uint32_t row = cf_text_line_of(&walk.content, walk.lines, walk.rows, text->caret);
    const uint32_t target =
        back ? (row > 0u ? row - 1u : 0u) : (row + 1u < walk.rows ? row + 1u : walk.rows - 1u);
    const float across = cf_text_caret_x(&walk.content, &walk.lines[row], text->caret);

    text->caret = cf_text_offset_at(&walk.content, &walk.lines[target], across);

    if (!shift) {
        text->mark = text->caret;
    }
}

/*
 * Where the run an input method is holding stands inside the box that carries it, on the row
 * that holds its end: the place a platform puts its candidate window beside. It is measured along
 * the same walk the paint painted, which is what lets the window sit under the letters a person is
 * reading rather than under a caret the preedit has already pushed past them. The answer is kept
 * relative to the box rather than as a point on the page because the frame that lays the box is the
 * one that reports it: a page that scrolls or resizes under a run the method has not finished
 * moves the window along with the underline instead of leaving it over the row the run came from.
 * Zero extent is the answer when there is nothing to put a window near - no frame has laid the box,
 * the run reaches no row the walk holds - and a platform told that answer puts its candidates away.
 */
static cf_rect run_offset(const field_data *field, uint32_t slot) {
    field_walk walk;

    if (field->marked.length == 0u || !walk_of(field->ctx, field, slot, &walk)) {
        return (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
    }

    const cf_point slide =
        cf_text_scroll(&walk.content, walk.lines, walk.rows, (cf_size){walk.wrap, walk.visible});
    const uint32_t across = field->marked.from + field->marked.length;
    const uint32_t row = cf_text_line_of(&walk.content, walk.lines, walk.rows, across);
    const cf_text_line *line = &walk.lines[row];
    /* The row's own first byte, read off the bytes the row points into: a run a narrow box
     * broke across rows is placed on the row its end sits, and what that row shows of the run
     * begins where the row begins. */
    const uint32_t start = (uint32_t)(line->text.chars - walk.content.text.chars);
    const float left = cf_text_caret_x(&walk.content, line,
                                       field->marked.from > start ? field->marked.from : start);
    const float right = cf_text_caret_x(&walk.content, line, across);

    return (cf_rect){walk.origin.x - walk.box.x - slide.x + left,
                     walk.origin.y - walk.box.y - slide.y + (float)row * walk.line_height,
                     right > left ? right - left : 0.0f, walk.line_height};
}

/*
 * The run is over. It goes out of the block the field keeps, out of the walk this frame lays
 * and out of the box the platform was told to put its candidates beside, and the keyboard's
 * claim with it - so the page's value is the only thing left standing, and the change door
 * never heard a word about bytes that were never the page's. Guarded on the frame holding one,
 * because reaching for the keyed block is a walk of the store and a plain keystroke in a box at
 * rest has no business paying for it.
 */
static void end_mark(field_data *field) {
    if (field->marked.length == 0u) {
        return;
    }

    field_state *state = field_hold(field, true);

    if (state != NULL) {
        state->marked = 0u;
    }

    field->marked = (field_marked){0};

    /* Somebody else's claim is not this box's to cancel: the field that owns it ends it in its
     * own handlers, and a box that lost the keyboard only drops its own bytes. */
    if (field->ctx->composing == field->key) {
        field->ctx->composing = CF_NO_NAME;
        field->ctx->composition_box = (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
        field->ctx->composition_id = CF_NO_ID;
    }
}

/*
 * What the method says it is holding, in place of whatever the previous event staged. The run
 * never reaches the page's buffer: it goes into the field's own block and out again through the
 * walk, so a run the person rejects costs a frame and a run they settle on arrives as text and
 * is the value's. The pair of offsets it covers is the selection the box stood at when the
 * composing began, and nothing below moves those while a run stands, which is what lets a
 * commit replace exactly the bytes the preedit was drawn over.
 */
static void mark_run(field_data *field, uint32_t slot, cf_string run) {
    cf_ctx *ctx = field->ctx;
    field_state *state = field_hold(field, true);

    if (state == NULL) {
        /* Nowhere to keep a run is no way to show one, and a key that lands nowhere is
         * otherwise indistinguishable from a key that was never sent: the box says so through
         * the count the page already reads for bytes the room cannot hold. */
        field->text->refusals++;
        return;
    }

    if (run.chars == NULL || run.length == 0u) {
        /* The method finished with the run without choosing anything out of it. */
        end_mark(field);
        return;
    }

    if (run.length > FIELD_MARKED_BYTES) {
        /* Half a sequence is worse than none: bytes cut between a letter and the mark written
         * onto it paint a rule under nothing an offset answers. */
        end_mark(field);
        field->text->refusals++;
        return;
    }

    state->from = sel_from(field->text);
    state->to = sel_to(field->text);
    memcpy(state->bytes, run.chars, run.length);
    state->marked = run.length;

    /* Read back rather than trust what was written: the offsets were taken before the copy, and
     * a build that rewrote the value behind the composition's back can make them lie. */
    marked_read(field, state);
    ctx->composing = field->marked.length != 0u ? field->key : CF_NO_NAME;
    /* The run's place inside the box, and which box: the answer a platform is told is read
     * against the rect this frame laid under it, so the candidate window follows the page rather
     * than staying where the keystroke landed. */
    ctx->composition_box = run_offset(field, slot);
    ctx->composition_id = cf_store_id(&ctx->nodes, slot);
}

static void field_key(cf_event *event, void *user_data) {
    field_data *field = user_data;
    cf_text_edit *text = field->text;

    field->edited = false;
    clamp_marks(text);

    /* What the method is holding is served before anything else, because the run is the state
     * the rest of this handler reads: the bytes go into the box's own block and out again
     * through the walk, and the key that carried them is the field's to stop - a page that
     * heard it would be answering bytes it never received. */
    if (event->type == CF_EVENT_COMPOSITION) {
        mark_run(field, event->target, event->text);
        cf_stop(event);
        return;
    }

    if (event->type == CF_EVENT_TEXT) {
        /* The words the person settled on. The run they replace goes first, so the insert lands
         * across exactly the bytes the preedit was drawn over and the value gains what was
         * chosen and nothing else. */
        end_mark(field);
        replace(field, event->text);
        claim(field, event);
        return;
    }

    if (event->type != CF_EVENT_KEY_DOWN) {
        return;
    }

    /* While a run is open the keyboard belongs to the method that staged it: the same keystroke
     * arrives as a key and as the bytes it made, and a box that served the key as well would
     * type it twice or delete a byte the person was still choosing. So the field stops the keys
     * it has no business answering and keeps the one it does - Tab, which is the tree's move
     * rather than the method's, and ends the run the way walking away does. Escape needs no
     * case of its own: the run is already gone and nothing else is served, so the key is
     * stopped and the box says no more than that. A layout with no method never opens a run,
     * so a plain keystroke pays nothing here. */
    if (field->marked.length != 0u) {
        const uint32_t key = event->key;

        end_mark(field);

        if (key != (uint32_t)CF_KEY_TAB) {
            cf_stop(event);
            return;
        }
    }

    /* The chords the field serves itself come before the keys that move the caret:
     * they are the ones the user asks the selection or the history to do something
     * about. */
    if (serve_shortcut(field, event)) {
        claim(field, event);
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
            text->caret = by_word ? back_word(text, text->caret) : back_cluster(text, text->caret);
        } else {
            text->caret =
                by_word ? forward_word(text, text->caret) : forward_cluster(text, text->caret);
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
            cut(field, back_cluster(text, text->caret), text->caret);
        }
        break;

    case CF_KEY_DELETE:
        if (sel_from(text) != sel_to(text)) {
            cut(field, sel_from(text), sel_to(text));
        } else if (text->caret < text->length) {
            cut(field, text->caret, forward_cluster(text, text->caret));
        }
        break;

    /* The space bar a field holds the focus under is the character the device sends
     * beside it, so the box keeps the key: an activation ask out of the space a person
     * is typing would be the framework deciding mid-word that the value is done. Only
     * the modifiers that make a key into a binding take it away - a shift leaves the
     * space the same character - and a chord climbs the way every chord the field has
     * no word for climbs. */
    case CF_KEY_SPACE:
        if ((event->mods &
             ((uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_META | (uint32_t)CF_MOD_ALT)) != 0u) {
            return;
        }
        break;

    /* Enter is the whole difference between the two modes. A single-line field leaves it
     * unstopped, which is how the page's `activate` gets the ask a form's save button
     * answers; a multiline one writes the break the row walk steps between and stops, so
     * the same key never both breaks a line and submits the value. A chord rides it the
     * way it rides the space: a binding the page made is not a character. */
    case CF_KEY_ENTER:
        if (field->mode == CF_FIELD_SINGLE ||
            (event->mods &
             ((uint32_t)CF_MOD_CONTROL | (uint32_t)CF_MOD_META | (uint32_t)CF_MOD_ALT)) != 0u) {
            return;
        }

        const char line_break = '\n';
        replace(field, (cf_string){&line_break, 1u});
        break;

    /* Up and down name rows, and only the mode says whether this field has any. It is the
     * build's word rather than a reading of the box: a multiline field in a box one row
     * tall still breaks lines on Enter, and a single-line one that grew past a row still
     * leaves both arrows to the page. */
    case CF_KEY_UP:
    case CF_KEY_DOWN:
        if (field->mode != CF_FIELD_MULTILINE) {
            return;
        }

        step_row(field, event->target, event->key == (uint32_t)CF_KEY_UP, shift);
        break;

    default:
        /* A key the field has no word for writes nothing and stops nothing:
         * the number a modifier carries is not a character, and an arrow the
         * field does not name belongs to whoever holds it. */
        return;
    }

    claim(field, event);
}

/*
 * The byte offset a point in the box lands on, read back along the lines the box paints:
 * the same insets, the same wrap and the same slide, so the caret goes where the glyph is.
 * False when the box has nothing to place a caret in.
 */
static bool offset_at(const cf_ctx *ctx, const field_data *field, uint32_t slot, cf_point where,
                      uint32_t *out) {
    field_walk walk;

    if (!walk_of(ctx, field, slot, &walk)) {
        return false;
    }

    const cf_point slide =
        cf_text_scroll(&walk.content, walk.lines, walk.rows, (cf_size){walk.wrap, walk.visible});
    /* `origin` is already a point on the page - the box's own corner plus its insets - so adding
     * the box again would move the answer a row higher for every row the page carried the box
     * down. The paint reads the same origin, which is what makes the caret land under the glyph. */
    const float y = where.y - (walk.origin.y - slide.y);
    uint32_t row = walk.line_height > 0.0f && y > 0.0f ? (uint32_t)(y / walk.line_height) : 0u;

    if (row >= walk.rows) {
        row = walk.rows - 1u; /* below the last row the walk laid */
    }

    *out = cf_text_offset_at(&walk.content, &walk.lines[row], where.x - (walk.origin.x - slide.x));
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

        /* A caret the hand places is the box's, not the method's: the run that was standing
         * where the old caret was has nowhere to be committed to any more. */
        end_mark(field);
        place_at(ctx, field, event->target, event->position, shift);
        cf_stop(event);
        return;
    }

    /* Only a move that comes out of a press on this field selects: hovering a
     * field must not move anyone's caret. */
    if (event->type == CF_EVENT_POINTER_MOVE && cf_pressed(ctx, cf_node_id(ctx, event->target))) {
        place_at(ctx, field, event->target, event->position, true);
        cf_stop(event);
    }
}

cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack *stack,
                 cf_field_mode mode) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || text == NULL || text->chars == NULL) {
        return node;
    }

    /* The handler's block lives in the frame arena beside the frame that declared
     * it: both are read by this frame's dispatch alone. */
    field_data *data = cf_frame_alloc(ctx, sizeof *data);

    if (data == NULL) {
        return node;
    }

    node.key = cf_ctx_key(ctx, name);

    *data = (field_data){.ctx = ctx,
                         .text = text,
                         .key = node.key,
                         .stack = stack != NULL ? *stack : (cf_text_stack){0},
                         .mode = mode};

    clamp_marks(text);

    /* Asking for the block is the whole point of the call: a name the builds stop requesting
     * loses its state the frame after, so declaring the field is what keeps its history and its
     * preedit alive. Nothing is noted here - the states the ring holds are the ones the field's
     * own keys produced, not the ones a build wrote behind them. The run the block carries is
     * read once, so every walk of this frame answers from one copy. */
    field_state *held = field_hold(data, ctx->composing == data->key);

    marked_read(data, held);

    /* The run is the keyboard's to hold, and the keyboard has moved on: a box that lost it while
     * a method was deciding hands the bytes back rather than going on painting a preedit nobody
     * can commit. The claim is gone by the time this build runs, because the events were served
     * before the frame began. */
    if (data->marked.length != 0u && ctx->composing != data->key) {
        end_mark(data);
    }

    node.role = CF_ROLE_FIELD;
    /* A field is a text box to a bridge, and the bytes it lays are its value rather than its
     * name: the words to announce are the ones the build put in `name`, since the caption a
     * person reads beside a field is a sibling no walk could pair with it. */
    node.access = CF_ACCESS_TEXT_BOX;
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
        cf_text_content *content = cf_frame_alloc(ctx, sizeof *content);

        if (content != NULL) {
            *content = content_of(data);
            node.measure = cf_text_measure;
            node.measure_data = content;
        }
    }

    return node;
}
