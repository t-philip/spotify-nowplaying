#include "http.h"

namespace Http {

bool sendRequest(TlsClient& tls, const char* method, const char* path,
                  const char* host, const String& extraHeaders,
                  const char* body) {
  if (!tls.connected()) return false;

  WiFiClientSecure& c = tls.stream();
  size_t bodyLen = body ? strlen(body) : 0;

  c.print(method);
  c.print(' ');
  c.print(path);
  c.print(" HTTP/1.1\r\n");
  c.print("Host: ");
  c.print(host);
  c.print("\r\n");
  c.print("Connection: keep-alive\r\n");
  c.print(extraHeaders);

  if (body != nullptr) {
    c.print("Content-Length: ");
    c.print(bodyLen);
    c.print("\r\n\r\n");
    if (bodyLen > 0) c.print(body);
  } else {
    c.print("\r\n");
  }

  tls.noteRequestSent();
  return true;
}

HttpResponse readStatusAndHeaders(TlsClient& tls) {
  HttpResponse resp;
  WiFiClientSecure& c = tls.stream();
  uint32_t deadline = millis() + TLS_READ_TIMEOUT_MS;

  // Status line, e.g. "HTTP/1.1 204 No Content"
  String statusLine;
  while (millis() < deadline) {
    if (c.available()) {
      statusLine = c.readStringUntil('\n');
      break;
    }
    delay(5);
  }
  if (statusLine.length() == 0) return resp; // headersOk stays false — timeout

  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace < 0) return resp;
  resp.statusCode = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();

  // Header lines until a blank line ("\r" only, since readStringUntil('\n')
  // leaves the trailing \r on each line).
  while (millis() < deadline) {
    if (!c.available()) { delay(5); continue; }
    String line = c.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) { resp.headersOk = true; break; }

    int colon = line.indexOf(':');
    if (colon < 0) continue;
    String key = line.substring(0, colon);
    String val = line.substring(colon + 1);
    val.trim();
    key.toLowerCase();

    if (key == "content-length") {
      resp.contentLength = val.toInt();
    } else if (key == "retry-after") {
      resp.retryAfterSeconds = (uint32_t)val.toInt();
    }
  }

  return resp;
}

size_t readBodyFixed(TlsClient& tls, char* buf, size_t bufCap, long contentLength) {
  if (contentLength <= 0) return 0;
  WiFiClientSecure& c = tls.stream();
  size_t toRead = (size_t)contentLength;
  if (toRead > bufCap - 1) toRead = bufCap - 1;

  size_t got = 0;
  uint32_t deadline = millis() + TLS_READ_TIMEOUT_MS;
  while (got < toRead && millis() < deadline) {
    if (c.available()) {
      int n = c.read(reinterpret_cast<uint8_t*>(buf + got), toRead - got);
      if (n > 0) got += n;
    } else {
      delay(5);
    }
  }
  buf[got] = '\0';
  return got;
}

}  // namespace Http
