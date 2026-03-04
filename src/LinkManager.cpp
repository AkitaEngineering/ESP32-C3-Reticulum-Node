#include "LinkManager.h"
#include "ReticulumNode.h"
#include "Utils.h"
#include "InterfaceManager.h" // Needed to call sendPacketRaw via ReticulumNode
#include <Arduino.h>          // For millis(), Serial
#include <new>                // For std::nothrow

LinkManager::LinkManager(ReticulumNode& owner) : _ownerRef(owner) {}

size_t LinkManager::getActiveLinkCount() const {
    return _activeLinks.size();
}

// Get existing link or create if possible
LinkManager::LinkPtr LinkManager::getOrCreateLink(const uint8_t* destination, bool create) {
    if (!destination) {
        DebugSerial.println("! LinkManager::getOrCreateLink: Null destination provided.");
        return nullptr;
    }
    std::array<uint8_t, RNS_ADDRESS_SIZE> destArray;
    memcpy(destArray.data(), destination, RNS_ADDRESS_SIZE);

    auto it = _activeLinks.find(destArray);
    if (it != _activeLinks.end()) {
        // Found existing link
        // it->second->updateActivity(); // Update activity time on access? Maybe not needed here.
        return it->second;
    } else if (create && _activeLinks.size() < LINK_MAX_ACTIVE) {
        // Create new link if allowed and space available
        DebugSerial.print("LinkManager: Creating new Link object for "); Utils::printBytes(destination, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
        try {
             // Use 'new (std::nothrow)' for slightly safer allocation check than make_shared exception
             // Link* rawPtr = new (std::nothrow) Link(destination, *this);
             // if (!rawPtr) {
             //      DebugSerial.println("! ERROR: Failed to allocate memory for new Link!");
             //      return nullptr;
             // }
             // LinkPtr newLink(rawPtr); // Wrap in shared_ptr

             // Or stick with make_shared and handle potential exception (less common on embedded)
             LinkPtr newLink = std::make_shared<Link>(destination, *this);

             _activeLinks[destArray] = newLink; // Add to map
             return newLink;
        } catch (const std::bad_alloc& e) {
             DebugSerial.println("! ERROR: std::bad_alloc creating new Link!");
             return nullptr;
        } catch (...) {
             DebugSerial.println("! ERROR: Unknown exception creating new Link!");
             return nullptr;
        }
    } else if (create) {
         DebugSerial.print("! WARN: Max active links ("); DebugSerial.print(LINK_MAX_ACTIVE); DebugSerial.print(") reached. Cannot create new link to ");
         Utils::printBytes(destination, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
    }
    return nullptr; // Not found and not created
}

// Process incoming link-related packets
void LinkManager::processPacket(const RnsPacketInfo& packetInfo, InterfaceType interface) {
    // Link packets are identified by source address (who sent it to us)
    LinkPtr link = getOrCreateLink(packetInfo.source, (packetInfo.context == RNS_CONTEXT_LINK_REQ));

    if (link) {
        link->handlePacket(packetInfo);
        // If handling the packet caused the link state to become CLOSED, pruneInactiveLinks will clean it up.
    } else {
         // If it wasn't a LINK_REQ or we couldn't create a link (e.g., max links reached), ignore it.
         if (packetInfo.context != RNS_CONTEXT_LINK_REQ) {
            DebugSerial.print("! LinkManager: Received non-REQ Link packet for unknown/uncreatable source: "); Utils::printBytes(packetInfo.source, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
         }
    }
}

// Initiate reliable data send
bool LinkManager::sendReliableData(const uint8_t* destination, const std::vector<uint8_t>& payload) {
    LinkPtr link = getOrCreateLink(destination, true); // Get or create link
    if (!link) {
         DebugSerial.println("! LinkManager::sendReliableData failed: Cannot get/create Link.");
         return false;
    }

    // If link is closed, try establishing it first
    if (!link->isActive()) { // Checks if state == CLOSED
        DebugSerial.println("LinkManager::sendReliableData: Link is inactive, attempting establishment.");
        if (!link->establish()) {
             // Establish might fail if state wasn't CLOSED or serialize failed
             DebugSerial.println("! ERROR: LinkManager::sendReliableData failed to initiate link establishment.");
             // Maybe remove the failed link attempt? Let prune handle it.
             return false;
        }
         DebugSerial.println("Link establishment initiated. Try sending data again after ESTABLISHED.");
         return false; // Indicate send didn't happen now
    }

    // If link is established, attempt to send data
    if (link->isEstablished()) {
        return link->sendData(payload); // Returns true if send attempt initiated
    } else {
        // Link is pending or closing, cannot send data now
        DebugSerial.println("! LinkManager::sendReliableData failed: Link not established yet (pending/closing).");
        return false;
    }
}

// Periodically check timeouts for all active links
void LinkManager::checkAllTimeouts() {
    // Use safe iteration because Link::checkTimeouts might trigger Link::teardown,
    // which now sets the state to CLOSED, allowing pruneInactiveLinks to remove it later.
    for (auto it = _activeLinks.begin(); it != _activeLinks.end(); ++it) {
        it->second->checkTimeouts();
    }
    // Prune links marked as CLOSED or inactive after checking timeouts
    pruneInactiveLinks();
}

// Clean up links that are CLOSED or haven't been active
void LinkManager::pruneInactiveLinks() {
    unsigned long now = millis();
     for (auto it = _activeLinks.begin(); it != _activeLinks.end(); /* manual increment */ ) {
         bool remove_it = false;
         if (!it->second->isActive()) { // Check if state is CLOSED
              // DebugSerial.print("Pruning explicitly closed link: "); Utils::printBytes(it->first.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println(); // Verbose
              remove_it = true;
         } else if (now - it->second->getLastActivityTime() > LINK_INACTIVITY_TIMEOUT_MS) {
              DebugSerial.print("! Link Inactivity timeout: "); Utils::printBytes(it->first.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
              it->second->teardown(); // Set state to CLOSED, doesn't remove from map directly
              remove_it = true; // Mark for removal
         }

         if (remove_it) {
             it = _activeLinks.erase(it); // Erase and get iterator to next
         } else {
             ++it; // Only increment if not erased
         }
     }
}


// Explicitly remove a link (e.g., called by Link on CLOSE_ACK or teardown)
void LinkManager::removeLink(const uint8_t* destination) {
     if (!destination) return;
     std::array<uint8_t, RNS_ADDRESS_SIZE> destArray;
     memcpy(destArray.data(), destination, RNS_ADDRESS_SIZE);

     auto it = _activeLinks.find(destArray);
     if (it != _activeLinks.end()) {
          DebugSerial.print("LinkManager removing link: "); Utils::printBytes(destination, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
          // Set state to closed just in case before erasing
          it->second->teardown(); // Ensure state is CLOSED
          _activeLinks.erase(it);
     } else {
         // DebugSerial.print("LinkManager removeLink: Link not found for "); Utils::printBytes(destination, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println(); // Verbose
     }
}

// --- Pass-through methods for Link instances ---
const uint8_t* LinkManager::getNodeAddress() const { return _ownerRef.getNodeAddress(); }
uint16_t LinkManager::getNextPacketId() { return _ownerRef.getNextPacketId(); }

// Send packet via the main node's interface manager
void LinkManager::sendPacketRaw(const uint8_t* buffer, size_t len, const uint8_t* destination) {
    if (!buffer || len == 0 || !destination) return;
    // Link layer packets generally bypass high-level routing and go direct if possible.
    // Use the InterfaceManager's sendPacket which uses the routing table.
    // This ensures links can be established even if only broadcast path exists initially.
     _ownerRef.getInterfaceManager().sendPacket(buffer, len, destination, InterfaceType::UNKNOWN);
}

// Callback called by Link instances when data is successfully received and acknowledged
void LinkManager::processReceivedLinkData(const uint8_t* source_address, const std::vector<uint8_t>& data) {
    _ownerRef.processAppData(source_address, data); // Pass data up to the main node's app handler
}
