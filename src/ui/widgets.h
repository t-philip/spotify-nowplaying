#pragma once
#include <TFT_eSPI.h>
#include "../config.h"
#include "fonts.h"

// Scrolling text band, progress bar, and status row (§12.2-§12.3). All
// three redraw only their own region — never a full-screen clear — per
// §12.4's dirty-region rule. Each owns at most one TFT_eSprite at a time,
// allocated/pushed/freed per update; a leaked sprite accumulates 2-8KB per
// track change (§12.2), so every code path that returns early MUST still
// free() before returning.

namespace Widgets {

// One scrolling text band (title or artist). Renders once into a sprite
// sized text_width+SCROLL_GAP_PX, then pushes a moving window at 30fps with
// continuous wrap (preferred over bounce — "less distracting", §12.2).
class ScrollingText {
 public:
  void begin(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t font, uint16_t fgColor, uint16_t bgColor);

  // Rebuilds the sprite only if the text actually changed (dirty-region key
  // is the caller's PlayerState.revision comparison, done by screens.cpp).
  void setText(const char* text);

  // Call at up to 30fps; internally rate-limits to SCROLL_FRAME_MS.
  void update();

  void freeSprite();

 private:
  int16_t _x = 0, _y = 0, _w = 0, _h = 0;
  uint8_t _font = FONT_TITLE;
  uint16_t _fg = COLOR_TEXT_PRIMARY, _bg = COLOR_BG;
  String _text;
  TFT_eSprite* _sprite = nullptr;
  int _spriteWidth = 0;
  bool _needsScroll = false;
  int _scrollX = 0;
  uint32_t _lastFrameMs = 0;
};

// Track/artist/album static or scrolling display + progress bar + status
// row are driven from screens.cpp; this file provides the drawing
// primitives it composes.
namespace ProgressBar {
void begin(int16_t x, int16_t y, int16_t w, int16_t h);

// Redraws only the delta region between the last drawn fill width and the
// new one (§12.3) — a full redraw at 4Hz on 40MHz SPI produces visible
// flicker. Pass displayedProgressMs already extrapolated per §6.4.
void update(uint32_t displayedProgressMs, uint32_t durationMs);

void reset(); // called on track change so the next update() draws fresh
}  // namespace ProgressBar

namespace StatusRow {
void begin(int16_t x, int16_t y, int16_t w, int16_t h);
void update(bool isPlaying, bool shuffle, uint8_t repeat, const char* deviceName);
}  // namespace StatusRow

}  // namespace Widgets
