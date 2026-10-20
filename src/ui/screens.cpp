#include "screens.h"
#include <time.h>
#include <qrcode.h>
#include "display.h"
#include "widgets.h"
#include "fonts.h"
#include "layout.h"
#include "../config.h"
#include "../app_state.h"
#include "../spotify/model.h"
#include "../store/nvs.h"

namespace Screens {

namespace {

// Reserve room either side of the progress bar for M:SS labels. Computed
// against whichever Layout::progress() rect is active (portrait or
// landscape) — see begin().
constexpr int kTimeLabelW = 28;
Rect s_progressRect;
Rect s_barRect; // progress() inset by the two time labels

Widgets::ScrollingText s_titleText;
Widgets::ScrollingText s_artistText;

AppState s_lastState = static_cast<AppState>(0xFF);
uint32_t s_lastAppRevision = 0xFFFFFFFF;

String s_lastAlbumDrawn = "\x01"; // sentinel that can never match a real album name
int s_lastElapsedSec = -1;
int s_lastIdleMinute = -1;
uint32_t s_lastRateLimitedSecDrawn = 0xFFFFFFFF;
bool s_wasShowingNowPlaying = false;
uint32_t s_lastPlayerRevisionDrawn = 0xFFFFFFFF;

String formatMmSs(uint32_t ms) {
  uint32_t totalSec = ms / 1000;
  uint32_t m = totalSec / 60;
  uint32_t s = totalSec % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)m, (unsigned)s);
  return String(buf);
}

void drawQrCode(const char* text, int x, int y, int maxSizePx) {
  // §12.5: encode ONLY the short http://ideaspeak.local URL (23 chars,
  // version 2) — the ~450-char Spotify authorisation URL needs version 16+
  // and is unscannable at this physical size; that link belongs on the web
  // page (web/pages.h), never here.
  QRCode qrcode;
  uint8_t qrData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrData, 3, ECC_LOW, text);

  int scale = maxSizePx / qrcode.size;
  if (scale < 1) scale = 1;
  int totalPx = qrcode.size * scale;
  int offsetX = x + (maxSizePx - totalPx) / 2;

  TFT_eSPI& tft = Display::tft();
  tft.fillRect(x, y, maxSizePx, totalPx, TFT_WHITE);
  for (uint8_t qy = 0; qy < qrcode.size; qy++) {
    for (uint8_t qx = 0; qx < qrcode.size; qx++) {
      if (qrcode_getModule(&qrcode, qx, qy)) {
        tft.fillRect(offsetX + qx * scale, y + qy * scale, scale, scale, TFT_BLACK);
      }
    }
  }
}

void drawAlbumLine(const char* album) {
  String a(album);
  if (a == s_lastAlbumDrawn) return;
  s_lastAlbumDrawn = a;

  TFT_eSPI& tft = Display::tft();
  Rect r = Layout::album();
  tft.setTextFont(FONT_BODY);

  String toDraw = a;
  bool truncated = false;
  while (tft.textWidth(toDraw.c_str()) > r.w && toDraw.length() > 1) {
    toDraw.remove(toDraw.length() - 1);
    truncated = true;
  }
  if (truncated && toDraw.length() > 3) {
    toDraw.remove(toDraw.length() - 3);
    toDraw += "...";
  }

  TFT_eSprite spr(&tft);
  spr.setColorDepth(16);
  spr.createSprite(r.w, r.h);
  spr.fillSprite(COLOR_BG);
  spr.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  spr.setTextFont(FONT_BODY);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(toDraw, 0, 0);
  spr.pushSprite(r.x, r.y);
  spr.deleteSprite();
}

void drawTimeLabels(uint32_t displayedMs, uint32_t durationMs) {
  int elapsedSec = displayedMs / 1000;
  if (elapsedSec == s_lastElapsedSec) return;
  s_lastElapsedSec = elapsedSec;

  TFT_eSPI& tft = Display::tft();
  tft.setTextFont(FONT_BODY);

  TFT_eSprite left(&tft);
  left.setColorDepth(16);
  left.createSprite(kTimeLabelW, s_progressRect.h);
  left.fillSprite(COLOR_BG);
  left.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  left.setTextFont(FONT_BODY);
  left.setTextDatum(TL_DATUM);
  left.drawString(formatMmSs(displayedMs), 0, 0);
  left.pushSprite(s_progressRect.x, s_progressRect.y);
  left.deleteSprite();

  uint32_t remainMs = (durationMs > displayedMs) ? (durationMs - displayedMs) : 0;
  TFT_eSprite right(&tft);
  right.setColorDepth(16);
  right.createSprite(kTimeLabelW, s_progressRect.h);
  right.fillSprite(COLOR_BG);
  right.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  right.setTextFont(FONT_BODY);
  right.setTextDatum(TR_DATUM);
  right.drawString(formatMmSs(remainMs), kTimeLabelW, 0);
  right.pushSprite(s_progressRect.x + s_progressRect.w - kTimeLabelW, s_progressRect.y);
  right.deleteSprite();
}

void drawMessage(const char* msg) {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(FONT_BODY);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString(msg, Layout::screenW() / 2, Layout::screenH() / 2);
  Display::unlock();
}

void drawBoot() {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(FONT_TITLE);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString(FW_NAME, Layout::screenW() / 2, Layout::screenH() / 2 - 20);
  tft.setTextFont(FONT_BODY);
  tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  tft.drawString(String("v") + FW_VERSION, Layout::screenW() / 2, Layout::screenH() / 2 + 10);
  Display::unlock();
}

void drawWifiSetup(const AppStatus& app) {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(FONT_BODY);

  // Portrait has 240px of height to stack five lines with room to spare;
  // landscape only has 135px, so this content needs tighter, two-column
  // spacing there instead of just reusing the portrait y-coordinates.
  bool landscape = (Layout::current() == Orientation::Landscape);
  int y0 = landscape ? 2 : 20;
  int step = landscape ? 22 : 30;

  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString("WiFi setup needed", 4, y0);
  tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  tft.drawString("Connect to:", 4, y0 + step);
  tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
  tft.drawString(app.wifiApSsid, 4, y0 + step * 2);
  tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  tft.drawString("Password:", 4, y0 + step * 3);
  tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
  tft.drawString(app.wifiApPassword, 4, y0 + step * 4);
  if (!landscape) {
    tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
    tft.drawString("Then open:", 4, y0 + step * 5);
    tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    tft.drawString("192.168.4.1", 4, y0 + step * 6); // WiFiManager's default portal AP address
  }
  Display::unlock();
}

void drawAuthNeeded(const AppStatus& app) {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(FONT_BODY);

  String url = String("http://") + WEB_MDNS_NAME + ".local";
  bool landscape = (Layout::current() == Orientation::Landscape);

  if (!landscape) {
    tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
    tft.drawString("Setup required", 4, 6);
    tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    tft.drawString(url, 4, 26);
    tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
    tft.drawString(app.localIp, 4, 44);

    drawQrCode(url.c_str(), (Layout::screenW() - 100) / 2, 62, 100);

    // §13.3: "generated randomly on first boot and displayed on the TFT
    // during setup" — this was missing from the initial build; without it
    // there is no way to learn the PIN the dashboard prompts for.
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
    tft.drawString("Dashboard PIN:", Layout::screenW() / 2, 168);
    tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
    tft.setTextFont(FONT_TITLE);
    tft.drawString(Nvs::getWebPin(), Layout::screenW() / 2, 186);
  } else {
    // Landscape: only 135px tall — QR code needs the full height, so put
    // the QR on the right and stack all the text tightly on the left.
    drawQrCode(url.c_str(), Layout::screenW() - 104, (Layout::screenH() - 100) / 2, 100);

    tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
    tft.drawString("Setup required", 4, 2);
    tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
    tft.drawString(url, 4, 22);
    tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
    tft.drawString(app.localIp, 4, 40);
    tft.drawString("PIN:", 4, 64);
    tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
    tft.setTextFont(FONT_TITLE);
    tft.drawString(Nvs::getWebPin(), 4, 82);
  }

  Display::unlock();
}

void drawRateLimited(const AppStatus& app) {
  uint32_t now = (uint32_t)time(nullptr);
  uint32_t remaining = (app.rateLimitedUntilEpoch > now) ? (app.rateLimitedUntilEpoch - now) : 0;
  if (remaining == s_lastRateLimitedSecDrawn) return;
  s_lastRateLimitedSecDrawn = remaining;

  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(FONT_BODY);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString("Spotify is throttling", Layout::screenW() / 2, Layout::screenH() / 2 - 20);
  char buf[40];
  snprintf(buf, sizeof(buf), "resuming in %us", (unsigned)remaining);
  tft.drawString(buf, Layout::screenW() / 2, Layout::screenH() / 2 + 10);
  Display::unlock();
}

void drawErrorOrSafeMode(const char* headline, const AppStatus& app) {
  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(FONT_BODY);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString(headline, Layout::screenW() / 2, Layout::screenH() / 2 - 20);
  tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  String url = String("http://") + WEB_MDNS_NAME + ".local";
  tft.drawString(url, Layout::screenW() / 2, Layout::screenH() / 2 + 10);
  Display::unlock();
}

void drawIdleClock() {
  time_t now = time(nullptr);
  struct tm tmNow;
  localtime_r(&now, &tmNow);
  if (tmNow.tm_min == s_lastIdleMinute && s_lastIdleMinute != -1) return;
  s_lastIdleMinute = tmNow.tm_min;

  if (!Display::lock()) return;
  TFT_eSPI& tft = Display::tft();
  tft.fillScreen(COLOR_BG);
  tft.setTextDatum(MC_DATUM);

  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tmNow.tm_hour, tmNow.tm_min);
  tft.setTextFont(FONT_CLOCK);
  tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
  tft.drawString(timeBuf, Layout::screenW() / 2, Layout::screenH() / 2 - 30);

  char dateBuf[16];
  strftime(dateBuf, sizeof(dateBuf), "%a %d %b", &tmNow);
  tft.setTextFont(FONT_BODY);
  tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
  tft.drawString(dateBuf, Layout::screenW() / 2, Layout::screenH() / 2 + 10);

  tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
  tft.drawString("Nothing playing", Layout::screenW() / 2, Layout::screenH() / 2 + 40);
  Display::unlock();
}

void drawNowPlaying(const Spotify::PlayerState& p) {
  bool revChanged = p.revision != s_lastPlayerRevisionDrawn;
  s_lastPlayerRevisionDrawn = p.revision;

  if (!Display::lock()) return;

  if (revChanged) {
    s_titleText.setText(p.track);
    s_artistText.setText(p.artist);
    drawAlbumLine(p.album);
  }
  s_titleText.update();
  s_artistText.update();

  // §6.4 local progress extrapolation between polls.
  uint32_t displayed = p.progress_ms;
  if (p.is_playing) {
    displayed += (millis() - p.progress_epoch_ms);
  }
  if (p.duration_ms > 0 && displayed > p.duration_ms) displayed = p.duration_ms;

  Widgets::ProgressBar::update(displayed, p.duration_ms);
  drawTimeLabels(displayed, p.duration_ms);
  Widgets::StatusRow::update(p.is_playing, p.shuffle, p.repeat, p.device);

  Display::unlock();
}

void renderRunning() {
  Spotify::PlayerState p = Spotify::PlayerStateStore::snapshot();
  bool showNowPlaying = p.valid;

  if (showNowPlaying != s_wasShowingNowPlaying) {
    if (showNowPlaying) {
      s_lastIdleMinute = -1; // force a fresh idle-clock draw if we return to idle later

      // Leaving IDLE_NO_PLAYBACK: drawNowPlaying() only repaints its own
      // small text bands, never the whole screen, so without this the
      // idle clock's "Nothing playing" / date / time text is left behind
      // underneath the new content (leftover text
      // visible around the Now Playing bands after a track starts).
      if (Display::lock()) {
        Display::tft().fillScreen(COLOR_BG);
        Display::unlock();
      }
      s_lastPlayerRevisionDrawn = 0xFFFFFFFF; // force title/artist/album to redraw against the clean screen
    } else {
      // Leaving NOW_PLAYING: free the scrolling sprites now rather than
      // leaving them allocated while idle (§12.2 MUST-free intent extended
      // to "no longer displayed", not just "track changed").
      s_titleText.freeSprite();
      s_artistText.freeSprite();
      Widgets::ProgressBar::reset();
      s_lastAlbumDrawn = "\x01";
      s_lastElapsedSec = -1;
    }
    s_wasShowingNowPlaying = showNowPlaying;
  }

  if (showNowPlaying) {
    drawNowPlaying(p);
  } else {
    drawIdleClock();
  }
}

}  // namespace

void begin() {
  Rect t = Layout::title();
  Rect a = Layout::artist();
  Rect st = Layout::status();
  s_progressRect = Layout::progress();
  s_barRect = {(int16_t)(s_progressRect.x + kTimeLabelW), (int16_t)(s_progressRect.y + 2),
               (int16_t)(s_progressRect.w - 2 * kTimeLabelW), 4};

  s_titleText.begin(t.x, t.y, t.w, t.h, FONT_TITLE, COLOR_TEXT_PRIMARY, COLOR_BG);
  s_artistText.begin(a.x, a.y, a.w, a.h, FONT_ARTIST, COLOR_TEXT_SECONDARY, COLOR_BG);
  Widgets::ProgressBar::begin(s_barRect.x, s_barRect.y, s_barRect.w, s_barRect.h);
  Widgets::StatusRow::begin(st.x, st.y, st.w, st.h);
}

void invalidateAll() {
  // Forces every cache to miss on the next render() call, so whatever
  // screen should be showing gets a full clean repaint. Needed after
  // main.cpp draws a temporary full-screen overlay outside the normal
  // AppState machine (the BOOT-button long-press IP display, §2.4) — none
  // of the per-widget dirty-region caches would otherwise know the
  // background was clobbered.
  s_lastState = static_cast<AppState>(0xFF);
  s_lastAppRevision = 0xFFFFFFFF;
  s_lastAlbumDrawn = "\x01";
  s_lastElapsedSec = -1;
  s_lastIdleMinute = -1;
  s_lastRateLimitedSecDrawn = 0xFFFFFFFF;
  s_lastPlayerRevisionDrawn = 0xFFFFFFFF;
  s_wasShowingNowPlaying = false;
}

void render() {
  AppStatus app = AppStateStore::snapshot();
  bool stateChanged = (app.state != s_lastState);
  bool appRevChanged = (app.revision != s_lastAppRevision);
  s_lastState = app.state;
  s_lastAppRevision = app.revision;

  switch (app.state) {
    case AppState::BOOT:
      if (stateChanged) drawBoot();
      break;
    case AppState::WIFI_INIT:
      if (stateChanged) drawMessage("Connecting to WiFi...");
      break;
    case AppState::WIFI_SETUP:
      if (stateChanged || appRevChanged) drawWifiSetup(app);
      break;
    case AppState::TIME_SYNC:
      if (stateChanged) drawMessage("Syncing time...");
      break;
    case AppState::AUTH_CHECK:
      if (stateChanged) drawMessage("Checking authorisation...");
      break;
    case AppState::AUTH_NEEDED:
      if (stateChanged || appRevChanged) drawAuthNeeded(app);
      break;
    case AppState::RATE_LIMITED:
      drawRateLimited(app); // throttles itself to 1x/second
      break;
    case AppState::ERROR_TRANSIENT:
      if (stateChanged || appRevChanged) drawErrorOrSafeMode(app.errorMessage, app);
      break;
    case AppState::SAFE_MODE:
      if (stateChanged) drawErrorOrSafeMode("Safe mode", app);
      break;
    case AppState::RUNNING:
      renderRunning();
      break;
  }
}

}  // namespace Screens
