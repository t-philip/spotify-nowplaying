#include "art_decode.h"
#include <TJpg_Decoder.h>
#include "../config.h"
#include "../ui/display.h"
#include "../ui/layout.h"
#include "../util/crc32.h"

namespace Art {

namespace {

// Decoded size at ART_JPG_SCALE=2 from the 300px-tier source Spotify art
// selected in §7.4 (300/2=150). The centre-crop offset below is derived
// from this regardless of the final on-screen art size, so a different
// Layout::art() rect (e.g. landscape's smaller 100x100) still crops
// correctly without needing a different scale factor.
constexpr int kDecodedSize = 150;

bool tjpgOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // setViewport() defaults to vpDatum=true (see decodeAndDraw): (0,0) here
  // already means the viewport's own top-left, and TFT_eSPI translates to
  // the physical artRect.x/y position internally — adding it again here
  // would double-offset the art.
  //
  // MUST always return true here. TJpg_Decoder/tjpgd.c treats a `false`
  // return from this callback as "abort the whole decode" (JDR_INTR), not
  // "skip this one out-of-bounds block" — an earlier version of this code
  // returned false for rows beyond the viewport as a supposed optimisation,
  // which instead aborted decoding partway through almost every image
  // (the centre-crop always pushes some rows outside the viewport by
  // design) and left whatever partial pixel data had already been pushed
  // on screen — the diagonal-stripe corruption seen on hardware. The
  // active viewport already clips out-of-bounds pixels for free in
  // pushImage(); there is nothing to skip manually here.
  Display::tft().pushImage(x, y, w, h, bitmap);
  return true;
}

// Standard HSV(h, s=1, v=1) -> RGB565, used for the placeholder tile hue.
uint16_t hueToRgb565(uint16_t hueDeg) {
  hueDeg = hueDeg % 360;
  uint8_t region = hueDeg / 60;
  uint8_t remainder = (hueDeg % 60) * 255 / 60;

  uint8_t r, g, b;
  switch (region) {
    case 0: r = 255; g = remainder; b = 0; break;
    case 1: r = 255 - remainder; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = remainder; break;
    case 3: r = 0; g = 255 - remainder; b = 255; break;
    case 4: r = remainder; g = 0; b = 255; break;
    default: r = 255; g = 0; b = 255 - remainder; break;
  }
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

}  // namespace

void initDecoder() {
  TJpgDec.setJpgScale(ART_JPG_SCALE);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tjpgOutputCallback);
}

bool decodeAndDraw(const uint8_t* buf, size_t len) {
  // §8.1 runs this from netTask while uiTask independently renders text/
  // progress on the same SPI bus — Display::lock() (see ui/display.h)
  // serialises the two. Held only across the actual draw, not the JPEG
  // download that preceded it.
  if (!Display::lock()) return false;
  TFT_eSPI& tft = Display::tft();
  Rect artRect = Layout::art();
  int offset = -((kDecodedSize - artRect.w) / 2); // §11.2 centre-crop, generalised for any art size

  tft.setViewport(artRect.x, artRect.y, artRect.w, artRect.h); // hardware clip — no CPU cost, §11.2
  JRESULT r = TJpgDec.drawJpg(offset, offset, buf, len);
  tft.resetViewport();
  Display::unlock();
  return r == JDR_OK;
}

void drawPlaceholder(const char* albumName) {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  Rect artRect = Layout::art();
  uint32_t hash = crc32_str((albumName && strlen(albumName) > 0) ? albumName : "?");
  uint16_t hue = hash % 360;
  uint16_t colour = hueToRgb565(hue);

  tft.setViewport(artRect.x, artRect.y, artRect.w, artRect.h);
  tft.fillRect(0, 0, artRect.w, artRect.h, colour);

  char label = (albumName && strlen(albumName) > 0) ? albumName[0] : '?';
  tft.setTextColor(TFT_WHITE, colour);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.drawString(String(label), artRect.w / 2, artRect.h / 2);
  tft.resetViewport();
  Display::unlock();
}

}  // namespace Art
