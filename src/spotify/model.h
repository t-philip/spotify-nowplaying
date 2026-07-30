#pragma once
#include <Arduino.h>
#include "../config.h"

namespace Spotify {

// Shared playback state (§8.1). netTask writes under the mutex in
// PlayerStateStore; uiTask takes a snapshot copy under the mutex and
// releases it before rendering — MUST NOT hold the mutex during SPI writes.
struct PlayerState {
  bool     valid = false;
  bool     is_playing = false;
  char     track[FIELD_MAX_TRACK + 1] = {0};
  char     artist[FIELD_MAX_ARTIST + 1] = {0};
  char     album[FIELD_MAX_ALBUM + 1] = {0};
  char     device[FIELD_MAX_DEVICE + 1] = {0};
  char     art_url[FIELD_MAX_ART_URL + 1] = {0};
  uint32_t duration_ms = 0;
  uint32_t progress_ms = 0;        // authoritative, from last poll
  uint32_t progress_epoch_ms = 0;  // millis() at last poll
  bool     shuffle = false;
  uint8_t  repeat = 0;             // 0=off 1=context 2=track
  uint8_t  volume = 0;
  bool     explicit_flag = false;
  uint32_t revision = 0;           // incremented on every change
};

namespace PlayerStateStore {

void begin();

// Copies out under the mutex and releases before returning — safe to call
// from uiTask at render rate.
PlayerState snapshot();

// netTask-only: replaces the whole struct and bumps revision. Callers
// should preserve fields between polls (e.g. progress extrapolation, §6.4)
// by starting from a snapshot() and mutating that copy before calling this.
void update(const PlayerState& newState);

}  // namespace PlayerStateStore
}  // namespace Spotify
