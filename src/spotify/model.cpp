#include "model.h"

namespace Spotify {
namespace PlayerStateStore {

static PlayerState s_state;
static SemaphoreHandle_t s_mutex = nullptr;

void begin() {
  s_mutex = xSemaphoreCreateMutex();
}

PlayerState snapshot() {
  PlayerState copy;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    copy = s_state;
    xSemaphoreGive(s_mutex);
  }
  return copy;
}

void update(const PlayerState& newState) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    PlayerState next = newState;
    next.revision = s_state.revision + 1;
    s_state = next;
    xSemaphoreGive(s_mutex);
  }
}

}  // namespace PlayerStateStore
}  // namespace Spotify
