#include <Arduino.h>
#include "Config.h"

// Minimal USB CDC test: initialize Serial, print early messages, blink LED.
void setup() {
  Serial.begin(115200);

  // Wait up to 3000 ms for USB CDC to enumerate
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
    delay(10);
  }

  Serial.println("[TEST] minimal_main starting");
  Serial.printf("[TEST] millis=%lu\r\n", millis());

  initStatusLed();
}

void loop() {
  setStatusLed(true);
  delay(250);
  setStatusLed(false);
  delay(250);
  if (Serial) {
    Serial.println("[TEST] alive");
  }
}
