#pragma once
#include <Arduino.h>

// Cross-cutting system state machine (§9), separate from spotify::PlayerState
// (which only ever describes what Spotify is playing). netTask drives
// transitions; uiTask reads a snapshot to choose which screen to draw
// (§12.5). Same rule as PlayerState (§8.1): netTask writes under the mutex,
// uiTask copies out and releases before any SPI/rendering work — never hold
// the mutex across a slow operation.

enum class AppState {
  BOOT,
  WIFI_INIT,
  WIFI_SETUP,
  TIME_SYNC,
  AUTH_CHECK,
  AUTH_NEEDED,
  RUNNING,          // sub-state lives in PlayerState.is_playing / valid
  RATE_LIMITED,
  ERROR_TRANSIENT,
  SAFE_MODE,        // F16 — crash loop detected, WiFi+web only, no Spotify traffic
};

struct AppStatus {
  AppState state = AppState::BOOT;

  // WIFI_SETUP screen
  char wifiApSsid[32] = {0};
  char wifiApPassword[16] = {0};

  // Shown on multiple screens / long-press IP display (§2.4)
  char localIp[16] = {0};
  char mdnsHost[32] = {0};

  // ERROR_TRANSIENT / AUTH_NEEDED detail line
  char errorMessage[96] = {0};

  // RATE_LIMITED countdown
  uint32_t rateLimitedUntilEpoch = 0;

  // Diagnostics surfaced on /healthz and the diagnostics panel (§13.2)
  bool     wifiConnected = false;
  int8_t   wifiRssiDbm = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t uptimeMs = 0;
  uint32_t requestsLastMinute = 0; // acceptance criterion #17
  uint8_t  lastHttpStatus = 0;

  // Temporary art-pipeline diagnostics (not in the original spec) — added
  // to chase a decode-corruption bug via /healthz without needing serial
  // access, since opening the serial port resets this board.
  int      lastArtFetchResult = -1; // Art::FetchResult, -1 = none yet
  uint32_t lastArtLen = 0;
  int      lastArtDecodeOk = -1;    // -1 = not attempted, 0 = false, 1 = true
  int      lastArtConnectOk = -1;   // -1 = n/a, 0 = false, 1 = true
  int      lastArtSendOk = -1;
  int      lastArtHttpStatus = 0;
  int32_t  lastArtContentLength = -2; // -2 = n/a, -1 = header absent

  uint32_t revision = 0;
};

namespace AppStateStore {

void begin();

// Blocking copy-out under mutex; safe to call from uiTask at any rate.
AppStatus snapshot();

// Mutating helpers used by netTask / main.cpp. Each bumps `revision`.
void setState(AppState s);
void setWifiSetupInfo(const char* ssid, const char* password);
void setNetworkInfo(const char* ip, const char* mdnsHost, bool connected, int8_t rssi);
void setError(const char* message);
void setRateLimitedUntil(uint32_t epoch);
void updateDiagnostics(uint32_t freeHeap, uint32_t uptimeMs, uint32_t reqLastMinute, uint8_t lastHttpStatus);
void updateArtDiagnostics(int fetchResult, uint32_t len, int decodeOk);
void updateArtFetchSteps(int connectOk, int sendOk, int httpStatus, int32_t contentLength);

}  // namespace AppStateStore
