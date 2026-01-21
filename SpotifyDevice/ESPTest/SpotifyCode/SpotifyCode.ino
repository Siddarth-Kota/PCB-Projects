#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <ArduinoJson.h>

// -------------------- Pins --------------------
static const uint8_t BTN_PLAY     = 4;   // Button 1: Play/Pause
static const uint8_t BTN_NEXT     = 5;   // Button 3: Next
static const uint8_t BTN_PREV     = 6;   // Button 2: Previous
static const uint8_t BTN_SPOTIFY  = 7;   // Button 5: Open Spotify hotkey
static const uint8_t BTN_MUTE     = 15;  // Button 4: Mute/Unmute (system mute)

static const uint8_t LED_MUTE     = 16;  // LED reflects system mute state (from Python)
static const uint8_t LED_BACK     = 18;  // Backlight

// -------------------- Display layout --------------------
static const int SCREEN_W = 480;
static const int SCREEN_H = 320;

static const int ART_SIZE = 240;
static const int ART_X = 0;
static const int ART_Y = 0;

static const int TEXT_X = ART_SIZE + 10;
static const int TEXT_Y = 10;
static const int TEXT_W = SCREEN_W - TEXT_X - 10;

static const int BAR_X = 10;
static const int BAR_Y = 286;
static const int BAR_W = SCREEN_W - 20;
static const int BAR_H = 18;

// -------------------- Globals --------------------
TFT_eSPI tft;

String gTitle  = "";
String gArtist = "";
String gAlbum  = "";

uint32_t gDurMs = 0;
uint32_t gPosMs = 0;
bool     gPlaying = false;

uint32_t gLastPosUpdateMs = 0;
uint32_t gLastBarDrawAtMs = 0;
int      gLastBarFillPx   = -1;

bool gMuted = false;

// -------------------- Simple debounce --------------------
struct DebouncedButton {
  uint8_t pin;
  bool stable;
  bool lastStable;
  uint32_t lastChange;
};

static const uint32_t DEBOUNCE_MS = 25;

DebouncedButton bPlay    { BTN_PLAY,    true, true, 0 };
DebouncedButton bPrev    { BTN_PREV,    true, true, 0 };
DebouncedButton bNext    { BTN_NEXT,    true, true, 0 };
DebouncedButton bMute    { BTN_MUTE,    true, true, 0 };
DebouncedButton bSpotify { BTN_SPOTIFY, true, true, 0 };

static bool updateButton(DebouncedButton &b) {
  bool raw = digitalRead(b.pin); // HIGH = not pressed (pullup), LOW = pressed
  if (raw != b.stable) {
    if (millis() - b.lastChange >= DEBOUNCE_MS) {
      b.lastStable = b.stable;
      b.stable = raw;
      b.lastChange = millis();
      return true;
    }
  } else {
    b.lastChange = millis();
  }
  return false;
}

static bool fell(const DebouncedButton &b) {
  return (b.lastStable == HIGH && b.stable == LOW);
}

// -------------------- TJpg_Decoder callback (CORRECT SIGNATURE) --------------------
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height()) return false;
  tft.pushImage(x, y, w, h, bitmap);
  return true;
}

// -------------------- Helpers --------------------
static void sendCmd(const char *cmd) {
  StaticJsonDocument<128> doc;
  doc["t"] = "cmd";
  doc["cmd"] = cmd;
  serializeJson(doc, Serial);
  Serial.print('\n');
}

static String formatTime(uint32_t ms) {
  uint32_t totalSec = ms / 1000;
  uint32_t m = totalSec / 60;
  uint32_t s = totalSec % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)m, (unsigned long)s);
  return String(buf);
}

static void drawWrappedText(const String &text, int x, int y, int w, int maxLines) {
  int idx = 0;
  int line = 0;
  int lh = tft.fontHeight() + 3;

  while (idx < (int)text.length() && line < maxLines) {
    while (idx < (int)text.length() && text[idx] == ' ') idx++;

    int start = idx;
    int lastSpace = -1;

    while (idx < (int)text.length()) {
      char c = text[idx];
      if (c == '\n') break;
      if (c == ' ') lastSpace = idx;

      String candidate = text.substring(start, idx + 1);
      if (tft.textWidth(candidate) > w) break;
      idx++;
    }

    int end = idx;

    if (idx < (int)text.length() && text[idx] == '\n') {
      end = idx;
      idx++;
    } else if (idx < (int)text.length() && tft.textWidth(text.substring(start, idx + 1)) > w) {
      if (lastSpace >= start) {
        end = lastSpace;
        idx = lastSpace + 1;
      }
    }

    String lineStr = text.substring(start, end);
    if (lineStr.length() == 0 && idx < (int)text.length()) {
      idx++;
      continue;
    }

    tft.drawString(lineStr, x, y + line * lh);
    line++;
  }
}

static void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);

  tft.drawRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, TFT_DARKGREY);
  tft.drawRect(TEXT_X - 5, 0, SCREEN_W - (TEXT_X - 5), BAR_Y - 5, TFT_DARKGREY);
  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_WHITE);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("ESP32 Spotify Display (USB)", 10, 250);
}

static void redrawMeta() {
  tft.fillRect(TEXT_X - 4, 1, SCREEN_W - (TEXT_X - 4) - 1, BAR_Y - 6, TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setTextFont(4);
  drawWrappedText(gTitle, TEXT_X, TEXT_Y, TEXT_W, 2);

  tft.setTextFont(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  drawWrappedText(gArtist, TEXT_X, TEXT_Y + 70, TEXT_W, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  drawWrappedText(gAlbum, TEXT_X, TEXT_Y + 120, TEXT_W, 3);

  gLastBarFillPx = -1;
}

static void initProgressBar() {
  tft.fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, TFT_BLACK);
  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_WHITE);
  gLastBarFillPx = 0;
}

static void drawProgress(uint32_t posMs, uint32_t durMs, bool playing) {
  if (durMs == 0) return;

  float frac = (float)posMs / (float)durMs;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;

  int fillPx = (int)((BAR_W - 2) * frac);
  if (fillPx == gLastBarFillPx) return;

  if (fillPx > gLastBarFillPx) {
    tft.fillRect(BAR_X + 1 + gLastBarFillPx, BAR_Y + 1, fillPx - gLastBarFillPx, BAR_H - 2, TFT_GREEN);
  } else {
    tft.fillRect(BAR_X + 1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, TFT_BLACK);
    tft.fillRect(BAR_X + 1, BAR_Y + 1, fillPx, BAR_H - 2, TFT_GREEN);
  }
  gLastBarFillPx = fillPx;

  tft.fillRect(BAR_X, BAR_Y - 18, BAR_W, 16, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  String left = formatTime(posMs);
  String right = formatTime(durMs);
  String mid = playing ? "PLAY" : "PAUSE";

  tft.drawString(left, BAR_X, BAR_Y - 18);
  tft.drawRightString(right, BAR_X + BAR_W, BAR_Y - 18, 2);
  tft.drawCentreString(mid, BAR_X + BAR_W / 2, BAR_Y - 18, 2);
}

static bool readExact(uint8_t *dst, size_t len, uint32_t timeoutMs = 5000) {
  size_t got = 0;
  uint32_t start = millis();
  while (got < len && (millis() - start) < timeoutMs) {
    int avail = Serial.available();
    if (avail > 0) {
      size_t chunk = (size_t)avail;
      if (chunk > (len - got)) chunk = len - got;
      got += Serial.readBytes(dst + got, chunk);
    } else {
      delay(1);
    }
  }
  return (got == len);
}

static void handleIncomingJsonLine(const String &line) {
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, line)) return;

  const char *t = doc["t"] | "";
  if (strcmp(t, "meta") == 0) {
    gTitle  = (const char *)(doc["title"]  | "");
    gArtist = (const char *)(doc["artist"] | "");
    gAlbum  = (const char *)(doc["album"]  | "");
    gDurMs  = (uint32_t)(doc["dur"] | 0);

    redrawMeta();
    initProgressBar();
  }
  else if (strcmp(t, "pos") == 0) {
    gPosMs = (uint32_t)(doc["pos"] | 0);
    gDurMs = (uint32_t)(doc["dur"] | gDurMs);
    gPlaying = (bool)(doc["playing"] | false);
    gLastPosUpdateMs = millis();
  }
  else if (strcmp(t, "mute") == 0) {
    gMuted = (bool)(doc["muted"] | false);
    digitalWrite(LED_MUTE, gMuted ? HIGH : LOW);
  }
  else if (strcmp(t, "img") == 0) {
    size_t imgLen = (size_t)(doc["len"] | 0);
    if (imgLen == 0 || imgLen > 500000) return;

    uint8_t *buf = (uint8_t*)ps_malloc(imgLen);
    if (!buf) buf = (uint8_t*)malloc(imgLen);
    if (!buf) return;

    if (!readExact(buf, imgLen, 8000)) {
      free(buf);
      return;
    }

    if (Serial.available() && Serial.peek() == '\n') Serial.read();

    tft.fillRect(ART_X + 1, ART_Y + 1, ART_SIZE - 2, ART_SIZE - 2, TFT_BLACK);
    TJpgDec.drawJpg(ART_X, ART_Y, buf, imgLen);

    free(buf);
  }
}

static String rxLine;
static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (rxLine.length() > 0) {
        handleIncomingJsonLine(rxLine);
        rxLine = "";
      }
    } else {
      if (rxLine.length() < 4096) rxLine += c;
    }
  }
}

void setup() {
  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_MUTE, INPUT_PULLUP);
  pinMode(BTN_SPOTIFY, INPUT_PULLUP);

  pinMode(LED_MUTE, OUTPUT);
  pinMode(LED_BACK, OUTPUT);

  digitalWrite(LED_MUTE, LOW);
  digitalWrite(LED_BACK, HIGH);

  Serial.begin(921600);

  tft.init();
  tft.setRotation(1);
  tft.setTextFont(2);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  drawStaticUI();
  initProgressBar();
}

void loop() {
  handleSerial();

  if (updateButton(bPlay) && fell(bPlay))       sendCmd("play_pause");
  if (updateButton(bPrev) && fell(bPrev))       sendCmd("prev");
  if (updateButton(bNext) && fell(bNext))       sendCmd("next");
  if (updateButton(bMute) && fell(bMute))       sendCmd("mute_toggle");
  if (updateButton(bSpotify) && fell(bSpotify)) sendCmd("spotify_open");

  if (gDurMs > 0 && millis() - gLastBarDrawAtMs > 120) {
    gLastBarDrawAtMs = millis();

    uint32_t shownPos = gPosMs;
    if (gPlaying && gLastPosUpdateMs != 0) {
      uint32_t delta = millis() - gLastPosUpdateMs;
      uint64_t temp = (uint64_t)gPosMs + (uint64_t)delta;
      shownPos = (temp > gDurMs) ? gDurMs : (uint32_t)temp;
    }
    drawProgress(shownPos, gDurMs, gPlaying);
  }

  delay(1);
}
