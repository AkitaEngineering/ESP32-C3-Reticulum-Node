#include <Arduino.h>

// Multi-pin blink + serial test for board LED discovery
const int PINS[] = {2, 8, 9, 10};
const size_t PIN_COUNT = sizeof(PINS) / sizeof(PINS[0]);

void setup() {
  Serial.begin(115200);
  for (size_t i = 0; i < PIN_COUNT; ++i) {
    pinMode(PINS[i], OUTPUT);
    digitalWrite(PINS[i], LOW);
  }
  Serial.println("Multi-pin blink test starting");
}

void loop() {
  for (size_t i = 0; i < PIN_COUNT; ++i) {
    int pin = PINS[i];
    digitalWrite(pin, HIGH);
    Serial.print("PIN "); Serial.print(pin); Serial.println(" ON");
    delay(250);
    digitalWrite(pin, LOW);
    Serial.print("PIN "); Serial.print(pin); Serial.println(" OFF");
    delay(250);
  }
}
