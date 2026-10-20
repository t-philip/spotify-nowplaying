#include "app_state.h"
#include "config.h"

namespace AppStateStore {

static AppStatus s_status;
static SemaphoreHandle_t s_mutex = nullptr;

void begin() {
  s_mutex = xSemaphoreCreateMutex();
}

AppStatus snapshot() {
  AppStatus copy;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    copy = s_status;
    xSemaphoreGive(s_mutex);
  }
  return copy;
}

void setState(AppState s) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_status.state = s;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void setWifiSetupInfo(const char* ssid, const char* password) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    strncpy(s_status.wifiApSsid, ssid, sizeof(s_status.wifiApSsid) - 1);
    strncpy(s_status.wifiApPassword, password, sizeof(s_status.wifiApPassword) - 1);
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void setNetworkInfo(const char* ip, const char* mdnsHost, bool connected, int8_t rssi) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    strncpy(s_status.localIp, ip, sizeof(s_status.localIp) - 1);
    strncpy(s_status.mdnsHost, mdnsHost, sizeof(s_status.mdnsHost) - 1);
    s_status.wifiConnected = connected;
    s_status.wifiRssiDbm = rssi;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void setError(const char* message) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    strncpy(s_status.errorMessage, message, sizeof(s_status.errorMessage) - 1);
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void setRateLimitedUntil(uint32_t epoch) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_status.rateLimitedUntilEpoch = epoch;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void updateDiagnostics(uint32_t freeHeap, uint32_t uptimeMs, uint32_t reqLastMinute, uint8_t lastHttpStatus) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_status.freeHeapBytes = freeHeap;
    s_status.uptimeMs = uptimeMs;
    s_status.requestsLastMinute = reqLastMinute;
    s_status.lastHttpStatus = lastHttpStatus;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void updateArtDiagnostics(int fetchResult, uint32_t len, int decodeOk) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_status.lastArtFetchResult = fetchResult;
    s_status.lastArtLen = len;
    s_status.lastArtDecodeOk = decodeOk;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

void updateArtFetchSteps(int connectOk, int sendOk, int httpStatus, int32_t contentLength) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_status.lastArtConnectOk = connectOk;
    s_status.lastArtSendOk = sendOk;
    s_status.lastArtHttpStatus = httpStatus;
    s_status.lastArtContentLength = contentLength;
    s_status.revision++;
    xSemaphoreGive(s_mutex);
  }
}

}  // namespace AppStateStore
