#pragma once
#include <Arduino.h>

// Base64URL encoding, no padding, per RFC 7636 (PKCE code_challenge, §5.3).
// out must be at least b64url_encoded_len(in_len)+1 bytes.
size_t b64url_encoded_len(size_t in_len);
size_t b64url_encode(const uint8_t* in, size_t in_len, char* out, size_t out_cap);
