#pragma once
#include <Arduino.h>

// NTP + timezone, and the TLS-readiness gate (§9 TIME_SYNC, §10.1).
// X.509 validation needs a correct clock — the ESP32 boots at epoch 0 and
// every certificate appears not-yet-valid until this completes.

namespace TimeSync {

// Blocks (with backoff, updating AppStateStore to TIME_SYNC) until
// time(nullptr) > TIME_VALID_EPOCH_FLOOR, or returns false after repeated
// timeouts so the caller can decide whether to keep retrying.
bool begin(const String& tz);

bool isTimeValid();

}  // namespace TimeSync
