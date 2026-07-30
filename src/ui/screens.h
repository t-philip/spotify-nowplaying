#pragma once
#include <Arduino.h>

// Screen state machine and layouts (§12.1, §12.5). Reads AppStateStore +
// Spotify::PlayerStateStore snapshots and decides what to draw; owns the
// text/progress/status widgets for the NOW_PLAYING layout. Does NOT touch
// the album art region (0,0,135,135) — that is netTask's job
// (art/art_decode.cpp) per §8.1, coordinated only through Display::lock().
//
// Called exclusively from uiTask.

namespace Screens {

void begin();

// Forces a full repaint on the next render() call. Call this after drawing
// anything on the TFT outside of Screens' own functions (currently just
// main.cpp's BOOT-button long-press IP/mDNS overlay, §2.4) so the dirty-
// region caches don't skip redrawing content that overlay clobbered.
void invalidateAll();

// Call once per uiTask loop iteration (target ~30fps to service scrolling
// text, §12.2). Internally rate-limits progress-bar/scroll updates and only
// redraws a screen's static parts when the relevant revision counter
// changes (§12.4 dirty-region rule).
void render();

}  // namespace Screens
