#include "tls_client.h"

#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define IDEASPEAK_HAVE_CRT_BUNDLE 1
// The linker symbol name changes between arduino-esp32 core releases
// (`_binary_x509_crt_bundle_start` vs `_binary_x509_crt_bundle_bin_start`,
// §10.1) — confirmed by an actual link failure against espressif32@6.13.0 /
// framework-arduinoespressif32@3.20017, which exports the shorter name
// (verified via `xtensa-esp32-elf-nm` on the SDK's prebuilt libmbedtls.a).
// If a future core bump breaks this again, it will surface as a link
// error at build time, not a silent runtime failure — re-run the same nm
// check against the new core's libmbedtls.a.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
#endif

TlsClient::TlsClient(TrustMode mode) : _mode(mode) {}

bool TlsClient::connect(const char* host, uint16_t port) {
  _host = host;

  if (_mode == TrustMode::CaBundle) {
#ifdef IDEASPEAK_HAVE_CRT_BUNDLE
    _client.setCACertBundle(rootca_crt_bundle_start);
#else
    // Fallback path per §10.1: only acceptable for the credential-bearing
    // hosts if a pinned root CA is supplied elsewhere; refusing to connect
    // insecurely here is deliberate — accounts.spotify.com and
    // api.spotify.com carry the refresh/access tokens and MUST NOT be
    // downgraded to setInsecure().
    return false;
#endif
  } else {
    // i.scdn.co only (album art). No credential is transmitted on this
    // connection; the worst case of a MITM here is a wrong album image,
    // which is an acceptable trade-off against the ~30KB heap cost and
    // browser-interstitial UX of a full on-device HTTPS/cert story (§4.1,
    // §10.1). This MUST NOT be applied to accounts.spotify.com or
    // api.spotify.com.
    _client.setInsecure();
  }

  _client.setTimeout(TLS_READ_TIMEOUT_MS);
  bool ok = _client.connect(host, port);
  if (ok) {
    _connectedAtMs = millis();
    _requestCount = 0;
  }
  return ok;
}

bool TlsClient::connected() {
  return _client.connected();
}

void TlsClient::stop() {
  _client.stop(); // explicit stop on every path (§10.2) — leaked TLS contexts
                   // are the classic slow heap leak on this board.
  _connectedAtMs = 0;
  _requestCount = 0;
}

bool TlsClient::shouldRecycle() const {
  if (_requestCount >= API_KEEPALIVE_MAX_REQUESTS) return true;
  if (_connectedAtMs != 0 && millis() - _connectedAtMs > API_KEEPALIVE_MAX_AGE_MS) return true;
  return false;
}

void TlsClient::noteRequestSent() {
  _requestCount++;
}
