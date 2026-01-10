#include <TFT_eSPI.h>
#include <DigitalRainAnimation.h>

TFT_eSPI tft = TFT_eSPI();
DigitalRainAnimation<TFT_eSPI> matrix_effect = DigitalRainAnimation<TFT_eSPI>();

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n TFT_eSPI Basic Test");

  tft.begin();
  tft.setRotation(1);
  matrix_effect.init(&tft);
}


void loop() {
  matrix_effect.loop();
}