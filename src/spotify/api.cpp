#include "api.h"
#include <ArduinoJson.h>
#include <time.h>
#include "auth.h"
#include "ratelimit.h"
#include "../config.h"
#include "../net/tls_client.h"
#include "../net/http.h"
#include "../util/utf8.h"

namespace Spotify {
namespace Api {

namespace {

// §10.2 SHOULD: reuse the connection to api.spotify.com across polls. One
// instance for the whole module — the single TLS connection this firmware
// ever holds to api.spotify.com, recycled per shouldRecycle().
TlsClient s_apiClient(TrustMode::CaBundle);

uint32_t s_reconcileAt1Ms = 0;
uint32_t s_reconcileAt2Ms = 0;

void ensureConnected() {
  if (s_apiClient.connected() && !s_apiClient.shouldRecycle()) return;
  s_apiClient.stop();
  s_apiClient.connect(SPOTIFY_API_HOST);
}

uint8_t repeatStateToEnum(const char* s) {
  if (strcmp(s, "track") == 0) return 2;
  if (strcmp(s, "context") == 0) return 1;
  return 0;
}

// Selection per §7.4: closest to ART_TARGET_WIDTH_PX without going below
// ART_MIN_WIDTH_PX; fall back to the smallest available if none qualify.
// Images expire, so this is recomputed from every fresh poll — never cached
// across reboots.
String pickArtUrl(JsonArray images) {
  int bestWidth = -1;
  String bestUrl;
  int smallestWidth = INT32_MAX;
  String smallestUrl;

  for (JsonObject img : images) {
    int w = img["width"] | 0;
    const char* url = img["url"] | "";
    if (w < smallestWidth) { smallestWidth = w; smallestUrl = url; }
    if (w >= ART_MIN_WIDTH_PX) {
      if (bestWidth < 0 || abs(w - ART_TARGET_WIDTH_PX) < abs(bestWidth - ART_TARGET_WIDTH_PX)) {
        bestWidth = w;
        bestUrl = url;
      }
    }
  }
  return bestWidth >= 0 ? bestUrl : smallestUrl;
}

String joinArtists(JsonArray artists) {
  String joined;
  int n = 0;
  for (JsonObject a : artists) {
    if (n < 3) {
      if (n > 0) joined += ", ";
      joined += (const char*)(a["name"] | "");
    }
    n++;
  }
  if (n > 3) {
    joined += " +";
    joined += String(n - 3);
  }
  return joined;
}

void applyPollJson(JsonDocument& doc) {
  PlayerState st = PlayerStateStore::snapshot();
  st.valid = true;
  st.is_playing = doc["is_playing"] | false;
  st.progress_ms = doc["progress_ms"] | 0;
  st.progress_epoch_ms = millis();
  st.duration_ms = doc["item"]["duration_ms"] | 0;
  st.shuffle = doc["shuffle_state"] | false;
  st.repeat = repeatStateToEnum(doc["repeat_state"] | "off");
  st.volume = doc["device"]["volume_percent"] | 0;
  st.explicit_flag = doc["item"]["explicit"] | false;

  utf8_safe_copy(st.track, sizeof(st.track), doc["item"]["name"] | "");
  utf8_safe_copy(st.album, sizeof(st.album), doc["item"]["album"]["name"] | "");
  utf8_safe_copy(st.device, sizeof(st.device), doc["device"]["name"] | "");
  utf8_safe_copy(st.artist, sizeof(st.artist), joinArtists(doc["item"]["artists"].as<JsonArray>()).c_str());
  utf8_safe_copy(st.art_url, sizeof(st.art_url), pickArtUrl(doc["item"]["album"]["images"].as<JsonArray>()).c_str());

  PlayerStateStore::update(st);
}

void markNothingPlaying() {
  PlayerState st;
  st.valid = false;
  st.is_playing = false;
  PlayerStateStore::update(st);
}

}  // namespace

PollOutcome pollPlayerState() {
  String token = Auth::getAccessToken();
  if (token.length() == 0) return PollOutcome::AuthExpired;

  for (int attempt = 0; attempt < 2; attempt++) {
    ensureConnected();
    if (!s_apiClient.connected()) return PollOutcome::NetworkError;

    String headers = "Authorization: Bearer " + token + "\r\n";
    if (!Http::sendRequest(s_apiClient, "GET", "/v1/me/player", SPOTIFY_API_HOST, headers, nullptr)) {
      s_apiClient.stop();
      return PollOutcome::NetworkError;
    }

    HttpResponse resp = Http::readStatusAndHeaders(s_apiClient);
    if (!resp.headersOk) {
      s_apiClient.stop();
      return PollOutcome::NetworkError;
    }

    uint32_t nowEpoch = (uint32_t)time(nullptr);
    RateLimit::onResponse(resp.statusCode, resp.retryAfterSeconds, nowEpoch);

    if (resp.statusCode == 401 && attempt == 0) {
      s_apiClient.stop();
      Auth::RefreshOutcome outcome = Auth::refreshNow();
      if (outcome != Auth::RefreshOutcome::Ok) {
        return outcome == Auth::RefreshOutcome::InvalidGrant
                   ? PollOutcome::AuthExpired
                   : PollOutcome::NetworkError;
      }
      token = Auth::getAccessToken();
      continue; // §5.5: retry the original request exactly once
    }

    switch (resp.statusCode) {
      case 200: {
        // §7.2 [CRITICAL]: filtered deserialisation reading directly from
        // the stream — never via an intermediate String buffer, which would
        // fragment/exhaust the heap on this no-PSRAM board.
        JsonDocument filter;
        filter["is_playing"] = true;
        filter["progress_ms"] = true;
        filter["currently_playing_type"] = true;
        filter["shuffle_state"] = true;
        filter["repeat_state"] = true;
        filter["device"]["name"] = true;
        filter["device"]["volume_percent"] = true;
        filter["device"]["is_active"] = true;
        filter["item"]["name"] = true;
        filter["item"]["duration_ms"] = true;
        filter["item"]["explicit"] = true;
        filter["item"]["album"]["name"] = true;
        filter["item"]["album"]["images"][0]["url"] = true;
        filter["item"]["album"]["images"][0]["width"] = true;
        filter["item"]["artists"][0]["name"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(
            doc, s_apiClient.stream(), DeserializationOption::Filter(filter));

        if (err != DeserializationError::Ok) {
          // F14: discard, keep previous state. Caller (net task) tracks
          // consecutive occurrences and only surfaces this after 3 in a row.
          return PollOutcome::MalformedJson;
        }
        applyPollJson(doc);
        return PollOutcome::Updated;
      }
      case 204:
        markNothingPlaying();
        return PollOutcome::NothingPlaying;
      case 403:
        return PollOutcome::Forbidden;
      case 429:
        return PollOutcome::RateLimited;
      default:
        if (resp.statusCode >= 500) return PollOutcome::ServerError;
        return PollOutcome::NetworkError;
    }
  }

  return PollOutcome::AuthExpired; // exhausted the single retry on 401
}

ControlResult sendControl(const ControlRequest& req) {
  String token = Auth::getAccessToken();
  if (token.length() == 0) return ControlResult::NetworkError;

  String method;
  String path;

  switch (req.action) {
    case ControlAction::Play:     method = "PUT";  path = "/v1/me/player/play"; break;
    case ControlAction::Pause:    method = "PUT";  path = "/v1/me/player/pause"; break;
    case ControlAction::Next:     method = "POST"; path = "/v1/me/player/next"; break;
    case ControlAction::Previous: method = "POST"; path = "/v1/me/player/previous"; break;
    case ControlAction::Seek:
      method = "PUT";
      path = "/v1/me/player/seek?position_ms=" + String(req.value);
      break;
    case ControlAction::Volume:
      method = "PUT";
      path = "/v1/me/player/volume?volume_percent=" + String(req.value);
      break;
    case ControlAction::Shuffle:
      method = "PUT";
      path = "/v1/me/player/shuffle?state=" + String(req.value ? "true" : "false");
      break;
    case ControlAction::Repeat: {
      const char* state = req.value == 2 ? "track" : (req.value == 1 ? "context" : "off");
      method = "PUT";
      path = "/v1/me/player/repeat?state=" + String(state);
      break;
    }
  }

  ensureConnected();
  if (!s_apiClient.connected()) return ControlResult::NetworkError;

  String headers = "Authorization: Bearer " + token + "\r\n";
  // §7.3: all control requests carry query params, no body — but
  // Content-Length: 0 MUST still be sent (empty, non-null body) or some
  // HTTP stacks hang awaiting one.
  if (!Http::sendRequest(s_apiClient, method.c_str(), path.c_str(), SPOTIFY_API_HOST, headers, "")) {
    s_apiClient.stop();
    return ControlResult::NetworkError;
  }

  HttpResponse resp = Http::readStatusAndHeaders(s_apiClient);
  if (!resp.headersOk) {
    s_apiClient.stop();
    return ControlResult::NetworkError;
  }

  RateLimit::onResponse(resp.statusCode, resp.retryAfterSeconds, (uint32_t)time(nullptr));

  switch (resp.statusCode) {
    case 204: return ControlResult::Ok;
    case 404: return ControlResult::NoActiveDevice;
    case 403: return ControlResult::Forbidden;
    default:  return ControlResult::NetworkError;
  }
}

void scheduleReconcilePolls() {
  uint32_t now = millis();
  s_reconcileAt1Ms = now + RECONCILE_POLL_1_MS;
  s_reconcileAt2Ms = now + RECONCILE_POLL_2_MS;
}

void pumpReconcilePolls() {
  uint32_t now = millis();
  // Each reconciliation poll still draws from the token bucket via the
  // caller's normal gating — pollPlayerState() itself does not bypass it,
  // the burst capacity of RATE_BUCKET_CAPACITY=8 is what accommodates this.
  if (s_reconcileAt1Ms != 0 && now >= s_reconcileAt1Ms) {
    s_reconcileAt1Ms = 0;
    if (RateLimit::tryAcquireToken()) pollPlayerState();
  }
  if (s_reconcileAt2Ms != 0 && now >= s_reconcileAt2Ms) {
    s_reconcileAt2Ms = 0;
    if (RateLimit::tryAcquireToken()) pollPlayerState();
  }
}

void closeApiConnection() {
  s_apiClient.stop();
}

}  // namespace Api
}  // namespace Spotify
