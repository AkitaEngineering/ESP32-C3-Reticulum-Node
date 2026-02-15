#include <Arduino.h>

// Minimal main that avoids early formatted I/O to isolate vfprintf/newlib issues.
// Serial is initialized later by a safe helper if needed; here we keep startup minimal.
void setup() {
  // Do not call Serial or printf here to avoid pulling in vfprintf early.
}

void loop() {
  // Minimal idle loop. Device stability check should be done with external serial monitor.
  delay(1000);
}
