#ifndef ROUTING_TABLE_H
#define ROUTING_TABLE_H

#include <list>
#include <cstdint>
#include <IPAddress.h>
#include <functional> // For callbacks if needed later
#include <map>        // For recent announces
#include <utility>    // For std::pair
#include <vector>     // For recent announce ID value
#include <array>      // For map key

#include "Config.h"
#include "ReticulumPacket.h" // For RnsPacketInfo

// Forward declaration
class InterfaceManager; // Needed? Only if RoutingTable needs to call InterfaceManager for peer removal

struct RouteEntry {
    uint8_t destination_addr[RNS_ADDRESS_SIZE];
    uint8_t next_hop_mac[6];
    IPAddress next_hop_ip;
    uint16_t next_hop_port;
    unsigned long last_heard_time;
    InterfaceType interface;
    uint8_t hops; // Store hops from Announce
    // Note: RSSI is not reliably available via Arduino ESP-NOW API
};

// Structure to store recent announce IDs (Packet ID + Source Addr prefix)
// Used as key in std::map for loop prevention
struct RecentAnnounceKey {
    uint16_t packet_id;
    uint8_t source_prefix[4]; // Use first 4 bytes of source addr

    // Need operator< for std::map
    bool operator<(const RecentAnnounceKey& other) const {
        if (packet_id != other.packet_id) return packet_id < other.packet_id;
        return memcmp(source_prefix, other.source_prefix, sizeof(source_prefix)) < 0;
    }
};


class RoutingTable {
public:
    RoutingTable();

    // Updates table based on a received Announce packet
    void update(const RnsPacketInfo &announcePacket, InterfaceType interface,
                const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port,
                InterfaceManager* ifManager = nullptr);

    // Finds the best route for a destination address
    RouteEntry* findRoute(const uint8_t *destination_addr);

    // Removes expired routes
    void prune(InterfaceManager* ifManager = nullptr); // Pass IfMgr if peer removal is needed

    // Prints the routing table to Serial
    void print();

    // Return current route count
    size_t getRouteCount() const;

    // Announce forwarding prevention
    bool shouldForwardAnnounce(uint16_t packet_id, const uint8_t* source_addr);
    void markAnnounceForwarded(uint16_t packet_id, const uint8_t* source_addr);
    void pruneRecentAnnounces(bool force = false);


private:
    std::list<RouteEntry> _routes;
    unsigned long _last_prune_time = 0;

    // Map to track recently forwarded announce IDs and when they were seen
    std::map<RecentAnnounceKey, unsigned long> _recentAnnounces;
    unsigned long _last_recent_announce_prune = 0;

};

#endif // ROUTING_TABLE_H
