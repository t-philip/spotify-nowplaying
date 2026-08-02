#include "server.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "pages.h"
#include "../config.h"
#include "../app_state.h"
#include "../store/nvs.h"
#include "../spotify/auth.h"
#include "../spotify/ratelimit.h"
#include "../spotify/model.h"
#include "../ui/display.h"

namespace Web {

namespace {

WebServer s_server(WEB_PORT);
QueueHandle_t s_controlQueue = nullptr;

uint8_t s_pinFailures = 0;
uint32_t s_pinLockoutUntilMs = 0;

bool checkPin() {
  uint32_t now = millis();
  if (now < s_pinLockoutUntilMs) {
    s_server.send(429, "text/plain", "Too many attempts, try again later");
    return false;
  }
  if (!s_server.hasHeader(WEB_PIN_HEADER)) {
    s_server.send(401, "text/plain", "PIN required");
    return false;
  }
  String provided = s_server.header(WEB_PIN_HEADER);
  String expected = Nvs::getWebPin();
  if (provided != expected) {
    s_pinFailures++;
    if (s_pinFailures >= WEB_PIN_MAX_FAILURES) {
      s_pinLockoutUntilMs = now + WEB_PIN_LOCKOUT_MS;
      s_pinFailures = 0;
    }
    s_server.send(401, "text/plain", "Invalid PIN");
    return false;
  }
  s_pinFailures = 0;
  return true;
}

bool actionFromString(const String& s, Spotify::Api::ControlAction& out) {
  if (s == "play") { out = Spotify::Api::ControlAction::Play; return true; }
  if (s == "pause") { out = Spotify::Api::ControlAction::Pause; return true; }
  if (s == "next") { out = Spotify::Api::ControlAction::Next; return true; }
  if (s == "previous") { out = Spotify::Api::ControlAction::Previous; return true; }
  if (s == "seek") { out = Spotify::Api::ControlAction::Seek; return true; }
  if (s == "volume") { out = Spotify::Api::ControlAction::Volume; return true; }
  if (s == "shuffle") { out = Spotify::Api::ControlAction::Shuffle; return true; }
  if (s == "repeat") { out = Spotify::Api::ControlAction::Repeat; return true; }
  return false;
}

// §7.3 "Optimistic UI with reconciliation": applied immediately and cheaply
// (mutex-only, no network) so the HTTP response can return well under the
// 400ms target — the actual Spotify call happens later on netTask via the
// queue, and the +700ms/+2000ms reconciliation polls correct any mismatch.
void applyOptimistic(Spotify::Api::ControlAction action, long value) {
  Spotify::PlayerState st = Spotify::PlayerStateStore::snapshot();
  if (!st.valid) return; // nothing sensible to guess without a known baseline

  switch (action) {
    case Spotify::Api::ControlAction::Play:  st.is_playing = true; break;
    case Spotify::Api::ControlAction::Pause: st.is_playing = false; break;
    case Spotify::Api::ControlAction::Next:
    case Spotify::Api::ControlAction::Previous:
      break; // no reliable guess at the new track — let the reconcile polls do it
    case Spotify::Api::ControlAction::Seek:
      st.progress_ms = (uint32_t)value;
      st.progress_epoch_ms = millis();
      break;
    case Spotify::Api::ControlAction::Volume:
      st.volume = (uint8_t)value;
      break;
    case Spotify::Api::ControlAction::Shuffle:
      st.shuffle = (value != 0);
      break;
    case Spotify::Api::ControlAction::Repeat:
      st.repeat = (uint8_t)value;
      break;
  }
  Spotify::PlayerStateStore::update(st);
}

void handleRoot() {
  s_server.send_P(200, "text/html", PAGE_DASHBOARD_HTML);
}

void handleApiNow() {
  // §13.1: MUST be served from cache — never triggers a Spotify request,
  // or every open browser tab would multiply the API load.
  Spotify::PlayerState p = Spotify::PlayerStateStore::snapshot();

  JsonDocument doc;
  doc["valid"] = p.valid;
  doc["is_playing"] = p.is_playing;
  doc["track"] = p.track;
  doc["artist"] = p.artist;
  doc["album"] = p.album;
  doc["device"] = p.device;
  doc["art_url"] = p.art_url;
  doc["duration_ms"] = p.duration_ms;

  uint32_t displayed = p.progress_ms;
  if (p.is_playing) displayed += (millis() - p.progress_epoch_ms);
  if (p.duration_ms > 0 && displayed > p.duration_ms) displayed = p.duration_ms;
  doc["progress_ms"] = displayed;

  doc["shuffle"] = p.shuffle;
  doc["repeat"] = p.repeat;
  doc["volume"] = p.volume;
  doc["explicit"] = p.explicit_flag;

  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

void handleApiControl() {
  if (!checkPin()) return;

  JsonDocument doc;
  if (deserializeJson(doc, s_server.arg("plain")) != DeserializationError::Ok) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  Spotify::Api::ControlAction action;
  if (!actionFromString(doc["action"] | "", action)) {
    s_server.send(400, "application/json", "{\"error\":\"unknown_action\"}");
    return;
  }
  long value = doc["value"] | 0;

  applyOptimistic(action, value);

  Spotify::Api::ControlRequest req{action, value};
  if (xQueueSend(s_controlQueue, &req, 0) != pdTRUE) {
    s_server.send(503, "application/json", "{\"error\":\"queue_full\"}");
    return;
  }
  s_server.send(204, "application/json", "");
}

void handleSetup() {
  if (!checkPin()) return;
  JsonDocument doc;
  doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
  doc["authNeeded"] = Spotify::Auth::isAuthNeeded();
  doc["hasClientId"] = Spotify::Auth::hasClientId();
  doc["orientation"] = Nvs::getOrientation();
  doc["poll_play"] = Nvs::getPollIntervalPlaying();
  doc["poll_pause"] = Nvs::getPollIntervalPaused();
  doc["brightness"] = Nvs::getBrightnessIndex();
  doc["art_on"] = Nvs::getArtEnabled();
  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

void handleAuthStart() {
  if (!checkPin()) return;
  if (!Spotify::Auth::hasClientId()) {
    s_server.send(400, "application/json", "{\"error\":\"no_client_id\"}");
    return;
  }
  String url = Spotify::Auth::startFlow();
  JsonDocument doc;
  doc["url"] = url;
  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

void handleAuthComplete() {
  if (!checkPin()) return;

  JsonDocument doc;
  deserializeJson(doc, s_server.arg("plain")); // malformed body just yields "" below -> BadInput
  String value = doc["value"] | "";

  Spotify::Auth::CompleteResult result = Spotify::Auth::completeFlow(value);
  const char* resultStr;
  switch (result) {
    case Spotify::Auth::CompleteResult::Ok:              resultStr = "ok"; break;
    case Spotify::Auth::CompleteResult::NoFlowInProgress: resultStr = "no_flow"; break;
    case Spotify::Auth::CompleteResult::FlowExpired:      resultStr = "expired"; break;
    case Spotify::Auth::CompleteResult::StateMismatch:    resultStr = "state_mismatch"; break;
    case Spotify::Auth::CompleteResult::BadInput:         resultStr = "bad_input"; break;
    default:                                               resultStr = "exchange_failed"; break;
  }

  JsonDocument out;
  out["result"] = resultStr;
  String outStr;
  serializeJson(out, outStr);
  s_server.send(200, "application/json", outStr);
}

void handleApiSettings() {
  if (!checkPin()) return;

  JsonDocument doc;
  if (deserializeJson(doc, s_server.arg("plain")) != DeserializationError::Ok) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  if (doc["poll_play"].is<int>()) Nvs::setPollIntervalPlaying(doc["poll_play"]);
  if (doc["poll_pause"].is<int>()) Nvs::setPollIntervalPaused(doc["poll_pause"]);
  if (doc["brightness"].is<int>()) {
    uint8_t idx = doc["brightness"];
    Nvs::setBrightnessIndex(idx);
    Display::setBrightnessIndex(idx); // safe here: this handler runs on uiTask, same task that owns the display
  }
  if (doc["art_on"].is<bool>()) Nvs::setArtEnabled(doc["art_on"]);
  // Not a dedicated route in §13.1's table — the spec gives Client ID no
  // route of its own, so /api/settings doubles as the general PIN-gated
  // settings PATCH, accepting this field too.
  if (doc["client_id"].is<const char*>()) {
    Spotify::Auth::setClientId(String((const char*)doc["client_id"]));
  }

  // Orientation (portrait/landscape) — added post-build per user request,
  // not in the original spec. Applying it needs TFT rotation and every
  // widget's geometry reinitialised, which only ever happens once at boot,
  // so save-and-reboot is simpler and safer than trying to re-layout live.
  if (doc["orientation"].is<int>()) {
    uint8_t newOrientation = doc["orientation"];
    if (newOrientation != Nvs::getOrientation()) {
      Nvs::setOrientation(newOrientation);
      s_server.send(200, "application/json", "{\"rebooting\":true}");
      delay(100);
      ESP.restart();
      return;
    }
  }

  s_server.send(204, "application/json", "");
}

void handleApiReset() {
  if (!checkPin()) return;
  s_server.send(204, "application/json", "");
  delay(100); // let the response flush before the reboot tears down the socket
  Nvs::factoryReset();
  ESP.restart();
}

void handleHealthz() {
  AppStatus app = AppStateStore::snapshot();
  JsonDocument doc;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = app.wifiRssiDbm;
  doc["uptime_ms"] = millis();
  doc["requests_last_minute"] = Spotify::RateLimit::requestsInLastMinute();
  doc["token_expires_in_s"] = Spotify::Auth::accessTokenExpiresInSeconds();
  // Deliberate diagnostics (not secrets) — this board resets whenever its
  // serial port is opened, so /healthz is the only practical channel for
  // on-device debugging without destroying the state you're trying to
  // observe. Kept permanently, not scaffolding left over from one bug.
  doc["app_state"] = (int)app.state;
  doc["retry_until_epoch"] = Nvs::getRetryUntilEpoch();
  doc["now_epoch"] = (uint32_t)time(nullptr);
  doc["auth_needed"] = Spotify::Auth::isAuthNeeded();
  doc["last_art_fetch_result"] = app.lastArtFetchResult; // Art::FetchResult: 0=Fetched 1=Unchanged 2=Failed
  doc["last_art_len"] = app.lastArtLen;
  doc["last_art_decode_ok"] = app.lastArtDecodeOk; // -1=not attempted 0=false 1=true
  doc["last_art_connect_ok"] = app.lastArtConnectOk;
  doc["last_art_send_ok"] = app.lastArtSendOk;
  doc["last_art_http_status"] = app.lastArtHttpStatus;
  doc["last_art_content_length"] = app.lastArtContentLength;
  // §13.3: no secret (refresh token, access token, PIN) appears in any
  // response body including /healthz — only this derived seconds-remaining
  // figure is exposed.
  String out;
  serializeJson(doc, out);
  s_server.send(200, "application/json", out);
}

}  // namespace

void begin() {
  s_controlQueue = xQueueCreate(8, sizeof(Spotify::Api::ControlRequest));

  static const char* kHeaderKeys[] = {WEB_PIN_HEADER};
  s_server.collectHeaders(kHeaderKeys, 1);

  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/now", HTTP_GET, handleApiNow);
  s_server.on("/api/control", HTTP_POST, handleApiControl);
  s_server.on("/setup", HTTP_GET, handleSetup);
  s_server.on("/auth/start", HTTP_GET, handleAuthStart);
  s_server.on("/auth/complete", HTTP_POST, handleAuthComplete);
  s_server.on("/api/settings", HTTP_POST, handleApiSettings);
  s_server.on("/api/reset", HTTP_POST, handleApiReset);
  s_server.on("/healthz", HTTP_GET, handleHealthz);
  s_server.begin();

  MDNS.begin(WEB_MDNS_NAME);
  MDNS.addService("http", "tcp", WEB_PORT);
}

void handleClient() {
  s_server.handleClient();
}

bool dequeueControlRequest(Spotify::Api::ControlRequest& outReq) {
  return xQueueReceive(s_controlQueue, &outReq, 0) == pdTRUE;
}

}  // namespace Web
