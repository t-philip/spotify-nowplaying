#pragma once
#include <TFT_eSPI.h>
#include "../config.h"
#include "layout.h"

// TFT init and backlight PWM (§12.6). Owns the single TFT_eSPI instance —
// every other ui/ and art/ module reaches the display through Display::tft().

namespace Display {

// rotation 0 = portrait (§2.3 default, 135x240); rotation 1 = landscape
// (240x135). Rotation 1 is a first guess for this board's landscape
// direction — if the image comes up mirrored/upside-down, try rotation 3
// instead (net effect of CGRAM_OFFSET + physical mounting can't be verified
// without the board in hand).
void begin(Orientation orientation);

// Access to the shared TFT_eSPI instance. Per §8.1, uiTask owns rendering —
// but §8.1 also assigns album-art *decode* (art/art_decode.cpp, which pushes
// pixels via TJpg_Decoder's callback) to netTask, on the other core, so the
// same TFT_eSPI/SPI bus is touched from both tasks. TFT_eSPI's SPI
// transactions are not safe against a concurrent caller from the other
// core, so every actual draw/push call — from either task — MUST be
// wrapped in lock()/unlock() below. This mutex is the fix for that gap;
// nothing else in the firmware serialises the two tasks' SPI access.
TFT_eSPI& tft();

bool lock(uint32_t timeoutMs = MUTEX_TIMEOUT_MS);
void unlock();

// Four levels: 15/40/70/100% (§12.6), indices 0-3. Cycled by BOOT
// short-press; persisted in NVS by the caller (store/nvs.h).
void setBrightnessIndex(uint8_t index);
uint8_t brightnessIndex();

}  // namespace Display
