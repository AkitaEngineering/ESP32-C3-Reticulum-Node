#ifndef INTERFACE_MANAGER_H
#define INTERFACE_MANAGER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "Config.h"
#if BLUETOOTH_CLASSIC_AVAILABLE
#include <BluetoothSerial.h> // Requires CONFIG_BT_ENABLED=y and CONFIG_CLASSIC_BT_ENABLED=y in sdkconfig
#endif
#include <esp_now.h>
#include <esp_idf_version.h>
#include <functional>
#include <vector>
#include <array>
#include <deque>
#include <IPAddress.h> // Include IPAddress

#ifdef LORA_ENABLED
#include <RadioLib.h>
#endif

#ifdef HAM_MODEM_ENABLED
  #ifdef AUDIO_MODEM_ENABLED
    #include "AudioModem.h"
  #endif
  #ifdef WINLINK_ENABLED
    #include "Winlink.h"
  #endif
#endif

#ifdef IPFS_ENABLED
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

#include "KISS.h"

// Forward declarations
class RoutingTable;
class ReticulumNode;

// Callback type for received packets:
// void packet_receiver(const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
//                      const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port)
using PacketReceiverCallback = std::function<void(const uint8_t*, size_t, InterfaceType, const uint8_t*, const IPAddress&, uint16_t)>;

class InterfaceManager {
public:
    InterfaceManager(PacketReceiverCallback receiver, RoutingTable& routingTable);

    void setup();
    void loop(); // Process inputs from interfaces

    // Sending methods
    // Sends packet out relevant interfaces based on routing (or broadcast), excluding source interface
    void sendPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, InterfaceType excludeInterface = InterfaceType::UNKNOWN);
    // Sends packet via a specific interface type (used internally or for specific needs)
    void sendPacketVia(InterfaceType ifType, const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    // Broadcasts an announce packet on relevant interfaces
    void broadcastAnnounce(const uint8_t *packetBuffer, size_t packetLen);
#if METRICS_ENABLED && METRICS_UDP_ENABLED
    // send periodic JSON metrics via UDP broadcast
    void sendUdpMetrics(const String &json);
#endif
    // ESP-NOW runtime control
    bool isEspNowInitialized() const { return _espNowInitialized; }
    void enableEspNow();
    void disableEspNow();

#ifdef LORA_ENABLED
    // LoRa-specific methods
    bool isLoRaInitialized() const { return _loraInitialized; }
#endif

#ifdef IPFS_ENABLED
    // IPFS methods
    bool fetchIPFSContent(const char* ipfsHash, std::vector<uint8_t>& output);
    bool publishToIPFS(const uint8_t* data, size_t len, String& ipfsHash);
#endif

#ifdef HAM_MODEM_ENABLED
    // HAM modem methods
    bool isHAMModemInitialized() const { return _hamModemInitialized; }
    void sendAPRSPacket(const char* destination, const char* message);
    void sendAPRSPosition(float lat, float lon, float altitude = 0, const char* comment = "");
    void sendAPRSWeather(float temp, float humidity, float pressure, const char* comment = "");
    void sendAPRSMessage(const char* addressee, const char* message);
    // Retrieve decoded AX.25 frames from audio modem (if enabled)
    bool pollAX25FromAudioModem();
#endif

    // ESP-NOW Peer Management (can be called by RoutingTable during prune)
    // internally we keep a mirror list of peers for diagnostics
    bool addEspNowPeer(const uint8_t* mac_addr);
    bool removeEspNowPeer(const uint8_t* mac_addr);
    bool checkEspNowPeer(const uint8_t* mac_addr);

    // Debug helpers
    void printEspNowPeers();

    // Static callbacks needed for C-style APIs like ESP-NOW
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    static void staticEspNowRecvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len);
#else
    static void staticEspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *incomingData, int len);
#endif
    // static void staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status); // Optional

private:
    struct EspNowRxAssembly {
        bool active = false;
        std::array<uint8_t, 6> senderMac = {0};
        uint16_t messageId = 0;
        uint8_t totalChunks = 0;
        bool hasCrc = false;
        uint32_t expectedCrc = 0;
        unsigned long lastUpdateMs = 0;
        std::vector<std::vector<uint8_t>> chunks;
        std::vector<uint8_t> receivedChunk;
        size_t receivedCount = 0;
    };

    struct EspNowQueuedPacket {
        std::vector<uint8_t> payload;
        std::array<uint8_t, RNS_ADDRESS_SIZE> destination = {0};
        bool hasDestination = false;
        uint8_t attempts = 0;
        unsigned long nextTryMs = 0;
    };

    static constexpr uint8_t ESPNOW_FRAG_MAGIC0 = 0x52; // 'R'
    static constexpr uint8_t ESPNOW_FRAG_MAGIC1 = 0x4E; // 'N'
    static constexpr uint8_t ESPNOW_FRAG_VERSION = 1;
    static constexpr size_t ESPNOW_FRAG_HEADER_LEN = 12;
    static constexpr size_t ESPNOW_MAX_PAYLOAD_LEN = 250;
    static constexpr size_t ESPNOW_FRAG_CHUNK_DATA_LEN = ESPNOW_MAX_PAYLOAD_LEN - ESPNOW_FRAG_HEADER_LEN;
    static constexpr size_t ESPNOW_MAX_RX_ASSEMBLIES = 6;
    static constexpr unsigned long ESPNOW_REASSEMBLY_TIMEOUT_MS = 1500;

    void setupWiFi();
    void setupESPNow();
    void setupBluetooth();
    void setupSerial();
#ifdef LORA_ENABLED
    void setupLoRa();
#endif
#ifdef HAM_MODEM_ENABLED
    void setupHAMModem();
#endif
#ifdef IPFS_ENABLED
    void setupIPFS();
#endif

    void processWiFiInput();
    void processSerialInput();
    void processBluetoothInput();
#ifdef LORA_ENABLED
    void processLoRaInput();
#endif
#ifdef HAM_MODEM_ENABLED
    void processHAMModemInput();
#endif
    bool parseEspNowFragment(const uint8_t *data, int len, uint16_t &messageId, uint8_t &chunkIndex, uint8_t &totalChunks, bool &hasCrc, uint32_t &expectedCrc, const uint8_t* &chunkData, size_t &chunkLen) const;
    EspNowRxAssembly* getEspNowAssemblySlot(const uint8_t *senderMac, uint16_t messageId, uint8_t totalChunks);
    void handleEspNowPayload(const uint8_t *senderMac, const uint8_t *incomingData, int len);
    void cleanupExpiredEspNowAssemblies();
    void processEspNowStoreForward();
    bool enqueueEspNowPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    bool sendPacketViaEspNowInternal(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, bool enqueueOnFailure);

    // Specific Send implementations called by public send methods
    bool sendPacketViaEspNow(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaWiFi(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
    void sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen);
    void sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen);
#ifdef LORA_ENABLED
    void sendPacketViaLoRa(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
#endif
#ifdef HAM_MODEM_ENABLED
    void sendPacketViaHAMModem(const uint8_t *packetBuffer, size_t packetLen);
#endif
#ifdef IPFS_ENABLED
    void sendPacketViaIPFS(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr);
#endif

    PacketReceiverCallback _packetReceiver; // Callback to ReticulumNode::handleReceivedPacket
    RoutingTable& _routingTableRef; // Reference for route lookups / peer management

    bool _espNowInitialized;

    // Keep a mirror of ESP-NOW peers added so we can display/remove them
    std::vector<std::array<uint8_t,6>> _espNowPeers;
    std::vector<EspNowRxAssembly> _espNowRxAssemblies;
    std::deque<EspNowQueuedPacket> _espNowStoreQueue;
    uint16_t _espNowTxMessageId = 0;

    WiFiUDP _udp;
#if BLUETOOTH_CLASSIC_AVAILABLE
    BluetoothSerial _serialBT; // Bluetooth Serial object
    KISSProcessor _bluetoothKissProcessor; // KISS processor for Bluetooth
#endif
    KISSProcessor _serialKissProcessor; // KISS processor for USB Serial
#ifdef LORA_ENABLED
    SX1278* _lora; // LoRa radio instance
    bool _loraInitialized;
#endif
#ifdef HAM_MODEM_ENABLED
    KISSProcessor _hamModemKissProcessor; // KISS processor for HAM modem
    bool _hamModemInitialized;
    #ifdef AUDIO_MODEM_ENABLED
    AudioModem* _audioModem; // Audio modem instance
    TaskHandle_t _audioCaptureTaskHandle = nullptr;
    #endif
    #ifdef WINLINK_ENABLED
    Winlink* _winlink; // Winlink instance
    #endif
#endif
#ifdef IPFS_ENABLED
    HTTPClient _httpClient; // HTTP client for IPFS gateway access
    bool _ipfsInitialized;
#endif

    // Static instance pointer for callbacks to access 'this'
    static InterfaceManager* _instance;

    // Kiss packet handler method (non-static member) called by KISSProcessor instances
    void handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface);

#if defined(HAM_MODEM_ENABLED) && defined(AUDIO_MODEM_ENABLED)
    // Audio capture loop (FreeRTOS task)
    static void audioCaptureTask(void* arg);
    void audioCaptureLoop();
#endif

#if defined(HAM_MODEM_ENABLED) && defined(WINLINK_ENABLED)
    static bool winlinkSendRaw(const uint8_t* data, size_t len, void* context);
#endif

};

#endif // INTERFACE_MANAGER_H
