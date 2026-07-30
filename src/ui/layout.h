#pragma once
#include <Arduino.h>

// Runtime screen geometry for the two supported layouts. Added after initial
// build/testing — the spec's §12.1 layout only defined portrait (135x240:
// art on top, text stacked below). Landscape (240x135) keeps the album art
// as the same 135x135 square — it already exactly fills the panel's short
// dimension in either orientation — and moves the text bands from "below
// the art" to "beside it".
//
// Switching orientation requires a reboot (see store/nvs.h's `orientation`
// key and web/server.cpp's handling of it in /api/settings) rather than a
// live re-layout: TFT_eSPI's rotation and every widget's fixed geometry are
// only ever set up once, at boot, and re-plumbing that live isn't worth the
// risk for a setting that changes rarely.

struct Rect {
  int16_t x, y, w, h;
};

enum class Orientation : uint8_t { Portrait = 0, Landscape = 1 };

namespace Layout {

// Call once at boot, after Nvs::begin(), before Display::begin()/Screens::begin().
void begin(Orientation o);

Orientation current();
int16_t screenW();
int16_t screenH();

Rect art();      // always 135x135; positioned top-left in both orientations
Rect title();
Rect artist();
Rect album();
Rect progress();  // widget further insets this for the elapsed/remaining time labels
Rect status();

}  // namespace Layout
