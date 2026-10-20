#include "b64url.h"

static const char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t b64url_encoded_len(size_t in_len) {
  return (in_len + 2) / 3 * 4; // upper bound before padding strip
}

size_t b64url_encode(const uint8_t* in, size_t in_len, char* out, size_t out_cap) {
  size_t oi = 0;
  size_t i = 0;
  while (i + 3 <= in_len) {
    uint32_t n = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
    if (oi + 4 > out_cap) return 0;
    out[oi++] = kAlphabet[(n >> 18) & 0x3F];
    out[oi++] = kAlphabet[(n >> 12) & 0x3F];
    out[oi++] = kAlphabet[(n >> 6) & 0x3F];
    out[oi++] = kAlphabet[n & 0x3F];
    i += 3;
  }
  size_t rem = in_len - i;
  if (rem == 1) {
    uint32_t n = (uint32_t)in[i] << 16;
    if (oi + 2 > out_cap) return 0;
    out[oi++] = kAlphabet[(n >> 18) & 0x3F];
    out[oi++] = kAlphabet[(n >> 12) & 0x3F];
    // no padding characters — PKCE requires padding stripped
  } else if (rem == 2) {
    uint32_t n = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8;
    if (oi + 3 > out_cap) return 0;
    out[oi++] = kAlphabet[(n >> 18) & 0x3F];
    out[oi++] = kAlphabet[(n >> 12) & 0x3F];
    out[oi++] = kAlphabet[(n >> 6) & 0x3F];
  }
  if (oi >= out_cap) return 0;
  out[oi] = '\0';
  return oi;
}
