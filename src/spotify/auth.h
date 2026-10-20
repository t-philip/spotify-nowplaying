#pragma once
#include <Arduino.h>

// Authorization Code + PKCE (§5). Owns the in-RAM PKCE flow state, the
// access-token cache, and drives refresh-token persistence through
// store/nvs.h's A/B scheme (§5.4 [CRITICAL]).

namespace Spotify {
namespace Auth {

void begin(); // loads client_id + refresh token presence from NVS

bool hasClientId();
void setClientId(const String& clientId);

bool isAuthNeeded(); // no valid refresh token in NVS

// ---- First-time authorisation (§5.2, §5.3) ----------------------------------

// Generates code_verifier/code_challenge/state, holds verifier+state in RAM
// (zeroised + expired after PKCE_FLOW_EXPIRY_MS), and returns the full
// https://accounts.spotify.com/authorize URL for the setup page to link to.
String startFlow();

enum class CompleteResult {
  Ok,
  NoFlowInProgress,
  FlowExpired,
  StateMismatch,
  BadInput,
  ExchangeFailed,
};

// Accepts either the full pasted redirect URL or a bare `code=...` value
// (§5.2 step 11). Strips whitespace/quotes, extracts code+state, verifies
// state in constant time, exchanges the code for tokens, and persists the
// refresh token via store/nvs.h BEFORE the access token is used for any API
// call (§5.4 requirement 1).
CompleteResult completeFlow(const String& pastedUrlOrCode);

// ---- Runtime token access ----------------------------------------------------

// Returns a valid access token, refreshing proactively at T-300s if needed
// (§5.5). Returns an empty string if refresh failed and no cached token
// remains valid — the caller (spotify/api.cpp) must then treat this as
// AUTH_NEEDED or a transient error per the last refresh outcome.
String getAccessToken();

enum class RefreshOutcome {
  Ok,
  InvalidGrant,   // refresh token dead — MUST NOT retry (§5.5)
  NetworkOrServerError,
};

// Forces a refresh now (boot-time AUTH_CHECK, or reactive on a 401). Never
// loops: at most one retry of the original request per §5.5.
RefreshOutcome refreshNow();

// Seconds remaining before the cached access token expires — for
// GET /healthz (§13.1: "token expiry (seconds remaining only)"). Never
// exposes the token itself (§5.4 requirement 4 / §13.3).
uint32_t accessTokenExpiresInSeconds();

}  // namespace Auth
}  // namespace Spotify
