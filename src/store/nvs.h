#pragma once
#include <Arduino.h>
#include "../config.h"

// Typed NVS access (namespace "ideaspeak", §16) plus the crash-safe A/B
// refresh-token slot scheme required by §5.4 [CRITICAL]. All refresh-token
// I/O in the firmware MUST go through this module — nothing else may touch
// the rt_a/rt_b/rt_seq_*/rt_crc_* keys directly.

namespace Nvs {

void begin();

// ---- Refresh token A/B (§5.4) ----------------------------------------------
// Reads whichever slot is CRC-valid and has the higher sequence number.
// Returns false (AUTH_NEEDED) if neither slot validates.
bool readRefreshToken(char* out, size_t out_cap);

// Full write algorithm: write to the inactive slot, commit, then flip the
// sequence number as the atomic switch. MUST be called before the new
// access token obtained alongside this refresh token is used for any API
// call (§5.4 requirement 1).
bool writeRefreshToken(const char* token);

// Wipes both slots — used on invalid_grant (F4) and factory reset.
void clearRefreshTokens();

bool hasValidRefreshToken();

// ---- WiFi / setup ----------------------------------------------------------
String getSpotifyClientId();
void   setSpotifyClientId(const String& clientId);

// ---- Poll cadence (user-configurable, clamped to [POLL_INTERVAL_MIN_S, POLL_INTERVAL_MAX_S]) ----
uint16_t getPollIntervalPlaying();
void     setPollIntervalPlaying(uint16_t seconds);
uint16_t getPollIntervalPaused();
void     setPollIntervalPaused(uint16_t seconds);

// ---- Display ----------------------------------------------------------------
uint8_t getBrightnessIndex();
void    setBrightnessIndex(uint8_t index);
bool    getArtEnabled();
void    setArtEnabled(bool enabled);

// 0 = portrait (default), 1 = landscape. Added post-build per user request —
// not in the original spec. Applying a change requires a reboot (TFT
// rotation + all widget geometry are only ever set up once, at boot).
uint8_t getOrientation();
void    setOrientation(uint8_t orientation);

// ---- Web UI ------------------------------------------------------------------
String getWebPin();          // generates+persists a random 6-digit PIN on first call
void   setWebPin(const String& pin);

// ---- Rate-limit state surviving reboot (§6.3b) --------------------------------
uint32_t getRetryUntilEpoch();
void     setRetryUntilEpoch(uint32_t epoch);

// ---- Crash-loop detection (F16) ------------------------------------------------
uint8_t getBootCount();
void    incrementBootCount();
void    clearBootCount();

// ---- Timezone -----------------------------------------------------------------
String getTimezone();
void   setTimezone(const String& tz);

// ---- Factory reset --------------------------------------------------------------
void factoryReset(); // wipes the entire "ideaspeak" namespace

}  // namespace Nvs
