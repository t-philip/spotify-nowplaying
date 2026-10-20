#include "auth.h"
#include <mbedtls/sha256.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../store/nvs.h"
#include "../net/tls_client.h"
#include "../net/http.h"
#include "../util/b64url.h"

namespace Spotify {
namespace Auth {

namespace {

const char kPkceAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
constexpr size_t kPkceAlphabetLen = sizeof(kPkceAlphabet) - 1; // 66

struct PendingFlow {
  bool active = false;
  char verifier[PKCE_VERIFIER_LEN + 1] = {0};
  char state[33] = {0}; // 16 bytes hex-encoded = 32 chars
  uint32_t createdAtMs = 0;
};

PendingFlow s_flow;
String s_clientId;

String s_accessToken;
uint32_t s_accessTokenExpiresAtEpoch = 0;

void zeroiseFlow() {
  memset(s_flow.verifier, 0, sizeof(s_flow.verifier));
  memset(s_flow.state, 0, sizeof(s_flow.state));
  s_flow.active = false;
}

// Rejection sampling avoids modulo bias across the 66-character alphabet
// (§5.3) — esp_random() % 66 alone would bias toward the low 256 % 66 = 58
// characters.
char pkceRandomChar() {
  while (true) {
    uint8_t b = esp_random() & 0xFF;
    if (b < (256 / kPkceAlphabetLen) * kPkceAlphabetLen) {
      return kPkceAlphabet[b % kPkceAlphabetLen];
    }
  }
}

void generateVerifier(char* out) {
  for (int i = 0; i < PKCE_VERIFIER_LEN; i++) out[i] = pkceRandomChar();
  out[PKCE_VERIFIER_LEN] = '\0';
}

void generateStateHex(char* out) {
  uint8_t raw[16];
  for (int i = 0; i < 16; i++) raw[i] = esp_random() & 0xFF;
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) {
    out[i * 2] = hex[(raw[i] >> 4) & 0xF];
    out[i * 2 + 1] = hex[raw[i] & 0xF];
  }
  out[32] = '\0';
}

String challengeFromVerifier(const char* verifier) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(verifier), strlen(verifier));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char out[64];
  b64url_encode(hash, sizeof(hash), out, sizeof(out));
  return String(out);
}

String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// Constant-time comparison so an attacker probing /auth/complete cannot use
// timing to guess the expected state value.
bool constantTimeEquals(const char* a, const char* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

String stripQuotesAndWhitespace(const String& in) {
  String s = in;
  s.trim();
  while (s.length() > 0 && (s[0] == '"' || s[0] == '\'')) s.remove(0, 1);
  while (s.length() > 0 && (s[s.length() - 1] == '"' || s[s.length() - 1] == '\'')) {
    s.remove(s.length() - 1, 1);
  }
  s.trim();
  return s;
}

// Extracts the value of `key=` from a query string or full URL, stopping at
// the next '&' or end of string. Returns "" if not present.
String extractParam(const String& s, const char* key) {
  String needle = String(key) + "=";
  int idx = s.indexOf(needle);
  if (idx < 0) return "";
  int start = idx + needle.length();
  int end = s.indexOf('&', start);
  if (end < 0) end = s.length();
  return s.substring(start, end);
}

// POSTs application/x-www-form-urlencoded to accounts.spotify.com/api/token
// and buffers the (small, ~1-2KB) JSON response fully — unlike the player
// endpoint, this body is small enough that §7.2's streaming-filter
// requirement does not apply.
bool postToken(const String& body, JsonDocument& outDoc, int& httpStatus) {
  TlsClient tls(TrustMode::CaBundle);
  if (!tls.connect(SPOTIFY_AUTH_HOST)) {
    httpStatus = 0;
    return false;
  }

  String headers = "Content-Type: application/x-www-form-urlencoded\r\n";
  Http::sendRequest(tls, "POST", "/api/token", SPOTIFY_AUTH_HOST, headers, body.c_str());

  HttpResponse resp = Http::readStatusAndHeaders(tls);
  httpStatus = resp.statusCode;

  bool ok = false;
  if (resp.headersOk && resp.contentLength > 0) {
    char buf[2048];
    size_t got = Http::readBodyFixed(tls, buf, sizeof(buf), resp.contentLength);
    DeserializationError err = deserializeJson(outDoc, buf, got);
    ok = (err == DeserializationError::Ok);
  }

  tls.stop();
  return ok;
}

void applyTokenResponse(JsonDocument& doc) {
  const char* access = doc["access_token"] | "";
  uint32_t expiresIn = doc["expires_in"] | 3600;
  s_accessToken = String(access);
  s_accessTokenExpiresAtEpoch = (uint32_t)time(nullptr) + expiresIn;

  // §5.4 requirement 1: persist a rotated refresh_token BEFORE the new
  // access token is used for any API call. We are still inside the
  // exchange/refresh call here, so this happens before the caller can use
  // s_accessToken for anything.
  if (doc["refresh_token"].is<const char*>()) {
    const char* rt = doc["refresh_token"];
    if (rt && strlen(rt) > 0) {
      Nvs::writeRefreshToken(rt);
    }
  }
}

}  // namespace

void begin() {
  s_clientId = Nvs::getSpotifyClientId();
}

bool hasClientId() { return s_clientId.length() > 0; }
void setClientId(const String& clientId) {
  s_clientId = clientId;
  Nvs::setSpotifyClientId(clientId);
}

bool isAuthNeeded() {
  return !Nvs::hasValidRefreshToken();
}

String startFlow() {
  generateVerifier(s_flow.verifier);
  generateStateHex(s_flow.state);
  s_flow.createdAtMs = millis();
  s_flow.active = true;

  String challenge = challengeFromVerifier(s_flow.verifier);

  String url = "https://accounts.spotify.com/authorize";
  url += "?client_id=" + urlEncode(s_clientId);
  url += "&response_type=code";
  url += "&redirect_uri=" + urlEncode(SPOTIFY_REDIRECT_URI);
  url += "&code_challenge_method=S256";
  url += "&code_challenge=" + challenge;
  url += "&state=" + String(s_flow.state);
  url += "&scope=" + urlEncode(SPOTIFY_SCOPES);
  return url;
}

CompleteResult completeFlow(const String& pastedUrlOrCode) {
  if (!s_flow.active) return CompleteResult::NoFlowInProgress;

  if (millis() - s_flow.createdAtMs > PKCE_FLOW_EXPIRY_MS) {
    zeroiseFlow();
    return CompleteResult::FlowExpired;
  }

  String cleaned = stripQuotesAndWhitespace(pastedUrlOrCode);
  if (cleaned.length() == 0) return CompleteResult::BadInput;

  String code = extractParam(cleaned, "code");
  if (code.length() == 0) return CompleteResult::BadInput;

  String state = extractParam(cleaned, "state");
  if (state.length() > 0) {
    if (state.length() != strlen(s_flow.state) ||
        !constantTimeEquals(state.c_str(), s_flow.state, state.length())) {
      zeroiseFlow();
      return CompleteResult::StateMismatch;
    }
  }

  String body = "grant_type=authorization_code";
  body += "&code=" + urlEncode(code);
  body += "&redirect_uri=" + urlEncode(SPOTIFY_REDIRECT_URI);
  body += "&client_id=" + urlEncode(s_clientId);
  body += "&code_verifier=" + String(s_flow.verifier);

  JsonDocument doc;
  int httpStatus = 0;
  bool ok = postToken(body, doc, httpStatus);

  zeroiseFlow(); // §5.3: verifier held in RAM only for one attempt, then zeroised

  if (!ok || httpStatus != 200) return CompleteResult::ExchangeFailed;

  applyTokenResponse(doc);
  return CompleteResult::Ok;
}

RefreshOutcome refreshNow() {
  char rt[512];
  if (!Nvs::readRefreshToken(rt, sizeof(rt))) {
    return RefreshOutcome::InvalidGrant; // no valid token to refresh with
  }

  String body = "grant_type=refresh_token";
  body += "&refresh_token=" + urlEncode(String(rt));
  body += "&client_id=" + urlEncode(s_clientId);

  JsonDocument doc;
  int httpStatus = 0;
  bool ok = postToken(body, doc, httpStatus);

  if (!ok) {
    return RefreshOutcome::NetworkOrServerError;
  }
  if (httpStatus == 400) {
    // §5.5: refresh token is dead. Clear both NVS slots, caller transitions
    // to AUTH_NEEDED. MUST NOT retry — retrying a dead grant in a loop is
    // exactly the pattern that gets a client flagged.
    Nvs::clearRefreshTokens();
    s_accessToken = "";
    return RefreshOutcome::InvalidGrant;
  }
  if (httpStatus != 200) {
    return RefreshOutcome::NetworkOrServerError;
  }

  applyTokenResponse(doc);
  return RefreshOutcome::Ok;
}

uint32_t accessTokenExpiresInSeconds() {
  uint32_t now = (uint32_t)time(nullptr);
  if (s_accessTokenExpiresAtEpoch == 0 || now >= s_accessTokenExpiresAtEpoch) return 0;
  return s_accessTokenExpiresAtEpoch - now;
}

String getAccessToken() {
  uint32_t now = (uint32_t)time(nullptr);
  bool expiringSoon = (s_accessTokenExpiresAtEpoch == 0) ||
                       (now + TOKEN_REFRESH_MARGIN_S >= s_accessTokenExpiresAtEpoch);

  if (s_accessToken.length() == 0 || expiringSoon) {
    RefreshOutcome outcome = refreshNow();
    if (outcome != RefreshOutcome::Ok) {
      return s_accessToken; // may be "" (never had one) or a stale-but-cached token
    }
  }
  return s_accessToken;
}

}  // namespace Auth
}  // namespace Spotify
