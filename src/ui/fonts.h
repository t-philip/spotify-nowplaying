#pragma once

// Font selection per §12.1 typography table. Using TFT_eSPI's built-in
// numbered fonts (the "or font 4" alternative to FreeSansBold9pt7b) keeps
// the flash footprint down — no separate GFXFF font file to embed, and
// LOAD_FONT2/LOAD_FONT4/LOAD_FONT6 are already enabled in platformio.ini.

constexpr uint8_t FONT_TITLE  = 4; // track title band
constexpr uint8_t FONT_ARTIST = 2; // artist band (bold not distinguished in numbered fonts)
constexpr uint8_t FONT_BODY   = 2; // album, times, status glyphs
constexpr uint8_t FONT_CLOCK  = 6; // IDLE_NO_PLAYBACK large clock
