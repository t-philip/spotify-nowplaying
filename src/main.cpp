#include <Arduino.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "config.h"
#include "app_state.h"
#include "store/nvs.h"
#include "net/wifi_mgr.h"
#include "net/time_sync.h"
#include "spotify/model.h"
#include "spotify/auth.h"
#include "spotify/ratelimit.h"
#include "spotify/api.h"
#include "art/art_fetch.h"
#include "art/art_decode.h"
#include "ui/display.h"
#include "ui/screens.h"
#include "ui/button.h"
#include "ui/fonts.h"
#include "web/server.h"

// Task model (§8.1): Arduino's own loop() already runs pinned to APP_CPU
// (core 1) at priority 1 — that IS uiTask, so no separate task is created
// for it. netTask is the one FreeRTOS task this firmware creates.

namespace {

bool s_safeMode = false;
TaskHandle_t s_netTaskHandle = nullptr;

void netTaskFn(void* /*pv*/) {
  if (!s_safeMode) {
    WifiMgr::begin(); // blocks until connected, or reboots on portal timeout
    while (!WifiMgr::isConnected()) {
      WifiMgr::loop();
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    while (!TimeSync::begin(Nvs::getTimezone())) {
      vTaskDelay(pdMS_TO_TICKS(1000)); // retry with backoff, §9 TIME_SYNC
    }

    Spotify::Auth::begin();
    Spotify::RateLimit::begin();
    AppStateStore::setState(AppState::AUTH_CHECK);

    if (Spotify::Auth::isAuthNeeded()) {
      AppStateStore::setState(AppState::AUTH_NEEDED);
    } else {
      // Phase 6 (§17): verify the stored refresh token proactively at boot
      // rather than waiting for the first poll to discover it's dead.
      Spotify::Auth::RefreshOutcome outcome = Spotify::Auth::refreshNow();
      AppStateStore::setState(outcome == Spotify::Auth::RefreshOutcome::InvalidGrant
                                   ? AppState::AUTH_NEEDED
                                   : AppState::RUNNING);
    }
  } else {
    AppStateStore::setState(AppState::SAFE_MODE);
    WifiMgr::begin(); // F16: WiFi + web UI only, no Spotify traffic below
  }

  // Registered only now, after any long blocking calls above (WiFiManager's
  // portal can legitimately block up to WIFI_PORTAL_TIMEOUT_S=300s) — the
  // watchdog would otherwise panic during ordinary first-time setup.
  esp_task_wdt_add(NULL);

  Web::begin();

  uint32_t lastPollMs = 0;
  uint32_t lastHeapLogMs = 0;
  uint8_t consecutiveMalformed = 0;
  uint32_t lastActivityMs = millis();
  bool bootCountCleared = false;

  // Art URL whose fetch/draw has already been handled. Deliberately NOT
  // derived from a per-iteration snapshot — see the art block below.
  String lastArtUrlHandled;
  bool artWasValid = false;

  while (true) {
    esp_task_wdt_reset();
    WifiMgr::loop();

    if (!bootCountCleared && millis() > BOOT_COUNT_CLEAR_UPTIME_MS) {
      Nvs::clearBootCount(); // F16: this boot has proven stable
      bootCountCleared = true;
    }

    if (!s_safeMode) {
      AppStatus app = AppStateStore::snapshot();

      if (app.state == AppState::AUTH_NEEDED) {
        // A completed login (POST /auth/complete, handled on uiTask) clears
        // the NVS-level auth-needed condition; pick that transition up here.
        if (!Spotify::Auth::isAuthNeeded()) {
          AppStateStore::setState(AppState::RUNNING);
        }
      } else if (app.state == AppState::RUNNING ||
                 app.state == AppState::RATE_LIMITED ||
                 app.state == AppState::ERROR_TRANSIENT) {
        // Drain control commands queued by the web UI. uiTask already
        // applied the optimistic state; this is the real Spotify call.
        Spotify::Api::ControlRequest req;
        while (Web::dequeueControlRequest(req)) {
          uint32_t nowEpoch = (uint32_t)time(nullptr);
          if (!Spotify::RateLimit::isBlocked(nowEpoch) && Spotify::RateLimit::tryAcquireToken()) {
            Spotify::Api::sendControl(req);
            Spotify::Api::scheduleReconcilePolls();
          }
          // If the bucket/blocked gate says no, the command is dropped —
          // §6.3a: dropping is safe, queueing further would not be.
        }
        Spotify::Api::pumpReconcilePolls();

        // Art-fetch check runs every iteration against whatever
        // PlayerStateStore currently holds — independent of which poll
        // path (this loop's own "due" poll below, or a reconciliation
        // poll fired after a control command) most recently updated it.
        // It used to live inside the "due poll, case Updated" branch only,
        // comparing against the url captured at the top of that same
        // iteration — but a reconciliation poll (§7.3, fired ~700ms/2000ms
        // after Next/Previous/Play/Pause) updates PlayerStateStore on its
        // own schedule, so by the time the next "due" poll ran, the
        // before/after comparison there had already gone stale and the
        // art fetch for that track was silently skipped forever (confirmed
        // on hardware: clicking Next left the old cover on screen
        // indefinitely). Comparing against this dedicated tracker instead
        // of a per-iteration snapshot catches the change on whichever
        // iteration first observes it, regardless of source.
        if (Nvs::getArtEnabled()) {
          Spotify::PlayerState cur = Spotify::PlayerStateStore::snapshot();

          // Going idle blanks the whole panel (IDLE_NO_PLAYBACK clock), and
          // coming back blanks it again — so the art must be redrawn on
          // resume even if playback resumes on the very same track. Clear
          // both this loop's tracker and art_fetch's own url cache,
          // otherwise fetch() would short-circuit to Unchanged and draw
          // nothing.
          if (artWasValid && !cur.valid) {
            lastArtUrlHandled = "";
            Art::invalidateCache();
          }
          artWasValid = cur.valid;

          if (cur.valid && String(cur.art_url) != lastArtUrlHandled) {
            // §15 ordering rule: art download/decode only after the API
            // TLS connection is closed — never overlap them.
            Spotify::Api::closeApiConnection();
            Art::ArtBuffer buf;
            Art::FetchResult fr = Art::fetch(cur.art_url, buf);
            if (fr == Art::FetchResult::Fetched) {
              bool ok = Art::decodeAndDraw(buf.data, buf.len);
              AppStateStore::updateArtDiagnostics((int)fr, (uint32_t)buf.len, ok ? 1 : 0);
              if (!ok) {
                Art::drawPlaceholder(cur.album);
              }
              free(buf.data); // every malloc has a matching free, incl. decode-failure path
            } else if (fr == Art::FetchResult::Failed) {
              AppStateStore::updateArtDiagnostics((int)fr, 0, -1);
              Art::drawPlaceholder(cur.album);
            }
            // Mark handled regardless of success — a persistently-failing
            // URL should show its placeholder once, not retry every ~50ms.
            lastArtUrlHandled = cur.art_url;
          }
        }

        Spotify::PlayerState p = Spotify::PlayerStateStore::snapshot();

        uint16_t baseIntervalS;
        if (!p.valid) {
          baseIntervalS = POLL_INTERVAL_NOTHING_S;
        } else if (!p.is_playing) {
          baseIntervalS = Nvs::getPollIntervalPaused();
        } else {
          baseIntervalS = Nvs::getPollIntervalPlaying();
          lastActivityMs = millis();
        }
        if (millis() - lastActivityMs > POLL_LONG_IDLE_AFTER_MS) {
          baseIntervalS = POLL_INTERVAL_LONG_IDLE_S; // §6.2 idle >15min row
        }

        uint32_t intervalMs =
            (uint32_t)(baseIntervalS * 1000.0f * Spotify::RateLimit::pollIntervalMultiplier());

        // §6.4: an immediate poll when the extrapolated progress reaches
        // duration — the track has almost certainly changed.
        bool trackLikelyEnded = false;
        if (p.valid && p.is_playing && p.duration_ms > 0) {
          uint32_t displayed = p.progress_ms + (millis() - p.progress_epoch_ms);
          trackLikelyEnded = displayed >= p.duration_ms;
        }

        uint32_t now = millis();
        bool due = trackLikelyEnded || (now - lastPollMs >= intervalMs);

        if (due) {
          uint32_t nowEpoch = (uint32_t)time(nullptr);
          if (!Spotify::RateLimit::isBlocked(nowEpoch) && Spotify::RateLimit::tryAcquireToken()) {
            lastPollMs = now;

            Spotify::Api::PollOutcome outcome = Spotify::Api::pollPlayerState();

            switch (outcome) {
              case Spotify::Api::PollOutcome::Updated: {
                consecutiveMalformed = 0;
                AppStateStore::setState(AppState::RUNNING);
                // Art is handled by the dedicated block at the top of this
                // loop, which picks up the change on the next iteration
                // (~50ms) regardless of whether this poll or a
                // reconciliation poll observed it first.
                break;
              }
              case Spotify::Api::PollOutcome::NothingPlaying:
                consecutiveMalformed = 0;
                AppStateStore::setState(AppState::RUNNING); // sub-state IDLE_NO_PLAYBACK via PlayerState.valid=false
                break;
              case Spotify::Api::PollOutcome::AuthExpired:
                AppStateStore::setState(AppState::AUTH_NEEDED);
                break;
              case Spotify::Api::PollOutcome::Forbidden:
                AppStateStore::setError("Not permitted on this account");
                AppStateStore::setState(AppState::ERROR_TRANSIENT);
                break;
              case Spotify::Api::PollOutcome::RateLimited:
                AppStateStore::setRateLimitedUntil(Nvs::getRetryUntilEpoch());
                AppStateStore::setState(AppState::RATE_LIMITED);
                break;
              case Spotify::Api::PollOutcome::ServerError:
                AppStateStore::setError("Spotify unreachable");
                AppStateStore::setState(AppState::ERROR_TRANSIENT);
                break;
              case Spotify::Api::PollOutcome::NetworkError:
                break; // transient — retried next cycle, stale display kept
              case Spotify::Api::PollOutcome::MalformedJson:
                // F14: discard, keep previous state; only surface after 3 in a row.
                consecutiveMalformed++;
                if (consecutiveMalformed >= 3) {
                  AppStateStore::setError("Data error, retrying");
                  AppStateStore::setState(AppState::ERROR_TRANSIENT);
                }
                break;
            }
          }
        }
      }
    }

    uint32_t now = millis();
    if (now - lastHeapLogMs > HEAP_LOG_INTERVAL_MS) {
      lastHeapLogMs = now;
      uint32_t freeHeap = ESP.getFreeHeap();
      AppStateStore::updateDiagnostics(freeHeap, now, Spotify::RateLimit::requestsInLastMinute(), 0);
#if !RELEASE_BUILD
      Serial.printf("[heap] free=%u bytes\n", (unsigned)freeHeap);
#endif
      if (freeHeap < HEAP_MIN_FREE_BYTES && Nvs::getArtEnabled()) {
        Nvs::setArtEnabled(false); // F12: disable art, continue text-only
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void handleButton() {
  Button::Action action = Button::poll();

  switch (action) {
    case Button::Action::ShortPress: {
      uint8_t next = (Display::brightnessIndex() + 1) % 4;
      Display::setBrightnessIndex(next);
      Nvs::setBrightnessIndex(next);
      break;
    }

    case Button::Action::LongPressTriggered: {
      AppStatus app = AppStateStore::snapshot();
      if (Display::lock()) {
        TFT_eSPI& tft = Display::tft();
        tft.fillScreen(COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(FONT_BODY);
        tft.setTextColor(COLOR_TEXT_PRIMARY, COLOR_BG);
        tft.drawString(app.localIp, Layout::screenW() / 2, Layout::screenH() / 2 - 30);
        tft.setTextColor(COLOR_TEXT_SECONDARY, COLOR_BG);
        tft.drawString(String(WEB_MDNS_NAME) + ".local", Layout::screenW() / 2, Layout::screenH() / 2 - 5);
        tft.setTextColor(COLOR_TEXT_TERTIARY, COLOR_BG);
        tft.drawString("PIN: " + Nvs::getWebPin(), Layout::screenW() / 2, Layout::screenH() / 2 + 25);
        Display::unlock();
      }
      uint32_t shownAt = millis();
      while (millis() - shownAt < BTN_LONG_PRESS_SHOW_MS) {
        esp_task_wdt_reset();
        delay(50);
      }
      Screens::invalidateAll(); // force a clean repaint of whatever should be showing now
      break;
    }

    case Button::Action::FactoryResetTriggered:
      // The on-screen countdown requirement (§2.4) is satisfied by
      // Button::heldForMs() being available for a future countdown overlay;
      // the abort-by-release path is handled entirely inside Button::poll()
      // — execution only reaches here once the full hold time has elapsed.
      Nvs::factoryReset();
      ESP.restart();
      break;

    case Button::Action::None:
    default:
      break;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  AppStateStore::begin();
  Spotify::PlayerStateStore::begin();
  Nvs::begin();

  Nvs::incrementBootCount();
  s_safeMode = Nvs::getBootCount() >= BOOT_LOOP_MAX_COUNT; // F16 crash-loop detection

  Orientation orientation = static_cast<Orientation>(Nvs::getOrientation());
  Layout::begin(orientation);
  Display::begin(orientation);
  Screens::begin();
  Button::begin();
  Art::initDecoder();

  esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT_S, true);
  esp_task_wdt_add(NULL); // this task becomes uiTask via loop() below

  Screens::render(); // draws the BOOT screen (default AppState), §12.5
  delay(1500);

  xTaskCreatePinnedToCore(netTaskFn, "netTask", NET_TASK_STACK, nullptr,
                          NET_TASK_PRIORITY, &s_netTaskHandle, NET_TASK_CORE);
}

void loop() {
  esp_task_wdt_reset();

  handleButton();
  Screens::render();
  Web::handleClient();

  delay(1000 / SCROLL_FPS); // ~30fps to service scrolling text (§12.2)
}
