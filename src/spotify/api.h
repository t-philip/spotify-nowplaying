#pragma once
#include <Arduino.h>
#include "model.h"

// Spotify Web API surface (§7): the filtered-streaming player poll and the
// eight control endpoints. Every call here MUST be gated by
// spotify::RateLimit::isBlocked()/tryAcquireToken() from the caller
// (net task loop) — this module does not itself decide whether to send,
// only how to send and how to interpret the response.

namespace Spotify {
namespace Api {

enum class PollOutcome {
  Updated,        // 200 — PlayerStateStore updated
  NothingPlaying, // 204 — not an error; PlayerStateStore.valid cleared
  AuthExpired,    // 401 — caller should have already retried once via Auth::refreshNow()
  Forbidden,      // 403 — scope/account restriction
  RateLimited,    // 429 — caller feeds this into RateLimit::onResponse
  ServerError,    // 5xx
  NetworkError,   // connect/timeout failure below the HTTP layer
  MalformedJson,  // F14 — discard, keep previous state
};

// GET /v1/me/player, filtered deserialisation straight off the TLS stream
// (§7.2 [CRITICAL] — never buffer the full 8-20KB body). Retries once on
// 401 via Auth::refreshNow(); never loops (§5.5).
PollOutcome pollPlayerState();

enum class ControlAction { Play, Pause, Next, Previous, Seek, Volume, Shuffle, Repeat };

struct ControlRequest {
  ControlAction action;
  long value = 0; // seek: position_ms; volume: 0-100; shuffle: 0/1; repeat: 0=off 1=context 2=track

  // Explicit constructor rather than relying on aggregate init: the ESP32
  // Arduino toolchain builds against a C++ standard where a default member
  // initializer (`value = 0` above) disqualifies a struct from aggregate
  // init, so `ControlRequest{action, value}` fails to find a matching
  // constructor without this.
  ControlRequest() = default;
  ControlRequest(ControlAction a, long v) : action(a), value(v) {}
};

enum class ControlResult {
  Ok,              // 204 — optimistic state already applied by the caller
  NoActiveDevice,  // 404 NO_ACTIVE_DEVICE
  Forbidden,       // 403 (non-Premium, or ad playing / start of queue on prev/next)
  NetworkError,
};

// Sends one control command. Applies Content-Length: 0 on bodyless PUTs
// (§7.3) so the server-side HTTP stack doesn't hang awaiting a body.
// Does NOT itself schedule the +700ms/+2000ms reconciliation polls or
// mutate PlayerStateStore optimistically — see web/server.cpp, which owns
// that UX policy and calls scheduleReconcilePolls() below after a Ok result.
ControlResult sendControl(const ControlRequest& req);

// Registers the two reconciliation poll timestamps (§7.3 "Optimistic UI
// with reconciliation"). netTask's loop calls pumpReconcilePolls() on every
// iteration; when a scheduled time is reached it performs an immediate
// pollPlayerState() (still drawing from the token bucket).
void scheduleReconcilePolls();
void pumpReconcilePolls();

// §15 ordering rule: "art download and decode happen only after the API
// TLS connection is closed. Never overlap them." netTask MUST call this
// before art::fetch() whenever a track change triggers a new art download —
// the keep-alive api.spotify.com connection would otherwise coexist with
// the art download's TLS handshake and exhaust heap on this no-PSRAM board.
void closeApiConnection();

}  // namespace Api
}  // namespace Spotify
