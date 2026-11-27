#include <string.h>

#include "cf/text.h"
#include "cform_test.h"

/*
 * The fixtures are spelled in bytes because the whole question is where the
 * boundaries fall among them. Each name says which characters those bytes are.
 */
#define ACUTE "\xCC\x81"              /* U+0301, written onto what precedes */
#define E_ACUTE "e" ACUTE             /* an e carrying an accent: one character */
#define ZWJ "\xE2\x80\x8D"            /* U+200D, the zero-width joiner */
#define ZWNJ "\xE2\x80\x8C"           /* U+200C, the non-joiner: a control to this rule */
#define MAN "\xF0\x9F\x91\xA8"        /* U+1F468 */
#define WOMAN "\xF0\x9F\x91\xA9"      /* U+1F469 */
#define GIRL "\xF0\x9F\x91\xA7"       /* U+1F467 */
#define FAMILY MAN ZWJ WOMAN ZWJ GIRL /* three pictures the joiner ties into one */
#define THUMB "\xF0\x9F\x91\x8D"      /* U+1F44D */
#define SKIN "\xF0\x9F\x8F\xBD"       /* U+1F3FD, an emoji modifier */
#define HEART "\xE2\x9D\xA4"          /* U+2764 */
#define SELECTOR "\xEF\xB8\x8F"       /* U+FE0F, the emoji variation selector */
#define RI_U "\xF0\x9F\x87\xBA"       /* U+1F1FA, a regional indicator */
#define RI_S "\xF0\x9F\x87\xB8"       /* U+1F1F8 */
#define RI_D "\xF0\x9F\x87\xA9"       /* U+1F1E9 */
#define ZW_SPACE "\xE2\x80\x8B"       /* U+200B, a zero-width space: a control */

/* Every boundary a forward walk reads off a text, ending with its length. */
static uint32_t boundaries(cf_string text, uint32_t *out, uint32_t capacity) {
    uint32_t count = 0u;
    uint32_t at = 0u;

    while (count < capacity) {
        out[count++] = at;

        const uint32_t next = cf_text_next_cluster(text, at);

        if (next == at) {
            break;
        }
        at = next;
    }

    return count;
}

static void plain_ascii_steps_one_character_at_a_time(void) {
    const cf_string text = cf_string_of("ab");

    CF_CHECK(cf_text_next_cluster(text, 0u) == 1u, "the first letter is one step");
    CF_CHECK(cf_text_next_cluster(text, 1u) == 2u, "the second is the next");
    CF_CHECK(cf_text_next_cluster(text, 2u) == 2u, "and nothing lies past the end");
    CF_CHECK(cf_text_prev_cluster(text, 2u) == 1u, "back over the second letter");
    CF_CHECK(cf_text_prev_cluster(text, 1u) == 0u, "and over the first");
    CF_CHECK(cf_text_prev_cluster(text, 0u) == 0u, "the front of the text is already a boundary");
}

static void an_accent_leaves_with_the_letter_under_it(void) {
    const cf_string text = cf_string_of(E_ACUTE "x");

    CF_CHECK(cf_text_next_cluster(text, 0u) == 3u, "the letter takes its accent with it");
    CF_CHECK(cf_text_next_cluster(text, 3u) == 4u, "and the letter after it is its own step");
    CF_CHECK(cf_text_prev_cluster(text, 4u) == 3u, "back off the plain letter");
    CF_CHECK(cf_text_prev_cluster(text, 3u) == 0u, "and the pair leaves together");
}

static void a_joined_family_is_one_character(void) {
    const cf_string text = cf_string_of(FAMILY "z");

    CF_CHECK(cf_text_next_cluster(text, 0u) == 18u, "three pictures and two joiners step at once");
    CF_CHECK(cf_text_next_cluster(text, 18u) == 19u, "the letter after them is on its own");
    CF_CHECK(cf_text_prev_cluster(text, 19u) == 18u, "back off the letter");
    CF_CHECK(cf_text_prev_cluster(text, 18u) == 0u, "and the family comes out whole");
}

static void a_skin_tone_and_a_selector_stay_on_their_picture(void) {
    const cf_string tone = cf_string_of(THUMB SKIN "a");
    const cf_string selector = cf_string_of(HEART SELECTOR "a");

    CF_CHECK(cf_text_next_cluster(tone, 0u) == 8u, "the modifier rides on the picture");
    CF_CHECK(cf_text_prev_cluster(tone, 8u) == 0u, "and leaves with it");
    CF_CHECK(cf_text_next_cluster(selector, 0u) == 6u, "the variation selector too");
    CF_CHECK(cf_text_prev_cluster(selector, 6u) == 0u, "which is one step back as well");
}

/*
 * One mark from every block the table in src/text/cluster.c lists, each written after the
 * letter it belongs to. The blocks are read by a search that assumes their order, so a row
 * written out of position is a block that stops attaching while the table still shows it -
 * this is the case that says a row is reachable and not merely present.
 */
static void a_mark_from_every_block_the_table_lists(void) {
    static const struct {
        const char *run;
        uint32_t end;
    } marks[] = {
        {"a"
         "\xCC\x80",
         3u}, /* U+0300 */
        {"a"
         "\xD2\x83",
         3u}, /* U+0483 */
        {"a"
         "\xD6\x91",
         3u}, /* U+0591 */
        {"a"
         "\xD6\xBF",
         3u}, /* U+05BF */
        {"a"
         "\xD7\x81",
         3u}, /* U+05C1 */
        {"a"
         "\xD7\x84",
         3u}, /* U+05C4 */
        {"a"
         "\xD7\x87",
         3u}, /* U+05C7 */
        {"a"
         "\xD8\x90",
         3u}, /* U+0610 */
        {"a"
         "\xD9\x8B",
         3u}, /* U+064B */
        {"a"
         "\xD9\xB0",
         3u}, /* U+0670 */
        {"a"
         "\xDB\x96",
         3u}, /* U+06D6 */
        {"a"
         "\xDB\x9F",
         3u}, /* U+06DF */
        {"a"
         "\xDB\xA7",
         3u}, /* U+06E7 */
        {"a"
         "\xDB\xAA",
         3u}, /* U+06EA */
        {"a"
         "\xDC\xB0",
         3u}, /* U+0730 */
        {"a"
         "\xDE\xA6",
         3u}, /* U+07A6 */
        {"a"
         "\xE0\xA4\x80",
         4u}, /* U+0900 */
        {"a"
         "\xE0\xA4\xBA",
         4u}, /* U+093A */
        {"a"
         "\xE0\xA5\x91",
         4u}, /* U+0951 */
        {"a"
         "\xE0\xA5\xA2",
         4u}, /* U+0962 */
        {"a"
         "\xE0\xA6\x81",
         4u}, /* U+0981 */
        {"a"
         "\xE0\xA6\xBC",
         4u}, /* U+09BC */
        {"a"
         "\xE0\xA6\xBE",
         4u}, /* U+09BE */
        {"a"
         "\xE0\xA7\x87",
         4u}, /* U+09C7 */
        {"a"
         "\xE0\xA7\x8B",
         4u}, /* U+09CB */
        {"a"
         "\xE0\xA7\x97",
         4u}, /* U+09D7 */
        {"a"
         "\xE0\xA7\xA2",
         4u}, /* U+09E2 */
        {"a"
         "\xE0\xB8\xB1",
         4u}, /* U+0E31 */
        {"a"
         "\xE0\xB8\xB4",
         4u}, /* U+0E34 */
        {"a"
         "\xE0\xB9\x87",
         4u}, /* U+0E47 */
        {"a"
         "\xE0\xBA\xB1",
         4u}, /* U+0EB1 */
        {"a"
         "\xE0\xBA\xB4",
         4u}, /* U+0EB4 */
        {"a"
         "\xE0\xBB\x88",
         4u}, /* U+0EC8 */
        {"a"
         "\xE1\xAA\xB0",
         4u}, /* U+1AB0 */
        {"a"
         "\xE1\xB7\x80",
         4u}, /* U+1DC0 */
        {"a"
         "\xE2\x83\x90",
         4u}, /* U+20D0 */
        {"a"
         "\xE2\xB3\xAF",
         4u}, /* U+2CEF */
        {"a"
         "\xE2\xB7\xA0",
         4u}, /* U+2DE0 */
        {"a"
         "\xE3\x80\xAA",
         4u}, /* U+302A */
        {"a"
         "\xE3\x82\x99",
         4u}, /* U+3099 */
        {"a"
         "\xEF\xB8\x80",
         4u}, /* U+FE00 */
        {"a"
         "\xEF\xB8\xA0",
         4u}, /* U+FE20 */
        {"a"
         "\xF0\x9F\x8F\xBB",
         5u}, /* U+1F3FB */
        {"a"
         "\xF3\xA0\x80\xA0",
         5u}, /* U+E0020 */
    };

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(marks) / sizeof(marks[0])); ++i) {
        const cf_string text = cf_string_of(marks[i].run);

        CF_CHECK(cf_text_next_cluster(text, 0u) == marks[i].end, "the mark rides on the letter");
        CF_CHECK(cf_text_prev_cluster(text, marks[i].end) == 0u, "and leaves with it");
    }
}

static void two_indicators_make_a_flag_and_a_third_starts_another(void) {
    const cf_string text = cf_string_of(RI_U RI_S RI_D);

    CF_CHECK(cf_text_next_cluster(text, 0u) == 8u, "the pair is one flag");
    CF_CHECK(cf_text_next_cluster(text, 8u) == 12u, "the third indicator opens the next step");
    CF_CHECK(cf_text_prev_cluster(text, 12u) == 8u, "back over the lonely indicator");
    CF_CHECK(cf_text_prev_cluster(text, 8u) == 0u, "and over the flag it did not join");
}

static void a_pasted_carriage_return_keeps_its_line_feed(void) {
    const cf_string text = cf_string_of("\r\n" E_ACUTE "x");

    CF_CHECK(cf_text_next_cluster(text, 0u) == 2u, "the break is one step, not two");
    CF_CHECK(cf_text_next_cluster(text, 2u) == 5u, "and the accented letter after it is the next");
    CF_CHECK(cf_text_prev_cluster(text, 5u) == 2u, "back over the letter with its accent");
    CF_CHECK(cf_text_prev_cluster(text, 2u) == 0u, "and over the pasted break");
}

static void a_control_stands_alone_even_beside_a_mark(void) {
    const cf_string text = cf_string_of("\n" ACUTE "a");
    const cf_string mark = cf_string_of("a" ZW_SPACE "b");
    const cf_string non_joiner = cf_string_of("a" ZWNJ ACUTE);

    CF_CHECK(cf_text_next_cluster(text, 0u) == 1u, "a line feed ends its cluster at itself");
    CF_CHECK(cf_text_next_cluster(text, 1u) == 3u, "an accent with nothing to sit on is its own");
    CF_CHECK(cf_text_prev_cluster(text, 3u) == 1u, "which is how it steps back too");
    CF_CHECK(cf_text_next_cluster(mark, 1u) == 4u, "the invisible space is a step of its own");
    CF_CHECK(cf_text_prev_cluster(mark, 5u) == 4u, "and the letter after it leaves separately");
    CF_CHECK(cf_text_prev_cluster(mark, 4u) == 1u, "back over the space, not off its edge");
    /* The non-joiner is a format character Unicode gives the Control class, which is why
     * it is not in the table of marks above even though it is invisible beside one. */
    CF_CHECK(cf_text_next_cluster(non_joiner, 0u) == 1u, "the letter stops at the non-joiner");
    CF_CHECK(cf_text_next_cluster(non_joiner, 1u) == 4u, "and the non-joiner is a step alone");
    CF_CHECK(cf_text_prev_cluster(non_joiner, 6u) == 4u, "leaving the accent after it its own");
}

/*
 * The joiner ties pictures to each other and nothing else: between two letters it is a
 * mark with nothing to join, and beside a letter a picture stays the character it is. A
 * rule that joined on the joiner alone would give a Hindi or Persian key the same reach
 * as an emoji sequence and leave a caret sitting inside a word.
 */
static void a_joiner_ties_pictures_and_nothing_else(void) {
    const cf_string text = cf_string_of("a" ZWJ "b");
    const cf_string letter_then_picture = cf_string_of("a" ZWJ THUMB "b");
    const cf_string picture_then_letter = cf_string_of(THUMB ZWJ "b");

    CF_CHECK(cf_text_next_cluster(text, 0u) == 4u, "the joiner attaches to what it follows");
    CF_CHECK(cf_text_next_cluster(text, 4u) == 5u, "and the letter after it is not tied to it");
    CF_CHECK(cf_text_prev_cluster(text, 5u) == 4u, "back off the loose letter");
    CF_CHECK(cf_text_prev_cluster(text, 4u) == 0u, "and off the letter carrying the joiner");
    CF_CHECK(cf_text_next_cluster(letter_then_picture, 0u) == 4u,
             "a joiner after a letter ties no picture to it");
    CF_CHECK(cf_text_next_cluster(letter_then_picture, 4u) == 8u, "so the picture is its own step");
    CF_CHECK(cf_text_prev_cluster(letter_then_picture, 8u) == 4u, "and the same walking back");
    CF_CHECK(cf_text_next_cluster(picture_then_letter, 0u) == 7u,
             "a joiner after a picture ties no letter to it either");
    CF_CHECK(cf_text_prev_cluster(picture_then_letter, 8u) == 7u, "which holds in both directions");
}

static void bytes_that_do_not_decode_step_one_at_a_time(void) {
    const cf_string truncated = cf_string_of("a\xC3"
                                             "b");
    const cf_string stray = cf_string_of("a\x80"
                                         "b");

    CF_CHECK(cf_text_next_cluster(truncated, 0u) == 1u, "a letter stands alone as ever");
    CF_CHECK(cf_text_next_cluster(truncated, 1u) == 2u, "a lead byte missing its tail is a step");
    CF_CHECK(cf_text_next_cluster(truncated, 2u) == 3u, "and the byte after it is the next");
    CF_CHECK(cf_text_next_cluster(stray, 1u) == 2u, "a continuation byte with no lead steps too");
    CF_CHECK(cf_text_prev_cluster(stray, 3u) == 2u, "and the walk back reads them the same way");
}

static void an_offset_inside_a_character_moves_out_of_it(void) {
    const cf_string text = cf_string_of(FAMILY);

    /* Four, seven, eleven and fourteen are the places between the family's own pieces -
     * where a click measured along the ink could reasonably land. */
    CF_CHECK(cf_text_prev_cluster(text, 4u) == 0u, "a caret in front of a joiner falls back");
    CF_CHECK(cf_text_prev_cluster(text, 7u) == 0u, "and so does one in front of a picture");
    CF_CHECK(cf_text_next_cluster(text, 4u) == 18u, "forward, the whole character goes");
    CF_CHECK(cf_text_next_cluster(text, 7u) == 18u, "from either side of the joiner");
}

static void an_empty_text_answers_its_own_ends(void) {
    const cf_string empty = cf_string_of("");

    CF_CHECK(cf_text_next_cluster(empty, 0u) == 0u, "nothing to step over forward");
    CF_CHECK(cf_text_prev_cluster(empty, 0u) == 0u, "and nothing behind");
    CF_CHECK(cf_text_next_cluster(empty, 9u) == 0u, "an offset past the end stays at it");
}

/*
 * The forward walk is the definition of a boundary and the backward walk has to agree
 * with it, since one key moves the caret out of a character and the other into it. A
 * rule the two read differently would let a caret travel somewhere the forward walk
 * says is inside a character, and the delete beside it would take half a picture.
 */
static void the_two_answers_agree_about_every_boundary(void) {
    static const char *const fixtures[] = {
        "hello world",
        E_ACUTE "x" E_ACUTE "y",
        FAMILY THUMB SKIN,
        RI_U RI_S RI_D RI_U RI_S,
        "a\r\n" E_ACUTE ZWJ "b\n" ACUTE "c",
        "a\xC3" ACUTE "\x80"
        "b",
        HEART SELECTOR ZWJ THUMB,
    };

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(fixtures) / sizeof(fixtures[0])); ++i) {
        const cf_string text = cf_string_of(fixtures[i]);
        uint32_t forward[32] = {0};
        const uint32_t count = boundaries(text, forward, 32u);
        bool agreed = count > 0u && forward[count - 1u] == text.length;
        uint32_t at = text.length;

        for (uint32_t seen = count; agreed && seen > 0u; --seen) {
            if (at != forward[seen - 1u]) {
                agreed = false;
                break;
            }
            if (seen > 1u) {
                at = cf_text_prev_cluster(text, at);
            }
        }

        CF_CHECK(agreed, "the walk back reads the same boundaries the walk forward laid");
    }
}

int main(void) {
    printf("text clusters\n");

    CF_RUN(plain_ascii_steps_one_character_at_a_time);
    CF_RUN(an_accent_leaves_with_the_letter_under_it);
    CF_RUN(a_joined_family_is_one_character);
    CF_RUN(a_skin_tone_and_a_selector_stay_on_their_picture);
    CF_RUN(a_mark_from_every_block_the_table_lists);
    CF_RUN(two_indicators_make_a_flag_and_a_third_starts_another);
    CF_RUN(a_pasted_carriage_return_keeps_its_line_feed);
    CF_RUN(a_control_stands_alone_even_beside_a_mark);
    CF_RUN(a_joiner_ties_pictures_and_nothing_else);
    CF_RUN(bytes_that_do_not_decode_step_one_at_a_time);
    CF_RUN(an_offset_inside_a_character_moves_out_of_it);
    CF_RUN(an_empty_text_answers_its_own_ends);
    CF_RUN(the_two_answers_agree_about_every_boundary);

    return CF_REPORT();
}
