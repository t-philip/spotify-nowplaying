# Spotify Now-Playing Display (ESP32)
## Firmware Design Specification v1.1 — As-Built

**Target:** ideaspark ESP32 development board, 1.14" ST7789 135×240 TFT, ESP32-WROOM-32, CH340, USB-C
**Framework:** Arduino / PlatformIO
**Author:** T. Philip — <https://github.com/t-philip>
**v1.0:** 28 July 2026 — approved for build
**v1.1:** 30 July 2026 — reconciled against the shipped firmware
**Status:** Built and verified on hardware. This document describes what the firmware
actually does, not only what was planned.

---

## 0. How to use this document

This is an implementation specification, not a tutorial. It defines *what* the firmware must do and the constraints it must respect. Section 18 lists acceptance criteria — the build is not complete until every one passes.

Where this document says **MUST**, the requirement is load-bearing: violating it causes either a broken device, a lost Spotify authorisation, or an API ban risk. Where it says **SHOULD**, deviation is acceptable with a comment in the code explaining why.

Three requirements are non-negotiable and have caused failures in nearly every comparable hobby project. They are flagged **[CRITICAL]** and appear in §5.4, §6.3 and §7.2.

### 0.1 What changed in v1.1

v1.0 was written before implementation. v1.1 is the same document reconciled against the
firmware that was actually built and run, so it can be trusted as a description of the
shipped device. All three **[CRITICAL]** requirements (§5.4 token rotation, §6.3 rate-limit
safety, §7.2 filtered streaming parse) were implemented as specified.

Deviations are listed here in full, including the one requirement that is **not** met.
Each is also commented at its site in the source.

| # | Section | Deviation | Why |
|---|---|---|---|
| 1 | §3.1 | Added `ricmoo/QRCode` to `lib_deps`. | §12.5 requires a QR code on the `AUTH_NEEDED` screen, but v1.0's dependency list contained nothing that could generate one. |
| 2 | §8.1 | `uiTask` is **not** a created task — Arduino's own `loop()` is `uiTask`. | `loop()` already runs pinned to core 1 at priority 1, exactly what the table asked for. Creating a second task to do the same thing would have cost another stack for no benefit. Consequence: `UI_TASK_STACK` / `UI_TASK_PRIORITY` / `UI_TASK_CORE` in `config.h` are vestigial and unreferenced. |
| 3 | §8.1 | Added a **display mutex**; album art is drawn directly by `netTask` rather than handed to `uiTask` as a buffer. | v1.0 assigned art *decode* to `netTask` while all other rendering belonged to `uiTask`, but both drive the same SPI bus and v1.0 specified nothing to serialise them. A mutex is cheaper than a 25 KB ownership handover on a no-PSRAM board. |
| 4 | §11.2, §15, F10 | Album-art size cap raised **48 KB → 96 KB**. | v1.0's 48 KB was an estimate. Measured against the live CDN, a real 300 px-tier cover came back at 60,378 bytes and was rejected — so the placeholder tile showed on *every* track. Free heap measured ~150 KB at the time, so 96 KB preserves the same margin v1.0 was reasoning about, against observed sizes rather than estimated ones. |
| 5 | §12.1 | **Landscape layout added** alongside v1.0's portrait-only geometry, selectable from the dashboard. | Requested after the first build. Geometry moved into `ui/layout.*` instead of being hardcoded. |
| 6 | §13.1 | `/api/settings` also accepts `client_id` and `orientation`. | v1.0 gave the Client ID no route of its own, and orientation did not exist. `/api/settings` doubles as the general PIN-gated settings write. |
| 7 | §13.1 | `/healthz` exposes seven additional album-art diagnostic fields. | Opening the serial port on this board asserts DTR/RTS and **resets it**, so serial is not usable for live diagnosis. HTTP became the only practical channel. No secret is exposed — §13.3 still holds. |
| 8 | §16 | NVS key `orientation` added. | Supports deviation 5. |
| 9 | §2.4 | **NOT IMPLEMENTED: the factory-reset on-screen countdown.** v1.0 says this MUST exist. | The abort-by-release requirement *is* met — releasing the BOOT button before 10 s cancels, and the reset only fires after the full hold. But nothing is drawn during the hold, so the user gets no visual warning. This is a genuine open gap, not a deliberate trade-off. |
| 10 | §12.2 | Continuous-wrap scrolling only; no pause-and-reverse mode. | v1.0 offered both and stated a preference for continuous wrap. `SCROLL_END_PAUSE_MS` is vestigial. |
| 11 | §15 | `HEAP_HEADROOM_TARGET_BYTES` and `WEB_CONTROL_LATENCY_TARGET_MS` are defined but unenforced. | Both are targets to measure against, not runtime limits. The enforced heap guard is F12's `HEAP_MIN_FREE_BYTES`. |

Two findings from hardware bring-up were folded into §11.2 and §11.5. Both cost real
debugging time and neither is discoverable from documentation.

---

## 1. Purpose and scope

### 1.1 In scope

A standalone WiFi device that displays the user's currently-playing Spotify track — album art, title, artist, album, progress — on a 135×240 colour TFT, and exposes transport controls via a web page reachable from any device on the same home network.

### 1.2 Out of scope (v1.0)

- The device is **not** a Spotify Connect playback target. It cannot play audio. It observes and controls playback happening on *other* devices.
- No audio hardware, no microphone, no physical buttons beyond the BOOT button.
- No multi-user support. One device, one Spotify account.
- No cloud service, no external server. The device talks directly to Spotify.

### 1.3 Known account constraints

The target user has **Spotify Premium**. Transport control endpoints (`/play`, `/pause`, `/next`, …) return HTTP 403 on Free accounts; the firmware MUST still handle 403 gracefully (§14) rather than assuming Premium, so the build survives an account change.

Control commands require an **active playback device** to exist. If Spotify reports no active device, control endpoints return 404 with `NO_ACTIVE_DEVICE`. The UI MUST communicate this rather than silently failing.

---

## 2. Hardware target

### 2.1 Board

ESP32-WROOM-32 (dual-core Xtensa LX6 @ 240 MHz), 520 KB SRAM total, ~320 KB usable DRAM, **4 MB flash, no PSRAM**. The absence of PSRAM is the dominant memory constraint and drives §11 (album art) and §12 (rendering strategy).

### 2.2 Display pinout

From the ideaspark official manual for this board:

| Signal | GPIO |
|---|---|
| TFT_MOSI | 23 |
| TFT_SCLK | 18 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | 4 |
| TFT_BL (backlight) | 32 |

**Verification step (do this first):** These pins differ from the widely-copied LilyGO T-Display configuration (MOSI 19, CS 5, DC 16, RST 23, BL 4). Copying the T-Display config produces a blank or white screen — the single most common first-build failure on this board. Before writing any application code, flash a minimal `tft.fillScreen(TFT_RED)` sketch with the pinout above and confirm the panel lights red. If it does not, halt and report; do not proceed to debug at the application layer.

### 2.3 Panel geometry

ST7789 driver, 135×240 visible area, which is a **windowed** region of the controller's 240×320 GRAM. This requires `CGRAM_OFFSET` to be enabled in TFT_eSPI, which applies the correct column/row offsets (52, 40). Without it, the image is displaced and wraps.

Default orientation: **rotation 0** (portrait, 135 wide × 240 tall).

### 2.4 Available input

GPIO0 (BOOT button) is the only user input. Reserved in v1.0 for:
- **Short press (< 800 ms):** cycle backlight brightness through 4 levels.
- **Long press (≥ 3 s):** display the device IP and mDNS name full-screen for 15 s.
- **Very long press (≥ 10 s):** factory reset — wipe NVS, reboot into setup mode. MUST require an on-screen countdown and MUST be abortable by releasing the button.

> **As-built (§0.1 #9) — partially met.** Abort-by-release works: the reset fires only
> after the full 10 s hold, and releasing early cancels it. **The on-screen countdown is
> not implemented** — nothing is drawn while the button is held, so there is no visual
> warning before the wipe. Known gap.

Long-press (≥ 3 s) additionally shows the web PIN alongside the IP and mDNS name, since
both are needed together to actually reach the dashboard.

Transport control is via the web UI (§13), not this button.

---

## 3. Toolchain and dependencies

### 3.1 PlatformIO environment

```ini
[env:ideaspeak]
platform      = espressif32@^6.5.0
board         = esp32dev
framework     = arduino
monitor_speed = 115200
upload_speed  = 921600

; Default 4MB partition table leaves only ~1.2MB for the app.
; TLS + TFT_eSPI + ArduinoJson + JPEG decoder + web server will exceed it.
board_build.partitions = min_spiffs.csv   ; ~1.9MB app partition

build_flags =
  ; ---- TFT_eSPI configuration (avoids editing User_Setup.h) ----
  -DUSER_SETUP_LOADED=1
  -DST7789_DRIVER=1
  -DTFT_WIDTH=135
  -DTFT_HEIGHT=240
  -DCGRAM_OFFSET=1
  -DTFT_MOSI=23
  -DTFT_SCLK=18
  -DTFT_CS=15
  -DTFT_DC=2
  -DTFT_RST=4
  -DTFT_BL=32
  -DTFT_BACKLIGHT_ON=HIGH
  -DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4 -DLOAD_FONT6 -DLOAD_GFXFF
  -DSMOOTH_FONT
  -DSPI_FREQUENCY=40000000
  -DSPI_READ_FREQUENCY=20000000
  ; ---- App ----
  -DCORE_DEBUG_LEVEL=3
  -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1

lib_deps =
  bodmer/TFT_eSPI @ ^2.5.43
  bodmer/TJpg_Decoder @ ^1.1.0
  bblanchon/ArduinoJson @ ^7.0.4
  tzapu/WiFiManager @ ^2.0.17
  ricmoo/QRCode @ ^0.0.1        ; as-built addition — see below
```

**As-built (§0.1 #1):** `ricmoo/QRCode` was added. §12.5 requires a QR code on the
`AUTH_NEEDED` screen and nothing else in this list can produce one.

**As-built:** `upload_port` / `monitor_port` are left commented out so PlatformIO
auto-detects the serial port on a fresh clone. Uncomment and set them only if the wrong
port is picked.

**Note on TFT_eSPI configuration:** passing the setup via `build_flags` (rather than editing the library's `User_Setup.h`) is REQUIRED so the configuration survives a clean checkout and a library reinstall. A `User_Setup.h` edit is not reproducible and will be silently reverted by PlatformIO on dependency update.

### 3.2 Deliberately excluded

- **LVGL** — overkill for six text regions and one image; costs ~40 KB RAM this board cannot spare.
- **AsyncWebServer** — its known stability issues on ESP32 with concurrent TLS are not worth the concurrency benefit for a single-user setup page. Use the synchronous `WebServer` from the core, serviced from the UI task.
- **Any third-party "Spotify for Arduino" wrapper** — these hardcode the deprecated auth flow (§5.1) and hide the token-rotation handling that §5.4 requires. Implement the HTTP directly.

---

## 4. Prerequisites — Spotify app registration

Before first boot the user must, at <https://developer.spotify.com/dashboard>:

1. Create an app. Note the **Client ID**. (The Client Secret is **not** used and MUST NOT be stored on the device — PKCE is a public-client flow.)
2. Set **Redirect URI** to exactly:
   ```
   http://127.0.0.1:8888/callback
   ```
3. Under APIs used, select **Web API**.

The app remains in *development mode*, which is correct and sufficient for personal use — it permits up to 25 registered users and does not require a quota extension request.

### 4.1 Why the redirect URI is a loopback address and not the device

**This is the single most important architectural constraint in the project.**

Spotify tightened redirect-URI validation in April 2025, with full enforcement from 27 November 2025. The rules are now:

- Redirect URIs **MUST use HTTPS**, with exactly one exception;
- the exception is **loopback literals** — `http://127.0.0.1:PORT` or `http://[::1]:PORT`;
- the hostname `localhost` is **rejected outright**.

Therefore `http://192.168.1.x/callback` — the obvious choice of pointing Spotify at the ESP32's own web server — **will be rejected at app-registration time**. Do not attempt it. Do not attempt to work around it with a self-signed HTTPS server on the device either: the browser interstitial makes the UX worse than the copy-paste flow, and it costs ~30 KB of heap the device does not have.

The consequence is the auth flow in §5.2: the browser redirect lands on the *laptop's* loopback, where nothing is listening, and the user transfers the resulting authorisation code to the device by copy-paste. This is a deliberate design choice, not a shortcut.

---

## 5. Authentication design

### 5.1 Flow

**Authorization Code with PKCE.** Not Implicit Grant (deprecated and removed), not Authorization Code with a client secret (requires storing a secret in flash on a physically accessible device — unacceptable).

### 5.2 First-time authorisation sequence

```
 ESP32                          User / Laptop browser              Spotify
   |                                     |                            |
   |-- [1] generate code_verifier -------|                            |
   |    (64 chars, CSPRNG)               |                            |
   |-- [2] challenge = b64url(sha256(v)) |                            |
   |-- [3] state = 16 random bytes ------|                            |
   |                                     |                            |
   |<-- [4] user opens http://ideaspeak.local/setup -----------------|
   |--- [5] page shows "Log in to Spotify" link ------------------->  |
   |                                     |                            |
   |                                     |-- [6] GET /authorize ----->|
   |                                     |<-- [7] consent screen -----|
   |                                     |-- [8] approve ------------>|
   |                                     |<-- [9] 302 to              |
   |                                     |    127.0.0.1:8888/callback |
   |                                     |    ?code=...&state=...     |
   |                                     |                            |
   |                                  [10] browser shows              |
   |                                  "can't connect" — EXPECTED      |
   |                                  User copies the FULL URL        |
   |                                  from the address bar            |
   |                                     |                            |
   |<-- [11] POST /auth/complete {url} --|                            |
   |-- [12] verify state matches --------|                            |
   |-- [13] POST /api/token (code + verifier) ---------------------->|
   |<-- [14] access_token, refresh_token -----------------------------|
   |-- [15] persist refresh_token to NVS (A/B write, §5.4)            |
   |-- [16] redirect user to dashboard --|                            |
```

**UX requirement for step 10:** the setup page MUST warn the user *in advance* that the browser will show a connection error, and that this is the expected and correct outcome. Without this warning users assume the flow has failed. The page MUST accept either the full pasted URL or a bare `code=` value, and MUST strip whitespace and surrounding quotes before parsing.

**Optional convenience (SHOULD implement, ship as `tools/spotify_login_helper.py`):** a ~25-line Python script the user can run on the laptop that listens on `127.0.0.1:8888`, captures the redirect, and POSTs the code straight to `http://ideaspeak.local/auth/complete`. This removes the copy-paste step entirely. It MUST be optional — the copy-paste path is the supported baseline and must work with no software installed.

### 5.3 PKCE parameter generation

**code_verifier**
- Length 64 characters.
- Alphabet: `A–Z a–z 0–9 - . _ ~` (RFC 7636 unreserved set).
- Source: `esp_random()` (hardware RNG). **MUST NOT** use `random()` or `rand()`.
- Rejection-sample to avoid modulo bias across the 66-character alphabet.
- Held in RAM only, for the lifetime of one auth attempt. Zeroise after use. Expire after 10 minutes and require restart of the flow.

**code_challenge**
- `BASE64URL( SHA256( ASCII(code_verifier) ) )`, no `=` padding, `+`→`-`, `/`→`_`.
- Use mbedTLS (`mbedtls/sha256.h`), already present in the ESP32 Arduino core.

**state**
- 16 bytes from `esp_random()`, hex-encoded. Compared in constant time on return. Mismatch MUST abort the flow with a distinct error message.

**Authorisation URL**
```
https://accounts.spotify.com/authorize
  ?client_id={CLIENT_ID}
  &response_type=code
  &redirect_uri=http%3A%2F%2F127.0.0.1%3A8888%2Fcallback
  &code_challenge_method=S256
  &code_challenge={CHALLENGE}
  &state={STATE}
  &scope=user-read-playback-state%20user-read-currently-playing%20user-modify-playback-state
```

**Scopes** — request exactly these three, no more:

| Scope | Needed for |
|---|---|
| `user-read-currently-playing` | track metadata |
| `user-read-playback-state` | device, shuffle, repeat, volume |
| `user-modify-playback-state` | transport controls |

### 5.4 [CRITICAL] Refresh token rotation and A/B persistence

Spotify's PKCE flow uses **refresh token rotation**: each successful refresh MAY (and usually does) return a **new** `refresh_token`, and the previously held token is invalidated. Rotation exists because public clients cannot hold a secret, so a leaked token must be detectable.

The failure mode this creates: the firmware refreshes, receives a new refresh token, does not persist it, then reboots — and the token in NVS is now dead. The device silently loses authorisation and the user must repeat the whole browser flow. This is the most common defect in comparable projects.

**Requirements:**

1. On **every** token response, if a `refresh_token` field is present, the firmware **MUST** persist it before the new access token is used for any API call.
2. Persistence **MUST** be crash-safe using an A/B slot scheme, so that a power loss mid-write cannot destroy both the old and new token:

```
NVS keys:
  rt_a      : string   refresh token, slot A
  rt_b      : string   refresh token, slot B
  rt_seq_a  : uint32   monotonic sequence number for slot A
  rt_seq_b  : uint32   monotonic sequence number for slot B
  rt_crc_a  : uint32   CRC32 of rt_a
  rt_crc_b  : uint32   CRC32 of rt_b

Write algorithm:
  active   = slot with the higher valid seq
  inactive = the other slot
  1. write token   -> inactive
  2. write crc32   -> inactive
  3. commit
  4. write seq = active_seq + 1 -> inactive     (this is the atomic switch)
  5. commit

Read algorithm (boot):
  for each slot: valid = (crc32(token) == stored crc)
  choose the valid slot with the higher seq
  if neither valid -> enter AUTH_NEEDED state
```

3. NVS write frequency: at most one rotation per access-token lifetime (3600 s). Approximately 8,760 writes/year. NVS wear levelling handles this comfortably; no additional throttling needed.
4. The refresh token **MUST NOT** be exposed through any HTTP route, log line, or serial output. Serial logging of tokens MUST be compiled out in release builds. Where a token must be logged for debugging, log only the first 6 characters and the length.

### 5.5 Token refresh

```
POST https://accounts.spotify.com/api/token
Content-Type: application/x-www-form-urlencoded

grant_type=refresh_token&refresh_token={RT}&client_id={CLIENT_ID}
```

- Refresh proactively at **T-300 s** before `expires_in` elapses, not reactively on 401. This avoids a user-visible stall.
- Also refresh reactively on any 401, then retry the original request **exactly once**. Never loop.
- On refresh failure with `400 invalid_grant`: the refresh token is dead. Clear both NVS slots, transition to `AUTH_NEEDED`, display re-authorisation instructions. **MUST NOT** retry — retrying a dead grant in a loop is exactly the pattern that gets an app flagged.
- On refresh failure with a network or 5xx error: exponential backoff (5 s, 15 s, 45 s, 120 s, cap 300 s), keep serving stale display data with a "reconnecting" indicator.

---

## 6. Polling strategy and rate-limit safety

### 6.1 Rate-limit model

Spotify computes rate limits over a **rolling 30-second window**, with the ceiling depending on whether the app is in development or extended-quota mode. Spotify does not publish the exact figure; community testing indicates roughly **180 requests/minute** is sustainable in development mode without triggering 429.

Exceeding it returns **HTTP 429** with a `Retry-After` header in seconds. Repeatedly ignoring `Retry-After` escalates the penalty duration.

### 6.2 Poll cadence

| State | Interval | Requests/min |
|---|---|---|
| Playing | 5 s | 12 |
| Paused | 30 s | 2 |
| Nothing playing (204) | 60 s | 1 |
| Idle > 15 min | 180 s | 0.3 |
| `AUTH_NEEDED` / no WiFi | no polling | 0 |

Peak sustained load is **12 req/min — approximately 7% of the observed ceiling**. This leaves ample headroom for control-command bursts.

The poll interval MUST be user-configurable via the web UI within a range of **3–60 s**, with a **hard floor of 3 s enforced in firmware** that no configuration value can override.

### 6.3 [CRITICAL] Rate-limit safety mechanisms

All four are required. Individually each is bypassable by a bug; together they make an accidental request storm structurally impossible.

**(a) Token bucket — hard ceiling.** Every outbound Spotify request MUST acquire a token first.
- Capacity: 8 tokens (allows a burst of control commands).
- Refill: 1 token per 3.5 s → ~17 req/min sustained.
- If the bucket is empty, the request is **dropped, not queued**. Dropping a poll is harmless; a queue that drains at reconnection is a request storm.

**(b) `Retry-After` honoured across reboot.** On 429, store `now + Retry-After` as an epoch value in NVS (`retry_until`). Check it on boot before the first request. Without this, a crash-loop bug — the device rebooting every few seconds — becomes an unthrottled request flood against Spotify, which is precisely the behaviour that gets a client blocked.

**(c) Adaptive cool-down.** After any 429, double the base poll interval for 10 minutes, then decay back by 25% per successful poll. Persist nothing except `retry_until`; the multiplier is in-RAM and resets on reboot (bounded by (b)).

**(d) Circuit breaker.** 5 consecutive 5xx responses → suspend all Spotify traffic for 5 minutes and display a "Spotify unreachable" state. Reset the counter on any 2xx.

### 6.4 Local progress extrapolation

Between polls, the progress bar MUST advance from `millis()` rather than from network data:

```
displayed_progress_ms = progress_ms_at_last_poll
                      + (millis() - millis_at_last_poll)   [if is_playing]
clamp to [0, duration_ms]
```

This gives a smooth 4 Hz progress bar at 0.2 req/s. When `displayed_progress_ms` reaches `duration_ms`, schedule an **immediate** poll (subject to the token bucket) — the track has almost certainly changed and this makes track transitions feel instant without raising the average rate.

Re-synchronise to the authoritative `progress_ms` on every poll. If drift exceeds 2 s, snap rather than ease — the user perceives a wrong number as a bug, but a jump as a correction.

---

## 7. Spotify API contract

### 7.1 Read endpoint

```
GET https://api.spotify.com/v1/me/player
Authorization: Bearer {access_token}
```

Preferred over `/v1/me/player/currently-playing` because it additionally returns device, shuffle, repeat and volume state in the same request — needed by the web UI, and one request is cheaper than two.

**Response handling:**

| Status | Meaning | Action |
|---|---|---|
| 200 | Playback state present | Parse, update display |
| **204** | No active device / nothing playing | **Not an error.** Enter IDLE screen. Do not retry aggressively. |
| 401 | Access token expired | Refresh (§5.5), retry once |
| 403 | Scope or account restriction | Show persistent error, disable controls |
| 429 | Rate limited | §6.3(b) |
| 5xx | Spotify problem | §6.3(d) |

204 returning an **empty body** is a frequent parsing crash: guard on `Content-Length == 0` before deserialising.

### 7.2 [CRITICAL] JSON parsing — filtered streaming

A full `/v1/me/player` response is **8–20 KB**, occasionally more for tracks with many artists or large `available_markets` arrays. Buffering it into a `String` and then parsing it will fragment and exhaust the heap on a no-PSRAM ESP32, especially while a TLS session is open. This is the second most common defect in comparable projects.

The firmware **MUST** use ArduinoJson's filtered deserialisation, reading **directly from the `WiFiClientSecure` stream**, never via an intermediate buffer:

```cpp
// ArduinoJson 7: use JsonDocument for both. StaticJsonDocument is deprecated.
JsonDocument filter;
filter["is_playing"]             = true;
filter["progress_ms"]            = true;
filter["currently_playing_type"] = true;
filter["shuffle_state"]          = true;
filter["repeat_state"]           = true;
filter["device"]["name"]           = true;
filter["device"]["volume_percent"] = true;
filter["device"]["is_active"]      = true;
filter["item"]["name"]        = true;
filter["item"]["duration_ms"] = true;
filter["item"]["explicit"]    = true;
filter["item"]["album"]["name"]                  = true;
filter["item"]["album"]["images"][0]["url"]      = true;
filter["item"]["album"]["images"][0]["width"]    = true;
filter["item"]["artists"][0]["name"]             = true;

JsonDocument doc;                      // ArduinoJson 7, elastic
DeserializationError err = deserializeJson(
    doc, client, DeserializationOption::Filter(filter));
```

Post-filter the retained document is **2–3 KB**. Copy the needed values into a fixed `PlayerState` struct immediately and let `doc` go out of scope before any further allocation.

Truncation limits, applied on copy-in:

| Field | Max stored | Note |
|---|---|---|
| Track name | 128 bytes | UTF-8; MUST NOT split a multi-byte sequence |
| Artist names | 128 bytes | Join first 3 artists with `", "`, append `" +N"` if more |
| Album name | 96 bytes | |
| Device name | 32 bytes | |
| Album art URL | 160 bytes | |

**UTF-8 handling:** track and artist names routinely contain non-ASCII characters (accents, CJK, emoji). Truncation MUST be codepoint-aware. Rendering: the default TFT_eSPI fonts are Latin-1 only. Characters outside the font MUST render as a placeholder glyph rather than as garbage or a crash. Document this as a known v1.0 limitation; a smooth font with wider coverage is a v1.1 candidate.

### 7.3 Control endpoints

All require `user-modify-playback-state` and an active device. All return **204** on success.

| Action | Request |
|---|---|
| Play / resume | `PUT /v1/me/player/play` |
| Pause | `PUT /v1/me/player/pause` |
| Next | `POST /v1/me/player/next` |
| Previous | `POST /v1/me/player/previous` |
| Seek | `PUT /v1/me/player/seek?position_ms={ms}` |
| Volume | `PUT /v1/me/player/volume?volume_percent={0-100}` |
| Shuffle | `PUT /v1/me/player/shuffle?state={true\|false}` |
| Repeat | `PUT /v1/me/player/repeat?state={track\|context\|off}` |

`PUT` requests with no body MUST send `Content-Length: 0`. Omitting it causes some HTTP stacks to hang awaiting a body.

**Error handling:**

| Status | Meaning | UI response |
|---|---|---|
| 404 `NO_ACTIVE_DEVICE` | Nothing to control | "Start playback on a device first" |
| 403 | Non-Premium, or restricted action | "Not permitted on this account" — disable controls for the session |
| 403 on prev/next | Ad playing, or start of queue | "Not available right now" |

**Optimistic UI with reconciliation:** on a successful control command, apply the expected state locally and immediately, then schedule reconciliation polls at **+700 ms** and **+2000 ms**. Spotify's state propagation is eventually consistent — a single immediate poll frequently returns the *pre-command* state and causes the UI to visibly bounce back. Both reconciliation polls draw from the token bucket; the burst capacity of 8 accommodates this.

### 7.4 Album art source

Album art URLs come from `item.album.images[]`, an array ordered **largest first**, typically 640, 300 and 64 px square, served from `i.scdn.co`.

Selection: choose the entry whose `width` is closest to **300** without going below 150. Fall back to the smallest available if none qualifies.

**These URLs expire.** Do not persist them across reboots expecting them to remain valid; always refetch from a fresh API response.

---

## 8. System architecture

### 8.1 Task model

Two FreeRTOS tasks, pinned, communicating through one mutex-protected state struct.

| Task | Core | Stack | Priority | Responsibility |
|---|---|---|---|---|
| `netTask` | 0 | 8192 B | 2 | WiFi, NTP, TLS, token lifecycle, Spotify polling, art download + decode |
| `uiTask` | 1 | 4096 B | 1 | Display rendering, text scrolling, BOOT button, `WebServer` service loop |

Rationale for the split: TLS handshakes block for 300–900 ms. On a single-task design this freezes text scrolling and stalls the web UI, which reads as a hung device. Pinning network work to core 0 (alongside the WiFi driver) and rendering to core 1 keeps the display fluid.

**As-built (§0.1 #2): only `netTask` is created.** Arduino's `loop()` already runs pinned to
core 1 (`APP_CPU`) at priority 1 — that *is* `uiTask`, so creating a second task with the
same properties would only cost another stack. `UI_TASK_STACK` / `UI_TASK_PRIORITY` /
`UI_TASK_CORE` therefore exist in `config.h` but are never referenced; `loop()` runs on the
Arduino core's own loop-task stack. Both tasks are registered with the task watchdog
(§14 F15) — `netTask` only *after* `WifiMgr::begin()`, because WiFiManager's captive portal
can legitimately block for the full 300 s portal timeout and would otherwise trip it during
ordinary first-time setup.

**As-built (§0.1 #3): a display mutex serialises all panel access.** v1.0 assigned art
decode to `netTask` and everything else to `uiTask`, but both drive the same SPI bus and
nothing in v1.0 serialised them. `Display::lock()` / `unlock()` (timeout
`MUTEX_TIMEOUT_MS`) now guards every write from either task.

**Shared state:**

```cpp
struct PlayerState {
  bool     valid;
  bool     is_playing;
  char     track[129];
  char     artist[129];
  char     album[97];
  char     device[33];
  char     art_url[161];
  uint32_t duration_ms;
  uint32_t progress_ms;        // authoritative, from last poll
  uint32_t progress_epoch_ms;  // millis() at last poll
  bool     shuffle;
  uint8_t  repeat;             // 0=off 1=context 2=track
  uint8_t  volume;
  bool     explicit_flag;
  uint32_t revision;           // incremented on every change
};
```

`revision` drives the dirty-region renderer (§12.4): `uiTask` compares the revision it last drew against the current one and redraws only changed regions. `netTask` writes under the mutex; `uiTask` takes a snapshot copy under the mutex and releases it before rendering — **MUST NOT** hold the mutex during SPI writes.

**As-built:** album art is **not** handed to `uiTask` as a buffer. `netTask` decodes and
draws it directly under the display mutex, then frees the buffer immediately. This avoids
the ownership handover entirely and keeps only one copy of the JPEG alive at any moment —
which matters more on a no-PSRAM board than task purity does.

One consequence found on hardware: the art-fetch check must run against whatever
`PlayerStateStore` currently holds, on its own loop iteration — **not** inside the
"poll returned Updated" branch comparing against a URL captured earlier in that same
iteration. Reconciliation polls (§7.3) update the player state on their own schedule
~700 ms / 2000 ms after a control command, so the before/after comparison in the poll
branch had already gone stale by the time the next due poll ran, and the art fetch for
that track was skipped permanently. Symptom: pressing Next left the previous cover on
screen indefinitely. Related: returning to `IDLE_NO_PLAYBACK` blanks the panel, so both
the loop's URL tracker and `art_fetch`'s own cache must be invalidated on resume,
otherwise the fetch short-circuits to `Unchanged` and draws nothing.

### 8.2 Module structure

```
src/
  main.cpp              task creation, setup, watchdog
  config.h              compile-time constants, feature flags
  app_state.cpp/.h      AppState machine + diagnostics snapshot (as-built)
  net/
    wifi_mgr.cpp/.h     provisioning, reconnect with backoff
    time_sync.cpp/.h    NTP, timezone, TLS-readiness gate
    tls_client.cpp/.h   WiFiClientSecure wrapper, CA bundle, connection reuse
    http.cpp/.h         minimal request/response helper, header parsing
  spotify/
    auth.cpp/.h         PKCE generation, code exchange, refresh, NVS A/B
    api.cpp/.h          player read, control commands, status mapping
    ratelimit.cpp/.h    token bucket, Retry-After, circuit breaker
    model.h             PlayerState
  art/
    art_fetch.cpp/.h    conditional download, size guard
    art_decode.cpp/.h   TJpg_Decoder integration, clipped rendering
  ui/
    display.cpp/.h      TFT init, backlight PWM, display mutex (as-built)
    layout.cpp/.h       portrait/landscape geometry (as-built, §0.1 #5)
    screens.cpp/.h      screen state machine, layouts
    widgets.cpp/.h      scrolling text, progress bar, status row
    button.cpp/.h       BOOT button press classification (as-built)
    fonts.h
  web/
    server.cpp/.h       routes
    pages.h             embedded HTML/CSS/JS as PROGMEM
  store/
    nvs.cpp/.h          typed NVS access, A/B token slots
  util/
    b64url.cpp/.h
    crc32.cpp/.h
    utf8.cpp/.h         codepoint-aware truncation
tools/
  spotify_login_helper.py
```

---

## 9. Application state machine

```
        ┌──────┐
        │ BOOT │
        └──┬───┘
           v
    ┌──────────────┐  no creds   ┌─────────────┐
    │  WIFI_INIT   ├────────────>│ WIFI_SETUP  │ (SoftAP "IdeaSpeak-Setup")
    └──────┬───────┘             └──────┬──────┘
           │ connected                  │ creds saved
           v                            v  (reboot)
    ┌──────────────┐
    │  TIME_SYNC   │  NTP; blocks TLS until epoch > 2020-01-01
    └──────┬───────┘
           v
    ┌──────────────┐  no valid RT   ┌──────────────┐
    │  AUTH_CHECK  ├───────────────>│ AUTH_NEEDED  │
    └──────┬───────┘                └──────┬───────┘
           │ RT valid                      │ auth completed
           v                               │
    ┌──────────────┐<─────────────────────-┘
    │   RUNNING    │
    └──┬───┬───┬───┘
       │   │   └─────> ERROR_TRANSIENT  (backoff, retry, stale display)
       │   └─────────> ERROR_FATAL      (invalid_grant → AUTH_NEEDED)
       └─────────────> RATE_LIMITED     (until retry_until)

  RUNNING sub-states: PLAYING | PAUSED | IDLE_NO_PLAYBACK
```

**WIFI_SETUP:** WiFiManager captive portal, SoftAP SSID `IdeaSpeak-Setup`, password printed on the display. Portal timeout 300 s, then reboot and retry the stored credentials (handles a router that was merely slow to come back).

**TIME_SYNC is mandatory before any TLS.** X.509 validation requires a correct clock; the ESP32 boots at epoch 0 and every certificate will appear not-yet-valid. Gate all TLS on `time(nullptr) > 1577836800`. Timeout 30 s, then retry with backoff while displaying "Syncing time". NTP: `pool.ntp.org`, `time.nist.gov`. Default timezone is the POSIX TZ string
`CET-1CEST,M3.5.0,M10.5.0/3` (Central European Time, with EU DST rules) — change
`DEFAULT_TZ` in `config.h`, or the `tz` NVS key, for another region.

---

## 10. Networking and TLS

### 10.1 Certificate validation

Three hosts, potentially three different certificate authorities:

| Host | Purpose | Sensitivity |
|---|---|---|
| `accounts.spotify.com` | token exchange, refresh | **High** — carries the refresh token |
| `api.spotify.com` | player state, control | **High** — carries the bearer token |
| `i.scdn.co` | album art | Low — no credentials sent |

**Preferred approach:** use the ESP32 Arduino core's built-in root CA bundle.

```cpp
#include "esp_crt_bundle.h"
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_bin_start");
client.setCACertBundle(rootca_crt_bundle_start);
```

**Verify the exact linker symbol name against the arduino-esp32 core version in use** — it has changed between core releases (`_binary_x509_crt_bundle_start` vs `_binary_x509_crt_bundle_bin_start`). A mismatch produces a link error, not a runtime failure, so it will surface immediately at build time.

**As-built:** the shorter name, `_binary_x509_crt_bundle_start`, is correct for
`espressif32@6.13.0` / `framework-arduinoespressif32@3.20017` — confirmed the hard way by
an actual link failure, then verified with `xtensa-esp32-elf-nm` against the SDK's prebuilt
`libmbedtls.a`. If a future core bump breaks this it will again be a build-time link error;
re-run the same `nm` check against the new core. The include is guarded with
`__has_include("esp_crt_bundle.h")`, and if the bundle is unavailable the credential-bearing
hosts **refuse to connect** rather than silently downgrading to `setInsecure()`.

This survives CA rotation, which a single pinned certificate does not — a pinned cert will break the device without warning when Spotify rotates issuers.

**Fallback if the bundle is unavailable in the selected core version:** pin the root CA for the two Spotify hosts. For `i.scdn.co` only, `setInsecure()` is acceptable — no credential is transmitted and the worst case of a MITM is a wrong album image. This trade-off MUST be documented in a code comment; it MUST NOT be applied to the credential-bearing hosts.

### 10.2 Connection management

- One TLS connection at a time. **Never** open the art download while a Spotify API connection is live — the combined handshake buffers will exhaust the heap.
- `client.setTimeout(8000)`. Guard every read with a deadline; a half-open socket must not stall the task indefinitely.
- Explicitly `client.stop()` in all paths including error paths. Leaked TLS contexts are the classic slow heap leak, presenting as a device that works for hours then reboots.
- **SHOULD** reuse the connection to `api.spotify.com` across polls (HTTP keep-alive) — this saves ~40 KB of transient handshake heap and ~500 ms per poll. Recycle the connection every 50 requests or 10 minutes, whichever comes first, and fall back to a fresh connection on any read error.

### 10.3 WiFi resilience

- `WiFi.setSleep(false)` — modem sleep introduces multi-second latency spikes that make polling unreliable.
- `WiFi.setAutoReconnect(true)`, plus an explicit supervisor: if disconnected > 30 s, `WiFi.disconnect(); WiFi.begin()`. If disconnected > 10 min, reboot.
- On disconnect, keep displaying the last known track dimmed with a WiFi-off icon. Do not blank the screen — a device that goes dark reads as dead.

---

## 11. Album art pipeline

### 11.1 Constraints

135×135 px target on a device with no PSRAM and ~150–190 KB free heap while WiFi is active.

### 11.2 Pipeline

```
1. Compare new art_url against cached art_url.
   Identical -> skip entirely. (Typical case: same album, many tracks.)

2. Streamed GET on i.scdn.co (a User-Agent header is mandatory — see §11.5).
   If Content-Length > 96 KB  -> abort, use placeholder.
   If Content-Length missing  -> stream with a hard 96 KB cap.

3. malloc the exact Content-Length. If malloc fails -> placeholder, log heap.
   Download fully into the buffer. Close TLS. Free the TLS client.

4. TJpg_Decoder:
     TJpgDec.setJpgScale(2);        // 300x300 -> 150x150
     TJpgDec.setSwapBytes(true);
     tft.setViewport(0, 0, 135, 135);   // hardware clip
     TJpgDec.drawJpg(-7, -7, buf, len); // centre-crop 150 -> 135
     tft.resetViewport();

5. free(buf) in every path, including decode failure.
```

**As-built (§0.1 #4): the cap is 96 KB, not 48 KB.** v1.0's 48 KB was an estimate against an
assumed 150–190 KB heap. In practice a real 300 px-tier cover from the CDN measured
**60,378 bytes** — over the cap, so every track fell through to the placeholder tile.
On-device `/healthz` reported ~150 KB free at that moment, so 96 KB keeps the margin v1.0
was reasoning about while accommodating sizes actually observed. §15's budget row and F10
are updated to match.

`setJpgScale` only **downscales** (1, 2, 4, 8). This is why the 300 px image is fetched rather than the 64 px one: 300÷2 = 150, cropped to 135, is sharp. Upscaling 64→135 is visibly blocky.

The `setViewport` call performs the crop in hardware — no CPU cost and no risk of writing outside the art region.

### 11.3 Placeholder

On any failure (oversized, malloc fail, decode error, 404), render a deterministic placeholder: a solid tile whose hue derives from `crc32(album_name) % 360`, with the album's first character centred in white. Deterministic means the same album always yields the same tile, which reads as intentional design rather than as an error.

### 11.4 Caching

In-RAM only: the current `art_url` string. Persisting decoded art to flash is explicitly **out of scope** — the write amplification is not worth it for a 135×135 image that redownloads in under a second.

### 11.5 Two findings from hardware bring-up

Neither of these is discoverable from documentation, and both cost real debugging time.
They are recorded here because anything built on the same libraries will hit them.

**(a) `TJpg_Decoder`'s output callback MUST always return `true`.** Returning `false` for
rows that fall outside the viewport looks like an obvious optimisation. It is not:
`tjpgd.c` treats a `false` return as `JDR_INTR` — *abort the entire decode* — not *skip
this block*. Because the centre-crop (`drawJpg(-7, -7, …)`) always pushes some rows outside
the 135×135 viewport, this aborted decoding partway through nearly every image and left
partially-decoded diagonal-stripe output on the panel. TFT_eSPI's active viewport already
clips out-of-bounds pixels at no cost, so the callback should simply always return `true`
and let the viewport do the work.

**(b) `i.scdn.co` will not respond without a `User-Agent` header.** TLS connect succeeded
and the request write succeeded — the CDN edge simply never replied, which presented as a
read timeout and looked like a network or heap fault. The identical URL fetched instantly
from a browser and from `curl`. Sending any `User-Agent` fixes it.

---

## 12. Display design

### 12.1 Geometry — NOW_PLAYING (rotation 0, 135×240)

```
┌───────────────────────────────┐  y=0
│                               │
│         ALBUM ART             │
│          135×135              │
│                               │
├───────────────────────────────┤  y=135
│  Track title            (24)  │  scrolls if wider than 131 px
├───────────────────────────────┤  y=159
│  Artist(s)              (20)  │  scrolls if needed
├───────────────────────────────┤  y=179
│  Album                  (18)  │  static, ellipsised
├───────────────────────────────┤  y=197
│  1:23 ▓▓▓▓▓░░░░░░░░ 3:45 (18) │  progress
├───────────────────────────────┤  y=215
│  ▶ ⇄ ↻   Kitchen     ((( 25)) │  status row
└───────────────────────────────┘  y=240
```

Left/right margin 2 px. Track title 4 px above baseline of its band.

**As-built (§0.1 #5): a landscape layout was added.** Portrait (rotation 0) remains the
default and the geometry above is unchanged. All band positions moved out of hardcoded
constants into `ui/layout.*`, which computes them for the active orientation, so
`Layout::screenW()` / `screenH()` are the source of truth rather than `SCREEN_W` /
`SCREEN_H` directly. Orientation is chosen from the dashboard and stored in NVS
(§16); **applying it reboots the device**, because the TFT rotation and every widget's
geometry are only ever initialised once at boot, and a save-and-reboot is simpler and
safer than re-laying-out live.

**Typography:** track title — `FreeSansBold9pt7b` or TFT_eSPI font 4. Artist — font 2 bold. Album and times — font 2, dimmed (`0x8410` mid-grey). Status glyphs — font 2 or small custom bitmaps.

**Colour:** background `TFT_BLACK`. Primary text `TFT_WHITE`. Secondary `0xBDF7`. Tertiary `0x8410`. Progress fill: Spotify green `#1DB954` → RGB565 **`0x1DCA`** (R 29→3, G 185→46, B 84→10). Progress track `0x2965`.

### 12.2 Scrolling text

Titles longer than the 131 px content width scroll horizontally.
- Render the text once into a `TFT_eSprite` of width `text_width + 40` (gap), height = band height.
- Push a moving window of that sprite at 30 fps, 1 px per frame.
- Pause 1500 ms at each end before reversing, or use continuous wrap with a 40 px gap — continuous wrap is preferred, it is less distracting.
- **As-built (§0.1 #10):** continuous wrap only; the pause-and-reverse mode was not built and
  `SCROLL_END_PAUSE_MS` is vestigial. TFT_eSPI has no windowed/wrapped sprite push, so the
  wrap is done with a row-by-row copy out of a circular sprite buffer.
- **MUST** free the sprite when the track changes. A leaked sprite is 2–8 KB and accumulates per track change.

### 12.3 Progress bar

- Track: 2 px inset, height 4 px, full content width, rounded ends.
- Fill width = `content_width × displayed_progress / duration`.
- Update at 4 Hz from the extrapolation in §6.4.
- Redraw **only the delta region**, not the whole bar — a full redraw at 4 Hz produces visible flicker on SPI at 40 MHz.

### 12.4 Dirty-region rendering

Full-screen redraws MUST NOT occur on every update. A full 135×240 16-bit sprite is 64,800 bytes — allocatable on this board but leaves too little for TLS.

Instead: per-region sprites (largest is the 135×24 title band at 6,480 bytes), each redrawn only when its source data changes, keyed on `PlayerState.revision` plus per-field comparison. At most one region sprite exists at a time; allocate, push, free.

### 12.5 Other screens

| Screen | Content |
|---|---|
| BOOT | Logo/name, firmware version, 1.5 s |
| WIFI_SETUP | "Connect to WiFi: IdeaSpeak-Setup", password, then portal IP |
| TIME_SYNC | "Syncing time…" with spinner |
| AUTH_NEEDED | "Setup required", `http://ideaspeak.local`, plus the IP as fallback, and a QR code of that **short local URL** |
| IDLE_NO_PLAYBACK | Large clock (HH:MM), date, "Nothing playing", dimmed |
| RATE_LIMITED | "Spotify is throttling — resuming in Ns" with countdown |
| ERROR | Short human-readable cause + the local URL |

**As-built:** the QR code is generated with `ricmoo/QRCode` (§0.1 #1). v1.0 required the QR
but listed no library able to produce one.

**QR code note:** encode only `http://ideaspeak.local` (23 chars, QR version 2, comfortably scannable at 100×100 px on this panel). **Do not** attempt to QR-encode the ~450-character Spotify authorisation URL — it requires QR version 16+, which is unscannable at this physical size. The Spotify link belongs on the web page, not on the display.

### 12.6 Backlight

GPIO32 via LEDC PWM, 5 kHz, 8-bit. Four levels: 15%, 40%, 70%, 100%. Default 70%. Persisted in NVS. Cycled by BOOT short-press.

**SHOULD:** dim to 15% after 30 minutes in `IDLE_NO_PLAYBACK`, restore instantly on playback resuming. OLED-style burn-in is not a concern on TFT, but a bright screen in a dark room at night is.

---

## 13. Web UI

Served on port 80 by the synchronous `WebServer`, polled from `uiTask`. Discoverable at **`http://ideaspeak.local`** via mDNS (`MDNS.begin("ideaspeak")`), with the raw IP always shown on the device display as a fallback for clients where mDNS resolution fails.

### 13.1 Routes

| Method | Path | Auth | Purpose |
|---|---|---|---|
| GET | `/` | — | Dashboard: now playing, controls, status |
| GET | `/api/now` | — | JSON state, polled by the page at 1 Hz |
| POST | `/api/control` | PIN | `{action, value}` |
| GET | `/setup` | PIN | WiFi, Client ID, Spotify login |
| GET | `/auth/start` | PIN | Generate PKCE, return the authorisation URL |
| POST | `/auth/complete` | PIN | Accept pasted redirect URL or bare code |
| POST | `/api/settings` | PIN | Poll interval, brightness, art on/off, **`client_id`**, **`orientation`** |
| POST | `/api/reset` | PIN | Wipe NVS, reboot |
| GET | `/healthz` | — | Uptime, free heap, WiFi RSSI, token expiry (seconds remaining only), request rate, **art diagnostics** |

**As-built (§0.1 #6):** v1.0 gave the Spotify Client ID no route of its own, so
`/api/settings` doubles as the general PIN-gated settings write and accepts `client_id`
alongside the fields above. It also accepts `orientation`; changing that value returns
`{"rebooting":true}` and restarts the device (§12.1).

**As-built (§0.1 #7):** `/healthz` additionally reports `app_state`, `retry_until_epoch`,
`now_epoch`, `auth_needed`, and seven album-art fields (`last_art_fetch_result`,
`last_art_len`, `last_art_decode_ok`, `last_art_connect_ok`, `last_art_send_ok`,
`last_art_http_status`, `last_art_content_length`). These exist because **opening the serial
port on this board asserts DTR/RTS and resets it**, so serial is unusable for observing a
live fault — during bring-up that behaviour also manufactured a false "stuck on Connecting
to WiFi" symptom. HTTP is the only practical diagnostic channel. None of these fields is a
secret and §13.3 is unaffected. `/healthz` is deliberately unauthenticated, so it must
remain LAN-only like the rest of the device.

`/api/now` MUST be served from the cached `PlayerState` — it **MUST NOT** trigger a Spotify request. Otherwise every open browser tab multiplies the API load, which defeats §6 entirely.

### 13.2 Page design

Single self-contained HTML page in PROGMEM, inline CSS and JS, no CDN dependencies (the device may have no internet path for the browser, and offline assets must work). Mobile-first, dark theme matching the device. Large touch targets — this will mostly be used from a phone.

Elements: album art (`<img>` pointing at the Spotify CDN URL — the browser fetches it, not the ESP32), track/artist/album, seekable progress bar, prev/play-pause/next, volume slider, shuffle and repeat toggles, active device name, and a collapsed diagnostics section (heap, RSSI, uptime, poll interval, last API status, rate-limit state).

Control latency target: **< 400 ms** from tap to Spotify acknowledgement.

### 13.3 Security

The device sits on a trusted home LAN, but "trusted" is not "unauthenticated".

- A **6-digit PIN**, generated randomly on first boot and displayed on the TFT during setup, stored in NVS. Required as an `X-IdeaSpeak-PIN` header on all mutating routes. The dashboard stores it in `localStorage` after first entry.
- Rate-limit PIN attempts: 5 failures → 60 s lockout. Prevents trivial brute force from a compromised device on the LAN.
- The refresh token, access token and PIN **MUST NOT** appear in any response body, including `/healthz` and any error page.
- CSRF: require the custom `X-IdeaSpeak-PIN` header on all mutating routes. A custom header cannot be sent by a simple cross-origin form post, which makes this sufficient here.
- README **MUST** state that this device must not be port-forwarded or exposed to the internet.

---

## 14. Failure modes

| # | Failure | Detection | Response | User-visible |
|---|---|---|---|---|
| F1 | WiFi credentials wrong | Connect timeout 30 s | SoftAP portal | "WiFi setup needed" + AP name |
| F2 | WiFi drops mid-run | `WiFi.status()` | Backoff reconnect; reboot at 10 min | Dimmed last track + WiFi-off icon |
| F3 | NTP fails | Epoch < 2020 after 30 s | Retry with backoff; block TLS | "Syncing time…" |
| F4 | Refresh token invalid (`invalid_grant`) | 400 on refresh | Clear both NVS slots → `AUTH_NEEDED` | "Re-authorisation needed" + URL |
| F5 | Access token expired | 401 | Refresh, retry once | None (transparent) |
| F6 | Rate limited | 429 | §6.3(b) + (c) | Countdown screen |
| F7 | Nothing playing | 204 | IDLE screen, 60 s poll | Clock + "Nothing playing" |
| F8 | No active device on control | 404 `NO_ACTIVE_DEVICE` | Reject command | "Start playback on a device first" |
| F9 | Non-Premium restriction | 403 | Disable controls for the session | "Not permitted on this account" |
| F10 | Album art too large | `Content-Length` > 96 KB (§0.1 #4) | Skip download | Placeholder tile |
| F11 | Art decode fails | TJpgDec error | Free buffer | Placeholder tile |
| F12 | Heap exhaustion | `ESP.getFreeHeap()` < 30 KB | Disable art, log, continue text-only | Placeholder tile; diagnostics warn |
| F13 | Spotify 5xx | 5 consecutive | Circuit breaker, 5 min | "Spotify unreachable" |
| F14 | JSON malformed | Deserialise error | Discard, keep previous state, count | None unless 3 consecutive |
| F15 | Task hang | Task watchdog | Reboot | Boot screen |
| F16 | Crash loop | Boot counter in NVS; 5 boots in 5 min | Safe mode: WiFi + web UI only, **no Spotify traffic** | "Safe mode" + URL |

**F16 is the safety net for §6.3(b).** A crash loop that also polls Spotify on every boot is the realistic path to getting the client blocked. Safe mode must engage before that can accumulate.

---

## 15. Memory budget

Free heap after WiFi + TFT init on ESP32-WROOM-32: approximately **180–200 KB**.

| Consumer | Peak | Notes |
|---|---|---|
| TLS session (handshake peak) | ~45 KB | One at a time only |
| ArduinoJson filtered doc | ~4 KB | Transient, scoped |
| Album art JPEG buffer | 96 KB cap | Freed immediately after decode (§0.1 #4) |
| Largest region sprite | ~6.5 KB | 135×24×2 B |
| WebServer + request | ~8 KB | Transient |
| PlayerState ×2 (live + snapshot) | ~1.2 KB | Static |
| Task stacks | 12 KB | Static |
| **Worst-case concurrent** | **~110 KB** | Art decode never overlaps TLS. Raised with the art cap (§0.1 #4); measured free heap was ~150 KB. |

**Headroom target: ≥ 60 KB free at all times.** Log free heap every 60 s at debug level. Any sustained downward trend over an hour is a leak — most likely a TLS client or a sprite not freed on an error path.

**As-built (§0.1 #11):** this target is a measurement aid, not an enforced limit —
`HEAP_HEADROOM_TARGET_BYTES` is defined but unreferenced. The *enforced* guard is F12's
`HEAP_MIN_FREE_BYTES` (30 KB), below which album art is switched off and the device
continues text-only. `WEB_CONTROL_LATENCY_TARGET_MS` is likewise a target, not a check.

Ordering rule that makes the budget work: **art download and decode happen only after the API TLS connection is closed.** Never overlap them.

---

## 16. Configuration and persistence

NVS namespace `ideaspeak`:

| Key | Type | Default | Notes |
|---|---|---|---|
| `wifi_ssid` | str | — | via WiFiManager |
| `wifi_pass` | str | — | |
| `sp_client_id` | str | — | 32 hex chars |
| `rt_a` / `rt_b` | str | — | refresh token A/B slots |
| `rt_seq_a` / `rt_seq_b` | u32 | 0 | |
| `rt_crc_a` / `rt_crc_b` | u32 | 0 | |
| `poll_play` | u16 | 5 | seconds, clamped 3–60 |
| `poll_pause` | u16 | 30 | seconds |
| `brightness` | u8 | 2 | index 0–3 |
| `art_on` | bool | true | |
| `web_pin` | str | random | 6 digits |
| `retry_until` | u32 | 0 | epoch, §6.3(b) |
| `boot_count` | u8 | 0 | crash-loop detection, cleared after 5 min uptime |
| `orientation` | u8 | 0 | 0 = portrait, 1 = landscape (as-built, §0.1 #8). Reboots on change. |
| `tz` | str | `CET-1CEST,M3.5.0,M10.5.0/3` | POSIX TZ string; change for your own region |

No secret may be logged at any level in a release build.

---

## 17. Implementation order

Each phase must be verified on hardware before starting the next. Do not build the whole thing and then debug it.

| Phase | Deliverable | Verified by |
|---|---|---|
| 1 | Display bring-up | Red screen, then text, correct orientation, no offset |
| 2 | WiFi + WiFiManager + NTP | Connects, correct local time on serial |
| 3 | TLS reachability | `GET https://api.spotify.com/v1/` returns 401 (not a TLS error) |
| 4 | PKCE generation | Verifier/challenge match a reference implementation for a fixed input |
| 5 | Full auth flow | Refresh token in NVS; survives reboot |
| 6 | Token refresh + rotation | Force a refresh; confirm the **new** token persisted; reboot; still works |
| 7 | Player poll + filtered parse | Correct track on serial; heap stable over 30 min |
| 8 | Display now-playing (no art) | All fields, progress bar, scrolling |
| 9 | Album art | Correct art, no leak over 50 track changes |
| 10 | Rate limiting | Unit tests for the bucket; verified `retry_until` survives reboot |
| 11 | Web UI read-only | Dashboard, `/api/now` at 1 Hz, no extra Spotify calls |
| 12 | Web UI controls | All actions, < 400 ms, optimistic UI reconciles |
| 13 | Failure handling | Every row of §14 exercised |
| 14 | 48-hour soak | §18 |

**Phase 6 deserves a dedicated test.** It is the one defect that manifests days later, after the original refresh token happens to expire — long after the developer has moved on.

---

## 18. Acceptance criteria

The build is complete when all of the following pass on hardware.

**As-built verification status — stated honestly.** The functional criteria (1–10) were
exercised end-to-end on hardware: cold boot and SoftAP portal, the full PKCE copy-paste
login without a serial console, live track metadata, album art across many albums, the
progress bar, both orientations, and Next/Previous from a phone browser, with
`http://ideaspeak.local` resolving from a laptop.

The **long-duration** criteria have **not** been run: #12 (20 power cycles over 3 days),
#21 (48-hour soak), and #22 (500 track changes) all require elapsed time rather than a
test action, and the device has not been running long enough. The mechanisms they test are
implemented — A/B token rotation (§5.4), `free()` on every path including decode failure,
and the 60 s heap log — but "implemented" is not "verified", and they should not be
treated as passing until someone actually runs them. Criterion #17's on-device request
counter is exposed at `/healthz` as `requests_last_minute`; #19's caching guarantee holds
by construction, since `/api/now` is served from the cached `PlayerState` and can never
trigger a Spotify request.

**Functional**

1. Cold boot with no credentials → SoftAP portal → WiFi connects → auth instructions displayed.
2. Full PKCE flow completes via copy-paste; device reaches `RUNNING` without a serial console.
3. Track playing on any Spotify device appears on the TFT within one poll interval.
4. Album art renders correctly, sharp, correctly cropped, for at least 20 consecutive different albums.
5. Progress bar advances smoothly between polls and never drifts more than 2 s from actual.
6. Track change reflected within 1 s of the previous track ending.
7. Pause on the phone → device shows paused within 5 s and drops to the 30 s poll cadence.
8. Stopping playback → IDLE clock screen within 60 s.
9. All eight control actions work from a phone browser in under 400 ms.
10. `http://ideaspeak.local` resolves from a laptop on the same network.

**Resilience**

11. Reboot mid-playback → correct state restored with no re-authorisation.
12. **Power-cycled 20 times over 3 days → never once requires re-authorisation.** (This is the §5.4 test.)
13. WiFi router rebooted → device reconnects unattended within 2 minutes.
14. Access token forcibly expired → refresh is transparent, no visible interruption.
15. Refresh token manually corrupted in NVS slot A → device recovers from slot B **or** cleanly requests re-auth; it does not hang or crash-loop.
16. WiFi disconnected for 15 minutes → device reboots and recovers unattended.

**Rate-limit safety**

17. Sustained rate never exceeds 20 req/min under any UI interaction pattern, verified by an on-device request counter exposed at `/healthz`.
18. Simulated 429 with `Retry-After: 30` → no Spotify request for 30 s, including across a forced reboot within that window.
19. Five browser tabs open on the dashboard simultaneously → Spotify request rate is **unchanged** (proves §13.1 caching).
20. Crash-loop simulation (5 reboots in 5 min) → safe mode engages, zero Spotify requests.

**Stability**

21. **48-hour continuous soak** with active playback: no reboot, no watchdog trigger, free heap at hour 48 within 10% of hour 1.
22. 500 track changes with art enabled: no heap decline attributable to art.
23. No secret (refresh token, access token, PIN) appears in serial output at any log level in a release build.

**Code quality**

24. No `delay()` longer than 50 ms in either task.
25. Every `malloc` has a matching `free` on all paths including error paths; every `WiFiClientSecure` has a matching `stop()`.
26. Every rate-limit-relevant constant is a named constant in `config.h`, not a magic number.

---

## 19. Deferred to v1.1+

- Spotify Connect playback target (requires librespot-esp32; likely infeasible on ESP32-WROOM without PSRAM).
- Lyrics (no official Spotify API).
- Audio-features visualisation (tempo, energy) from `/v1/audio-features`.
- Wider font coverage for CJK and Cyrillic track names.
- OTA firmware update.
- Multiple saved Spotify accounts.
- Deep sleep between polls (currently precluded by the always-on display).

---

## 20. Reference links

- Spotify — Authorization Code with PKCE: <https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow>
- Spotify — Redirect URI rules: <https://developer.spotify.com/documentation/web-api/concepts/redirect_uri>
- Spotify — Security requirements change, Feb 2025: <https://developer.spotify.com/blog/2025-02-12-increasing-the-security-requirements-for-integrating-with-spotify>
- Spotify — OAuth migration reminder, Oct 2025: <https://developer.spotify.com/blog/2025-10-14-reminder-oauth-migration-27-nov-2025>
- Spotify — Rate limits: <https://developer.spotify.com/documentation/web-api/concepts/rate-limits>
- Spotify — Get Playback State: <https://developer.spotify.com/documentation/web-api/reference/get-information-about-the-users-current-playback>
- ideaspark board manual (pinout): <https://manuals.plus/asin/B0D7S7YQMC>
- TFT_eSPI: <https://github.com/Bodmer/TFT_eSPI>
- TJpg_Decoder: <https://github.com/Bodmer/TJpg_Decoder>
- ArduinoJson filtering: <https://arduinojson.org/v7/api/json/deserializejson/>

---

## 21. Licence and provenance

This specification is published alongside the firmware it describes, under **GPL-3.0**,
so the design reasoning is available and not only the code.

Written and built by **T. Philip** — <https://github.com/t-philip>.
Firmware: <https://github.com/t-philip/spotify-nowplaying>

---

*End of specification v1.1 — as-built, 30 July 2026*
