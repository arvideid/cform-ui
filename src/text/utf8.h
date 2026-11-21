#ifndef CFORM_TEXT_UTF8_H
#define CFORM_TEXT_UTF8_H

#include "cf/base.h"

/* U+FFFD stands in for a sequence that does not decode; it advances the pen
 * like any glyph, so malformed text measures instead of derailing the walk. */
#define REPLACEMENT 0xFFFDu

/* The byte length of the UTF-8 sequence `lead` begins, 1 for a byte that
 * cannot begin one: a walk steps past malformed bytes one at a time. */
uint32_t utf8_sequence_length(uint8_t lead);

/* The codepoint encoded at `offset`, REPLACEMENT where the bytes do not
 * decode; *next lands one past what was consumed either way. */
uint32_t utf8_decode_at(cf_string text, uint32_t offset, uint32_t *next);

#endif
