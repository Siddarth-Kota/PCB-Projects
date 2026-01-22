#include <TFT_eSPI.h> 
#include <SPI.h>
#include <TJpg_Decoder.h>
#include "USB.h"
#include "USBHIDConsumerControl.h" 
#include "USBHIDKeyboard.h" // Added for specific key combos

TFT_eSPI tft = TFT_eSPI(); 
USBHIDConsumerControl ConsumerControl;
USBHIDKeyboard Keyboard; // Create Keyboard object

uint8_t* bigJpgBuffer = nullptr;

// --- GPIO Pin Definitions ---
const int PIN_MUTE_LED = 16; 
const int PIN_BACKLIGHT = 18; 
const int BTN_PLAY = 4, BTN_PREV = 5, BTN_NEXT = 6, BTN_MUTE = 7, BTN_5 = 15;

const int IMG_X = 10, IMG_Y = 60, IMG_SIZE = 180;
bool isMuted = false;

void setup() {
  Serial.begin(115200);
  ConsumerControl.begin();
  Keyboard.begin(); // Initialize Keyboard
  USB.begin();

  pinMode(PIN_MUTE_LED, OUTPUT);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWrite(PIN_BACKLIGHT, 255); 

  pinMode(BTN_PLAY, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_MUTE, INPUT_PULLUP);
  pinMode(BTN_5,    INPUT_PULLUP);

  if (psramInit()) {
    bigJpgBuffer = (uint8_t*) ps_malloc(100000); // PSRAM for N16R8
  }

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback([](int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) -> bool {
    if (y >= tft.height()) return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
  });
}

void handleButtons() {
  static unsigned long lastBtnPress = 0;
  if (millis() - lastBtnPress < 250) return; // Debounce

  // PLAY/PAUSE
  if (digitalRead(BTN_PLAY) == LOW) { 
    ConsumerControl.press(CONSUMER_CONTROL_PLAY_PAUSE); 
    ConsumerControl.release(); // Must release to avoid "stuck" key
    lastBtnPress = millis(); 
  }
  
  // NEXT TRACK
  if (digitalRead(BTN_NEXT) == LOW) { 
    ConsumerControl.press(CONSUMER_CONTROL_SCAN_NEXT); 
    ConsumerControl.release();
    lastBtnPress = millis(); 
  }
  
  // PREVIOUS TRACK
  if (digitalRead(BTN_PREV) == LOW) { 
    ConsumerControl.press(CONSUMER_CONTROL_SCAN_PREVIOUS); 
    ConsumerControl.release();
    lastBtnPress = millis(); 
  }
  
  // MUTE Toggle with LED
  if (digitalRead(BTN_MUTE) == LOW) { 
    ConsumerControl.press(CONSUMER_CONTROL_MUTE); 
    ConsumerControl.release();
    isMuted = !isMuted; 
    digitalWrite(PIN_MUTE_LED, isMuted ? HIGH : LOW); 
    lastBtnPress = millis(); 
  }

  // Button 5: Open Spotify Shortcut (Ctrl + Alt + Shift + S)
  if (digitalRead(BTN_5) == LOW) {
    Keyboard.press(KEY_LEFT_CTRL);
    Keyboard.press(KEY_LEFT_ALT);
    Keyboard.press(KEY_LEFT_SHIFT);
    Keyboard.press('s');
    delay(100);
    Keyboard.releaseAll(); 
    lastBtnPress = millis();
  }
}

void loop() {
  handleButtons();
  if (Serial.available() > 0) {
    String header = Serial.readStringUntil('|');
    if (header == "TXT") {
      String title = Serial.readStringUntil('|');
      String artist = Serial.readStringUntil('|');
      String album = Serial.readStringUntil('\n');
      tft.fillRect(205, 60, 275, 150, TFT_BLACK); 
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString(title, 210, 60, 4);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString(artist, 210, 105, 4);
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.drawString(album, 210, 150, 2);
    } 
    else if (header == "IMG") {
      uint32_t imgSize;
      Serial.readBytes((char*)&imgSize, 4);
      if (bigJpgBuffer && imgSize < 100000) {
        Serial.readBytes(bigJpgBuffer, imgSize);
        tft.fillRect(IMG_X - 2, IMG_Y - 2, IMG_SIZE + 4, IMG_SIZE + 4, TFT_BLACK); 
        tft.drawRect(IMG_X - 1, IMG_Y - 1, IMG_SIZE + 2, IMG_SIZE + 2, TFT_WHITE);
        TJpgDec.drawJpg(IMG_X, IMG_Y, bigJpgBuffer, imgSize); 
      }
    }
    else if (header == "PROG") {
      int percent = Serial.parseInt();
      int fillWidth = map(percent, 0, 100, 0, 460);
      tft.fillRect(10, 245, fillWidth, 16, 0x1DB9); 
      tft.fillRect(10 + fillWidth, 245, 460 - fillWidth, 16, 0x39C7); 
    }
  }
}