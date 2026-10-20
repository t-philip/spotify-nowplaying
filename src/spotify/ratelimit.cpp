#include "ratelimit.h"
#include "../config.h"
#include "../store/nvs.h"

namespace Spotify {
namespace RateLimit {

// ---- (a) Token bucket -------------------------------------------------------
static float    s_tokens = RATE_BUCKET_CAPACITY;
static uint32_t s_lastRefillMs = 0;

// ---- (b) Retry-After, persisted across reboot -------------------------------
static uint32_t s_retryUntilEpoch = 0;

// ---- (c) Adaptive cool-down multiplier (in-RAM only) ------------------------
static float    s_pollMultiplier = 1.0f;
static uint32_t s_cooldownUntilMs = 0;

// ---- (d) Circuit breaker -----------------------------------------------------
static uint8_t  s_consecutive5xx = 0;
static uint32_t s_circuitOpenUntilMs = 0;

// ---- Rolling request counter for /healthz -----------------------------------
static constexpr size_t kHistCap = 64;
static uint32_t s_reqHistMs[kHistCap] = {0};
static size_t   s_reqHistHead = 0;
static size_t   s_reqHistCount = 0;

void begin() {
  s_retryUntilEpoch = Nvs::getRetryUntilEpoch();
  s_lastRefillMs = millis();
}

static void refillTokens() {
  uint32_t now = millis();
  uint32_t elapsed = now - s_lastRefillMs;
  if (elapsed == 0) return;
  float refill = (float)elapsed / (float)RATE_BUCKET_REFILL_MS;
  s_tokens = min((float)RATE_BUCKET_CAPACITY, s_tokens + refill);
  s_lastRefillMs = now;
}

bool isBlocked(uint32_t nowEpoch) {
  if (s_retryUntilEpoch != 0 && nowEpoch < s_retryUntilEpoch) return true;
  if (s_circuitOpenUntilMs != 0 && millis() < s_circuitOpenUntilMs) return true;
  return false;
}

bool tryAcquireToken() {
  refillTokens();
  if (s_tokens < 1.0f) return false;
  s_tokens -= 1.0f;

  if (s_reqHistCount < kHistCap) {
    s_reqHistMs[(s_reqHistHead + s_reqHistCount) % kHistCap] = millis();
    s_reqHistCount++;
  } else {
    s_reqHistMs[s_reqHistHead] = millis();
    s_reqHistHead = (s_reqHistHead + 1) % kHistCap;
  }
  return true;
}

uint32_t requestsInLastMinute() {
  uint32_t now = millis();
  uint32_t count = 0;
  for (size_t i = 0; i < s_reqHistCount; i++) {
    size_t idx = (s_reqHistHead + i) % kHistCap;
    if (now - s_reqHistMs[idx] <= 60000UL) count++;
  }
  return count;
}

void onResponse(int httpStatus, uint32_t retryAfterSeconds, uint32_t nowEpoch) {
  if (httpStatus == 429) {
    uint32_t until = nowEpoch + retryAfterSeconds;
    s_retryUntilEpoch = until;
    Nvs::setRetryUntilEpoch(until); // §6.3b — survives reboot, incl. a crash loop

    s_pollMultiplier = min(8.0f, s_pollMultiplier * 2.0f);
    s_cooldownUntilMs = millis() + RATE_COOLDOWN_DURATION_MS;
    return;
  }

  if (httpStatus >= 500 && httpStatus < 600) {
    s_consecutive5xx++;
    if (s_consecutive5xx >= CIRCUIT_BREAKER_5XX_THRESHOLD) {
      s_circuitOpenUntilMs = millis() + CIRCUIT_BREAKER_SUSPEND_MS;
    }
    return;
  }

  if (httpStatus >= 200 && httpStatus < 300) {
    s_consecutive5xx = 0; // §6.3d: reset on any 2xx

    // §6.3c: decay 25% per successful poll, back toward 1.0, once the
    // 10-minute cool-down window has elapsed.
    if (millis() > s_cooldownUntilMs && s_pollMultiplier > 1.0f) {
      s_pollMultiplier = max(1.0f, s_pollMultiplier - (s_pollMultiplier - 1.0f) * (1.0f - RATE_COOLDOWN_DECAY_FACTOR));
    }

    // A clean response past retry_until means the throttling window has
    // elapsed; clear it so isBlocked() doesn't keep gating on a stale value.
    if (s_retryUntilEpoch != 0 && nowEpoch >= s_retryUntilEpoch) {
      s_retryUntilEpoch = 0;
      Nvs::setRetryUntilEpoch(0);
    }
  }
}

float pollIntervalMultiplier() {
  return s_pollMultiplier;
}

}  // namespace RateLimit
}  // namespace Spotify
