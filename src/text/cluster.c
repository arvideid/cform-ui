#include "cf/text.h"
#include "utf8.h"

/* Nothing lies behind: what a scalar walk answers at the front of the buffer. */
#define NO_SCALAR 0xFFFFFFFFu

#define CARRIAGE_RETURN 0x0Du
#define LINE_FEED 0x0Au
#define ZERO_WIDTH_JOINER 0x200Du

#define RANGE_COUNT(table) (uint32_t)(sizeof(table) / sizeof((table)[0]))

typedef struct codepoint_range {
    uint32_t first;
    uint32_t last;
} codepoint_range;

/*
 * The scalars that are written onto the one before them rather than on their
 * own: what Unicode calls Extend - the combining marks, the variation selectors
 * that pick a picture's rendering, the skin-tone modifiers, the keycap sign and
 * the tag characters an emoji sequence still carries - together with the spacing
 * marks of the scripts listed here, since a vowel sign drawn beside its
 * consonant belongs to the same character to the person reading it.
 *
 * The table is the well-known Mark blocks, not the whole property table: Hangul
 * jamo sequences, the prepending marks of a few right-to-left scripts and the
 * rarer combining blocks are absent, so those still come apart as more than one
 * cluster. docs/backlog.md keeps that gap.
 *
 * The rows are in ascending order and do not overlap, which is what the search
 * below assumes: a row written out of order is a block that quietly stops
 * attaching. `text_cluster` takes one mark from every row to keep that honest.
 */
static const codepoint_range attaching[] = {
    {0x0300u, 0x036Fu}, /* Latin, Greek and Cyrillic diacritics */
    {0x0483u, 0x0489u}, /* Cyrillic titla and the Old Slavonic marks */
    {0x0591u, 0x05BDu}, /* Hebrew pointing */
    {0x05BFu, 0x05BFu},   {0x05C1u, 0x05C2u}, {0x05C4u, 0x05C5u}, {0x05C7u, 0x05C7u},
    {0x0610u, 0x061Au}, /* Arabic Quranic annotation */
    {0x064Bu, 0x065Fu}, /* Arabic harakat */
    {0x0670u, 0x0670u}, /* Arabic superscript alef */
    {0x06D6u, 0x06DCu},   {0x06DFu, 0x06E4u}, {0x06E7u, 0x06E8u}, {0x06EAu, 0x06EDu},
    {0x0730u, 0x074Au}, /* Syriac */
    {0x07A6u, 0x07B0u}, /* Thaana */
    {0x0900u, 0x0903u}, /* Devanagari: anusvara, visarga and candrabindu */
    {0x093Au, 0x094Fu}, /* Devanagari: nukta, the vowel signs and virama */
    {0x0951u, 0x0957u},   {0x0962u, 0x0963u}, {0x0981u, 0x0983u}, /* Bengali */
    {0x09BCu, 0x09BCu},   {0x09BEu, 0x09C4u}, {0x09C7u, 0x09C8u}, {0x09CBu, 0x09CDu},
    {0x09D7u, 0x09D7u},   {0x09E2u, 0x09E3u}, {0x0E31u, 0x0E31u}, /* Thai: the vowel signs that sit
                                                                     around a consonant */
    {0x0E34u, 0x0E3Au},   {0x0E47u, 0x0E4Eu}, {0x0EB1u, 0x0EB1u}, /* Lao */
    {0x0EB4u, 0x0EBCu},   {0x0EC8u, 0x0ECDu}, {0x1AB0u, 0x1AFFu}, /* Latin extensions beyond the
                                                                     basic block */
    {0x1DC0u, 0x1DFFu},   /* combining diacritical marks supplement */
    {0x20D0u, 0x20F0u},   /* marks for symbols, including the keycap sign */
    {0x2CEFu, 0x2CF1u},   /* Coptic and the Greek musical notations */
    {0x2DE0u, 0x2DFFu},   /* Cyrillic extended-A combining marks */
    {0x302Au, 0x302Fu},   /* CJK tone marks */
    {0x3099u, 0x309Au},   /* kana voicing marks */
    {0xFE00u, 0xFE0Fu},   /* variation selectors */
    {0xFE20u, 0xFE2Fu},   /* combining half marks */
    {0x1F3FBu, 0x1F3FFu}, /* the skin-tone modifiers */
    {0xE0020u, 0xE007Fu}, /* the tag characters a flag subdivision carries */
};

/* Two of these beside each other are one flag, and no more than two. */
static const codepoint_range regional_indicators[] = {
    {0x1F1E6u, 0x1F1FFu},
};

/*
 * A stand-in for Extended_Pictographic: the symbol blocks and the emoji planes,
 * which is where a joiner is ever meant to tie two pictures into one. The list
 * is wider than Unicode's property and says nothing about the picture characters
 * outside it; both show up only where a joiner already sits between two scalars,
 * where joining is what the person who inserted it asked for.
 */
static const codepoint_range pictures[] = {
    {0x2190u, 0x2BFFu},
    {0x1F000u, 0x1FAFFu},
};

static bool in_table(uint32_t codepoint, const codepoint_range *table, uint32_t count) {
    uint32_t low = 0u;
    uint32_t high = count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;

        if (codepoint < table[middle].first) {
            high = middle;
        } else if (codepoint > table[middle].last) {
            low = middle + 1u;
        } else {
            return true;
        }
    }

    return false;
}

static bool is_extend(uint32_t codepoint) {
    return in_table(codepoint, attaching, RANGE_COUNT(attaching));
}

static bool is_picture(uint32_t codepoint) {
    return in_table(codepoint, pictures, RANGE_COUNT(pictures));
}

static bool is_indicator(uint32_t codepoint) {
    return in_table(codepoint, regional_indicators, RANGE_COUNT(regional_indicators));
}

/*
 * UAX #29's Control: the characters that end a cluster on both sides, because a
 * cluster is what a reader sees and a control is an instruction to the walk. The
 * joiner at U+200D is the one exception the rules make, since GB11 is about the
 * pair of pictures it stands between; the non-joiner beside it stays a control,
 * which is what Unicode gives it.
 */
static bool is_control(uint32_t codepoint) {
    if (codepoint < 0x20u || (codepoint >= 0x7Fu && codepoint <= 0x9Fu)) {
        return true;
    }
    if (codepoint == 0x00ADu || codepoint == 0x2028u || codepoint == 0x2029u ||
        codepoint == 0xFEFFu) {
        return true;
    }
    if (codepoint >= 0x200Bu && codepoint <= 0x200Fu && codepoint != ZERO_WIDTH_JOINER) {
        return true;
    }
    return codepoint >= 0x2060u && codepoint <= 0x206Fu;
}

/* The scalar ending at `at`, with *next one past its start. A byte that is not the
 * end of the sequence it claims stands alone, so a malformed run still steps one
 * byte at a time instead of trapping a caret. */
static uint32_t scalar_before(cf_string text, uint32_t at, uint32_t *start) {
    if (at == 0u || at > text.length) {
        return NO_SCALAR;
    }

    uint32_t lead = at - 1u;
    while (lead > 0u && ((uint8_t)text.chars[lead] & 0xC0u) == 0x80u) {
        lead--;
    }

    uint32_t after = 0u;
    const uint32_t codepoint = utf8_decode_at(text, lead, &after);

    if (after != at) {
        *start = at - 1u;
        return REPLACEMENT;
    }

    *start = lead;
    return codepoint;
}

/* What opened the cluster standing at `before_here`: the first scalar of the run,
 * found by walking back over the marks that hang off it. */
static uint32_t cluster_opener(cf_string text, uint32_t before_here) {
    uint32_t at = before_here;

    for (;;) {
        uint32_t prior = 0u;
        const uint32_t codepoint = scalar_before(text, at, &prior);

        if (codepoint == NO_SCALAR) {
            return NO_SCALAR;
        }
        if (codepoint != ZERO_WIDTH_JOINER && !is_extend(codepoint)) {
            return codepoint;
        }
        at = prior;
    }
}

/*
 * Whether the scalar starting at `at` continues the cluster before it - the same
 * agreement the forward walk keeps, read backwards from the position it lands on.
 * GB3 (a carriage return and the line feed after it), GB4 and GB5 (a control
 * stands alone), GB9 (a mark never opens a cluster), GB11 (a joiner ties a
 * picture to the picture after it) and GB12 and GB13 (indicators pair up) each
 * have their backward form here; Hangul and the prepending marks have no form at
 * all, since the table does not carry them.
 */
static bool continues_cluster(cf_string text, uint32_t at, uint32_t codepoint) {
    if (at == 0u) {
        return false;
    }

    uint32_t prior = 0u;
    const uint32_t before = scalar_before(text, at, &prior);

    if (before == NO_SCALAR) {
        return false;
    }
    if (before == CARRIAGE_RETURN && codepoint == LINE_FEED) {
        return true;
    }
    if (is_control(before) || is_control(codepoint)) {
        return false;
    }
    if (codepoint == ZERO_WIDTH_JOINER || is_extend(codepoint)) {
        return true;
    }
    if (is_indicator(codepoint) && is_indicator(before)) {
        /* The pairs count from the front of the run, so this indicator joins when the
         * ones ahead of it come to an odd number. */
        uint32_t run = 0u;
        uint32_t scan = at;

        for (;;) {
            uint32_t step = 0u;
            const uint32_t seen = scalar_before(text, scan, &step);
            if (seen == NO_SCALAR || !is_indicator(seen)) {
                break;
            }
            run++;
            scan = step;
        }

        return run % 2u == 1u;
    }
    if (before == ZERO_WIDTH_JOINER && is_picture(codepoint)) {
        return is_picture(cluster_opener(text, prior));
    }

    return false;
}

uint32_t cf_text_prev_cluster(cf_string text, uint32_t offset) {
    uint32_t at = offset < text.length ? offset : text.length;

    while (at > 0u) {
        uint32_t start = 0u;
        const uint32_t codepoint = scalar_before(text, at, &start);

        if (codepoint == NO_SCALAR) {
            return 0u;
        }
        if (!continues_cluster(text, start, codepoint)) {
            return start;
        }
        at = start;
    }

    return 0u;
}

/* The first boundary at or after `offset`: a caret a click placed inside a cluster
 * is brought out to where the cluster begins, so the same key moves the whole of it
 * whichever side of it the pointer chose. */
static uint32_t cluster_start(cf_string text, uint32_t offset) {
    if (offset == 0u || offset >= text.length) {
        return offset;
    }

    uint32_t after = 0u;
    const uint32_t codepoint = utf8_decode_at(text, offset, &after);

    return continues_cluster(text, offset, codepoint) ? cf_text_prev_cluster(text, offset) : offset;
}

/*
 * What the walk forward has to remember about the cluster it stands in: the scalar
 * that opened it, whether the last scalar was a joiner waiting for a picture, and how
 * many indicator letters it has taken. The rules below are UAX #29's in the order the
 * specification tests them, and they are the same agreement `continues_cluster` reads
 * backwards - one of them changing without the other would let a caret come to rest
 * inside a character.
 */
typedef struct cluster_walk {
    uint32_t base;
    bool after_joiner;
    uint32_t indicators;
} cluster_walk;

static bool walk_takes(const cluster_walk *walk, uint32_t codepoint) {
    if (walk->base == CARRIAGE_RETURN && codepoint == LINE_FEED) {
        return true; /* GB3: a pasted windows break is one thing */
    }
    if (is_control(walk->base) || is_control(codepoint)) {
        return false; /* GB4, GB5: a control ends a cluster on both sides */
    }
    if (codepoint == ZERO_WIDTH_JOINER || is_extend(codepoint)) {
        return true; /* GB9: a mark never opens a cluster */
    }
    if (walk->after_joiner && is_picture(walk->base) && is_picture(codepoint)) {
        return true; /* GB11: the joiner ties a picture to the picture after it */
    }

    /* GB12 and GB13: indicators pair up, so a third one opens the next flag. */
    return is_indicator(codepoint) && is_indicator(walk->base) && walk->indicators == 1u;
}

static void walk_take(cluster_walk *walk, uint32_t codepoint) {
    walk->after_joiner = codepoint == ZERO_WIDTH_JOINER;
    walk->indicators = is_indicator(codepoint) ? walk->indicators + 1u : 0u;
}

uint32_t cf_text_next_cluster(cf_string text, uint32_t offset) {
    if (offset >= text.length) {
        return text.length;
    }

    uint32_t after = 0u;
    cluster_walk walk = {0};

    walk.base = utf8_decode_at(text, cluster_start(text, offset), &after);
    walk.indicators = is_indicator(walk.base) ? 1u : 0u;

    while (after < text.length) {
        uint32_t next = 0u;
        const uint32_t codepoint = utf8_decode_at(text, after, &next);

        if (!walk_takes(&walk, codepoint)) {
            break;
        }

        walk_take(&walk, codepoint);
        after = next;
    }

    return after;
}
