#include "crc32.h"

// Precomputed at first use rather than stored as 1KB of flash constant data —
// this board's flash budget is tight (§3.1) and the table only costs a few ms
// of one-time boot computation.
static uint32_t s_table[256];
static bool     s_table_ready = false;

static void build_table() {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) {
      c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    }
    s_table[i] = c;
  }
  s_table_ready = true;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  if (!s_table_ready) build_table();
  for (size_t i = 0; i < len; i++) {
    crc = s_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc;
}
