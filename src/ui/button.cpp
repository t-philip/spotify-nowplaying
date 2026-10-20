#include "button.h"
#include "../config.h"

namespace Button {

namespace {
constexpr uint32_t kDebounceMs = 25;

bool s_rawState = false;       // true = pressed (active LOW)
bool s_debouncedState = false;
uint32_t s_lastRawChangeMs = 0;
uint32_t s_pressStartMs = 0;
bool s_longFired = false;
bool s_resetFired = false;
}  // namespace

void begin() {
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
}

Action poll() {
  bool raw = (digitalRead(PIN_BOOT_BUTTON) == LOW);
  uint32_t now = millis();

  if (raw != s_rawState) {
    s_rawState = raw;
    s_lastRawChangeMs = now;
  }

  bool debouncedEdge = false;
  if ((now - s_lastRawChangeMs) >= kDebounceMs && s_rawState != s_debouncedState) {
    s_debouncedState = s_rawState;
    debouncedEdge = true;
  }

  Action action = Action::None;

  if (debouncedEdge && s_debouncedState) {
    // Just pressed.
    s_pressStartMs = now;
    s_longFired = false;
    s_resetFired = false;
  } else if (s_debouncedState) {
    uint32_t held = now - s_pressStartMs;
    if (held >= BTN_FACTORY_RESET_MS && !s_resetFired) {
      s_resetFired = true;
      action = Action::FactoryResetTriggered;
    } else if (held >= BTN_LONG_PRESS_MIN_MS && !s_longFired) {
      s_longFired = true;
      action = Action::LongPressTriggered;
    }
  } else if (debouncedEdge && !s_debouncedState) {
    // Just released.
    uint32_t held = now - s_pressStartMs;
    if (held < BTN_SHORT_PRESS_MAX_MS) {
      action = Action::ShortPress;
    }
    // Otherwise: released after a long/factory-reset threshold already
    // fired (nothing more to do), or released before any threshold fired
    // (an aborted long-press attempt, §2.4 — no action).
  }

  return action;
}

bool isHeld() { return s_debouncedState; }

uint32_t heldForMs() {
  return s_debouncedState ? (millis() - s_pressStartMs) : 0;
}

}  // namespace Button
