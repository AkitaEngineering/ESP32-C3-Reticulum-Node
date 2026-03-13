#include "LinkManager.h"
#include "ReticulumNode.h"
#include "RNSCrypto.h"
#include "Utils.h"
#include "InterfaceManager.h"
#include <Arduino.h>
#include <new>

LinkManager::LinkManager(ReticulumNode& owner) : _ownerRef(owner) {}

size_t LinkManager::getActiveLinkCount() const {
    return _activeLinks.size();
}

LinkManager::LinkPtr LinkManager::findLink(const uint8_t link_id[16]) {
    std::array<uint8_t, 16> key;
    memcpy(key.data(), link_id, 16);
    auto it = _activeLinks.find(key);
    if (it != _activeLinks.end()) return it->second;
    return nullptr;
}

// ========================================================================
// Process incoming link-related packets
// ========================================================================

void LinkManager::processPacket(const RnsPacketInfo& packetInfo, InterfaceType /*interface*/) {
    // LINKREQUEST packets (packet_type=0x02) are handled separately
    if (packetInfo.packet_type == RNS_PACKET_LINKREQ) {
        handleLinkRequest(packetInfo, nullptr, 0);
        return;
    }

    // All other link packets are addressed by link_id in the destination field
    handleLinkPacket(packetInfo);
}

// ========================================================================
// Handle incoming LINKREQUEST
// ========================================================================

void LinkManager::handleLinkRequest(const RnsPacketInfo& packetInfo,
                                     const uint8_t* raw, size_t rawLen) {
    // Validate payload size: [X25519_pub 32][Ed25519_sig_pub 32][signalling 3] = 67
    if (packetInfo.data.size() != RNS_LINK_REQUEST_SIZE) {
        DebugSerial.print("! LinkManager: Invalid LINKREQUEST size ");
        DebugSerial.println(packetInfo.data.size());
        return;
    }

    if (_activeLinks.size() >= LINK_MAX_ACTIVE) {
        DebugSerial.println("! LinkManager: Max links reached, dropping LINKREQUEST");
        return;
    }

    const uint8_t* peer_x25519_pub = packetInfo.data.data();
    const uint8_t* peer_sig_pub = packetInfo.data.data() + 32;
    const uint8_t* signalling = packetInfo.data.data() + 64;

    // Check mode
    uint8_t mode = RNSLink::modeFromSignalling(signalling);
    if (mode != RNS_LINK_MODE_AES256_CBC) {
        DebugSerial.print("! LinkManager: Unsupported link mode ");
        DebugSerial.println(mode);
        return;
    }

    // Compute link_id from hashable part of the LINKREQUEST packet
    // hashable_part = (flags & 0x0F) + raw[2:] minus the MTU signalling bytes
    // Since we're in deserialized form, reconstruct the hashable part:
    // For a Header Type 1 LINKREQUEST: [flags 1][hops 1][dest_hash 16][context 1][data 67]
    // hashable = (flags & 0x0F) + dest_hash(16) + context(1) + data_without_signalling(64)
    // = 1 + 16 + 1 + 64 = 82 bytes
    uint8_t hashable[82];
    size_t h_off = 0;

    // Reconstruct flags byte: header_type=0 (bits 6-7), packet_type=LINKREQ=0x02 (bits 0-1),
    //                          dest_type=SINGLE=0x00 (bits 2-3)
    uint8_t flags = (packetInfo.packet_type & 0x03) |
                    ((packetInfo.destination_type & 0x03) << 2) |
                    ((packetInfo.propagation_type & 0x01) << 4) |
                    ((packetInfo.context_flag ? 1 : 0) << 5);
    hashable[h_off++] = flags & 0x0F;  // Only lower nibble for hashing

    // dest_hash (16 bytes)
    memcpy(hashable + h_off, packetInfo.destination, 16); h_off += 16;

    // context byte
    hashable[h_off++] = packetInfo.context;

    // Data without signalling (first 64 bytes = X25519_pub + Ed25519_sig_pub)
    memcpy(hashable + h_off, packetInfo.data.data(), 64); h_off += 64;

    uint8_t link_id[16];
    RNSIdentity::truncated_hash(hashable, h_off, link_id);

    DebugSerial.print("[LinkManager] LINKREQUEST received, link_id=");
    Utils::printBytes(link_id, 16, DebugSerial);
    DebugSerial.println();

    // Check if we already have this link
    if (findLink(link_id)) {
        DebugSerial.println("[LinkManager] Duplicate LINKREQUEST, ignoring");
        return;
    }

    // Create new link as responder
    RNSCrypto& identity = getIdentity();
    auto link = std::make_shared<RNSLink>(link_id, peer_x25519_pub, peer_sig_pub, *this, identity);

    // Perform ECDH handshake
    if (!link->handshake()) {
        DebugSerial.println("! LinkManager: handshake failed");
        return;
    }

    // Send proof
    if (!link->prove()) {
        DebugSerial.println("! LinkManager: prove failed");
        return;
    }

    // Register the link (in HANDSHAKE state, waiting for LRRTT)
    std::array<uint8_t, 16> key;
    memcpy(key.data(), link_id, 16);
    _activeLinks[key] = link;

    DebugSerial.println("[LinkManager] Link registered (HANDSHAKE), awaiting LRRTT");
}

// ========================================================================
// Handle link-addressed packets (LRPROOF, LRRTT, DATA, KEEPALIVE, etc.)
// ========================================================================

void LinkManager::handleLinkPacket(const RnsPacketInfo& packetInfo) {
    // For link-addressed packets, the destination field IS the link_id
    auto link = findLink(packetInfo.destination);
    if (!link) {
        // Not found — could be LRPROOF for an initiator link (look up by link_id)
        // (initiator outbound links would also be in _activeLinks)
        return;
    }

    link->handlePacket(packetInfo);

    // Prune if link closed
    if (link->getState() == RNSLinkState::CLOSED) {
        std::array<uint8_t, 16> key;
        memcpy(key.data(), link->getLinkId(), 16);
        _activeLinks.erase(key);
    }
}

// ========================================================================
// Periodic maintenance
// ========================================================================

void LinkManager::checkAllTimeouts() {
    for (auto it = _activeLinks.begin(); it != _activeLinks.end(); ++it) {
        it->second->checkTimeouts();
    }
    pruneClosedLinks();
}

void LinkManager::pruneClosedLinks() {
    for (auto it = _activeLinks.begin(); it != _activeLinks.end(); ) {
        if (it->second->getState() == RNSLinkState::CLOSED) {
            it = _activeLinks.erase(it);
        } else {
            ++it;
        }
    }
}

void LinkManager::removeLink(const uint8_t link_id[16]) {
    std::array<uint8_t, 16> key;
    memcpy(key.data(), link_id, 16);
    auto it = _activeLinks.find(key);
    if (it != _activeLinks.end()) {
        it->second->close(false);
        _activeLinks.erase(it);
    }
}

// --- Pass-through methods for RNSLink instances ---
const uint8_t* LinkManager::getNodeAddress() const { return _ownerRef.getNodeAddress(); }
uint16_t LinkManager::getNextPacketId() { return _ownerRef.getNextPacketId(); }

void LinkManager::sendPacketRaw(const uint8_t* buffer, size_t len, const uint8_t* destination) {
    if (!buffer || len == 0 || !destination) return;
    _ownerRef.getInterfaceManager().sendPacket(buffer, len, destination, InterfaceType::UNKNOWN);
}

void LinkManager::processReceivedLinkData(const uint8_t* link_id, const std::vector<uint8_t>& data) {
    _ownerRef.processAppData(link_id, data);
}

RNSCrypto& LinkManager::getIdentity() {
    return _ownerRef.getIdentity();
}
