#pragma once
#include <Arduino.h>

// Album art download (§11.2). Caller MUST have already called
// Spotify::Api::closeApiConnection() — this module assumes it is the only
// TLS connection open (§10.2, §15).

namespace Art {

struct ArtBuffer {
  uint8_t* data = nullptr;
  size_t   len = 0;
};

enum class FetchResult {
  Fetched,   // outBuf populated; caller owns the buffer and MUST free() it
  Unchanged, // url identical to the last successful fetch — skip entirely
  Failed,    // any failure (oversized, malloc fail, network) — use placeholder
};

// Compares `url` against the cached last-fetched url (in-RAM only, §11.4 —
// no flash caching of decoded art). On a genuine change, streams the JPEG
// with a hard ART_MAX_CONTENT_LENGTH cap, mallocs the exact size, downloads
// fully, and closes the TLS connection before returning.
FetchResult fetch(const char* url, ArtBuffer& outBuf);

// Forgets the "last fetched url" so the next fetch() of that same url
// re-downloads instead of returning Unchanged. Needed whenever something
// has painted over the art region and it must be redrawn even though the
// track never changed — currently the IDLE_NO_PLAYBACK clock screen, which
// clears the full screen and is itself cleared on the way back to
// NOW_PLAYING.
void invalidateCache();

}  // namespace Art
