#ifndef LINK_MANAGER_H
#define LINK_MANAGER_H

#include <map>
#include <array>
#include <vector>
#include <memory>     // For std::shared_ptr
#include <cstring>    // For memcmp, memcpy
#include <functional> // For std::function if needed later

#include "Config.h"
#include "Link.h" // Include Link class definition
#include "ReticulumPacket.h" // For RnsPacketInfo

// Forward declaration
class ReticulumNode;

// Custom comparator for using std::array as map key
struct ArrayCompare {
    bool operator()(const std::array<uint8_t, RNS_ADDRESS_SIZE>& a,
                    const std::array<uint8_t, RNS_ADDRESS_SIZE>& b) const {
        return std::memcmp(a.data(), b.data(), RNS_ADDRESS_SIZE) < 0;
    }
};

class LinkManager {
public:
    LinkManager(ReticulumNode& owner);

    // Called from ReticulumNode::handleReceivedPacket for link-related packets
    void processPacket(const RnsPacketInfo& packetInfo, InterfaceType interface);

    // Called from application logic or command handler to send reliable data
    bool sendReliableData(const uint8_t* destination, const std::vector<uint8_t>& payload);

    // Called from ReticulumNode::loop periodically
    void checkAllTimeouts();

    // Called by Link::teardown or externally if needed
    void removeLink(const uint8_t* destination);

    // Return the number of active links
    size_t getActiveLinkCount() const;

    // --- Methods needed by Link instances (called via ownerRef) ---
    const uint8_t* getNodeAddress() const;
    uint16_t getNextPacketId();
    // Send packet using the main node's interface manager
    void sendPacketRaw(const uint8_t* buffer, size_t len, const uint8_t* destination);
    // Callback to pass received data up to the application layer via ReticulumNode
    void processReceivedLinkData(const uint8_t* source_address, const std::vector<uint8_t>& data);


private:
    // Use shared_ptr for automatic memory management of Link objects
    using LinkPtr = std::shared_ptr<Link>;
    using LinkMap = std::map<std::array<uint8_t, RNS_ADDRESS_SIZE>, LinkPtr, ArrayCompare>;

    LinkMap _activeLinks;       // Map destination address to Link object
    ReticulumNode& _ownerRef; // Reference back to main node

    // Helper to find or create a link
    LinkPtr getOrCreateLink(const uint8_t* destination, bool create = true);
    // Helper to clean up inactive/closed links
    void pruneInactiveLinks();

};

#endif // LINK_MANAGER_H
