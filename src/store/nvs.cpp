#include "nvs.h"
#include <Preferences.h>
#include "../util/crc32.h"

namespace Nvs {

static Preferences prefs;

struct SlotState {
  bool valid;
  uint32_t seq;
  String token;
};

static SlotState readSlot(char which) {
  char keyTok[8], keySeq[10], keyCrc[10];
  snprintf(keyTok, sizeof(keyTok), "rt_%c", which);
  snprintf(keySeq, sizeof(keySeq), "rt_seq_%c", which);
  snprintf(keyCrc, sizeof(keyCrc), "rt_crc_%c", which);

  SlotState s;
  s.token = prefs.getString(keyTok, "");
  s.seq = prefs.getUInt(keySeq, 0);
  uint32_t storedCrc = prefs.getUInt(keyCrc, 0);
  s.valid = s.token.length() > 0 && crc32_str(s.token.c_str()) == storedCrc;
  return s;
}

static void writeSlot(char which, const String& token, uint32_t seq) {
  char keyTok[8], keySeq[10], keyCrc[10];
  snprintf(keyTok, sizeof(keyTok), "rt_%c", which);
  snprintf(keySeq, sizeof(keySeq), "rt_seq_%c", which);
  snprintf(keyCrc, sizeof(keyCrc), "rt_crc_%c", which);

  // Step 1+2+3: token then crc, each Preferences::put* commits to NVS
  // immediately — there is no separate flush step needed on this platform.
  prefs.putString(keyTok, token);
  prefs.putUInt(keyCrc, crc32_str(token.c_str()));
  // Step 4+5: sequence bump is the atomic switch to the new slot.
  prefs.putUInt(keySeq, seq);
}

void begin() {
  prefs.begin(NVS_NAMESPACE, false);
}

bool readRefreshToken(char* out, size_t out_cap) {
  SlotState a = readSlot('a');
  SlotState b = readSlot('b');

  const SlotState* chosen = nullptr;
  if (a.valid && b.valid) {
    chosen = (a.seq >= b.seq) ? &a : &b;
  } else if (a.valid) {
    chosen = &a;
  } else if (b.valid) {
    chosen = &b;
  }

  if (!chosen) return false;
  strncpy(out, chosen->token.c_str(), out_cap - 1);
  out[out_cap - 1] = '\0';
  return true;
}

bool writeRefreshToken(const char* token) {
  SlotState a = readSlot('a');
  SlotState b = readSlot('b');
  uint32_t effA = a.valid ? a.seq : 0;
  uint32_t effB = b.valid ? b.seq : 0;

  char inactive;
  uint32_t activeSeq;
  if (effA >= effB) {
    inactive = 'b';
    activeSeq = effA;
  } else {
    inactive = 'a';
    activeSeq = effB;
  }

  writeSlot(inactive, String(token), activeSeq + 1);
  return true;
}

void clearRefreshTokens() {
  prefs.putString("rt_a", "");
  prefs.putUInt("rt_crc_a", 0);
  prefs.putUInt("rt_seq_a", 0);
  prefs.putString("rt_b", "");
  prefs.putUInt("rt_crc_b", 0);
  prefs.putUInt("rt_seq_b", 0);
}

bool hasValidRefreshToken() {
  char buf[512];
  return readRefreshToken(buf, sizeof(buf));
}

String getSpotifyClientId() { return prefs.getString("sp_client_id", ""); }
void   setSpotifyClientId(const String& clientId) { prefs.putString("sp_client_id", clientId); }

uint16_t getPollIntervalPlaying() {
  uint16_t v = prefs.getUShort("poll_play", POLL_INTERVAL_PLAYING_S_DEFAULT);
  return constrain(v, POLL_INTERVAL_MIN_S, POLL_INTERVAL_MAX_S);
}
void setPollIntervalPlaying(uint16_t seconds) {
  prefs.putUShort("poll_play", constrain(seconds, POLL_INTERVAL_MIN_S, POLL_INTERVAL_MAX_S));
}
uint16_t getPollIntervalPaused() {
  uint16_t v = prefs.getUShort("poll_pause", POLL_INTERVAL_PAUSED_S_DEFAULT);
  return constrain(v, POLL_INTERVAL_MIN_S, POLL_INTERVAL_MAX_S);
}
void setPollIntervalPaused(uint16_t seconds) {
  prefs.putUShort("poll_pause", constrain(seconds, POLL_INTERVAL_MIN_S, POLL_INTERVAL_MAX_S));
}

uint8_t getBrightnessIndex() { return prefs.getUChar("brightness", BACKLIGHT_DEFAULT_INDEX); }
void    setBrightnessIndex(uint8_t index) { prefs.putUChar("brightness", index % 4); }
bool    getArtEnabled() { return prefs.getBool("art_on", true); }
void    setArtEnabled(bool enabled) { prefs.putBool("art_on", enabled); }

uint8_t getOrientation() { return prefs.getUChar("orientation", 0); }
void    setOrientation(uint8_t orientation) { prefs.putUChar("orientation", orientation % 2); }

String getWebPin() {
  String pin = prefs.getString("web_pin", "");
  if (pin.length() == WEB_PIN_DIGITS) return pin;

  uint32_t v = esp_random() % 1000000UL;
  char buf[7];
  snprintf(buf, sizeof(buf), "%06u", (unsigned)v);
  pin = String(buf);
  prefs.putString("web_pin", pin);
  return pin;
}
void setWebPin(const String& pin) { prefs.putString("web_pin", pin); }

uint32_t getRetryUntilEpoch() { return prefs.getUInt("retry_until", 0); }
void     setRetryUntilEpoch(uint32_t epoch) { prefs.putUInt("retry_until", epoch); }

uint8_t getBootCount() { return prefs.getUChar("boot_count", 0); }
void    incrementBootCount() { prefs.putUChar("boot_count", getBootCount() + 1); }
void    clearBootCount() { prefs.putUChar("boot_count", 0); }

String getTimezone() { return prefs.getString("tz", DEFAULT_TZ); }
void   setTimezone(const String& tz) { prefs.putString("tz", tz); }

void factoryReset() {
  prefs.clear();
}

}  // namespace Nvs
