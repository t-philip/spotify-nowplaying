#pragma once
#include <Arduino.h>
#include "tls_client.h"

// Minimal HTTP/1.1 request/response helper over a connected TlsClient. Not a
// general-purpose client — just enough for Spotify's token/player/control
// endpoints (§7) and the art download (§11.2): one request in flight, no
// chunked-transfer support (Spotify's API responses are not chunked), and
// header parsing limited to what the firmware actually needs.

struct HttpResponse {
  int statusCode = 0;
  long contentLength = -1;    // -1 = not present / unknown
  uint32_t retryAfterSeconds = 0; // 0 = header absent
  bool headersOk = false;
};

namespace Http {

// Writes the request line, Host header, any caller-supplied headers, and
// (if body is non-null) Content-Length + the body. `extraHeaders` must
// already be CRLF-terminated per header, e.g. "Authorization: Bearer x\r\n".
// PUTs with no body still send `Content-Length: 0` (§7.3) since some HTTP
// stacks hang awaiting a body otherwise — pass body="" (non-null, empty) for
// that case, not nullptr.
bool sendRequest(TlsClient& tls, const char* method, const char* path,
                  const char* host, const String& extraHeaders,
                  const char* body);

// Reads the status line and headers (up to the blank line), stopping before
// the body so the caller can stream-parse it (e.g. ArduinoJson filtered
// deserialisation directly off the socket, §7.2). Enforces TLS_READ_TIMEOUT_MS
// per read so a half-open socket cannot stall netTask indefinitely (§10.2).
HttpResponse readStatusAndHeaders(TlsClient& tls);

// Reads exactly `contentLength` bytes of body into a caller-provided buffer.
// Used only for small, fully-buffered bodies (token exchange JSON, art
// Content-Length probe) — the player-state body is never buffered this way,
// see §7.2.
size_t readBodyFixed(TlsClient& tls, char* buf, size_t bufCap, long contentLength);

}  // namespace Http
