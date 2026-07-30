#pragma once
#include <Arduino.h>

// The four rate-limit safety mechanisms of §6.3 [CRITICAL]. All four are
// required; together they make an accidental request storm structurally
// impossible even if one of them has a bug. Every Spotify request (poll or
// control command) MUST go through isBlocked() + tryAcquireToken() before
// being sent, and report its outcome via onResponse() afterward.

namespace Spotify {
namespace RateLimit {

// Loads persisted `retry_until` from NVS (§6.3b) — call once at boot,
// before the first request, so a crash-loop cannot flood Spotify.
void begin();

// (b) + (d): true if a 429 Retry-After window or the 5xx circuit breaker is
// currently suppressing all Spotify traffic. Does not consume a token.
bool isBlocked(uint32_t nowEpoch);

// (a) Token bucket. Capacity/refill are the named constants in config.h.
// Returns false if the bucket is empty — the caller MUST drop the request,
// never queue it (§6.3a: "a queue that drains at reconnection is a request
// storm").
bool tryAcquireToken();

// Feed back every Spotify HTTP outcome. Updates:
//  - circuit breaker consecutive-5xx counter (reset on any 2xx)
//  - retry_until in NVS + adaptive cool-down multiplier on 429
//  - the rolling request-rate counter used by /healthz (acceptance #17)
void onResponse(int httpStatus, uint32_t retryAfterSeconds, uint32_t nowEpoch);

// (c) Current multiplier to apply to the base poll interval. 1.0 = normal.
// Doubles on a 429, decays 25%/successful-poll back toward 1.0. In-RAM only
// (resets on reboot; bounded by the persisted retry_until in (b)).
float pollIntervalMultiplier();

// For §13.2 diagnostics / acceptance criterion #17 (on-device request
// counter exposed at /healthz — sustained rate must never exceed 20/min).
uint32_t requestsInLastMinute();

}  // namespace RateLimit
}  // namespace Spotify
