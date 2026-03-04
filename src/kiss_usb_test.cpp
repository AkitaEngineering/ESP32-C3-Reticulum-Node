// Progressive KISS_OVER_USB test: add subsystems one at a time to find USB killer.
// Flash with:  pio run -e esp32-c3-kiss-usb-test -t upload --upload-port /dev/ttyACMx
// LED blink count tells you it reached that stage. Solid = loop running.
// Connect USB-UART to GPIO4 (TX) to see debug, or just watch /dev/ttyACM*.
//
// Enable stages with defines (all default ON):
//   TEST_WIFI=1   TEST_ESPNOW=1   TEST_EEPROM=1   TEST_WIFI_CONNECT=1
//   TEST_NTP=1    TEST_UDP=1      TEST_WEBSERVER=1

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <EEPROM.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// Stage toggles — define these via build_flags to isolate
#ifndef TEST_WIFI
#define TEST_WIFI 1
#endif
#ifndef TEST_ESPNOW
#define TEST_ESPNOW 1
#endif
#ifndef TEST_EEPROM
#define TEST_EEPROM 1
#endif
#ifndef TEST_WIFI_CONNECT
#define TEST_WIFI_CONNECT 1
#endif
#ifndef TEST_NTP
#define TEST_NTP 0   // off by default, needs WiFi connected
#endif
#ifndef TEST_UDP
#define TEST_UDP 0   // off by default, needs WiFi connected
#endif
#ifndef TEST_WEBSERVER
#define TEST_WEBSERVER 0 // off by default
#endif
#ifndef TEST_SERIAL_IO
#define TEST_SERIAL_IO 1
#endif

void blinkN(int n, int ms) {
    for (int i = 0; i < n; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(ms);
        digitalWrite(LED_BUILTIN, LOW);
        delay(ms);
    }
}

static void espnow_recv_cb(const uint8_t *mac, const uint8_t *data, int len) {
    Serial1.print("[ESPNOW-RX] ");
    Serial1.print(len);
    Serial1.println(" bytes");
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    blinkN(2, 100);
    delay(200);

    // Debug UART on safe pins
    Serial1.begin(115200, SERIAL_8N1, 2, 4);
    Serial1.println("\n[TEST] Stage: Serial1 debug on GPIO2/4");
    blinkN(3, 100);
    delay(200);

    // USB wait
    Serial1.println("[TEST] Stage: USB wait (5s max)...");
    unsigned long start = millis();
    while (!Serial && millis() - start < 5000) {
        digitalWrite(LED_BUILTIN, ((millis() / 250) & 1) ? HIGH : LOW);
        delay(10);
    }
    Serial1.print("[TEST] Serial=");
    Serial1.println(Serial ? "true" : "false");
    blinkN(4, 100);
    delay(200);

#if TEST_EEPROM
    Serial1.println("[TEST] Stage: EEPROM.begin(512)...");
    if (EEPROM.begin(512)) {
        Serial1.println("[TEST] EEPROM OK");
        // Read a byte to exercise it
        uint8_t val = EEPROM.read(0);
        Serial1.print("[TEST] EEPROM[0]=0x");
        Serial1.println(val, HEX);
    } else {
        Serial1.println("[TEST] EEPROM FAILED");
    }
    blinkN(5, 100);
    delay(200);
#endif

#if TEST_WIFI
    Serial1.println("[TEST] Stage: WiFi init...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    Serial1.println("[TEST] WiFi AP_STA mode set");
    blinkN(6, 100);
    delay(200);
#endif

#if TEST_WIFI_CONNECT && TEST_WIFI
    Serial1.println("[TEST] Stage: WiFi.begin() connecting...");
    WiFi.begin("", ""); // empty SSID — will fail fast
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 6) {
        delay(500);
        Serial1.print(".");
        attempts++;
    }
    Serial1.println();
    Serial1.print("[TEST] WiFi status=");
    Serial1.println(WiFi.status());
    blinkN(7, 100);
    delay(200);
#endif

#if TEST_ESPNOW
    Serial1.println("[TEST] Stage: ESP-NOW init...");
    if (esp_now_init() == ESP_OK) {
        Serial1.println("[TEST] ESP-NOW OK");
        esp_now_register_recv_cb(espnow_recv_cb);
        esp_now_peer_info_t peerInfo = {};
        memset(peerInfo.peer_addr, 0xFF, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
        Serial1.println("[TEST] Broadcast peer added");
    } else {
        Serial1.println("[TEST] ESP-NOW FAILED");
    }
    blinkN(8, 100);
    delay(200);
#endif

#if TEST_SERIAL_IO
    // Actively try reading/writing USB CDC in a burst
    Serial1.println("[TEST] Stage: Serial I/O burst test...");
    Serial.println("[TEST] Hello from USB CDC");
    Serial.flush();
    delay(100);
    // Drain any pending input
    while (Serial.available()) Serial.read();
    Serial1.println("[TEST] Serial I/O burst done");
    blinkN(9, 100);
    delay(200);
#endif

    digitalWrite(LED_BUILTIN, HIGH);
    Serial1.println("[TEST] Setup complete. Entering main loop.");
    Serial1.print("[TEST] Free heap: ");
    Serial1.println(ESP.getFreeHeap());
}

void loop() {
    // Echo USB CDC data
    while (Serial.available()) {
        uint8_t b = Serial.read();
        Serial.write(b);
    }

    // Heartbeat
    static unsigned long lastHB = 0;
    if (millis() - lastHB > 5000) {
        lastHB = millis();
        Serial1.print("[HB] t=");
        Serial1.print(millis() / 1000);
        Serial1.print("s Serial=");
        Serial1.print(Serial ? "Y" : "N");
        Serial1.print(" heap=");
        Serial1.println(ESP.getFreeHeap());
    }
}
