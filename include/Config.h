#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <array> // For group addresses

// --- LED Configuration ---
// Provide a fallback for boards that don't define LED_BUILTIN
#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // Common default for many ESP32 boards
#endif

// --- Debug and Interface Configuration ---
// Serial port selection can be swapped at compile time. By default
// the USB/UART0 port (Serial) is used for debug and a hardware UART
// (Serial1/Serial2) is used for the KISS data interface. This keeps
// debug traffic separate from KISS frames. If you want to run KISS
// directly over the USB CDC connection you can compile with
// -DKISS_OVER_USB=1. Doing so will move debug output to Serial1 and
// use Serial (USB) for KISS. Be aware that mixing debug text with
// KISS packets may corrupt the data stream unless debugging is kept
// very quiet or disabled.

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 1  // Set to 1 to enable debug logging
#endif

// Choose which physical Serial port the debug shim will wrap. The
// default is USB serial (Serial) but using KISS_OVER_USB will switch
// it to Serial1 so the USB port is free for KISS traffic.
#ifndef DEBUG_PORT
    #ifdef KISS_OVER_USB
        #define DEBUG_PORT Serial1
    #else
        #define DEBUG_PORT Serial
    #endif
#endif

// When KISS_OVER_USB is active, debug goes to a hardware UART.  We define
// the debug UART pin numbers BEFORE the DebugSerialShim class so they are
// visible inside the begin() method.
#if defined(KISS_OVER_USB)
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
        #ifndef DEBUG_UART_RX
        #define DEBUG_UART_RX 2
        #endif
        #ifndef DEBUG_UART_TX
        #define DEBUG_UART_TX 4
        #endif
    #else
        #ifndef DEBUG_UART_RX
        #define DEBUG_UART_RX -1
        #endif
        #ifndef DEBUG_UART_TX
        #define DEBUG_UART_TX -1
        #endif
    #endif
#endif

// Debug serial shim: when DEBUG_ENABLED is 0, debug output is suppressed while
// still allowing code to compile unchanged.
class DebugSerialShim : public Stream {
public:
    void begin(unsigned long baud) {
        // initialize whichever serial port is configured for debugging
#if defined(KISS_OVER_USB) && defined(DEBUG_UART_RX) && (DEBUG_UART_RX >= 0)
        // When KISS owns the USB CDC port, debug goes to a hardware UART.
        // We MUST supply explicit pin numbers to avoid clobbering the USB
        // D+/D− GPIOs (the default UART1 pins on ESP32-C3 are 18/19).
        DEBUG_PORT.begin(baud, SERIAL_8N1, DEBUG_UART_RX, DEBUG_UART_TX);
#else
        DEBUG_PORT.begin(baud);
#endif
    }

    int available() override { return DEBUG_ENABLED ? DEBUG_PORT.available() : 0; }
    int read() override { return DEBUG_ENABLED ? DEBUG_PORT.read() : -1; }
    int peek() override { return DEBUG_ENABLED ? DEBUG_PORT.peek() : -1; }
    void flush() override { if (DEBUG_ENABLED) DEBUG_PORT.flush(); }
    size_t write(uint8_t b) override { return DEBUG_ENABLED ? DEBUG_PORT.write(b) : 1; }
    size_t write(const uint8_t *buffer, size_t size) override {
        return DEBUG_ENABLED ? DEBUG_PORT.write(buffer, size) : size;
    }
};

extern DebugSerialShim DebugSerial; // Use USB/UART0 for debug (Arduino Serial Monitor)

// Platform-specific UART configuration
// ESP32-C3, ESP32-C5, ESP32-C6: Only UART0 and UART1 (no UART2)
// ESP32, ESP32-S2, ESP32-S3: UART0, UART1, and UART2
#if defined(KISS_OVER_USB)
    // If we're running KISS over the USB CDC port, just alias the
    // primary Serial object as the KISS transport.  No external pins
    // are required.  Debug output will be moved to Serial1 by the
    // DEBUG_PORT mechanics above.
    #define KissSerial Serial
    // RX/TX pin defines are unused when using USB, but define them anyway
    #define KISS_UART_RX 0
    #define KISS_UART_TX 0

    // DEBUG_UART_RX/TX are now defined above (before DebugSerialShim)

#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32C6)
    #define KissSerial Serial1    // Use UART1 for KISS
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
        // On the ESP32-C3 the native USB D+/D- lines are on GPIO18/19.  Using
        // the UART1 default pins (18/19) for the KISS interface will reconfigure
        // the USB pins and disconnect the CDC port immediately after it
        // enumerates, causing the host to report an "unknown device" and
        // produce a beep.  Map UART1 to alternate GPIOs that are free by
        // default (e.g. GPIO2/GPIO4) so USB can remain intact.  These choices
        // can be overridden if your hardware requires different pins.
        #define KISS_UART_RX 2    // alternate pin for UART1 RX (not USB)
        #define KISS_UART_TX 4    // alternate pin for UART1 TX (not USB)
    #elif defined(CONFIG_IDF_TARGET_ESP32C5)
        #define KISS_UART_RX 18   // ESP32-C5 UART1 default RX pin
        #define KISS_UART_TX 19   // ESP32-C5 UART1 default TX pin
    #elif defined(CONFIG_IDF_TARGET_ESP32C6)
        #define KISS_UART_RX 18   // ESP32-C6 UART1 default RX pin
        #define KISS_UART_TX 19   // ESP32-C6 UART1 default TX pin
    #endif
#else
    // Default: prefer a UART that exists on the target. ESP32-S2 does not
    // provide a Serial2 in the Arduino core, so use Serial1 there. ESP32-S3
    // and original ESP32 typically provide Serial2.
    #if defined(CONFIG_IDF_TARGET_ESP32S2)
        #define KissSerial Serial1    // Use UART1 for KISS on ESP32-S2
        #define KISS_UART_RX 33       // ESP32-S2 UART1 RX pin (adjust if needed)
        #define KISS_UART_TX 34       // ESP32-S2 UART1 TX pin (adjust if needed)
    #elif defined(CONFIG_IDF_TARGET_ESP32S3)
        #define KissSerial Serial2    // Use UART2 for KISS
        #define KISS_UART_RX 17       // ESP32-S3 UART2 RX pin
        #define KISS_UART_TX 18       // ESP32-S3 UART2 TX pin
    #else
        #define KissSerial Serial2    // Use UART2 for KISS (ESP32 original)
        #define KISS_UART_RX 16      // ESP32 (original) UART2 RX pin
        #define KISS_UART_TX 17      // ESP32 (original) UART2 TX pin
    #endif
#endif

// Bluetooth availability
// Conservative whitelist: only the original ESP32 (Xtensa core) is known to
// reliably provide Bluetooth Classic in the Arduino/IDF builds used here.
// Treat all other IDF targets as not having Classic to avoid compiling code
// that depends on Classic-only APIs on cores that only provide BLE or no BT.
#if defined(CONFIG_IDF_TARGET_ESP32)
    #define BLUETOOTH_CLASSIC_AVAILABLE 1
#else
    #define BLUETOOTH_CLASSIC_AVAILABLE 0
#endif

#define KISS_SERIAL_SPEED 115200

// Demo traffic configuration (disabled by default for production)
#ifndef DEMO_TRAFFIC_ENABLED
#define DEMO_TRAFFIC_ENABLED 0
#endif

// --- Runtime features & Web UI (disabled by default) ---
// Enable a lightweight Web UI + REST API for status/config (requires enabling and adding AsyncWebServer to build flags)
#ifndef WEBSERVER_ENABLED
#define WEBSERVER_ENABLED 0
#endif

// Enable runtime JSON config persisted in LittleFS/SPIFFS
#ifndef JSON_CONFIG_ENABLED
#define JSON_CONFIG_ENABLED 0
#endif

// Enable secure OTA (signed updates) support (requires OTA code + signature checks)
#ifndef OTA_ENABLED
#define OTA_ENABLED 0
#endif

// sanity checks for feature combinations
#if WEBSERVER_ENABLED && !JSON_CONFIG_ENABLED
    #error "WEBSERVER_ENABLED requires JSON_CONFIG_ENABLED for runtime config"
#endif
#if WEBSERVER_ENABLED && !OTA_ENABLED
    #error "WEBSERVER_ENABLED should accompany OTA_ENABLED to provide firmware updates"
#endif
#if OTA_ENABLED && !JSON_CONFIG_ENABLED
    #warning "OTA_ENABLED without JSON_CONFIG_ENABLED means updates cannot be disabled/controlled at runtime"
#endif
#ifndef WEBSERVER_PORT
#define WEBSERVER_PORT 80
#endif

// Web UI authentication (Bearer token). When enabled and a token is present
// in the runtime JSON config the REST API will require Authorization: Bearer <token>
#ifndef WEBSERVER_AUTH_ENABLED
#define WEBSERVER_AUTH_ENABLED 1
#endif

// BLE provisioning (GATT) for WiFi / callsign setup
#ifndef BLE_PROVISIONING_ENABLED
#define BLE_PROVISIONING_ENABLED 0
#endif

// Runtime metrics endpoint (JSON) and adjustable log levels
#ifndef METRICS_ENABLED
#define METRICS_ENABLED 0
#endif

// Metrics/telemetry configuration
#ifndef METRICS_UDP_ENABLED
#define METRICS_UDP_ENABLED 1    // send periodic UDP heartbeat when WiFi is up
#endif
#ifndef METRICS_UDP_PORT
#define METRICS_UDP_PORT 4243    // port used for UDP metrics broadcasts
#endif
// interval for sending UDP metrics (usually same as MEM_CHECK_INTERVAL_MS)
#ifndef METRICS_INTERVAL_MS
#define METRICS_INTERVAL_MS (MEM_CHECK_INTERVAL_MS)
#endif

// Keep WiFi radio active for ESP-NOW, but optionally skip STA/AP association.
// 0 = ESP-NOW-only mode (default), 1 = connect to AP if credentials are set.
#ifndef WIFI_STA_CONNECT_ENABLED
#define WIFI_STA_CONNECT_ENABLED 0
#endif

// ESP-NOW channel selection.
// In AP-connected mode use 0 (inherit AP channel). In ESP-NOW-only mode,
// default to channel 1 so all nodes land on a deterministic channel.
#ifndef ESP_NOW_CHANNEL
    #if WIFI_STA_CONNECT_ENABLED
        #define ESP_NOW_CHANNEL 0
    #else
        #define ESP_NOW_CHANNEL 1
    #endif
#endif

// When set to a non-zero channel number, the firmware will force the WiFi
// radio to that channel before initializing ESP-NOW. This can be useful when
// running in AP-only or STA-only mode without joining an access point, or when
// you want to ensure all nodes operate on a known channel regardless of the
// AP's current channel.
// Usage: add -DESP_NOW_CHANNEL=6 to build_flags or modify Config.h.

// --- WiFi Credentials ---
extern const char *WIFI_SSID; // <<< CHANGE ME in Config.cpp
extern const char *WIFI_PASSWORD; // <<< CHANGE ME in Config.cpp

// --- Node Configuration ---
extern const char *BT_DEVICE_NAME;
const int EEPROM_ADDR_NODE = 0;  // 8 bytes
const int EEPROM_ADDR_PKTID = 8; // 2 bytes (Start after node address)
const int EEPROM_SIZE = 16;      // Min size needed (8+2 = 10, use 16 or 32)

// --- Reticulum Network Parameters ---
const size_t RNS_ADDRESS_SIZE = 8;
const size_t RNS_MAX_PAYLOAD = 200; // Max data payload size (adjust based on memory/MTU)
const uint16_t RNS_UDP_PORT = 4242; // Default Reticulum UDP port
const uint8_t MAX_HOPS = 15;        // Max hop count for packets

// --- Timing & Intervals (milliseconds) ---
const uint16_t PACKET_ID_SAVE_INTERVAL = 100; // Save counter every N packets generated
const unsigned long ANNOUNCE_INTERVAL_MS = 180000; // Announce every 3 minutes
const unsigned long ROUTE_TIMEOUT_MS = ANNOUNCE_INTERVAL_MS * 3 + 15000; // Timeout after ~3 missed announces
const unsigned long PRUNE_INTERVAL_MS = ANNOUNCE_INTERVAL_MS / 2; // Check for old routes periodically
const unsigned long MEM_CHECK_INTERVAL_MS = 15000; // Check memory every 15 seconds
const unsigned long RECENT_ANNOUNCE_TIMEOUT_MS = ANNOUNCE_INTERVAL_MS / 2; // How long to remember forwarded announces

// --- Link Layer Parameters ---
const unsigned long LINK_REQ_TIMEOUT_MS = 10000; // Timeout for initial Link Request ACK
const unsigned long LINK_RETRY_TIMEOUT_MS = 5000; // Timeout for data packet ACK
const unsigned long LINK_INACTIVITY_TIMEOUT_MS = ROUTE_TIMEOUT_MS * 2; // Timeout for closing inactive links
const uint8_t LINK_MAX_RETRIES = 3; // Max retries for a packet before closing link
const size_t LINK_MAX_ACTIVE = 10; // Max concurrent active links (Adjust based on memory)

static_assert(LINK_MAX_ACTIVE > 0, "LINK_MAX_ACTIVE must be at least 1");

// --- Routing & Limits ---
const size_t MAX_ROUTES = 20;             // Max entries in routing table
const size_t MAX_RECENT_ANNOUNCES = 40; // Max announce IDs to remember for loop prevention

// compile‑time sanity
static_assert(MAX_ROUTES > 0, "MAX_ROUTES must be positive");
static_assert(MAX_RECENT_ANNOUNCES > 0, "MAX_RECENT_ANNOUNCES must be positive");

// --- Group Addresses ---
// Define groups this node belongs to. Example:
const std::vector<std::array<uint8_t, RNS_ADDRESS_SIZE>> SUBSCRIBED_GROUPS = {
    // {0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x01}, // Example Group 1
    // {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78}  // Example Group 2
    // Destination hash for PLAIN destination ["esp32", "node"] - calculated by tests/read_from_reticulum.py
    // This allows the ESP32 to receive messages sent to this destination
    {0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E} // First 8 bytes of 16-byte hash (truncated for group)

};

// --- LoRa Configuration ---
#ifdef LORA_ENABLED
    // Heltec LoRa32 v3 (ESP32-S3) pin definitions
    #if defined(HELTEC_LORA32_V3)
        #define LORA_CS_PIN 8
        #define LORA_RST_PIN 12
        #define LORA_DIO0_PIN 14
        #define LORA_SPI_SCK 9
        #define LORA_SPI_MISO 11
        #define LORA_SPI_MOSI 10
        #define LORA_FREQUENCY 915.0  // MHz (adjust for your region)
    // Heltec LoRa32 v4 (ESP32-C6) pin definitions
    #elif defined(HELTEC_LORA32_V4)
        #define LORA_CS_PIN 8
        #define LORA_RST_PIN 12
        #define LORA_DIO0_PIN 14
        #define LORA_SPI_SCK 9
        #define LORA_SPI_MISO 11
        #define LORA_SPI_MOSI 10
        #define LORA_FREQUENCY 915.0  // MHz (adjust for your region)
    // Generic LoRa configuration (customize for your board)
    #else
        #define LORA_CS_PIN 5
        #define LORA_RST_PIN 14
        #define LORA_DIO0_PIN 2
        #define LORA_SPI_SCK 18
        #define LORA_SPI_MISO 19
        #define LORA_SPI_MOSI 23
        #define LORA_FREQUENCY 915.0  // MHz (adjust for your region)
    #endif
    
    #define LORA_BANDWIDTH 125.0  // kHz
    #define LORA_SPREADING_FACTOR 7
    #define LORA_CODING_RATE 5
    #define LORA_SYNC_WORD 0x12
    #define LORA_OUTPUT_POWER 10  // dBm
    #define LORA_PREAMBLE_LENGTH 8
    #define LORA_GAIN 0  // 0 = automatic gain control
#endif

// --- HAM Modem Configuration ---
#ifdef HAM_MODEM_ENABLED
    // HAM modem interface configuration
    // Most HAM TNCs use KISS protocol over serial, which we already support
    // This enables additional HAM-specific features like APRS, AX.25, etc.
    #define HAM_MODEM_SERIAL Serial1  // Use a separate serial port for HAM modem
    #define HAM_MODEM_BAUD 9600       // Standard TNC baud rate (adjust as needed)
    #define HAM_MODEM_RX_PIN 4         // Adjust for your board
    #define HAM_MODEM_TX_PIN 5         // Adjust for your board
    
    // APRS (Automatic Packet Reporting System) configuration
    #define APRS_ENABLED 1
    #define APRS_CALLSIGN "N0CALL"     // <<< CHANGE ME: Your HAM callsign
    #define APRS_SSID 0                // APRS SSID (0-15)
    #define APRS_SYMBOL "["            // APRS symbol (see APRS spec)
    #define APRS_COMMENT "Reticulum"    // APRS comment field
    
    // Audio Modem Configuration
    #define AUDIO_MODEM_ENABLED 1
    #define AUDIO_MODEM_SAMPLE_RATE 8000  // Hz (8kHz typical for AFSK)
    #define AUDIO_MODEM_MARK_FREQ 1200     // Hz (Bell 202 mark frequency)
    #define AUDIO_MODEM_SPACE_FREQ 2200    // Hz (Bell 202 space frequency)
    #define AUDIO_MODEM_BAUD_RATE 1200     // baud (Bell 202 standard)
    #define AUDIO_MODEM_RX_PIN 34          // ADC pin for audio input
    #define AUDIO_MODEM_TX_PIN 25          // DAC pin for audio output (ESP32)
    
    // AX.25 Protocol Configuration
    #define AX25_ENABLED 1
    #define AX25_MAX_FRAME_SIZE 330        // Max AX.25 frame size
    #define AX25_DEFAULT_TX_DELAY 10       // TX delay in 10ms units
    #define AX25_DEFAULT_PERSISTENCE 63    // Persistence parameter (0-255)
    #define AX25_DEFAULT_SLOT_TIME 0      // Slot time in 10ms units
    #define AX25_DEFAULT_TX_TAIL 5        // TX tail in 10ms units
    #define AX25_DEFAULT_FULL_DUPLEX 0    // 0 = half duplex, 1 = full duplex
    
    // Winlink Configuration
    #define WINLINK_ENABLED 1
    #define WINLINK_BBS_CALLSIGN "N0BBS"   // <<< CHANGE ME: Winlink BBS callsign
    #define WINLINK_PASSWORD ""            // <<< CHANGE ME: Winlink password (if required)
#endif

// --- IPFS Configuration ---
#ifdef IPFS_ENABLED
    // IPFS gateway configuration (lightweight client approach)
    #define IPFS_GATEWAY_URL "https://ipfs.io/ipfs/"  // Public IPFS gateway
    // Alternative gateways: "https://gateway.pinata.cloud/ipfs/", "https://dweb.link/ipfs/"
    #define IPFS_MAX_CONTENT_SIZE 10240  // Max content size to fetch (10KB, adjust based on memory)
    #define IPFS_TIMEOUT_MS 10000        // HTTP request timeout
    #define IPFS_CACHE_SIZE 5            // Number of IPFS objects to cache in memory
    
    // IPFS Local Node API (for publishing)
    #define IPFS_LOCAL_NODE_URL "http://localhost:5001"  // Local IPFS node API
    #define IPFS_LOCAL_NODE_ENABLED 0    // Set to 1 if you have a local IPFS node
    #define IPFS_PUBLISH_TIMEOUT_MS 30000 // Publishing timeout (longer for large files)
#endif

// --- Interface Identifiers ---
enum class InterfaceType {
    UNKNOWN,
    LOCAL, // For packets originating from this node
    SERIAL_PORT,
    BLUETOOTH,
    ESP_NOW,
    WIFI_UDP,
    LORA,
    HAM_MODEM,  // HAM radio modem interface
    IPFS         // IPFS content addressing (virtual interface)
};

// --- Packet Contexts (Includes Link and Local Command) ---
#define RNS_CONTEXT_NONE        0x00
#define RNS_CONTEXT_LINK_REQ    0xA1 // Request to establish link
#define RNS_CONTEXT_LINK_CLOSE  0xA2 // Request to close link
#define RNS_CONTEXT_LINK_DATA   0xA3 // Data packet over an established link
#define RNS_CONTEXT_ACK         0xA4 // Context used in ACK header_type packets
#define RNS_CONTEXT_LOCAL_CMD   0xFE // Context for local commands via KISS

// Sequence number size (placed at start of payload for LINK_DATA/ACK)
const size_t RNS_SEQ_SIZE = 2;


#endif // CONFIG_H
