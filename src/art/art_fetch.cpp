#include "art_fetch.h"
#include "../config.h"
#include "../net/tls_client.h"
#include "../net/http.h"
#include "../app_state.h"

namespace Art {

namespace {

String s_lastFetchedUrl; // in-RAM only (§11.4) — never persisted across reboots

void splitUrl(const String& url, String& host, String& path) {
  String s = url;
  if (s.startsWith("https://")) s = s.substring(8);
  else if (s.startsWith("http://")) s = s.substring(7);

  int slash = s.indexOf('/');
  if (slash < 0) {
    host = s;
    path = "/";
  } else {
    host = s.substring(0, slash);
    path = s.substring(slash);
  }
}

}  // namespace

void invalidateCache() {
  s_lastFetchedUrl = "";
}

FetchResult fetch(const char* url, ArtBuffer& outBuf) {
  if (!url || strlen(url) == 0) return FetchResult::Failed;

  String urlStr(url);
  if (s_lastFetchedUrl == urlStr) return FetchResult::Unchanged; // typical case: same album

  String host, path;
  splitUrl(urlStr, host, path);

  // i.scdn.co only — no credential transmitted, setInsecure() is the
  // documented trade-off in tls_client.cpp / §10.1.
  TlsClient tls(TrustMode::InsecureArtOnly);
  bool connected = tls.connect(host.c_str());
  if (!connected) {
    AppStateStore::updateArtFetchSteps(0, -1, 0, -2);
    return FetchResult::Failed;
  }

  // Confirmed via /healthz diagnostics on hardware: TLS connect and the
  // request write both succeed, but i.scdn.co never sends a response
  // within the timeout when the request carries only a Host header — a
  // plain PowerShell/browser request to the same URL responds instantly.
  // Most likely explanation is a CDN edge that expects a User-Agent (and
  // Accept) before it'll respond at all. (Http::sendRequest already adds
  // its own Connection: keep-alive — do not add a second Connection
  // header here, that would send two conflicting values.)
  String headers =
      "User-Agent: IdeaSpeak-ESP32/1.0\r\n"
      "Accept: image/jpeg\r\n";
  bool sent = Http::sendRequest(tls, "GET", path.c_str(), host.c_str(), headers, nullptr);
  if (!sent) {
    AppStateStore::updateArtFetchSteps(1, 0, 0, -2);
    tls.stop();
    return FetchResult::Failed;
  }

  HttpResponse resp = Http::readStatusAndHeaders(tls);
  AppStateStore::updateArtFetchSteps(1, 1, resp.headersOk ? resp.statusCode : -1,
                                      resp.headersOk ? (int32_t)resp.contentLength : -2);
  if (!resp.headersOk || resp.statusCode != 200) {
    tls.stop();
    return FetchResult::Failed;
  }

  bool knownLength = resp.contentLength > 0;
  if (knownLength && (size_t)resp.contentLength > ART_MAX_CONTENT_LENGTH) {
    tls.stop(); // §11.2 step 2: oversized -> abort, use placeholder
    return FetchResult::Failed;
  }
  size_t toRead = knownLength ? (size_t)resp.contentLength : ART_MAX_CONTENT_LENGTH;

  uint8_t* buf = (uint8_t*)malloc(toRead);
  if (!buf) {
    tls.stop(); // §11.2 step 3: malloc fail -> placeholder (caller logs heap)
    return FetchResult::Failed;
  }

  size_t got = 0;
  uint32_t deadline = millis() + TLS_READ_TIMEOUT_MS;
  WiFiClientSecure& stream = tls.stream();
  while (got < toRead && millis() < deadline) {
    if (stream.available()) {
      int n = stream.read(buf + got, toRead - got);
      if (n > 0) {
        got += n;
        deadline = millis() + TLS_READ_TIMEOUT_MS;
      }
    } else if (!tls.connected()) {
      break; // server closed the connection — expected end for the
             // unknown-length streamed-with-cap case
    } else {
      delay(5);
    }
  }
  tls.stop(); // §11.2 step 5 / §10.2: close in every path

  if (got == 0 || (knownLength && got != toRead)) {
    free(buf);
    return FetchResult::Failed;
  }

  outBuf.data = buf;
  outBuf.len = got;
  s_lastFetchedUrl = urlStr;
  return FetchResult::Fetched;
}

}  // namespace Art
