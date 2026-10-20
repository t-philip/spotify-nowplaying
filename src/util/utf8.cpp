#include "utf8.h"

static int utf8_seq_len(uint8_t lead) {
  if ((lead & 0x80) == 0x00) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1; // invalid lead byte — treat as single byte so we still advance
}

size_t utf8_safe_copy(char* dst, size_t dst_cap, const char* src) {
  if (dst_cap == 0) return 0;
  size_t max_bytes = dst_cap - 1;
  size_t written = 0;
  size_t i = 0;
  size_t src_len = strlen(src);

  while (i < src_len) {
    int seq = utf8_seq_len((uint8_t)src[i]);
    if (i + (size_t)seq > src_len) seq = 1; // truncated source, don't overread
    if (written + (size_t)seq > max_bytes) break; // would split or overflow
    memcpy(dst + written, src + i, seq);
    written += seq;
    i += seq;
  }
  dst[written] = '\0';
  return written;
}

bool utf8_is_renderable_ascii_latin1(uint32_t codepoint) {
  return codepoint <= 0xFF;
}

uint32_t utf8_next_codepoint(const char** p) {
  const uint8_t* s = reinterpret_cast<const uint8_t*>(*p);
  uint8_t lead = s[0];
  int seq = utf8_seq_len(lead);
  uint32_t cp = 0;

  switch (seq) {
    case 1:
      cp = lead;
      break;
    case 2:
      cp = (lead & 0x1F) << 6 | (s[1] & 0x3F);
      break;
    case 3:
      cp = (lead & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F);
      break;
    case 4:
      cp = (lead & 0x07) << 18 | (s[1] & 0x3F) << 12 | (s[2] & 0x3F) << 6 | (s[3] & 0x3F);
      break;
  }
  *p += seq;
  return cp;
}
