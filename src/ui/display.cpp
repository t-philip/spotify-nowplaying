#include "display.h"
#include "../config.h"

namespace Display {

namespace {
TFT_eSPI s_tft = TFT_eSPI();
uint8_t s_brightnessIndex = BACKLIGHT_DEFAULT_INDEX;
SemaphoreHandle_t s_spiMutex = nullptr;
}  // namespace

void begin(Orientation orientation) {
  s_spiMutex = xSemaphoreCreateMutex();

  s_tft.init();
  s_tft.setRotation(orientation == Orientation::Landscape ? 1 : 0); // §2.3 default is portrait (rotation 0)
  s_tft.fillScreen(COLOR_BG);

  ledcSetup(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_PWM_FREQ_HZ, BACKLIGHT_PWM_RES_BITS);
  ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CHANNEL);
  setBrightnessIndex(s_brightnessIndex);
}

TFT_eSPI& tft() { return s_tft; }

bool lock(uint32_t timeoutMs) {
  return xSemaphoreTake(s_spiMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void unlock() {
  xSemaphoreGive(s_spiMutex);
}

void setBrightnessIndex(uint8_t index) {
  s_brightnessIndex = index % 4;
  uint8_t percent = BACKLIGHT_LEVELS[s_brightnessIndex];
  uint32_t maxDuty = (1UL << BACKLIGHT_PWM_RES_BITS) - 1;
  uint32_t duty = (uint32_t)percent * maxDuty / 100;
  ledcWrite(BACKLIGHT_PWM_CHANNEL, duty);
}

uint8_t brightnessIndex() { return s_brightnessIndex; }

}  // namespace Display
