#include "time_sync.h"
#include <time.h>
#include "../config.h"
#include "../app_state.h"

namespace TimeSync {

bool isTimeValid() {
  return time(nullptr) > TIME_VALID_EPOCH_FLOOR;
}

bool begin(const String& tz) {
  AppStateStore::setState(AppState::TIME_SYNC);

  configTzTime(tz.c_str(), NTP_SERVER_1, NTP_SERVER_2);

  uint32_t start = millis();
  while (!isTimeValid()) {
    if (millis() - start > TIME_SYNC_TIMEOUT_MS) {
      // §9: "Timeout 30s, then retry with backoff while displaying
      // 'Syncing time'." Caller loops us again; we don't spin forever here
      // so netTask can still service the watchdog and WiFi supervisor.
      return false;
    }
    delay(200);
  }
  return true;
}

}  // namespace TimeSync
