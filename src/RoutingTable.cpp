#include "RoutingTable.h"
#include "Config.h"
#include "Utils.h"    // For printBytes, compareAddresses
#include "InterfaceManager.h" // Need definition for prune's peer removal call
#include <Arduino.h> // For millis(), Serial
#include <algorithm> // For std::min_element
#include <cstring>   // For memcpy

// Constructor
RoutingTable::RoutingTable() : _last_prune_time(0), _last_recent_announce_prune(0) {}

bool RoutingTable::routeMatchesCandidate(const RouteEntry& entry, const uint8_t *destination_addr,
                                         InterfaceType interface, const uint8_t* sender_mac,
                                         const IPAddress& sender_ip, uint16_t sender_port) {
    if (!destination_addr || !Utils::compareAddresses(entry.destination_addr, destination_addr)) {
        return false;
    }

    if (entry.interface != interface) {
        return false;
    }

    switch (interface) {
        case InterfaceType::ESP_NOW:
            return sender_mac && memcmp(entry.next_hop_mac, sender_mac, 6) == 0;
        case InterfaceType::WIFI_UDP:
            return sender_ip && entry.next_hop_ip == sender_ip && entry.next_hop_port == sender_port;
        default:
            return true;
    }
}

int RoutingTable::routePriority(InterfaceType interface) {
    const RoutePriorityConfig& priorities = getConfiguredRoutePriorities();
    switch (interface) {
        case InterfaceType::WIFI_UDP: return priorities.wifi_udp;
        case InterfaceType::ESP_NOW: return priorities.esp_now;
        case InterfaceType::LORA: return priorities.lora;
        case InterfaceType::HAM_MODEM: return priorities.ham_modem;
        case InterfaceType::SERIAL_PORT: return priorities.serial_port;
        case InterfaceType::BLUETOOTH: return priorities.bluetooth;
        case InterfaceType::IPFS: return priorities.ipfs;
        default: return 0;
    }
}

bool RoutingTable::isBetterRouteCandidate(const RouteEntry& candidate, const RouteEntry& currentBest) {
    if (candidate.hops != currentBest.hops) {
        return candidate.hops < currentBest.hops;
    }

    const int candidatePriority = routePriority(candidate.interface);
    const int currentPriority = routePriority(currentBest.interface);
    if (candidatePriority != currentPriority) {
        return candidatePriority > currentPriority;
    }

    return candidate.last_heard_time > currentBest.last_heard_time;
}

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

    // Check if this exact route candidate already exists
    for (auto it = _routes.begin(); it != _routes.end(); ++it) {
        if (routeMatchesCandidate(*it, announcePacket.source, interface, sender_mac, sender_ip, sender_port)) {
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

    // If not found, add a new candidate route if space allows
    if (!found) {
        if (_routes.size() < MAX_ROUTES) {
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

RouteEntry* RoutingTable::findRoute(const uint8_t *destination_addr,
                                    InterfaceType excludeInterface,
                                    const std::function<bool(InterfaceType)> &isInterfaceUsable) {
    if (!destination_addr) return nullptr;

    RouteEntry* best = nullptr;
    for (auto it = _routes.begin(); it != _routes.end(); ++it) {
        if (!Utils::compareAddresses(it->destination_addr, destination_addr)) {
            continue;
        }

        if (excludeInterface != InterfaceType::UNKNOWN && it->interface == excludeInterface) {
            continue;
        }

        if (isInterfaceUsable && !isInterfaceUsable(it->interface)) {
            continue;
        }

        if (!best || isBetterRouteCandidate(*it, *best)) {
            best = &(*it);
        }
    }

    return best;
}

RouteEntry* RoutingTable::findRouteForInterface(const uint8_t *destination_addr, InterfaceType interface) {
    if (!destination_addr) return nullptr;

    RouteEntry* best = nullptr;
    for (auto it = _routes.begin(); it != _routes.end(); ++it) {
        if (!Utils::compareAddresses(it->destination_addr, destination_addr) || it->interface != interface) {
            continue;
        }

        if (!best || isBetterRouteCandidate(*it, *best)) {
            best = &(*it);
        }
    }

    return best;
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
    std::vector<std::array<uint8_t, RNS_ADDRESS_SIZE>> destinations;
    destinations.reserve(_routes.size());

    for (const auto& entry : _routes) {
        bool seen = false;
        for (const auto& destination : destinations) {
            if (memcmp(destination.data(), entry.destination_addr, RNS_ADDRESS_SIZE) == 0) {
                seen = true;
                break;
            }
        }

        if (!seen) {
            std::array<uint8_t, RNS_ADDRESS_SIZE> destination = {0};
            memcpy(destination.data(), entry.destination_addr, RNS_ADDRESS_SIZE);
            destinations.push_back(destination);
        }
    }

    return destinations.size();
}

size_t RoutingTable::getRouteCandidateCount() const {
    return _routes.size();
}

size_t RoutingTable::getRouteCandidateCountForInterface(InterfaceType interface) const {
    size_t count = 0;
    for (const auto& entry : _routes) {
        if (entry.interface == interface) {
            ++count;
        }
    }
    return count;
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
