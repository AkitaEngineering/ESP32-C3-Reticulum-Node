#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <vector>
#include <array> // For group addresses

// --- Debug and Interface Configuration ---
// Use Serial (UART0/USB) for debug messages (normal Arduino Serial Monitor)
// Use Serial1/Serial2 (UART1/UART2) for KISS interface with Reticulum

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 1  // Set to 1 to enable debug logging
#endif

// Debug serial shim: when DEBUG_ENABLED is 0, debug output is suppressed while
// still allowing code to compile unchanged.
class DebugSerialShim : public Stream {
public:
    void begin(unsigned long baud) {
        (void)baud;
        // Keep Serial initialized for potential USB connection even if debug is disabled.
        Serial.begin(baud);
    }

    int available() override { return DEBUG_ENABLED ? Serial.available() : 0; }
    int read() override { return DEBUG_ENABLED ? Serial.read() : -1; }
    int peek() override { return DEBUG_ENABLED ? Serial.peek() : -1; }
    void flush() override { if (DEBUG_ENABLED) Serial.flush(); }
    size_t write(uint8_t b) override { return DEBUG_ENABLED ? Serial.write(b) : 1; }
    size_t write(const uint8_t *buffer, size_t size) override {
        return DEBUG_ENABLED ? Serial.write(buffer, size) : size;
    }
};

extern DebugSerialShim DebugSerial; // Use USB/UART0 for debug (Arduino Serial Monitor)

// Platform-specific UART configuration
// ESP32-C3, ESP32-C5, ESP32-C6: Only UART0 and UART1 (no UART2)
// ESP32, ESP32-S2, ESP32-S3: UART0, UART1, and UART2
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32C6)
    #define KissSerial Serial1    // Use UART1 for KISS
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
        #define KISS_UART_RX 18   // ESP32-C3 UART1 default RX pin
        #define KISS_UART_TX 19   // ESP32-C3 UART1 default TX pin
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
#ifndef WEBSERVER_PORT
#define WEBSERVER_PORT 80
#endif

// Web UI authentication (Bearer token). When enabled and a token is present
// in the runtime JSON config the REST API will require Authorization: Bearer <token>
#ifndef WEBSERVER_AUTH_ENABLED
#define WEBSERVER_AUTH_ENABLED 1
#endif

// Enable runtime JSON config persisted in LittleFS/SPIFFS
#ifndef JSON_CONFIG_ENABLED
#define JSON_CONFIG_ENABLED 0
#endif
// Example path: /config.json in SPIFFS/LittleFS

// Enable secure OTA (signed updates) support (requires OTA code + signature checks)
#ifndef OTA_ENABLED
#define OTA_ENABLED 0
#endif

// BLE provisioning (GATT) for WiFi / callsign setup
#ifndef BLE_PROVISIONING_ENABLED
#define BLE_PROVISIONING_ENABLED 0
#endif

// Runtime metrics endpoint (JSON) and adjustable log levels
#ifndef METRICS_ENABLED
#define METRICS_ENABLED 0
#endif

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
const uint16_t PACKET_ID_SAVE_INTERVAL = 5000; // Save counter every N packets generated (increased to reduce EEPROM wear)
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

// --- Routing & Limits ---
const size_t MAX_ROUTES = 20;             // Max entries in routing table
const size_t MAX_RECENT_ANNOUNCES = 40; // Max announce IDs to remember for loop prevention

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
