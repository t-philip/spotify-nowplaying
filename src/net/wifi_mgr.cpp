#include "wifi_mgr.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include "../config.h"
#include "../app_state.h"

namespace WifiMgr {

static WiFiManager s_wm;
static uint32_t s_disconnectedSinceMs = 0;
static bool s_wasConnected = false;

String apPassword() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[9];
  // 8 hex chars, deterministic per device, printed on the WIFI_SETUP screen
  // and reproducible after a factory reset without a dedicated NVS key.
  snprintf(buf, sizeof(buf), "%08llx", (unsigned long long)(mac & 0xFFFFFFFFULL));
  return String(buf);
}

static void onConfigPortalStart(WiFiManager* wm) {
  AppStateStore::setState(AppState::WIFI_SETUP);
  AppStateStore::setWifiSetupInfo(WIFI_SETUP_AP_SSID, apPassword().c_str());
}

void begin() {
  WiFi.setSleep(false); // §10.3 — modem sleep causes multi-second latency spikes

  AppStateStore::setState(AppState::WIFI_INIT);

  s_wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  s_wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_MS / 1000);
  s_wm.setAPCallback(onConfigPortalStart);

  bool connected = s_wm.autoConnect(WIFI_SETUP_AP_SSID, apPassword().c_str());

  if (!connected) {
    // Portal timed out with no credentials entered. Per §9 WIFI_SETUP note:
    // "Portal timeout 300s, then reboot and retry the stored credentials"
    // (handles a router that was merely slow to come back).
    ESP.restart();
  }

  s_wasConnected = true;
  s_disconnectedSinceMs = 0;
  WiFi.setAutoReconnect(true);
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void loop() {
  bool connected = isConnected();
  uint32_t now = millis();

  if (connected) {
    s_disconnectedSinceMs = 0;
    if (!s_wasConnected) {
      AppStateStore::setState(AppState::AUTH_CHECK); // caller may override
    }
    s_wasConnected = true;
    AppStateStore::setNetworkInfo(
        WiFi.localIP().toString().c_str(),
        WEB_MDNS_NAME,
        true,
        (int8_t)WiFi.RSSI());
    return;
  }

  s_wasConnected = false;
  if (s_disconnectedSinceMs == 0) {
    s_disconnectedSinceMs = now;
  }

  uint32_t downMs = now - s_disconnectedSinceMs;

  if (downMs > WIFI_DISCONNECT_REBOOT_S * 1000UL) {
    // §10.3 / F2 / acceptance criterion #16 — 15 min disconnected -> reboot.
    ESP.restart();
  } else if (downMs > WIFI_DISCONNECT_RECONNECT_S * 1000UL) {
    WiFi.disconnect();
    WiFi.begin();
  }

  AppStateStore::setNetworkInfo("0.0.0.0", WEB_MDNS_NAME, false, 0);
}

}  // namespace WifiMgr
