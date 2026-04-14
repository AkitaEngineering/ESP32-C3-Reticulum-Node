#include "RoutingTable.h"
#include "Config.h"
#include "Utils.h"    // For printBytes, compareAddresses
#include "InterfaceManager.h" // Need definition for prune's peer removal call
#include <Arduino.h> // For millis(), Serial
#include <algorithm> // For std::min_element
#include <cstring>   // For memcpy

// Constructor
RoutingTable::RoutingTable() : _last_prune_time(0), _last_recent_announce_prune(0) {}

void RoutingTable::update(const RnsPacketInfo &announcePacket, InterfaceType interface,
                           const uint8_t* sender_mac, const IPAddress& sender_ip, uint16_t sender_port,
                           InterfaceManager* ifManager)
{
    // Validate sender info based on interface
    if (interface == InterfaceType::ESP_NOW && !sender_mac) return;
    // Allow 0.0.0.0 IP? Maybe not useful. Check for valid IP.
    if (interface == InterfaceType::WIFI_UDP && (!sender_ip || sender_ip == INADDR_NONE || sender_ip[0] == 0)) return;

    unsigned long now = millis();
    bool found = false;

    // Check if route already exists
    for (auto it = _routes.begin(); it != _routes.end(); ++it) {
        if (Utils::compareAddresses(it->destination_addr, announcePacket.source)) {
            // Route exists. Update if new info is better or from different interface?
            // Simple strategy: Always update timestamp, interface, hops, next hop.
            // More complex: Update only if hops are lower or similar but timestamp newer.
            // Current: Always update with latest info.
            it->last_heard_time = now;
            it->interface = interface;
            it->hops = announcePacket.hops;

            if (interface == InterfaceType::ESP_NOW) {
                memcpy(it->next_hop_mac, sender_mac, 6);
                it->next_hop_ip = IPAddress(); // Clear IP
                it->next_hop_port = 0;
                // Make sure the peer is registered so that future sends succeed
                if (ifManager) {
                    ifManager->addEspNowPeer(sender_mac);
                }
            } else if (interface == InterfaceType::WIFI_UDP) {
                 it->next_hop_ip = sender_ip;
                 it->next_hop_port = RNS_UDP_PORT; // Assume standard RNS port for outgoing
                 memset(it->next_hop_mac, 0, 6);   // Clear MAC
            }
            found = true;
            break;
        }
    }

    // If not found, add new route if space allows
    if (!found) {
        if (_routes.size() < MAX_ROUTES) {
            // DebugSerial.print("RT: Adding new route for "); Utils::printBytes(announcePacket.source, RNS_ADDRESS_SIZE, DebugSerial); // Verbose
            RouteEntry newEntry;
            memcpy(newEntry.destination_addr, announcePacket.source, RNS_ADDRESS_SIZE);
            newEntry.last_heard_time = now;
            newEntry.interface = interface;
            newEntry.hops = announcePacket.hops;

             if (interface == InterfaceType::ESP_NOW) {
                memcpy(newEntry.next_hop_mac, sender_mac, 6);
                // register peer immediately to simplify future sends
                if (ifManager) {
                    ifManager->addEspNowPeer(sender_mac);
                }
            } else if (interface == InterfaceType::WIFI_UDP) {
                 newEntry.next_hop_ip = sender_ip;
                 newEntry.next_hop_port = RNS_UDP_PORT;
            }
            // DebugSerial.print(" via If="); DebugSerial.print(static_cast<int>(interface)); DebugSerial.print(" Hops="); DebugSerial.println(newEntry.hops); // Verbose
            _routes.push_back(newEntry);
        } else {
            // Table full - Replace oldest entry
            auto oldest_it = std::min_element(_routes.begin(), _routes.end(),
                [](const RouteEntry& a, const RouteEntry& b) {
                    return a.last_heard_time < b.last_heard_time;
                });

              if (oldest_it != _routes.end()) {
                 DebugSerial.print("! RT Full. Replacing oldest route to "); Utils::printBytes(oldest_it->destination_addr, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
                  // If replacing an ESP-NOW route, remove the old peer to avoid stale entries.
                  if (ifManager && oldest_it->interface == InterfaceType::ESP_NOW) {
                     ifManager->removeEspNowPeer(oldest_it->next_hop_mac);
                  }

                // Overwrite the oldest entry with new data
                memcpy(oldest_it->destination_addr, announcePacket.source, RNS_ADDRESS_SIZE);
                oldest_it->last_heard_time = now;
                oldest_it->interface = interface;
                oldest_it->hops = announcePacket.hops;
                 if (interface == InterfaceType::ESP_NOW) { memcpy(oldest_it->next_hop_mac, sender_mac, 6); oldest_it->next_hop_ip=IPAddress(); oldest_it->next_hop_port=0; }
                 else if (interface == InterfaceType::WIFI_UDP) { oldest_it->next_hop_ip = sender_ip; oldest_it->next_hop_port=RNS_UDP_PORT; memset(oldest_it->next_hop_mac,0,6); }
            } else {
                 DebugSerial.println("! RT Full. Error finding oldest route to replace."); // Should not happen if list not empty
            }
        }
    }
}

RouteEntry* RoutingTable::findRoute(const uint8_t *destination_addr) {
    if (!destination_addr) return nullptr;
    for (auto it = _routes.begin(); it != _routes.end(); ++it) {
        if (Utils::compareAddresses(it->destination_addr, destination_addr)) {
            return &(*it); // Return pointer to the found entry
        }
    }
    return nullptr; // Not found
}

// Pass InterfaceManager to handle peer removal during pruning
void RoutingTable::prune(InterfaceManager* ifManager) {
    unsigned long now = millis();
    if (now - _last_prune_time > PRUNE_INTERVAL_MS) {
        //bool changed = false; // unused in current logic, kept for potential future logging
        for (auto it = _routes.begin(); it != _routes.end(); /* manual increment */ ) {
            if (now - it->last_heard_time > ROUTE_TIMEOUT_MS) {
                 DebugSerial.print("RT: Route timed out for "); Utils::printBytes(it->destination_addr, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
                 // If it was an ESP-NOW route, remove the peer via InterfaceManager
                 if (ifManager && it->interface == InterfaceType::ESP_NOW) {
                      ifManager->removeEspNowPeer(it->next_hop_mac);
                 }
                it = _routes.erase(it); // Erase and get iterator to next element
                // changed = true; // tracking variable removed
            } else {
                ++it; // Only increment if not erased
            }
        }
        _last_prune_time = now;
        // if (changed) print(); // Optional: Print table if changed
    }
}

void RoutingTable::print() {
    DebugSerial.println("--- Routing Table ---");
    if (_routes.empty()) { DebugSerial.println("(Empty)"); return; }
    int i = 0;
    unsigned long now = millis();
    for (const auto& entry : _routes) {
        DebugSerial.print(i++); DebugSerial.print(": Dst="); Utils::printBytes(entry.destination_addr, RNS_ADDRESS_SIZE, DebugSerial);
        DebugSerial.print(" If="); DebugSerial.print(static_cast<int>(entry.interface));
        DebugSerial.print(" Hops="); DebugSerial.print(entry.hops);
        if (entry.interface == InterfaceType::ESP_NOW) { DebugSerial.print(" MAC="); Utils::printBytes(entry.next_hop_mac, 6, DebugSerial); }
        else if (entry.interface == InterfaceType::WIFI_UDP) { DebugSerial.print(" IP="); DebugSerial.print(entry.next_hop_ip); }
        DebugSerial.print(" Age="); DebugSerial.print((now - entry.last_heard_time) / 1000); DebugSerial.println("s");
    }
    DebugSerial.println("---------------------");
}

size_t RoutingTable::getRouteCount() const {
    return _routes.size();
}

// --- Announce Forwarding Prevention ---
bool RoutingTable::shouldForwardAnnounce(uint32_t packet_id, const uint8_t* source_addr) {
    if (!source_addr) return false;
    pruneRecentAnnounces();
    RecentAnnounceKey key;
    key.packet_id = packet_id;
    memcpy(key.source_prefix, source_addr, sizeof(key.source_prefix));
    return _recentAnnounces.find(key) == _recentAnnounces.end();
}

void RoutingTable::markAnnounceForwarded(uint32_t packet_id, const uint8_t* source_addr) {
    if (!source_addr) return;
    RecentAnnounceKey key;
    key.packet_id = packet_id;
    memcpy(key.source_prefix, source_addr, sizeof(key.source_prefix));
    _recentAnnounces[key] = millis();
    if (_recentAnnounces.size() > MAX_RECENT_ANNOUNCES) { // Prune if exceeds limit slightly
        pruneRecentAnnounces(true);
    }
}

void RoutingTable::pruneRecentAnnounces(bool force) {
    unsigned long now = millis();
    if (!force && (now - _last_recent_announce_prune < PRUNE_INTERVAL_MS / 2)) {
        return;
    }
    for (auto it = _recentAnnounces.begin(); it != _recentAnnounces.end(); ) {
        if (now - it->second > RECENT_ANNOUNCE_TIMEOUT_MS) { it = _recentAnnounces.erase(it); }
        else { ++it; }
    }
    _last_recent_announce_prune = now;
}
