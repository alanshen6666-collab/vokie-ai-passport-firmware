#pragma once

// Pure layout helpers for the optional host_text reply overlay.
//
// The overlay body is 208 px wide and about 246 px tall. The reply font is
// 16 px, so a line holds roughly 13 full-width (CJK) glyphs or 26 narrow
// glyphs, and about 12 lines fit the body. Pages are measured in display
// units where full-width glyphs count 2 and everything else counts 1, which
// keeps this logic independent of the renderer and host-testable.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UI_REPLY_UNITS_PER_PAGE 288

// Decode the next UTF-8 code point in text[*pos..len). Invalid bytes decode to
// U+FFFD and advance one byte. *pos always advances by at least one byte.
static inline uint32_t ui_reply_next_codepoint(const char *text, size_t len,
                                               size_t *pos)
{
    const unsigned char *bytes = (const unsigned char *)text;
    size_t index = *pos;
    unsigned char first = bytes[index];
    uint32_t codepoint;
    size_t continuation;

    if (first < 0x80)
    {
        *pos = index + 1;
        return first;
    }
    if ((first & 0xE0) == 0xC0)
    {
        codepoint = first & 0x1F;
        continuation = 1;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        codepoint = first & 0x0F;
        continuation = 2;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        codepoint = first & 0x07;
        continuation = 3;
    }
    else
    {
        *pos = index + 1;
        return 0xFFFD;
    }
    if (index + continuation >= len)
    {
        *pos = len;
        return 0xFFFD;
    }
    for (size_t offset = 1; offset <= continuation; ++offset)
    {
        unsigned char next = bytes[index + offset];
        if ((next & 0xC0) != 0x80)
        {
            *pos = index + 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
    }
    *pos = index + continuation + 1;
    return codepoint;
}

// Full-width code points occupy two display units; everything else one.
static inline int ui_reply_codepoint_units(uint32_t codepoint)
{
    if (codepoint >= 0x1100 &&
        (codepoint <= 0x115F ||                              // Hangul Jamo
         (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||     // CJK .. Yi
         (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||     // Hangul syllables
         (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||     // CJK compatibility
         (codepoint >= 0xFE30 && codepoint <= 0xFE4F) ||     // CJK forms
         (codepoint >= 0xFF00 && codepoint <= 0xFF60) ||     // Full-width forms
         (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) ||
         codepoint >= 0x20000))
        return 2;
    return 1;
}

// Byte offset where the page starting at `from` ends (exclusive). A newline
// ends the page unless it is the first character of the page.
static inline size_t ui_reply_page_end(const char *text, size_t from)
{
    size_t len = strlen(text);
    size_t pos = from;
    int units = 0;
    while (pos < len)
    {
        uint32_t codepoint = ui_reply_next_codepoint(text, len, &pos);
        if (codepoint == '\n' && units > 0) return pos;
        units += ui_reply_codepoint_units(codepoint);
        if (units >= UI_REPLY_UNITS_PER_PAGE) return pos;
    }
    return len;
}

static inline int ui_reply_page_count(const char *text)
{
    size_t len = strlen(text);
    if (len == 0) return 1;
    int pages = 0;
    size_t pos = 0;
    while (pos < len)
    {
        pos = ui_reply_page_end(text, pos);
        ++pages;
    }
    return pages;
}

// Copy one page into out (NUL-terminated). Returns the copied byte length.
// The caller sizes out to hold the largest possible page plus the terminator.
static inline size_t ui_reply_page_copy(const char *text, int page, char *out,
                                        size_t cap)
{
    if (cap == 0) return 0;
    size_t len = strlen(text);
    size_t pos = 0;
    for (int current = 0; current < page && pos < len; ++current)
        pos = ui_reply_page_end(text, pos);
    if (pos >= len)
    {
        if (cap > 0) out[0] = '\0';
        return 0;
    }
    size_t end = ui_reply_page_end(text, pos);
    size_t amount = end - pos;
    if (amount > cap - 1) amount = cap - 1;
    memcpy(out, text + pos, amount);
    out[amount] = '\0';
    return amount;
}
