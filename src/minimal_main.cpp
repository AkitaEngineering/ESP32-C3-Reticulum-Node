#include <Arduino.h>

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

  // Blink setup: try LED_BUILTIN if available, otherwise use GPIO8 as fallback
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#else
  const int ledPin = 8;
  pinMode(ledPin, OUTPUT);
#endif
}

void loop() {
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, HIGH);
  delay(250);
  digitalWrite(LED_BUILTIN, LOW);
  delay(250);
#else
  digitalWrite(8, HIGH);
  delay(250);
  digitalWrite(8, LOW);
  delay(250);
#endif
  if (Serial) {
    Serial.println("[TEST] alive");
  }
}
