#pragma once
#include <Arduino.h>
#include "../spotify/api.h"

// Synchronous WebServer (§13, port 80), serviced from uiTask (§8.1). Every
// mutating route requires the X-IdeaSpeak-PIN header (§13.3); GET /api/now
// and GET /healthz are unauthenticated but read-only and MUST NOT trigger
// any Spotify request (§13.1) — they only ever read cached state.
//
// Control commands (POST /api/control) are NOT sent to Spotify from here.
// uiTask applies the optimistic state update immediately (cheap, mutex-only)
// and enqueues the actual network call for netTask to execute — this
// preserves the §8.1 task split (TLS blocking must never stall uiTask/
// rendering) while still hitting the <400ms control latency target (§13.2),
// since the HTTP response doesn't wait on the network round-trip at all.

namespace Web {

void begin();

// Call every uiTask loop iteration.
void handleClient();

// netTask calls this each of its loop iterations to drain commands queued
// by POST /api/control. Returns false if the queue was empty.
bool dequeueControlRequest(Spotify::Api::ControlRequest& outReq);

}  // namespace Web
