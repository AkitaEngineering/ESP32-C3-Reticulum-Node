#include "Config.h"

// Debug serial shim instance
DebugSerialShim DebugSerial;

// --- WiFi Credentials ---
const char *WIFI_SSID = ""; // <<< CHANGE ME
const char *WIFI_PASSWORD = ""; // <<< CHANGE ME

// --- Node Configuration ---
const char *BT_DEVICE_NAME = "ESP32-RNSGW"; // Changed slightly from default
