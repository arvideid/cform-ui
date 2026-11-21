#include "utf8.h"

uint32_t utf8_sequence_length(uint8_t lead) {
    if ((lead & 0x80u) == 0x00u) {
        return 1u;
    }
    if ((lead & 0xE0u) == 0xC0u) {
        return 2u;
    }
    if ((lead & 0xF0u) == 0xE0u) {
        return 3u;
    }
    if ((lead & 0xF8u) == 0xF0u) {
        return 4u;
    }
    return 1u;
}

uint32_t utf8_decode_at(cf_string text, uint32_t offset, uint32_t *next) {
    const uint8_t lead = (uint8_t)text.chars[offset];
    const uint32_t end = offset + utf8_sequence_length(lead);

    if (end > text.length) {
        *next = offset + 1u;
        return REPLACEMENT;
    }

    uint32_t codepoint = 0u;
    switch (end - offset) {
    case 1u:
        codepoint = lead;
        break;
    case 2u:
        codepoint = lead & 0x1Fu;
        break;
    case 3u:
        codepoint = lead & 0x0Fu;
        break;
    case 4u:
        codepoint = lead & 0x07u;
        break;
    }

    for (uint32_t at = offset + 1u; at < end; ++at) {
        const uint8_t continuation = (uint8_t)text.chars[at];
        if ((continuation & 0xC0u) != 0x80u) {
            *next = offset + 1u;
            return REPLACEMENT;
        }
        codepoint = codepoint << 6u | (continuation & 0x3Fu);
    }

    *next = end;
    return codepoint;
}
