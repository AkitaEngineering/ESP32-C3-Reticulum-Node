#include "ReticulumNode.h"
#include "Config.h"
#include "Utils.h"
#include "ReticulumPacket.h"
#include "LinkManager.h"      // Needs definition for _linkManager member
#include "InterfaceManager.h" // Needs definition for _interfaceManager member
#include "RoutingTable.h"     // Needs definition for _routingTable member
#include <EEPROM.h>           // Include EEPROM library

// Constructor: Initialize members, especially LinkManager passing *this
ReticulumNode::ReticulumNode() :
    _packetCounter(0),
    _packetIdUnsavedCount(0),
    _routingTable(), // Default constructor
    // Initialize InterfaceManager first, pass its callback lambda and routing table ref
    _interfaceManager(nullptr, _routingTable),
    // Initialize LinkManager, passing *this ReticulumNode reference
    _linkManager(*this),
    _last_announce_time(0),
    _last_mem_check_time(0),
    _appDataHandler(nullptr) // Initialize callback to null
{
    memset(_nodeAddress, 0, RNS_ADDRESS_SIZE); // Clear address initially
}

void ReticulumNode::setup() {
    // Load config must happen first
    loadConfig(); // Loads address, packet ID
    printNodeAddress();
    _subscribedGroups = SUBSCRIBED_GROUPS; // Copy groups from Config.h

    // Setup interfaces (which also sets up UDP, ESP-NOW etc)
    _interfaceManager.setup();

    // Initialize timers
    _last_mem_check_time = millis();
    // Announce sooner after boot
    _last_announce_time = millis() - ANNOUNCE_INTERVAL_MS + random(5000, 15000); // Add random delay
    // Ensure routing table prune timer is initialized
    _routingTable.prune(nullptr); // Initial call to set timer base

    DebugSerial.print("Node Setup Complete. Free Heap: "); DebugSerial.println(ESP.getFreeHeap());
}

void ReticulumNode::loop() {
    unsigned long now = millis(); // Get time once per loop

    _interfaceManager.loop();     // Process interface inputs
    _linkManager.checkAllTimeouts(); // Check Link timeouts/retransmissions
    _routingTable.prune(&_interfaceManager); // Prune old routes, pass IfMgr for peer removal
    sendAnnounceIfNeeded();     // Send periodic announce
    checkMemoryUsage();         // Check free memory

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
    DebugSerial.println("Loading config from EEPROM...");
    loadOrGenerateAddress(); // Load or generate node address
    loadPacketCounter(); // Load last packet counter
    // EEPROM.end(); // Optional: close EEPROM if not needed constantly
}

void ReticulumNode::loadOrGenerateAddress() {
    bool needsGenerating = false;
    uint8_t storedAddr[RNS_ADDRESS_SIZE];
    for (int i = 0; i < RNS_ADDRESS_SIZE; ++i) {
        storedAddr[i] = EEPROM.read(EEPROM_ADDR_NODE + i);
    }
    // Check if address is all 0x00 or all 0xFF (common uninitialized states)
    bool allZeros = true;
    bool allFs = true;
    for(int i=0; i<RNS_ADDRESS_SIZE; ++i) {
        if (storedAddr[i] != 0x00) allZeros = false;
        if (storedAddr[i] != 0xFF) allFs = false;
    }
    if (allZeros || allFs) {
        needsGenerating = true;
        DebugSerial.println("No valid address in EEPROM or first boot.");
    }

    if (needsGenerating) {
        generateNodeAddress();
        saveNodeAddress();
    } else {
        memcpy(_nodeAddress, storedAddr, RNS_ADDRESS_SIZE);
        DebugSerial.println("Loaded address from EEPROM.");
    }
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

    // Validate loaded value: treat 0xFFFF, 0x0000 or very small values as uninitialized/corrupt
    if (_packetCounter == 0xFFFF || _packetCounter == 0x0000 || _packetCounter < 10) {
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
        DebugSerial.print("[Mem] Free Heap: "); DebugSerial.print(ESP.getFreeHeap());
        // Add more stats if needed (e.g., Link count, Route count)
        // DebugSerial.print(" Links: "); DebugSerial.print(_linkManager.getActiveLinkCount()); // Need method in LinkManager
        // DebugSerial.print(" Routes: "); DebugSerial.print(_routingTable.getRouteCount()); // Need method in RoutingTable
        DebugSerial.println();
        _last_mem_check_time = now;
    }
}

void ReticulumNode::sendAnnounceIfNeeded() {
    unsigned long now = millis();
    // Check if announce interval has passed
    if (now - _last_announce_time > ANNOUNCE_INTERVAL_MS) {
        // DebugSerial.println("Generating Announce packet..."); // Verbose
        RnsPacketInfo announcePkt;
        announcePkt.header_type = RNS_HEADER_TYPE_ANN;
        announcePkt.context = RNS_CONTEXT_NONE;
        announcePkt.packet_id = getNextPacketId(); // Use own method
        announcePkt.hops = 0;
        announcePkt.destination_type = RNS_DST_TYPE_GROUP; // Implicit broadcast
        memset(announcePkt.destination, 0, RNS_ADDRESS_SIZE);
        announcePkt.source_type = RNS_DST_TYPE_SINGLE;
        memcpy(announcePkt.source, _nodeAddress, RNS_ADDRESS_SIZE);
        // announcePkt.payload = {'G','W','v','3'}; // Optional: Add application aspects/version

        uint8_t buffer[MAX_PACKET_SIZE];
        size_t len = 0;
        if (ReticulumPacket::serialize(buffer, len,
            announcePkt.destination, announcePkt.source, announcePkt.destination_type,
            announcePkt.header_type, announcePkt.context, announcePkt.packet_id,
            announcePkt.hops, announcePkt.payload, 0)) // No sequence number
        {
            _interfaceManager.broadcastAnnounce(buffer, len); // Use InterfaceManager to send
        } else {
             DebugSerial.println("! ERROR: Failed to serialize own Announce packet!");
        }
        _last_announce_time = now; // Reset timer *after* sending attempt
        // _routingTable.print(); // Optional: Print table after sending announce
    }
}

// --- Core Packet Handling ---
void ReticulumNode::handleReceivedPacket(const uint8_t *packetBuffer, size_t packetLen, InterfaceType interface,
                                           const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port)
{
    RnsPacketInfo packetInfo;
    if (!ReticulumPacket::deserialize(packetBuffer, packetLen, packetInfo)) {
        // DebugSerial.println("! Deserialize failed in Node. Discarding."); // Verbose
        return;
    }

    // Ignore packets sourced from self that might have looped back
    if (Utils::compareAddresses(packetInfo.source, _nodeAddress)) { return; }

    // --- 1. Link Layer Packet Handling ---
    if (packetInfo.context == RNS_CONTEXT_LINK_REQ ||
        packetInfo.context == RNS_CONTEXT_LINK_CLOSE ||
        packetInfo.context == RNS_CONTEXT_LINK_DATA ||
        (packetInfo.header_type == RNS_HEADER_TYPE_ACK && packetInfo.context == RNS_CONTEXT_ACK) )
    {
        // DebugSerial.println("Node: Passing packet to Link Manager."); // Verbose
        _linkManager.processPacket(packetInfo, interface);
        return; // Link manager handles these exclusively
    }

    // --- 2. Announce Packet Handling ---
    if ((packetInfo.header_type & RNS_HEADER_TYPE_MASK) == RNS_HEADER_TYPE_ANN) {
        // DebugSerial.println("Node: Processing Announce..."); // Verbose
        _routingTable.update(packetInfo, interface, sender_mac, sender_ip, sender_port, &_interfaceManager);
        forwardAnnounce(packetInfo, interface); // Attempt re-broadcast
        return; // Announce handled
    }

    // --- 3. Data / Other Packet Handling (Check Destination) ---
    bool processedLocally = false;
    bool isGroupMember = false;

    // Check destination: Single Address Match
    if (packetInfo.destination_type == RNS_DST_TYPE_SINGLE &&
        Utils::compareAddresses(packetInfo.destination, _nodeAddress))
    {
        processPacketForSelf(packetInfo, interface);
        // Do not forward packets addressed directly to this node
        return;
    }
    // Check destination: Group Address Match
    else if (packetInfo.destination_type == RNS_DST_TYPE_GROUP)
    {
        for (const auto& group : _subscribedGroups) {
            if (Utils::compareAddresses(packetInfo.destination, group.data())) {
                processPacketForSelf(packetInfo, interface);
                isGroupMember = true; // Mark that we processed it as a group member
                break; // Processed locally, but MUST continue to forwarding
            }
        }
    }
    // Check destination: PLAIN destination (unencrypted broadcast) - compare first 8 bytes of hash
    else if (packetInfo.destination_type == RNS_DEST_PLAIN)
    {
        for (const auto& group : _subscribedGroups) {
            // PLAIN destinations use a 16-byte hash, but we compare first 8 bytes
            if (Utils::compareAddresses(packetInfo.destination_hash, group.data())) {
                processPacketForSelf(packetInfo, interface);
                isGroupMember = true; // Mark that we processed it
                break; // Processed locally, but MUST continue to forwarding
            }
        }
    }

    // --- 4. Forwarding Logic (If not single-addressed to self) ---
    // Forward packets that were not single-addressed to us, OR group packets
    // (Announce and Link packets were already handled and returned earlier)
    forwardPacket(packetInfo, interface);

} // end handleReceivedPacket


// Handles non-link packets addressed to this node (or group), including LOCAL_CMD
void ReticulumNode::processPacketForSelf(const RnsPacketInfo& packetInfo, InterfaceType interface) {

    // Check for Local Command Context from Serial/BT to INITIATE reliable send
    if (packetInfo.context == RNS_CONTEXT_LOCAL_CMD &&
       (interface == InterfaceType::SERIAL_PORT || interface == InterfaceType::BLUETOOTH))
    {
        if (packetInfo.payload.size() >= RNS_ADDRESS_SIZE) { // Must have at least destination addr
            uint8_t targetDest[RNS_ADDRESS_SIZE];
            memcpy(targetDest, packetInfo.payload.data(), RNS_ADDRESS_SIZE);

            // Extract actual data payload after the address
            std::vector<uint8_t> actualPayload;
            if (packetInfo.payload.size() > RNS_ADDRESS_SIZE) {
                 actualPayload.assign(packetInfo.payload.begin() + RNS_ADDRESS_SIZE, packetInfo.payload.end());
            } // else: payload is empty, might be a ping command?

            DebugSerial.print("> CMD: Send Reliable to "); Utils::printBytes(targetDest, RNS_ADDRESS_SIZE, Serial);
            DebugSerial.print(" DataLen="); DebugSerial.println(actualPayload.size());

            // Initiate reliable send via LinkManager
            if (!_linkManager.sendReliableData(targetDest, actualPayload)) {
                 DebugSerial.println("! CMD Failed: Could not initiate reliable send.");
                 // Optionally send failure notification back via KISS? Complex.
            }

        } else {
             DebugSerial.println("! Invalid Local Command: payload too short.");
        }
        return; // Command processed
    }

    // --- Standard Unreliable Packet Processing for Self ---
    // (e.g., pings, service discovery, non-link application data)
    DebugSerial.print("> Self Packet! Dst="); Utils::printBytes(packetInfo.destination, RNS_ADDRESS_SIZE, Serial);
    DebugSerial.print(" Src="); Utils::printBytes(packetInfo.source, RNS_ADDRESS_SIZE, Serial);
    DebugSerial.print(" If="); DebugSerial.print(static_cast<int>(interface));
    DebugSerial.print(" Ctx="); DebugSerial.print(packetInfo.context, HEX);
    DebugSerial.print(" Payload: [");
    for(uint8_t byte : packetInfo.payload) { if(isprint(byte)) DebugSerial.print((char)byte); else DebugSerial.print('.'); }
    DebugSerial.println("]");

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

    // Create forwarding packet info (increment hops)
    RnsPacketInfo forwardInfo = packetInfo; // Creates a copy
    forwardInfo.hops++;

    uint8_t forwardBuffer[MAX_PACKET_SIZE];
    size_t forwardLen = 0;
    // Need to serialize using the *original* data payload, not link-processed one
    if (!ReticulumPacket::serialize(forwardBuffer, forwardLen,
        forwardInfo.destination, forwardInfo.source, forwardInfo.destination_type,
        forwardInfo.header_type, forwardInfo.context, forwardInfo.packet_id,
        forwardInfo.hops, forwardInfo.payload, 0)) // Pass original payload, no sequence num
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
    if (!_routingTable.shouldForwardAnnounce(packetInfo.packet_id, packetInfo.source)) {
         // DebugSerial.println("Announce recently forwarded or loop detected. Skipping re-broadcast."); // Verbose
        return;
    }
    // Mark as forwarded BEFORE sending
    _routingTable.markAnnounceForwarded(packetInfo.packet_id, packetInfo.source);

    // Create forwarding packet info (increment hops)
    RnsPacketInfo forwardInfo = packetInfo;
    forwardInfo.hops++;

    uint8_t forwardBuffer[MAX_PACKET_SIZE];
    size_t forwardLen = 0;
    // Serialize announce (no sequence number)
    if (!ReticulumPacket::serialize(forwardBuffer, forwardLen,
        forwardInfo.destination, forwardInfo.source, forwardInfo.destination_type,
        forwardInfo.header_type, forwardInfo.context, forwardInfo.packet_id,
        forwardInfo.hops, forwardInfo.payload, 0))
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
    DebugSerial.print(">> App Data Received! Src: "); Utils::printBytes(source_address, RNS_ADDRESS_SIZE, Serial);
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
