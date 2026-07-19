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
    uint8_t destination_addr[RNS_TRUNCATED_HASHLENGTH_BYTES] = {0};
    uint8_t next_hop_mac[6] = {0};
    IPAddress next_hop_ip;
    uint16_t next_hop_port = 0;
    unsigned long last_heard_time = 0;
    InterfaceType interface = InterfaceType::ESP_NOW;
    uint8_t hops = 0;
};

struct RouteDiagnosticCandidate {
    std::array<uint8_t, 6> next_hop_mac = {0};
    IPAddress next_hop_ip;
    uint16_t next_hop_port = 0;
    unsigned long last_heard_uptime_ms = 0;
    unsigned long age_ms = 0;
    InterfaceType interface = InterfaceType::UNKNOWN;
    uint8_t hops = 0;
    int interface_priority = 0;
    bool usable = false;
    bool selected = false;
};

struct RouteDiagnosticGroup {
    std::array<uint8_t, RNS_TRUNCATED_HASHLENGTH_BYTES> destination_addr = {0};
    std::vector<RouteDiagnosticCandidate> candidates;
};

// Structure to store recent announce IDs (announce hash + source addr prefix)
// Used as key in std::map for loop prevention
struct RecentAnnounceKey {
    std::array<uint8_t, RNS_TRUNCATED_HASHLENGTH_BYTES> packet_hash = {0};

    // Need operator< for std::map
    bool operator<(const RecentAnnounceKey& other) const {
        return packet_hash < other.packet_hash;
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
    RouteEntry* findRoute(const uint8_t *destination_addr,
                          InterfaceType excludeInterface = InterfaceType::UNKNOWN,
                          const std::function<bool(InterfaceType)> &isInterfaceUsable = nullptr);
    RouteEntry* findRouteForInterface(const uint8_t *destination_addr, InterfaceType interface);

    // Removes expired routes
    void prune(InterfaceManager* ifManager = nullptr); // Pass IfMgr if peer removal is needed

    // Prints the routing table to Serial
    void print();

    // Return count of distinct destinations currently reachable.
    size_t getRouteCount() const;
    // Return total number of candidate route entries stored.
    size_t getRouteCandidateCount() const;
    // Return total number of candidate route entries stored for an interface.
    size_t getRouteCandidateCountForInterface(InterfaceType interface) const;
    std::vector<RouteDiagnosticGroup> getRouteDiagnostics(const std::function<bool(InterfaceType)> &isInterfaceUsable = nullptr) const;

    // Announce forwarding prevention
    bool shouldForwardAnnounce(const uint8_t packet_hash[32]);
    void markAnnounceForwarded(const uint8_t packet_hash[32]);
    void pruneRecentAnnounces(bool force = false);


private:
    static bool routeMatchesCandidate(const RouteEntry& entry, const uint8_t *destination_addr,
                                      InterfaceType interface, const uint8_t* sender_mac,
                                      const IPAddress& sender_ip, uint16_t sender_port);
    static int routePriority(InterfaceType interface);
    static bool isBetterRouteCandidate(const RouteEntry& candidate, const RouteEntry& currentBest);
    bool isEspNowPeerReferenced(const uint8_t mac[6], const RouteEntry* excluding = nullptr) const;

    std::list<RouteEntry> _routes;
    unsigned long _last_prune_time = 0;

    // Map to track recently forwarded announce IDs and when they were seen
    std::map<RecentAnnounceKey, unsigned long> _recentAnnounces;
    unsigned long _last_recent_announce_prune = 0;

};

#endif // ROUTING_TABLE_H
