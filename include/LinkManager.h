#ifndef LINK_MANAGER_H
#define LINK_MANAGER_H

#include <map>
#include <array>
#include <vector>
#include <memory>
#include <cstring>
#include <functional>

#include "Config.h"
#include "Link.h"
#include "ReticulumPacket.h"

// Forward declarations
class ReticulumNode;
class RNSCrypto;

// Comparator for 16-byte link IDs as map keys
struct LinkIdCompare {
    bool operator()(const std::array<uint8_t, 16>& a,
                    const std::array<uint8_t, 16>& b) const {
        return std::memcmp(a.data(), b.data(), 16) < 0;
    }
};

class LinkManager {
public:
    LinkManager(ReticulumNode& owner);

    /**
     * Process an incoming link-related packet.
     * Routes to the appropriate RNSLink by link_id, or creates a new link
     * for incoming LINKREQUEST packets.
     */
    void processPacket(const RnsPacketInfo& packetInfo, InterfaceType interface);

    /**
     * Handle an incoming LINKREQUEST packet_type=0x02.
     * Creates a new RNSLink as responder, performs handshake, and sends proof.
     */
    void handleLinkRequest(const RnsPacketInfo& packetInfo, InterfaceType interface);

    /**
     * Handle an incoming LRPROOF (or other link-addressed packet).
     * Routes to existing link by destination hash (which is the link_id).
     */
    void handleLinkPacket(const RnsPacketInfo& packetInfo, InterfaceType interface);

    /** Periodically check all links for timeouts. */
    void checkAllTimeouts();

    /** Remove a link by its 16-byte link_id. */
    void removeLink(const uint8_t link_id[16]);

    /** Number of active links. */
    size_t getActiveLinkCount() const;

    /** True when this node owns the addressed link. */
    bool hasLink(const uint8_t link_id[16]) const;

    /** Start an outgoing link using a destination hash and announced public key. */
    bool establishLink(const uint8_t dest_hash[16], const uint8_t dest_pub_key[64],
                       uint8_t out_link_id[16] = nullptr);

    /** Send application data over an established link. */
    bool sendLinkData(const uint8_t link_id[16], const uint8_t* data, size_t len);

    // --- Methods for RNSLink instances ---
    const uint8_t* getNodeAddress() const;
    uint16_t getNextPacketId();
    void sendPacketRaw(const uint8_t* buffer, size_t len, const uint8_t* destination,
                       InterfaceType interface = InterfaceType::UNKNOWN);
    void processReceivedLinkData(const uint8_t* link_id, const std::vector<uint8_t>& data);
    RNSCrypto& getIdentity();

private:
    using LinkPtr = std::shared_ptr<RNSLink>;
    using LinkMap = std::map<std::array<uint8_t, 16>, LinkPtr, LinkIdCompare>;

    LinkMap _activeLinks;
    ReticulumNode& _ownerRef;

    LinkPtr findLink(const uint8_t link_id[16]);
    std::shared_ptr<const RNSLink> findLink(const uint8_t link_id[16]) const;
    void pruneClosedLinks();
};

#endif // LINK_MANAGER_H
