#include "widgets.h"
#include "display.h"

namespace Widgets {

// Extremely long track/artist names (up to FIELD_MAX_TRACK=128 bytes, §7.2)
// could in principle request a very wide scroll sprite. §12.2 specifies
// width = text_width + gap with no explicit cap; this clamp is a deliberate
// addition to protect the heap (F12) against that pathological case —
// beyond it the sprite still renders, just with the tail of very long text
// clipped rather than risking an oversized malloc.
static constexpr int kMaxScrollSpriteWidth = 480;

// ---- ScrollingText ----------------------------------------------------------

void ScrollingText::begin(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t font, uint16_t fgColor, uint16_t bgColor) {
  _x = x; _y = y; _w = w; _h = h;
  _font = font; _fg = fgColor; _bg = bgColor;
}

void ScrollingText::setText(const char* text) {
  String next(text);
  if (next == _text) return; // dirty-region: identical text, nothing to do

  freeSprite();
  _text = next;
  _scrollX = 0;
  _lastFrameMs = 0;

  TFT_eSPI& tft = Display::tft();
  tft.setTextFont(_font);
  int textW = tft.textWidth(_text.c_str());
  _needsScroll = textW > _w;

  if (!_needsScroll) {
    // Static fit: transient region sprite, allocate/push/free immediately
    // (§12.4's "at most one region sprite exists at a time" pattern).
    TFT_eSprite spr(&tft);
    spr.setColorDepth(16);
    spr.createSprite(_w, _h);
    spr.fillSprite(_bg);
    spr.setTextColor(_fg, _bg);
    spr.setTextFont(_font);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(_text, 0, 0);
    spr.pushSprite(_x, _y);
    spr.deleteSprite();
    return;
  }

  int desired = textW + SCROLL_GAP_PX;
  _spriteWidth = min(desired, kMaxScrollSpriteWidth);

  _sprite = new TFT_eSprite(&tft);
  _sprite->setColorDepth(16);
  _sprite->createSprite(_spriteWidth, _h);
  _sprite->fillSprite(_bg);
  _sprite->setTextColor(_fg, _bg);
  _sprite->setTextFont(_font);
  _sprite->setTextDatum(TL_DATUM);
  _sprite->drawString(_text, 0, 0);
}

void ScrollingText::update() {
  if (!_needsScroll || !_sprite) return;

  uint32_t now = millis();
  if (now - _lastFrameMs < SCROLL_FRAME_MS) return;
  _lastFrameMs = now;

  uint16_t* buf = (uint16_t*)_sprite->getPointer();
  TFT_eSPI& tft = Display::tft();

  // Row-by-row copy from the circular sprite buffer lets us wrap the
  // window seamlessly using only the documented pushImage(w,h=1,...)
  // primitive — TFT_eSPI has no built-in windowed/wrapped sprite push.
  for (int r = 0; r < _h; r++) {
    const uint16_t* rowBase = buf + (size_t)r * _spriteWidth;
    int firstWidth = min((int)_w, _spriteWidth - _scrollX);
    if (firstWidth > 0) {
      tft.pushImage(_x, _y + r, firstWidth, 1, rowBase + _scrollX);
    }
    if (firstWidth < _w) {
      int remain = _w - firstWidth;
      tft.pushImage(_x + firstWidth, _y + r, remain, 1, rowBase);
    }
  }

  _scrollX = (_scrollX + 1) % _spriteWidth;
}

void ScrollingText::freeSprite() {
  if (_sprite) {
    _sprite->deleteSprite();
    delete _sprite;
    _sprite = nullptr;
  }
  _needsScroll = false;
  _spriteWidth = 0;
  _scrollX = 0;
}

// ---- ProgressBar --------------------------------------------------------------

namespace ProgressBar {

namespace {
int16_t s_x, s_y, s_w, s_h;
int s_lastFillWidth = -1;
}  // namespace

void begin(int16_t x, int16_t y, int16_t w, int16_t h) {
  s_x = x; s_y = y; s_w = w; s_h = h;
  s_lastFillWidth = -1;
}

void update(uint32_t displayedProgressMs, uint32_t durationMs) {
  TFT_eSPI& tft = Display::tft();

  float frac = 0.0f;
  if (durationMs > 0) {
    frac = (float)displayedProgressMs / (float)durationMs;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
  }
  int fillWidth = (int)(s_w * frac);

  if (fillWidth == s_lastFillWidth) return; // §12.3: redraw only the delta region

  if (s_lastFillWidth < 0) {
    tft.fillRoundRect(s_x, s_y, s_w, s_h, s_h / 2, COLOR_PROGRESS_TRACK);
  }

  if (fillWidth > s_lastFillWidth) {
    int from = max(0, s_lastFillWidth);
    tft.fillRect(s_x + from, s_y, fillWidth - from, s_h, COLOR_PROGRESS_FILL);
  } else {
    // Drift-correction snap (§6.4: "snap rather than ease") — repaint fully.
    tft.fillRoundRect(s_x, s_y, s_w, s_h, s_h / 2, COLOR_PROGRESS_TRACK);
    if (fillWidth > 0) tft.fillRect(s_x, s_y, fillWidth, s_h, COLOR_PROGRESS_FILL);
  }

  s_lastFillWidth = fillWidth;
}

void reset() {
  s_lastFillWidth = -1;
}

}  // namespace ProgressBar

// ---- StatusRow ------------------------------------------------------------------

namespace StatusRow {

namespace {
int16_t s_x, s_y, s_w, s_h;
bool s_first = true;
bool s_lastPlaying = false;
bool s_lastShuffle = false;
uint8_t s_lastRepeat = 0;
String s_lastDevice;
}  // namespace

void begin(int16_t x, int16_t y, int16_t w, int16_t h) {
  s_x = x; s_y = y; s_w = w; s_h = h;
  s_first = true;
}

void update(bool isPlaying, bool shuffle, uint8_t repeat, const char* deviceName) {
  String device(deviceName);
  if (!s_first && isPlaying == s_lastPlaying && shuffle == s_lastShuffle &&
      repeat == s_lastRepeat && device == s_lastDevice) {
    return;
  }
  s_first = false;
  s_lastPlaying = isPlaying;
  s_lastShuffle = shuffle;
  s_lastRepeat = repeat;
  s_lastDevice = device;

  TFT_eSPI& tft = Display::tft();
  TFT_eSprite spr(&tft);
  spr.setColorDepth(16);
  spr.createSprite(s_w, s_h);
  spr.fillSprite(COLOR_BG);
  spr.setTextFont(FONT_BODY);

  // ASCII-safe status glyphs — the default TFT_eSPI fonts are Latin-1 only
  // (§7.2), so firmware-drawn chrome uses plain characters rather than the
  // Unicode glyphs shown in the spec's §12.1 mockup.
  String glyphs = isPlaying ? ">" : "||";
  glyphs += "  ";
  glyphs += shuffle ? "S" : "-";
  glyphs += "  ";
  glyphs += (repeat == 0 ? "-" : (repeat == 1 ? "R" : "R1"));

  spr.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(glyphs, 0, (s_h - 8) / 2);

  spr.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(device, s_w, (s_h - 8) / 2);

  spr.pushSprite(s_x, s_y);
  spr.deleteSprite();
}

}  // namespace StatusRow

}  // namespace Widgets
