#pragma once
#include <Arduino.h>

// Standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320), table-based.
// Used for NVS A/B refresh-token integrity checks (§5.4) and the
// deterministic album-art placeholder hue (§11.3).
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len);

inline uint32_t crc32_buf(const uint8_t* data, size_t len) {
  return crc32_update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

inline uint32_t crc32_str(const char* s) {
  return crc32_buf(reinterpret_cast<const uint8_t*>(s), strlen(s));
}
