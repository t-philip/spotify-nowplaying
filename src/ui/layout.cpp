#include "layout.h"
#include "../config.h"

namespace Layout {

namespace {
Orientation s_orientation = Orientation::Portrait;
int16_t s_screenW = SCREEN_W;
int16_t s_screenH = SCREEN_H;

Rect s_art;
Rect s_title;
Rect s_artist;
Rect s_album;
Rect s_progress;
Rect s_status;
}  // namespace

void begin(Orientation o) {
  s_orientation = o;

  if (o == Orientation::Portrait) {
    s_screenW = SCREEN_W;   // 135
    s_screenH = SCREEN_H;   // 240
    s_art = {0, 0, ART_SIZE, ART_SIZE};

    s_title    = {CONTENT_MARGIN_PX, BAND_TITLE_Y + 4, SCREEN_W - 2 * CONTENT_MARGIN_PX, BAND_TITLE_H - 4};
    s_artist   = {CONTENT_MARGIN_PX, BAND_ARTIST_Y, SCREEN_W - 2 * CONTENT_MARGIN_PX, BAND_ARTIST_H};
    s_album    = {CONTENT_MARGIN_PX, BAND_ALBUM_Y, SCREEN_W - 2 * CONTENT_MARGIN_PX, BAND_ALBUM_H};
    s_progress = {CONTENT_MARGIN_PX, BAND_PROGRESS_Y, SCREEN_W - 2 * CONTENT_MARGIN_PX, BAND_PROGRESS_H};
    s_status   = {CONTENT_MARGIN_PX, BAND_STATUS_Y, SCREEN_W - 2 * CONTENT_MARGIN_PX, BAND_STATUS_H};
  } else {
    s_screenW = SCREEN_H;   // 240 — panel rotated 90 degrees
    s_screenH = SCREEN_W;   // 135

    // First landscape pass used the same 135x135 art as portrait, which
    // left only ~99px for the text column and read as too cramped
    // (feedback from initial hardware test). Shrinking art to 100x100
    // frees the column up to ~134px — close to portrait's own 131px
    // content width, so scrolling feels comparably roomy in both
    // orientations. See art_decode.cpp for the matching crop-math change
    // (it no longer assumes a fixed 135x135 target).
    constexpr int16_t kLandscapeArtSize = 100;
    s_art = {0, (int16_t)((s_screenH - kLandscapeArtSize) / 2), kLandscapeArtSize, kLandscapeArtSize};

    const int16_t colX = kLandscapeArtSize + CONTENT_MARGIN_PX * 2;
    const int16_t colW = s_screenW - colX - CONTENT_MARGIN_PX;

    int16_t y = CONTENT_MARGIN_PX;
    s_title    = {colX, y, colW, BAND_TITLE_H - 4}; y += (BAND_TITLE_H - 4) + 2;
    s_artist   = {colX, y, colW, BAND_ARTIST_H};     y += BAND_ARTIST_H + 2;
    s_album    = {colX, y, colW, BAND_ALBUM_H};      y += BAND_ALBUM_H + 2;
    s_progress = {colX, y, colW, BAND_PROGRESS_H};   y += BAND_PROGRESS_H + 2;
    s_status   = {colX, y, colW, BAND_STATUS_H};
  }
}

Orientation current() { return s_orientation; }
int16_t screenW() { return s_screenW; }
int16_t screenH() { return s_screenH; }

Rect art() { return s_art; }
Rect title() { return s_title; }
Rect artist() { return s_artist; }
Rect album() { return s_album; }
Rect progress() { return s_progress; }
Rect status() { return s_status; }

}  // namespace Layout
