#ifndef RETICULUM_NODE_H
#define RETICULUM_NODE_H

#include <Arduino.h>
#include <vector>
#include <list>
#include <array>
#include <functional> // For std::function

#include "Config.h"
// Include necessary component headers
#include "ReticulumPacket.h"
#include "InterfaceManager.h"
#include "RoutingTable.h"
#include "LinkManager.h" // Include the LinkManager header
#include "RNSCrypto.h"   // Reticulum-compatible cryptographic identity

// Callback for application layer to receive data from Links
using AppDataHandler = std::function<void(const uint8_t* source_address, const std::vector<uint8_t>& data)>;

class ReticulumNode {
public:
    ReticulumNode(); // Constructor
    void setup();    // System initialization
    void loop();     // Main execution loop

    // --- Public methods needed by owned classes (e.g., LinkManager) ---
    const uint8_t* getNodeAddress() const { return _nodeAddress; }
    uint16_t getNextPacketId(); // Now public for LinkManager access
    InterfaceManager& getInterfaceManager() { return _interfaceManager; }
    LinkManager& getLinkManager() { return _linkManager; }
    RoutingTable& getRoutingTable() { return _routingTable; }
    RNSCrypto& getIdentity() { return _identity; }

    // --- Application Layer Integration ---
    // Sets the handler function for received link data
    void setAppDataHandler(AppDataHandler handler);
    // Called by LinkManager when reliable data arrives for the app
    void processAppData(const uint8_t* source_address, const std::vector<uint8_t>& data);

    // --- Persistence / test helpers ---
    // Force reload config from EEPROM (useful for tests)
    void loadConfigFromEEPROM();
    // Synchronously save node address + packet counter to EEPROM
    void saveConfigNow();
    // Return current in-memory packet counter (for tests/inspection)
    uint16_t getPacketCounter() const;

private:
    // --- Initialization Helpers ---
    void loadConfig();                // Loads address/packet ID from EEPROM
    void loadOrGenerateAddress();     // Load existing or generate new node address
    void savePacketCounterIfNeeded(); // Saves packet ID to EEPROM periodically
    void loadPacketCounter();
    void generateNodeAddress();
    void deriveNodeAddressFromEfuse(uint8_t outAddr[RNS_ADDRESS_SIZE]);
    void saveNodeAddress();
    void printNodeAddress();

    // --- Periodic Tasks ---
    void checkMemoryUsage();
    void sendAnnounceIfNeeded(); // Generates and sends announce packets

    // --- Debug CLI ---
    void processDebugCommands(); // Process text commands from debug serial
    void printStatus();          // Print node/interface status summary

    // --- Core Packet Handling ---
    // Main callback passed to InterfaceManager
    void handleReceivedPacket(const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
                              const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port);
    // Handles packets addressed to this node (or subscribed groups) that are NOT link-related
    void processPacketForSelf(const RnsPacketInfo& packetInfo, InterfaceType interface);
    // Handles forwarding of non-link, non-announce packets
    void forwardPacket(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface);
    // Handles forwarding/re-broadcasting of announce packets
    void forwardAnnounce(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface);

    // --- Member Variables ---
    uint8_t _nodeAddress[RNS_ADDRESS_SIZE];
    uint16_t _packetCounter = 0;
    uint16_t _packetIdUnsavedCount = 0;

    RNSCrypto _identity;  // Reticulum-compatible cryptographic identity
    uint8_t _destinationHash[16]; // 16-byte destination hash derived from identity

    RoutingTable _routingTable;       // Owns the routing table instance
    InterfaceManager _interfaceManager; // Owns the interface manager instance
    LinkManager _linkManager;         // Owns the link manager instance

    // Timers for periodic tasks
    unsigned long _last_announce_time = 0;
    unsigned long _last_mem_check_time = 0;

    // Application layer data handler
    AppDataHandler _appDataHandler = nullptr;

    // Runtime-selected Reticulum application name.
    String _appName;

    // Subscribed Groups (loaded from Config)
    std::vector<std::array<uint8_t, RNS_ADDRESS_SIZE>> _subscribedGroups;

    // Data packet dedup: ring buffer of recent forwarded-packet hashes
    static constexpr size_t RECENT_DATA_PKT_SIZE = 64;
    uint32_t _recentDataPkts[RECENT_DATA_PKT_SIZE];
    size_t _recentDataPktIdx;

    // Debug CLI input buffer
    String _debugCmdBuf;
};

#endif // RETICULUM_NODE_H
