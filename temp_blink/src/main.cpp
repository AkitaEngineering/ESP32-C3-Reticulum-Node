#include <Arduino.h>

const int LED = 2; // common onboard LED pin for many TTGO boards

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  Serial.println("Blink test starting");
}

void loop() {
  digitalWrite(LED, HIGH);
  Serial.println("LED ON");
  delay(500);
  digitalWrite(LED, LOW);
  Serial.println("LED OFF");
  delay(500);
}
