#pragma once
#include <Arduino.h>

// BOOT button (GPIO0) handling — the only user input (§2.4). Not called out
// as its own file in the spec's §8.2 module list, but the three-tier
// press/hold behaviour (with an abortable countdown for factory reset) is
// substantial enough to warrant its own module rather than living inline
// in main.cpp. Polled from uiTask.

namespace Button {

enum class Action {
  None,
  ShortPress,        // < BTN_SHORT_PRESS_MAX_MS: cycle backlight brightness
  LongPressTriggered, // crossed BTN_LONG_PRESS_MIN_MS while still held: show IP/mDNS full-screen
  FactoryResetTriggered, // crossed BTN_FACTORY_RESET_MS while still held: wipe NVS, reboot
};

void begin();

// Call every uiTask loop iteration. Debounced internally. Fires each Action
// at most once per press — release-before-threshold aborts a pending
// long/factory-reset action with no side effect (§2.4 "MUST be abortable by
// releasing the button").
Action poll();

bool isHeld();
uint32_t heldForMs(); // for rendering the on-screen factory-reset countdown

}  // namespace Button
