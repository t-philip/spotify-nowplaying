#!/usr/bin/env python3
"""Optional convenience for the Spotify PKCE login flow (design spec §5.2).

Listens on 127.0.0.1:8888 (Spotify's registered redirect URI), captures the
browser's redirect after you approve the Spotify consent screen, and POSTs
it straight to the device's /auth/complete — removing the copy-paste step.
This is NOT required: the copy-paste flow (paste the "can't connect" URL
into the device's setup page) is the supported baseline and works with no
software installed.

Usage:
    python spotify_login_helper.py <device-pin> [device-host]

    device-host defaults to ideaspeak.local.
"""
import sys
import json
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        pin, host = self.server.ideaspeak_pin, self.server.ideaspeak_host
        full_url = f"http://127.0.0.1:8888{self.path}"

        body = json.dumps({"value": full_url}).encode()
        req = urllib.request.Request(
            f"http://{host}/auth/complete",
            data=body,
            method="POST",
            headers={"Content-Type": "application/json", "X-IdeaSpeak-PIN": pin},
        )
        try:
            with urllib.request.urlopen(req, timeout=8) as resp:
                result = json.loads(resp.read())["result"]
        except Exception as e:  # noqa: BLE001 — this is a throwaway CLI helper
            result = f"error contacting device: {e}"

        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(
            f"<h2>IdeaSpeak login</h2><p>Result: {result}</p>"
            f"<p>You can close this tab.</p>".encode()
        )

    def log_message(self, fmt, *args):  # quieter default logging
        pass


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    pin = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else "ideaspeak.local"

    server = HTTPServer(("127.0.0.1", 8888), Handler)
    server.ideaspeak_pin = pin
    server.ideaspeak_host = host

    print(f"Listening on http://127.0.0.1:8888 — will forward to http://{host}/auth/complete")
    print("Now start the login from the device's Setup panel, then approve in the browser.")
    server.handle_request()  # exactly one request: the redirect, then exit


if __name__ == "__main__":
    main()
