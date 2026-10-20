#pragma once
#include <Arduino.h>

// WiFi provisioning (WiFiManager captive portal, §9 WIFI_SETUP) and runtime
// resilience supervisor (§10.3). Runs entirely on netTask.

namespace WifiMgr {

// Blocks until connected (using stored credentials or a fresh portal
// session). Publishes WIFI_INIT / WIFI_SETUP into AppStateStore as it goes,
// and WIFI_SETUP's AP SSID/password so uiTask can render §12.5's screen.
// Returns once WiFi.status() == WL_CONNECTED.
void begin();

// Call frequently from netTask's loop. Implements §10.3:
//  - WiFi.setSleep(false) already applied in begin()
//  - disconnected > 30s -> WiFi.disconnect(); WiFi.begin()
//  - disconnected > 10min -> reboot
// Also refreshes AppStateStore's network info (IP, RSSI) periodically.
void loop();

bool isConnected();

// Deterministic AP password derived from the chip's factory MAC, so it is
// reproducible across factory resets without needing its own NVS key.
String apPassword();

}  // namespace WifiMgr
