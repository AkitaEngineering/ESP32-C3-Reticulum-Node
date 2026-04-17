#include "ReticulumNode.h"
#include "Config.h"
#include "Utils.h"
#include "ReticulumPacket.h"
#include "LinkManager.h"      // Needs definition for _linkManager member
#include "InterfaceManager.h" // Needs definition for _interfaceManager member
#include "RoutingTable.h"     // Needs definition for _routing_table member
#include "Log.h"
#include <EEPROM.h>           // Include EEPROM library
#include <WiFi.h>             // For WiFi.status(), WiFi.macAddress() in printStatus
#include <freertos/FreeRTOS.h> // for uxTaskGetStackHighWaterMark
#if WEBSERVER_ENABLED
#include "WebServer.h"
static WebServerManager _webServerManager;
#endif
#if METRICS_ENABLED
#include <ArduinoJson.h>
#endif

// Constructor: Initialize members, especially LinkManager passing *this
ReticulumNode::ReticulumNode() :
    _packetCounter(0),
    _packetIdUnsavedCount(0),
    _routingTable(), // Default constructor
    // Initialize InterfaceManager with a packet receiver callback bound to this
    _interfaceManager(
        [this](const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
               const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port)
        {
            this->handleReceivedPacket(packetBuffer, packetLen, interface, sender_mac, sender_ip, sender_port);
        },
        _routingTable),
    // Initialize LinkManager, passing *this ReticulumNode reference
    _linkManager(*this),
    _last_announce_time(0),
    _last_mem_check_time(0),
    _appDataHandler(nullptr), // Initialize callback to null
    _recentDataPktIdx(0)
{
    memset(_nodeAddress, 0, RNS_ADDRESS_SIZE); // Clear address initially
    memset(_destinationHash, 0, sizeof(_destinationHash));
    memset(_recentDataPkts, 0, sizeof(_recentDataPkts));
}

void ReticulumNode::setup() {
    // Load config must happen first
    loadConfig(); // Loads address, packet ID
    printNodeAddress();

    _appName = getConfiguredAppName();

    // Initialize RNS cryptographic identity (loads from EEPROM or generates new)
    if (_identity.begin()) {
        // Compute destination hash for this node's announce destination
        _identity.getDestinationHash(_appName.c_str(), _destinationHash);
        DebugSerial.print("[Identity] Hash: ");
        Utils::printBytes(_identity.getIdentityHash(), 16, DebugSerial);
        DebugSerial.println();
        DebugSerial.print("[Identity] Dest: ");
        Utils::printBytes(_destinationHash, 16, DebugSerial);
        DebugSerial.println();
    } else {
        LOG_ERROR("Failed to initialize RNS identity!");
    }

    _subscribedGroups = SUBSCRIBED_GROUPS; // Copy extra groups from Config.h
    uint8_t plainDestinationHash[16] = {0};
    RNSIdentity::destination_hash(_appName.c_str(), nullptr, plainDestinationHash);
    std::array<uint8_t, RNS_ADDRESS_SIZE> defaultPlainGroup = {0};
    memcpy(defaultPlainGroup.data(), plainDestinationHash, RNS_ADDRESS_SIZE);
    bool hasDefaultPlainGroup = false;
    for (const auto& group : _subscribedGroups) {
        if (group == defaultPlainGroup) {
            hasDefaultPlainGroup = true;
            break;
        }
    }
    if (!hasDefaultPlainGroup) {
        _subscribedGroups.insert(_subscribedGroups.begin(), defaultPlainGroup);
    }

    // runtime sanity checks
    if (strlen(WIFI_SSID) == 0 || strlen(WIFI_PASSWORD) == 0) {
        LOG_WARN("WiFi credentials appear empty; WiFi interface may not connect.");
    }

    // Setup interfaces (which also sets up UDP, ESP-NOW etc)
    _interfaceManager.setup();

    // Initialize timers
    _last_mem_check_time = millis();
    // Announce quickly after boot to speed first route convergence.
    _last_announce_time = millis() - ANNOUNCE_INTERVAL_MS + random(1000, 3000);
    // Ensure routing table prune timer is initialized
    _routingTable.prune(nullptr); // Initial call to set timer base

    LOG_INFO("Node Setup Complete. Free Heap: %u", ESP.getFreeHeap());

#if WEBSERVER_ENABLED
    _webServerManager.begin();
#endif
}

void ReticulumNode::loop() {
    // unsigned long now = millis(); // Get time once per loop (unused)

    _interfaceManager.loop();     // Process interface inputs
    _linkManager.checkAllTimeouts(); // Check Link timeouts/retransmissions
    _routingTable.prune(&_interfaceManager); // Prune old routes, pass IfMgr for peer removal
    sendAnnounceIfNeeded();     // Send periodic announce
    checkMemoryUsage();         // Check free memory
    processDebugCommands();     // Process text commands from debug serial

#if WEBSERVER_ENABLED
    _webServerManager.loop();
#endif

    // delay(1); // Generally avoid delay() in main loop if possible
}

// --- Config Loading/Saving ---
void ReticulumNode::loadConfig() {
    if (!EEPROM.begin(EEPROM_SIZE)) {
        DebugSerial.println("! ERROR: Failed to initialise EEPROM! Using temporary values.");
        generateNodeAddress(); // Generate temp address
        _packetCounter = random(0,0xFFFF); // Start with random counter
        return;
    }
    LOG_INFO("Loading config from EEPROM...");
    loadOrGenerateAddress(); // Load or generate node address
    loadPacketCounter(); // Load last packet counter
    // EEPROM.end(); // Optional: close EEPROM if not needed constantly
}

void ReticulumNode::loadOrGenerateAddress() {
    uint8_t storedAddr[RNS_ADDRESS_SIZE];
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        storedAddr[i] = EEPROM.read(EEPROM_ADDR_NODE + i);
    }

#if NODE_ADDRESS_FROM_EFUSE
    uint8_t derivedAddr[RNS_ADDRESS_SIZE];
    deriveNodeAddressFromEfuse(derivedAddr);
    memcpy(_nodeAddress, derivedAddr, RNS_ADDRESS_SIZE);

    if (memcmp(storedAddr, derivedAddr, RNS_ADDRESS_SIZE) != 0) {
        LOG_INFO("Updating EEPROM node address from unique eFuse identity.");
        saveNodeAddress();
    } else {
        LOG_INFO("Loaded deterministic eFuse-derived node address.");
    }
#else
    bool needsGenerating = false;
    // Check if address is all 0x00 or all 0xFF (common uninitialized states)
    bool allZeros = true;
    bool allFs = true;
    for(int i=0; i<RNS_ADDRESS_SIZE; ++i) {
        if (storedAddr[i] != 0x00) allZeros = false;
        if (storedAddr[i] != 0xFF) allFs = false;
    }
    if (allZeros || allFs) {
        needsGenerating = true;
        LOG_WARN("No valid address in EEPROM or first boot.");
    }

    if (needsGenerating) {
        generateNodeAddress();
        saveNodeAddress();
    } else {
        memcpy(_nodeAddress, storedAddr, RNS_ADDRESS_SIZE);
        LOG_INFO("Loaded address from EEPROM.");
    }
#endif
}

void ReticulumNode::generateNodeAddress() {
    DebugSerial.println("Generating random node address...");
    // Ensure random is seeded reasonably well using esp_random() which is available on all ESP32 variants
    // Combine with millis() for additional entropy
    uint32_t seed = esp_random() ^ (millis() << 16) ^ (millis() & 0xFFFF);
    randomSeed(seed);
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        _nodeAddress[i] = random(0, 256);
    }
    // Ensure address is not easily guessable or problematic (like all zeros)
    if (Utils::compareAddresses(_nodeAddress, (const uint8_t*)"\x00\x00\x00\x00\x00\x00\x00\x00")) {
        _nodeAddress[0] = random(1, 256); // Ensure first byte is non-zero
    }
}

void ReticulumNode::deriveNodeAddressFromEfuse(uint8_t outAddr[RNS_ADDRESS_SIZE]) {
    uint64_t efuse = ESP.getEfuseMac();

    // Expand 48-bit chip MAC into a stable 8-byte address using simple mixing.
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        uint8_t b = static_cast<uint8_t>((efuse >> ((i % 6) * 8)) & 0xFF);
        uint8_t m = static_cast<uint8_t>((efuse >> (((i + 3) % 6) * 8)) & 0xFF);
        outAddr[i] = static_cast<uint8_t>(b ^ ((m << 1) | (m >> 7)) ^ (0xA5u + i * 17u));
    }

    bool allZero = true;
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        if (outAddr[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        outAddr[0] = 0x01;
    }
}

void ReticulumNode::saveNodeAddress() {
    DebugSerial.print("Saving node address to EEPROM: "); printNodeAddress(); // Print before saving
     // EEPROM.begin required before write if not already called or ended
     if (!EEPROM.begin(EEPROM_SIZE)) { DebugSerial.println("! EEPROM begin failed for save!"); return; }
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        EEPROM.write(EEPROM_ADDR_NODE + i, _nodeAddress[i]);
    }
    if (!EEPROM.commit()) {
        DebugSerial.println("! WARNING: EEPROM commit failed saving address!");
    }
    // EEPROM.end();
}

void ReticulumNode::loadPacketCounter() {
    // Manual read for compatibility:
    _packetCounter = (EEPROM.read(EEPROM_ADDR_PKTID + 0) << 8) | EEPROM.read(EEPROM_ADDR_PKTID + 1);

    // Validate loaded value: treat 0xFFFF and 0x0000 as uninitialized/corrupt
    if (_packetCounter == 0xFFFF || _packetCounter == 0x0000) {
        uint16_t old = _packetCounter;
        _packetCounter = (uint16_t)(esp_random() & 0xFFFF);
        DebugSerial.print("Invalid/empty packet counter in EEPROM (read="); DebugSerial.print(old);
        DebugSerial.print(") — using random start: "); DebugSerial.println(_packetCounter);
    } else {
        DebugSerial.print("Loaded packet counter start: "); DebugSerial.println(_packetCounter);
    }

    _packetIdUnsavedCount = 0; // Reset unsaved counter
}

// Public helper: force-load config from EEPROM (useful in tests)
void ReticulumNode::loadConfigFromEEPROM() {
    loadConfig();
}

// Public helper: return current in-memory packet counter
uint16_t ReticulumNode::getPacketCounter() const {
    return _packetCounter;
}

// Public helper: save node address and packet counter synchronously to EEPROM
void ReticulumNode::saveConfigNow() {
    if (!EEPROM.begin(EEPROM_SIZE)) { DebugSerial.println("! EEPROM begin failed for saveConfigNow()"); return; }

    // Save node address
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        EEPROM.write(EEPROM_ADDR_NODE + i, _nodeAddress[i]);
    }

    // Save packet counter
    EEPROM.write(EEPROM_ADDR_PKTID + 0, (_packetCounter >> 8) & 0xFF);
    EEPROM.write(EEPROM_ADDR_PKTID + 1, _packetCounter & 0xFF);

    if (!EEPROM.commit()) {
        DebugSerial.println("! WARNING: EEPROM commit failed in saveConfigNow()");
    }

    _packetIdUnsavedCount = 0; // Reset throttle counter
}


void ReticulumNode::savePacketCounterIfNeeded() {
    _packetIdUnsavedCount++;
    if (_packetIdUnsavedCount >= PACKET_ID_SAVE_INTERVAL) {
         // DebugSerial.print("Saving packet counter: "); DebugSerial.println(_packetCounter); // Verbose
         // EEPROM.begin required before write if not already called or ended
         if (!EEPROM.begin(EEPROM_SIZE)) { DebugSerial.println("! EEPROM begin failed for save!"); return; }
         EEPROM.write(EEPROM_ADDR_PKTID + 0, (_packetCounter >> 8) & 0xFF);
         EEPROM.write(EEPROM_ADDR_PKTID + 1, _packetCounter & 0xFF);
         if (!EEPROM.commit()) {
             DebugSerial.println("! WARNING: Failed to save packet counter!");
         }
          // EEPROM.end();
         _packetIdUnsavedCount = 0; // Reset counter
    }
}

// Public method for LinkManager to get next ID
uint16_t ReticulumNode::getNextPacketId() {
    _packetCounter++;
    savePacketCounterIfNeeded(); // Throttle EEPROM writes
    return _packetCounter;
}

void ReticulumNode::printNodeAddress() {
    DebugSerial.print("Node Address: ");
    Utils::printBytes(_nodeAddress, RNS_ADDRESS_SIZE, DebugSerial);
    DebugSerial.println();
}

// --- Periodic Tasks ---
void ReticulumNode::checkMemoryUsage() {
    unsigned long now = millis();
    if (now - _last_mem_check_time > MEM_CHECK_INTERVAL_MS) {
        size_t free_heap = ESP.getFreeHeap();
        size_t highwater = uxTaskGetStackHighWaterMark(NULL);
        LOG_INFO("[Mem] Free Heap: %u, Stack high-water: %u, Routes: %u, Links: %u",
                 free_heap,
                 highwater,
                 _routingTable.getRouteCount(),
                 _linkManager.getActiveLinkCount());
        // Print esp-now peers for debugging (may be verbose)
        _interfaceManager.printEspNowPeers();

#if METRICS_ENABLED && METRICS_UDP_ENABLED
        // send JSON metrics over UDP broadcast
        DynamicJsonDocument doc(256);
        doc["heap_free"] = free_heap;
        doc["stack_high"] = highwater;
        doc["routes"] = _routingTable.getRouteCount();
        doc["links"] = _linkManager.getActiveLinkCount();
        String out; serializeJson(doc, out);
        _interfaceManager.sendUdpMetrics(out);
#endif
        _last_mem_check_time = now;
    }
}

// --- Debug CLI ---
void ReticulumNode::processDebugCommands() {
    while (DebugSerial.available()) {
        char c = DebugSerial.read();
        if (c == '\n' || c == '\r') {
            _debugCmdBuf.trim();
            if (_debugCmdBuf.length() > 0) {
                String cmd = _debugCmdBuf;
                _debugCmdBuf = "";
                cmd.toLowerCase();

                if (cmd == "status") {
                    printStatus();
                } else if (cmd == "routes") {
                    _routingTable.print();
                } else if (cmd == "peers") {
                    _interfaceManager.printEspNowPeers();
                } else if (cmd == "espnow on") {
                    _interfaceManager.enableEspNow();
                } else if (cmd == "espnow off") {
                    _interfaceManager.disableEspNow();
                } else if (cmd == "help") {
                    DebugSerial.println("Commands: status, routes, peers, espnow on/off, help");
                } else {
                    DebugSerial.print("Unknown command: ");
                    DebugSerial.println(cmd);
                    DebugSerial.println("Type 'help' for available commands.");
                }
            }
        } else if (_debugCmdBuf.length() < 32) {
            _debugCmdBuf += c;
        }
    }
}

void ReticulumNode::printStatus() {
    DebugSerial.println("=== Node Status ===");
    DebugSerial.print("  Address: ");  printNodeAddress();
    DebugSerial.print("  App Name: "); DebugSerial.println(_appName);
    DebugSerial.print("  Uptime: ");   DebugSerial.print(millis() / 1000); DebugSerial.println("s");
    DebugSerial.print("  Free Heap: "); DebugSerial.println(ESP.getFreeHeap());
    DebugSerial.print("  Routes: ");   DebugSerial.println(_routingTable.getRouteCount());
    DebugSerial.print("  Links: ");    DebugSerial.println(_linkManager.getActiveLinkCount());
    DebugSerial.println("--- Interfaces ---");
    DebugSerial.println("  KISS USB: ON");
    DebugSerial.print("  ESP-NOW:  "); DebugSerial.println(_interfaceManager.isEspNowInitialized() ? "ON" : "OFF");
    DebugSerial.print("  WiFi AP:  "); DebugSerial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "not connected");
    DebugSerial.print("  MAC:      "); DebugSerial.println(WiFi.macAddress());
    DebugSerial.println("===================");
}

void ReticulumNode::sendAnnounceIfNeeded() {
    unsigned long now = millis();
    // Check if announce interval has passed
    if (now - _last_announce_time > ANNOUNCE_INTERVAL_MS) {
        if (!_identity.isReady()) {
            DebugSerial.println("[Node] Identity not ready, skipping announce.");
            _last_announce_time = now;
            return;
        }

        DebugSerial.println("[Node] Sending periodic announce (alive).");

        // Build cryptographic announce payload matching reference RNS:
        // [PUB_KEY 64][NAME_HASH 10][RANDOM_HASH 10][SIGNATURE 64][APP_DATA...]
        // signed_data = dest_hash + pub_key + name_hash + random_hash + app_data
        std::vector<uint8_t> announcePayload = _identity.buildAnnouncePayload(_appName.c_str());

        uint8_t buffer[MAX_PACKET_SIZE];
        size_t len = 0;
        if (ReticulumPacket::serialize(buffer, len,
            _destinationHash,                // 16-byte destination hash (from identity)
            RNS_PACKET_ANNOUNCE,             // packet_type = ANNOUNCE
            RNS_DEST_SINGLE,                // destination_type = SINGLE (identity-associated)
            RNS_PROPAGATION_BROADCAST,       // propagation = broadcast
            RNS_CONTEXT_NONE,                // context = NONE
            0,                               // hops = 0 (originator)
            announcePayload))                // payload: pub_key + name_hash + random_hash + sig
        {
            _interfaceManager.broadcastAnnounce(buffer, len); // Use InterfaceManager to send
            // blink LED briefly to show we're alive
            setStatusLed(true);
            delay(20);
            setStatusLed(false);
        } else {
             DebugSerial.println("! ERROR: Failed to serialize own Announce packet!");
        }
        _last_announce_time = now; // Reset timer *after* sending attempt
    }
}

// --- Core Packet Handling ---
void ReticulumNode::handleReceivedPacket(const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
                                           const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port)
{
    RnsPacketInfo packetInfo;
    if (!ReticulumPacket::deserialize(packetBuffer, packetLen, packetInfo)) {
        DebugSerial.println("! Node: failed to deserialize incoming packet, discarding.");
        return;
    }

    // Ignore packets sourced from self that might have looped back
    if (Utils::compareAddresses(packetInfo.source, _nodeAddress)) {
        DebugSerial.println("[Node] Dropping packet sourced from self (loopback).");
        return;
    }

    // --- 1. Link Layer Packet Handling ---
    // LINKREQUEST packets: initiates a new link handshake
    if (packetInfo.packet_type == RNS_PACKET_LINKREQ) {
        _linkManager.handleLinkRequest(packetInfo, packetBuffer, packetLen);
        return;
    }
    // All packets addressed to a LINK destination (LRPROOF, data, keepalive, close, RTT)
    if (packetInfo.destination_type == RNS_DEST_LINK) {
        _linkManager.handleLinkPacket(packetInfo);
        return;
    }

    // --- 2. Announce Packet Handling ---
    // Official Reticulum format: announces identified by packet_type field (bits 0-1 of flags)
    if (packetInfo.packet_type == RNS_PACKET_ANNOUNCE) {
        // DebugSerial.println("Node: Processing Announce..."); // Verbose
        _routingTable.update(packetInfo, interface, sender_mac, sender_ip, sender_port, &_interfaceManager);
        forwardAnnounce(packetInfo, interface); // Attempt re-broadcast
        return; // Announce handled
    }

    // --- 3. Data / Other Packet Handling (Check Destination) ---
    // Check destination: Single Address Match
    if (packetInfo.destination_type == RNS_DEST_SINGLE &&
        Utils::compareAddresses(packetInfo.destination, _nodeAddress))
    {
        DebugSerial.println("[Node] Packet addressed to self (single).");
        processPacketForSelf(packetInfo, interface);
        // Do not forward packets addressed directly to this node
        return;
    }
    // Check destination: Group Address Match
    else if (packetInfo.destination_type == RNS_DEST_GROUP)
    {
        for (const auto& group : _subscribedGroups) {
            if (Utils::compareAddresses(packetInfo.destination, group.data())) {
                DebugSerial.println("[Node] Packet addressed to subscribed group.");
                processPacketForSelf(packetInfo, interface);
                // (keep iterating to allow logging of first-matching group if needed)
                break; // Processed locally, but MUST continue to forwarding
            }
        }
    }
    // Check destination: PLAIN destination (unencrypted broadcast) - compare first 8 bytes of hash
    else if (packetInfo.destination_type == RNS_DEST_PLAIN)
    {
        bool plainMatched = false;
        for (const auto& group : _subscribedGroups) {
            // PLAIN destinations use a 16-byte hash, but we compare first 8 bytes
            if (Utils::compareAddresses(packetInfo.destination_hash, group.data())) {
                DebugSerial.println("[Node] Packet addressed to subscribed PLAIN destination.");
                processPacketForSelf(packetInfo, interface);
                plainMatched = true;
                break; // Processed locally, but MUST continue to forwarding
            }
        }

#if RNS_ACCEPT_ALL_PLAIN_DESTINATIONS
        if (!plainMatched) {
            DebugSerial.println("[Node] Accepting PLAIN packet in indiscriminate mode.");
            processPacketForSelf(packetInfo, interface);
        }
#endif
    }

    // --- 4. Forwarding Logic (If not single-addressed to self) ---
    // Forward packets that were not single-addressed to us, OR group packets
    // (Announce and Link packets were already handled and returned earlier)
    forwardPacket(packetInfo, interface);

} // end handleReceivedPacket


// Handles non-link packets addressed to this node (or group), including LOCAL_CMD
void ReticulumNode::processPacketForSelf(const RnsPacketInfo& packetInfo, InterfaceType interface) {

    // Check for Local Command Context from Serial/BT to INITIATE reliable send or debug
    if (packetInfo.context == RNS_CONTEXT_LOCAL_CMD &&
       (interface == InterfaceType::SERIAL_PORT || interface == InterfaceType::BLUETOOTH))
    {
        // allow zero‑length payload as a simple "PING" from the host
        if (packetInfo.payload.empty()) {
            DebugSerial.println("> CMD: received local ping from host – node is alive");
            return; // nothing else to do
        }
        if (packetInfo.payload.size() >= RNS_ADDRESS_SIZE) { // Must have at least destination addr
            uint8_t targetDest[RNS_ADDRESS_SIZE];
            memcpy(targetDest, packetInfo.payload.data(), RNS_ADDRESS_SIZE);

            // Extract actual data payload after the address
            std::vector<uint8_t> actualPayload;
            if (packetInfo.payload.size() > RNS_ADDRESS_SIZE) {
                 actualPayload.assign(packetInfo.payload.begin() + RNS_ADDRESS_SIZE, packetInfo.payload.end());
            } // else: payload is empty, might be a ping command from host

            // Look for text commands (e.g. "routes" or "peers") when dest is our own address or all zeros
            bool handled = false;
            if (Utils::compareAddresses(targetDest, _nodeAddress) || Utils::isAllZeros(targetDest, RNS_ADDRESS_SIZE)) {
                std::string cmd;
                cmd.reserve(actualPayload.size());
                for (auto b : actualPayload) {
                    if (isprint(b)) cmd.push_back((char)b);
                }
                if (cmd == "routes") {
                    _routingTable.print();
                    handled = true;
                } else if (cmd == "peers") {
                    _interfaceManager.printEspNowPeers();
                    handled = true;
                } else if (cmd == "status") {
                    printStatus();
                    handled = true;
                }
            }

            if (!handled) {
                DebugSerial.print("> CMD: Unrecognised local command, dest=");
                Utils::printBytes(targetDest, RNS_ADDRESS_SIZE, DebugSerial);
                DebugSerial.print(" DataLen="); DebugSerial.println(actualPayload.size());
            }

        } else {
             DebugSerial.println("! Invalid Local Command: payload too short.");
        }
        return; // Command processed
    }

    // --- Standard Unreliable Packet Processing for Self ---
    // (e.g., pings, service discovery, non-link application data)
    DebugSerial.print("> Self Packet! Dst="); Utils::printBytes(packetInfo.destination, RNS_ADDRESS_SIZE, DebugSerial);
    DebugSerial.print(" Src="); Utils::printBytes(packetInfo.source, RNS_ADDRESS_SIZE, DebugSerial);
    DebugSerial.print(" If="); DebugSerial.print(static_cast<int>(interface));
    DebugSerial.print(" Ctx="); DebugSerial.print(packetInfo.context, HEX);
    DebugSerial.print(" Payload: [");
    for(uint8_t byte : packetInfo.payload) { if(isprint(byte)) DebugSerial.print((char)byte); else DebugSerial.print('.'); }
    DebugSerial.println("]");

    // respond to simple ping payloads automatically
    if (packetInfo.payload.size() == 4 &&
        packetInfo.payload[0] == 'p' && packetInfo.payload[1] == 'i' &&
        packetInfo.payload[2] == 'n' && packetInfo.payload[3] == 'g')
    {
        if (Utils::isAllZeros(packetInfo.source, RNS_ADDRESS_SIZE)) {
            DebugSerial.println("[Node] Received ping without source context; skipping pong reply.");
        } else {
            DebugSerial.println("[Node] Received ping, sending pong reply.");
            std::vector<uint8_t> pong = {'p','o','n','g'};
            uint8_t buf[MAX_PACKET_SIZE];
            size_t outlen = 0;

            // Construct 16-byte reply destination from sender's source address.
            // The remaining bytes stay zero-initialized because internal routing
            // uses the 8-byte address prefix.
            uint8_t replyDestHash[RNS_TRUNCATED_HASHLENGTH_BYTES] = {0};
            memcpy(replyDestHash, packetInfo.source, RNS_ADDRESS_SIZE);

            if (ReticulumPacket::serialize(buf, outlen,
                                           replyDestHash,
                                           RNS_PACKET_DATA,
                                           RNS_DEST_SINGLE,
                                           RNS_PROPAGATION_BROADCAST,
                                           RNS_CONTEXT_NONE,
                                           0,
                                           pong))
            {
                _interfaceManager.sendPacket(buf, outlen, packetInfo.source, interface);
            }
        }
    }

    // Call app handler for unreliable data too
    if (_appDataHandler) { _appDataHandler(packetInfo.source, packetInfo.payload); }
}

// Handles forwarding of "normal" data packets
void ReticulumNode::forwardPacket(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface) {
     // Check hop limit
    if (packetInfo.hops >= MAX_HOPS) {
        DebugSerial.println("Hop limit exceeded. Not forwarding."); // Verbose
        return;
    }

    // Duplicate detection: compute hash of destination + payload to avoid
    // forwarding the same data packet multiple times (prevents exponential
    // traffic amplification in a mesh).
    uint32_t pktHash = 0;
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) pktHash = pktHash * 31 + packetInfo.destination[i];
    for (auto b : packetInfo.data) pktHash = pktHash * 31 + b;
    for (size_t i = 0; i < RECENT_DATA_PKT_SIZE; ++i) {
        if (_recentDataPkts[i] == pktHash) {
            // Already forwarded recently
            return;
        }
    }
    _recentDataPkts[_recentDataPktIdx % RECENT_DATA_PKT_SIZE] = pktHash;
    _recentDataPktIdx++;

    // Create forwarding packet info (increment hops)
    RnsPacketInfo forwardInfo = packetInfo; // Creates a copy
    forwardInfo.hops++;

    uint8_t forwardBuffer[MAX_PACKET_SIZE];
    size_t forwardLen = 0;
    // Use official Reticulum wire format for forwarding
    if (!ReticulumPacket::serialize(forwardBuffer, forwardLen,
        forwardInfo.destination_hash,      // 16-byte destination hash
        forwardInfo.packet_type,           // preserve packet type
        forwardInfo.destination_type,      // preserve dest type
        forwardInfo.propagation_type,      // preserve propagation type
        forwardInfo.context,               // preserve context
        forwardInfo.hops,                  // already incremented
        forwardInfo.data))                 // full data payload
    {
        DebugSerial.println("! ERROR: Failed to serialize packet for forwarding!");
        return;
    }

    // DebugSerial.print("Forwarding packet ID "); DebugSerial.print(forwardInfo.packet_id); DebugSerial.print(" Hops "); DebugSerial.println(forwardInfo.hops); // Verbose
    // Use InterfaceManager to send via appropriate interfaces (routing or broadcast)
    _interfaceManager.sendPacket(forwardBuffer, forwardLen, forwardInfo.destination, incomingInterface);
}

// Handles re-broadcasting/forwarding of Announce packets
void ReticulumNode::forwardAnnounce(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface) {
    // Check hop limit (allow MAX_HOPS-1 for forwarding)
    if (packetInfo.hops >= MAX_HOPS -1) {
        // DebugSerial.println("Announce hop limit reached for forwarding."); // Verbose
        return;
    }

    // Check if this announce was recently forwarded to prevent loops/storms
    // Use a hash of source + payload as the key since packet_id is always 0 for announces
    uint32_t announceHash = 0;
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) announceHash = announceHash * 31 + packetInfo.source[i];
    for (auto b : packetInfo.data) announceHash = announceHash * 31 + b;
    uint32_t announceKey = announceHash;

    if (!_routingTable.shouldForwardAnnounce(announceKey, packetInfo.source)) {
         // DebugSerial.println("Announce recently forwarded or loop detected. Skipping re-broadcast."); // Verbose
        return;
    }
    // Mark as forwarded BEFORE sending
    _routingTable.markAnnounceForwarded(announceKey, packetInfo.source);

    // Create forwarding packet info (increment hops)
    RnsPacketInfo forwardInfo = packetInfo;
    forwardInfo.hops++;

    uint8_t forwardBuffer[MAX_PACKET_SIZE];
    size_t forwardLen = 0;
    // Use official Reticulum format; forwardInfo.data still contains [SOURCE 8][payload]
    if (!ReticulumPacket::serialize(forwardBuffer, forwardLen,
        forwardInfo.destination_hash,     // 16-byte destination hash
        RNS_PACKET_ANNOUNCE,              // packet_type = ANNOUNCE
        forwardInfo.destination_type,     // preserve dest type
        RNS_PROPAGATION_BROADCAST,        // always broadcast announces
        forwardInfo.context,              // preserve context
        forwardInfo.hops,                 // already incremented
        forwardInfo.data))                // full data payload (includes source prefix)
    {
        DebugSerial.println("! ERROR: Failed to serialize announce for forwarding!");
        return;
    }

    // DebugSerial.print("Re-broadcasting Announce ID "); DebugSerial.print(forwardInfo.packet_id); DebugSerial.print(" Hops "); DebugSerial.println(forwardInfo.hops); // Verbose
    // Announce should be broadcast, not routed to specific dest
    _interfaceManager.broadcastAnnounce(forwardBuffer, forwardLen);
}

// --- Application Layer Integration ---
void ReticulumNode::setAppDataHandler(AppDataHandler handler) {
    _appDataHandler = handler;
    DebugSerial.println("Application data handler registered.");
}

// Called by LinkManager when reliable data arrives
void ReticulumNode::processAppData(const uint8_t* source_address, const std::vector<uint8_t>& data) {
    // This is where received Link data ends up
    DebugSerial.print(">> App Data Received! Src: "); Utils::printBytes(source_address, RNS_ADDRESS_SIZE, DebugSerial);
    DebugSerial.print(" Len: "); DebugSerial.print(data.size()); DebugSerial.println();

    if (_appDataHandler) {
        try {
             _appDataHandler(source_address, data); // Call the registered handler
        } catch (const std::exception& e) {
             DebugSerial.print("! ERROR in AppDataHandler: "); DebugSerial.println(e.what());
        } catch (...) {
             DebugSerial.println("! ERROR in AppDataHandler: Unknown exception.");
        }
    } else {
        DebugSerial.println(" (No AppDataHandler registered)");
    }
}
