#pragma once

// ---------------------------------------------------------------------------
// IdeaSpeak — Spotify Now-Playing Display
// Compile-time constants and feature flags. See design spec v1.0 for rationale.
// Every rate-limit-relevant constant lives here (acceptance criterion #26) —
// no magic numbers in spotify/ or net/.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// ---- Firmware identity -----------------------------------------------------
#define FW_NAME    "IdeaSpeak Spotify Now-Playing"
#define FW_VERSION "1.0.0"

// ---- Input ------------------------------------------------------------------
constexpr gpio_num_t PIN_BOOT_BUTTON = GPIO_NUM_0;
constexpr uint32_t BTN_SHORT_PRESS_MAX_MS = 800;
constexpr uint32_t BTN_LONG_PRESS_MIN_MS  = 3000;
constexpr uint32_t BTN_LONG_PRESS_SHOW_MS = 15000;   // how long IP/mDNS screen stays up
constexpr uint32_t BTN_FACTORY_RESET_MS   = 10000;

// ---- Backlight ---------------------------------------------------------------
constexpr int      BACKLIGHT_PWM_CHANNEL = 0;
constexpr int      BACKLIGHT_PWM_FREQ_HZ = 5000;
constexpr int      BACKLIGHT_PWM_RES_BITS = 8;
constexpr uint8_t  BACKLIGHT_LEVELS[4] = {15, 40, 70, 100}; // percent duty
constexpr uint8_t  BACKLIGHT_DEFAULT_INDEX = 2;             // 70%
constexpr uint32_t BACKLIGHT_IDLE_DIM_AFTER_MS = 30UL * 60UL * 1000UL; // 30 min
constexpr uint8_t  BACKLIGHT_IDLE_DIM_INDEX = 0;             // 15%

// ---- Display geometry ---------------------------------------------------------
constexpr int SCREEN_W = 135;
constexpr int SCREEN_H = 240;
constexpr int ART_SIZE = 135;
constexpr int CONTENT_MARGIN_PX = 2;
constexpr int BAND_TITLE_Y   = 135;
constexpr int BAND_ARTIST_Y  = 159;
constexpr int BAND_ALBUM_Y   = 179;
constexpr int BAND_PROGRESS_Y = 197;
constexpr int BAND_STATUS_Y  = 215;
constexpr int BAND_TITLE_H   = 24;
constexpr int BAND_ARTIST_H  = 20;
constexpr int BAND_ALBUM_H   = 18;
constexpr int BAND_PROGRESS_H = 18;
constexpr int BAND_STATUS_H  = 25;

// ---- Colours (RGB565) -----------------------------------------------------
constexpr uint16_t COLOR_BG           = 0x0000; // TFT_BLACK
constexpr uint16_t COLOR_TEXT_PRIMARY = 0xFFFF; // TFT_WHITE
constexpr uint16_t COLOR_TEXT_SECONDARY = 0xBDF7;
constexpr uint16_t COLOR_TEXT_TERTIARY  = 0x8410;
constexpr uint16_t COLOR_PROGRESS_FILL  = 0x1DCA; // Spotify green #1DB954
constexpr uint16_t COLOR_PROGRESS_TRACK = 0x2965;

// ---- Scrolling text ---------------------------------------------------------
constexpr int      SCROLL_GAP_PX = 40;
constexpr int      SCROLL_FPS = 30;
constexpr uint32_t SCROLL_FRAME_MS = 1000 / SCROLL_FPS;
constexpr uint32_t SCROLL_END_PAUSE_MS = 1500;

// ---- Progress bar ---------------------------------------------------------
constexpr int      PROGRESS_UPDATE_HZ = 4;
constexpr uint32_t PROGRESS_UPDATE_MS = 1000 / PROGRESS_UPDATE_HZ;
constexpr uint32_t PROGRESS_RESYNC_SNAP_THRESHOLD_MS = 2000;

// ---- Task model ---------------------------------------------------------
constexpr int NET_TASK_CORE = 0;
constexpr int UI_TASK_CORE  = 1;
constexpr uint32_t NET_TASK_STACK = 8192;
constexpr uint32_t UI_TASK_STACK  = 4096;
constexpr UBaseType_t NET_TASK_PRIORITY = 2;
constexpr UBaseType_t UI_TASK_PRIORITY  = 1;
constexpr uint32_t MUTEX_TIMEOUT_MS = 200;

// ---- WiFi ---------------------------------------------------------------
constexpr const char* WIFI_SETUP_AP_SSID = "IdeaSpeak-Setup";
constexpr uint32_t WIFI_PORTAL_TIMEOUT_S = 300;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000;
constexpr uint32_t WIFI_DISCONNECT_RECONNECT_S = 30;
constexpr uint32_t WIFI_DISCONNECT_REBOOT_S = 10 * 60;

// ---- Time sync ---------------------------------------------------------
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
constexpr const char* DEFAULT_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr time_t      TIME_VALID_EPOCH_FLOOR = 1577836800; // 2020-01-01
constexpr uint32_t    TIME_SYNC_TIMEOUT_MS = 30000;

// ---- TLS / networking ---------------------------------------------------------
constexpr uint32_t TLS_READ_TIMEOUT_MS = 8000;
constexpr uint32_t API_KEEPALIVE_MAX_REQUESTS = 50;
constexpr uint32_t API_KEEPALIVE_MAX_AGE_MS = 10UL * 60UL * 1000UL;

// ---- Spotify OAuth / PKCE ---------------------------------------------------
constexpr const char* SPOTIFY_AUTH_HOST  = "accounts.spotify.com";
constexpr const char* SPOTIFY_API_HOST   = "api.spotify.com";
constexpr const char* SPOTIFY_ART_HOST   = "i.scdn.co";
constexpr const char* SPOTIFY_REDIRECT_URI = "http://127.0.0.1:8888/callback";
constexpr const char* SPOTIFY_SCOPES = "user-read-currently-playing user-read-playback-state user-modify-playback-state";
constexpr int      PKCE_VERIFIER_LEN = 64;
constexpr uint32_t PKCE_FLOW_EXPIRY_MS = 10UL * 60UL * 1000UL; // 10 min

// ---- Polling cadence (seconds) ---------------------------------------------
constexpr uint16_t POLL_INTERVAL_PLAYING_S_DEFAULT = 5;
constexpr uint16_t POLL_INTERVAL_PAUSED_S_DEFAULT  = 30;
constexpr uint16_t POLL_INTERVAL_NOTHING_S = 60;
constexpr uint16_t POLL_INTERVAL_LONG_IDLE_S = 180;
constexpr uint32_t POLL_LONG_IDLE_AFTER_MS = 15UL * 60UL * 1000UL; // 15 min
constexpr uint16_t POLL_INTERVAL_MIN_S = 3;   // hard floor, unconfigurable
constexpr uint16_t POLL_INTERVAL_MAX_S = 60;

// ---- Rate-limit safety (§6.3) ---------------------------------------------
constexpr uint8_t  RATE_BUCKET_CAPACITY = 8;
constexpr uint32_t RATE_BUCKET_REFILL_MS = 3500;   // 1 token / 3.5s => ~17 req/min
constexpr uint32_t RATE_COOLDOWN_DURATION_MS = 10UL * 60UL * 1000UL; // 10 min after 429
constexpr float    RATE_COOLDOWN_DECAY_FACTOR = 0.75f; // 25% decay per successful poll
constexpr uint8_t  CIRCUIT_BREAKER_5XX_THRESHOLD = 5;
constexpr uint32_t CIRCUIT_BREAKER_SUSPEND_MS = 5UL * 60UL * 1000UL; // 5 min

// ---- Reconciliation polls after control commands (§7.3) --------------------
constexpr uint32_t RECONCILE_POLL_1_MS = 700;
constexpr uint32_t RECONCILE_POLL_2_MS = 2000;

// ---- Refresh token proactive refresh ----------------------------------------
constexpr uint32_t TOKEN_REFRESH_MARGIN_S = 300; // T-300s

// ---- Refresh failure backoff (network/5xx) ---------------------------------
constexpr uint32_t REFRESH_BACKOFF_MS[] = {5000, 15000, 45000, 120000, 300000};
constexpr size_t   REFRESH_BACKOFF_STEPS = sizeof(REFRESH_BACKOFF_MS) / sizeof(REFRESH_BACKOFF_MS[0]);

// ---- JSON / field truncation limits (§7.2) ----------------------------------
constexpr size_t FIELD_MAX_TRACK  = 128;
constexpr size_t FIELD_MAX_ARTIST = 128;
constexpr size_t FIELD_MAX_ALBUM  = 96;
constexpr size_t FIELD_MAX_DEVICE = 32;
constexpr size_t FIELD_MAX_ART_URL = 160;

// ---- Album art pipeline (§11) -----------------------------------------------
// §11.2 originally specified a 48KB cap sized against a no-PSRAM heap
// budget of ~150-190KB. Measured against live Spotify CDN responses
// (2026-07-29): a real 300px-tier JPEG came back at 60,378 bytes,
// comfortably over 48KB, and got rejected — showing the placeholder tile
// on every track instead of real art. On-device /healthz showed ~150KB
// free heap at the time, so raising the cap is safe; 96KB leaves the same
// kind of margin the original 48KB figure was reasoning about, just against
// the sizes actually observed rather than the spec's estimate.
constexpr size_t   ART_MAX_CONTENT_LENGTH = 96UL * 1024UL;
constexpr int      ART_TARGET_WIDTH_PX = 300;
constexpr int      ART_MIN_WIDTH_PX = 150;
constexpr uint8_t  ART_JPG_SCALE = 2; // 300x300 -> 150x150
constexpr int      ART_DRAW_OFFSET_PX = -7; // centre-crop 150 -> 135

// ---- Heap guard (§15, F12) ---------------------------------------------------
constexpr uint32_t HEAP_MIN_FREE_BYTES = 30UL * 1024UL;
constexpr uint32_t HEAP_HEADROOM_TARGET_BYTES = 60UL * 1024UL;
constexpr uint32_t HEAP_LOG_INTERVAL_MS = 60000;

// ---- Web server / PIN security (§13.3) --------------------------------------
constexpr uint16_t WEB_PORT = 80;
constexpr const char* WEB_MDNS_NAME = "ideaspeak";
constexpr const char* WEB_PIN_HEADER = "X-IdeaSpeak-PIN";
constexpr uint8_t  WEB_PIN_DIGITS = 6;
constexpr uint8_t  WEB_PIN_MAX_FAILURES = 5;
constexpr uint32_t WEB_PIN_LOCKOUT_MS = 60000;
constexpr uint32_t WEB_CONTROL_LATENCY_TARGET_MS = 400;

// ---- Crash-loop detection (F16) ----------------------------------------------
constexpr uint8_t  BOOT_LOOP_MAX_COUNT = 5;
constexpr uint32_t BOOT_LOOP_WINDOW_MS = 5UL * 60UL * 1000UL;   // 5 min
constexpr uint32_t BOOT_COUNT_CLEAR_UPTIME_MS = 5UL * 60UL * 1000UL;

// ---- Watchdog ---------------------------------------------------------------
constexpr uint32_t TASK_WATCHDOG_TIMEOUT_S = 30;

// ---- Feature flags -----------------------------------------------------------
#ifndef RELEASE_BUILD
#define RELEASE_BUILD 0   // set to 1 for production flashes: compiles out token/secret logging
#endif

// ---- NVS namespace / keys (§16) ----------------------------------------------
constexpr const char* NVS_NAMESPACE = "ideaspeak";
