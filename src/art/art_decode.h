#pragma once
#include <Arduino.h>

// TJpg_Decoder integration (§11.2) and the deterministic placeholder tile
// (§11.3). Draws directly into the 135x135 ALBUM ART region at the top of
// the screen (§12.1) via the shared TFT_eSPI instance owned by ui/display.h.

namespace Art {

// One-time registration of TJpg_Decoder's pixel-push callback. Call once
// from setup(), after ui::Display::begin().
void initDecoder();

// Decodes a JPEG buffer (owned by the caller, freed by the caller in every
// path — see art_fetch.h) and draws it into the ART region using hardware
// viewport clipping. setJpgScale(2) downscales 300x300 -> 150x150, then the
// draw offset centre-crops 150 -> 135 (§11.2). Returns false on decode
// failure — caller should then call drawPlaceholder().
bool decodeAndDraw(const uint8_t* buf, size_t len);

// Deterministic solid tile: hue = crc32(albumName) % 360, first character of
// albumName centred in white (§11.3). Same album always yields the same
// tile, which reads as intentional design rather than an error.
void drawPlaceholder(const char* albumName);

}  // namespace Art
