#pragma once
#include <Arduino.h>

// Codepoint-aware truncation for copying Spotify JSON strings into fixed
// PlayerState buffers (§7.2). Track/artist/album names routinely contain
// multi-byte UTF-8 (accents, CJK, emoji); a byte-count truncation can split
// a sequence and corrupt the tail of the string.
//
// Copies at most (dst_cap - 1) bytes from src into dst, never splitting a
// multi-byte sequence, and always null-terminates. Returns the number of
// bytes written (excluding the terminator).
size_t utf8_safe_copy(char* dst, size_t dst_cap, const char* src);

// True if a font glyph should render as-is; a codepoint at or above 0x100
// is outside the default TFT_eSPI Latin-1 fonts and MUST render as the
// placeholder glyph (§7.2) instead of raw bytes.
bool utf8_is_renderable_ascii_latin1(uint32_t codepoint);

// Advances *p to the start of the next codepoint, returning the codepoint
// value. Used by the widget renderer to walk a string glyph-by-glyph.
uint32_t utf8_next_codepoint(const char** p);
