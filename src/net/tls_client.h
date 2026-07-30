#pragma once
#include <WiFiClientSecure.h>
#include "../config.h"

// WiFiClientSecure wrapper implementing §10.1 (CA bundle validation) and
// §10.2 (one-connection-at-a-time discipline, keep-alive recycling, explicit
// stop() on every path). Only one TlsClient may be connected at a time
// across the whole firmware — callers are responsible for closing the
// Spotify API connection before opening the art-download connection (§15
// ordering rule); this class does not enforce that globally, it only
// manages its own single underlying socket safely.

enum class TrustMode {
  CaBundle,       // accounts.spotify.com, api.spotify.com — credential-bearing (§10.1 High)
  InsecureArtOnly // i.scdn.co only — no credentials transmitted, worst case is a wrong image
};

class TlsClient {
 public:
  explicit TlsClient(TrustMode mode);

  bool connect(const char* host, uint16_t port = 443);
  bool connected();

  // Access the underlying stream, e.g. for ArduinoJson's filtered
  // deserializeJson(doc, client, ...) which reads directly off the socket
  // (§7.2) rather than through an intermediate String buffer.
  WiFiClientSecure& stream() { return _client; }

  void stop();

  // §10.2: "recycle the connection every 50 requests or 10 minutes,
  // whichever comes first."
  bool shouldRecycle() const;
  void noteRequestSent();

  const String& host() const { return _host; }

 private:
  WiFiClientSecure _client;
  TrustMode _mode;
  String _host;
  uint32_t _connectedAtMs = 0;
  uint32_t _requestCount = 0;
};
