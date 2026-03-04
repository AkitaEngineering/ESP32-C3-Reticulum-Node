#include <Arduino.h>
#include <U8g2lib.h>

// Try I2C SSD1306 (common address 0x3C). If not present, no display output.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Display test: setup");

  // init U8g2 (will start Wire)
  u8g2.begin();

  // Try to draw a simple message
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "BOOT OK");
  u8g2.drawStr(0, 28, "ESP32 Display Test");
  u8g2.sendBuffer();

  Serial.println("Display test: drawn BOOT OK");
}

void loop() {
  // keep something visible; blink a tiny dot
  static bool on = false;
  on = !on;
  u8g2.setDrawColor(on ? 1 : 0);
  u8g2.drawBox(120, 0, 8, 8);
  u8g2.sendBuffer();
  delay(500);
}
