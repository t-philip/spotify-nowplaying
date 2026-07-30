# Spotify Now-Playing Display (ESP32)

A standalone WiFi display that shows whatever is currently playing on your Spotify
account — album art, title, artist, album and a live progress bar — on a small
ST7789 TFT, with transport controls exposed through a PIN-gated web dashboard.

It is **not** a Spotify Connect target. It observes and controls playback happening
on your *other* devices (phone, desktop, speaker), which means it needs no audio
hardware and works with whatever you're already listening on.

Licensed under [GPL-3.0](LICENSE). Please credit **t-philip** if you use or share this.

---

## What it does

- **Live now-playing** — album art (JPEG, decoded on-device), scrolling title/artist/album, progress bar
- **Web dashboard** at `http://ideaspeak.local` — play/pause, next/previous, backlight, orientation
- **Portrait and landscape** layouts, switchable at runtime
- **PKCE authentication** against your own Spotify app registration — no client secret on the device
- **Crash-safe token rotation** — A/B refresh-token slots in NVS, so a power cut mid-refresh cannot brick auth
- **Layered rate-limit protection** — adaptive polling, backoff, and a hard request ceiling

## Hardware

| Item | Notes |
|---|---|
| ESP32 dev board with built-in ST7789 TFT | 135×240. Developed on an ideaspark board; any ESP32 + ST7789 works if you adjust the pin defines in `platformio.ini`. |
| USB cable | For flashing and power. |

No PSRAM required — the firmware is written to fit the heap budget of a plain ESP32,
which is why it never holds two TLS connections open at once.

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/t-philip/spotify-nowplaying.git
cd spotify-nowplaying
pio run --target upload
```

All display pin mappings and TFT_eSPI settings are supplied via `build_flags` in
`platformio.ini`, so you do **not** need to edit the library's `User_Setup.h`.

## First-run setup

1. **Create a Spotify app** at the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
   Add `http://127.0.0.1:8888/callback` as a Redirect URI.
   > Spotify rejects private-IP redirect URIs at registration time, so pointing it
   > at the device's own web server is not possible — hence the loopback + copy-paste flow.
2. **Power on the device.** It starts a WiFi setup portal (`IdeaSpeak-Setup`). Join it and
   pick your network.
3. **Open the dashboard** at `http://ideaspeak.local` and enter your Spotify **Client ID**.
4. **Authorise.** The device shows a QR code and URL; complete the login in a browser and
   paste the resulting code back. `tools/spotify_login_helper.py` can assist with this step.

Transport controls require **Spotify Premium** — the API returns HTTP 403 on Free
accounts. The firmware handles that gracefully and simply disables the controls.

## Architecture

Two pinned FreeRTOS tasks on separate cores:

| Task | Core | Owns |
|---|---|---|
| `netTask` | 0 | WiFi, TLS, token lifecycle, API polling, album-art fetch and decode |
| `uiTask` | 1 | Rendering, scrolling text, progress animation, button input |

Both drive the same SPI bus, so display access is serialised behind a mutex.
JSON is parsed as a filtered stream straight off the TLS socket rather than
buffered whole, keeping peak heap low.

```
src/
├── spotify/   API client, PKCE auth, model, rate limiting
├── net/       WiFi manager, TLS client, HTTP, NTP time sync
├── art/       Album-art fetch and JPEG decode
├── ui/        Display, layouts, screens, widgets, fonts, button
├── store/     NVS persistence
├── web/       Dashboard HTTP server
└── util/      base64url, CRC32, UTF-8 helpers
```

A full design specification covering the auth flow, heap budget, rate-limit strategy
and error-handling matrix will be published in this repository.

## Two findings worth knowing

If you're building anything similar, these two cost real debugging time:

1. **`TJpg_Decoder`'s output callback must always return `true`.** Returning `false`
   for rows outside the viewport looks like an obvious optimisation, but the decoder
   treats a `false` return as *abort the entire decode*, not *skip this block*. The
   result is partially-decoded, striped images. TFT_eSPI's viewport already clips
   out-of-bounds pixels for free.

2. **`i.scdn.co` will not respond without a `User-Agent` header.** TLS connect and
   request write both succeed; the CDN simply never replies, which presents as a read
   timeout. The identical URL fetches instantly from a browser or `curl`.

---

Built and maintained by [t-philip](https://github.com/t-philip).
