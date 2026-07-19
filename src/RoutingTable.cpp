#include "RoutingTable.h"
#include "Config.h"
#include "Utils.h"    // For printBytes, compareAddresses
#include "InterfaceManager.h" // Need definition for prune's peer removal call
#include <Arduino.h> // For millis(), Serial
#include <algorithm> // For std::min_element
#include <cstring>   // For memcpy
#include <iterator>  // For std::next

// Constructor
RoutingTable::RoutingTable() : _last_prune_time(0), _last_recent_announce_prune(0) {}

bool RoutingTable::routeMatchesCandidate(const RouteEntry& entry, const uint8_t *destination_addr,
                                         InterfaceType interface, const uint8_t* sender_mac,
                                         const IPAddress& sender_ip, uint16_t sender_port) {
    if (!destination_addr || memcmp(entry.destination_addr, destination_addr,
                                    RNS_TRUNCATED_HASHLENGTH_BYTES) != 0) {
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

    const unsigned long now = millis();
    return (now - candidate.last_heard_time) < (now - currentBest.last_heard_time);
}

bool RoutingTable::isEspNowPeerReferenced(const uint8_t mac[6], const RouteEntry* excluding) const {
    if (!mac) return false;
    for (const auto& entry : _routes) {
        if (&entry != excluding && entry.interface == InterfaceType::ESP_NOW &&
            memcmp(entry.next_hop_mac, mac, 6) == 0) {
            return true;
        }
    }
    return false;
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
        if (routeMatchesCandidate(*it, announcePacket.destination_hash, interface, sender_mac, sender_ip, sender_port)) {
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
                 it->next_hop_port = sender_port != 0 ? sender_port : RNS_UDP_PORT;
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
            memcpy(newEntry.destination_addr, announcePacket.destination_hash, RNS_TRUNCATED_HASHLENGTH_BYTES);
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
                 newEntry.next_hop_port = sender_port != 0 ? sender_port : RNS_UDP_PORT;
            }
            _routes.push_back(newEntry);
        } else {
            // Table full - Replace oldest entry
            auto oldest_it = std::max_element(_routes.begin(), _routes.end(),
                [now](const RouteEntry& a, const RouteEntry& b) {
                    return (now - a.last_heard_time) < (now - b.last_heard_time);
                });

              if (oldest_it != _routes.end()) {
                 DebugSerial.print("! RT Full. Replacing oldest route to "); Utils::printBytes(oldest_it->destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES, DebugSerial); DebugSerial.println();
                  // If replacing an ESP-NOW route, remove the old peer to avoid stale entries.
                  if (ifManager && oldest_it->interface == InterfaceType::ESP_NOW &&
                      !isEspNowPeerReferenced(oldest_it->next_hop_mac, &(*oldest_it))) {
                     ifManager->removeEspNowPeer(oldest_it->next_hop_mac);
                  }

                // Overwrite the oldest entry with new data
                memcpy(oldest_it->destination_addr, announcePacket.destination_hash, RNS_TRUNCATED_HASHLENGTH_BYTES);
                oldest_it->last_heard_time = now;
                oldest_it->interface = interface;
                oldest_it->hops = announcePacket.hops;
                 if (interface == InterfaceType::ESP_NOW) {
                     memcpy(oldest_it->next_hop_mac, sender_mac, 6);
                     oldest_it->next_hop_ip = IPAddress();
                     oldest_it->next_hop_port = 0;
                     if (ifManager) ifManager->addEspNowPeer(sender_mac);
                 }
                 else if (interface == InterfaceType::WIFI_UDP) { oldest_it->next_hop_ip = sender_ip; oldest_it->next_hop_port = sender_port != 0 ? sender_port : RNS_UDP_PORT; memset(oldest_it->next_hop_mac,0,6); }
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
        if (memcmp(it->destination_addr, destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES) != 0) {
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
        if (memcmp(it->destination_addr, destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES) != 0 || it->interface != interface) {
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
        for (auto it = _routes.begin(); it != _routes.end(); /* manual increment */ ) {
            if (now - it->last_heard_time > ROUTE_TIMEOUT_MS) {
                 DebugSerial.print("RT: Route timed out for "); Utils::printBytes(it->destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES, DebugSerial); DebugSerial.println();
                 // If it was an ESP-NOW route, remove the peer via InterfaceManager
                 const bool removePeer = ifManager && it->interface == InterfaceType::ESP_NOW &&
                     !isEspNowPeerReferenced(it->next_hop_mac, &(*it));
                 uint8_t expiredMac[6] = {0};
                 if (removePeer) memcpy(expiredMac, it->next_hop_mac, sizeof(expiredMac));
                it = _routes.erase(it); // Erase and get iterator to next element
                if (removePeer) ifManager->removeEspNowPeer(expiredMac);
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
        DebugSerial.print(i++); DebugSerial.print(": Dst="); Utils::printBytes(entry.destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES, DebugSerial);
        DebugSerial.print(" If="); DebugSerial.print(static_cast<int>(entry.interface));
        DebugSerial.print(" Hops="); DebugSerial.print(entry.hops);
        if (entry.interface == InterfaceType::ESP_NOW) { DebugSerial.print(" MAC="); Utils::printBytes(entry.next_hop_mac, 6, DebugSerial); }
        else if (entry.interface == InterfaceType::WIFI_UDP) { DebugSerial.print(" IP="); DebugSerial.print(entry.next_hop_ip); }
        DebugSerial.print(" Age="); DebugSerial.print((now - entry.last_heard_time) / 1000); DebugSerial.println("s");
    }
    DebugSerial.println("---------------------");
}

size_t RoutingTable::getRouteCount() const {
    std::vector<std::array<uint8_t, RNS_TRUNCATED_HASHLENGTH_BYTES>> destinations;
    destinations.reserve(_routes.size());

    for (const auto& entry : _routes) {
        bool seen = false;
        for (const auto& destination : destinations) {
            if (memcmp(destination.data(), entry.destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES) == 0) {
                seen = true;
                break;
            }
        }

        if (!seen) {
            std::array<uint8_t, RNS_TRUNCATED_HASHLENGTH_BYTES> destination = {0};
            memcpy(destination.data(), entry.destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES);
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

std::vector<RouteDiagnosticGroup> RoutingTable::getRouteDiagnostics(const std::function<bool(InterfaceType)> &isInterfaceUsable) const {
    std::vector<RouteDiagnosticGroup> groups;
    groups.reserve(_routes.size());

    const unsigned long now = millis();
    for (const auto& entry : _routes) {
        RouteDiagnosticGroup* group = nullptr;
        for (auto& candidateGroup : groups) {
            if (memcmp(candidateGroup.destination_addr.data(), entry.destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES) == 0) {
                group = &candidateGroup;
                break;
            }
        }

        if (!group) {
            RouteDiagnosticGroup newGroup;
            memcpy(newGroup.destination_addr.data(), entry.destination_addr, RNS_TRUNCATED_HASHLENGTH_BYTES);
            groups.push_back(std::move(newGroup));
            group = &groups.back();
        }

        RouteDiagnosticCandidate candidate;
        memcpy(candidate.next_hop_mac.data(), entry.next_hop_mac, sizeof(entry.next_hop_mac));
        candidate.next_hop_ip = entry.next_hop_ip;
        candidate.next_hop_port = entry.next_hop_port;
        candidate.last_heard_uptime_ms = entry.last_heard_time;
        candidate.age_ms = now - entry.last_heard_time;
        candidate.interface = entry.interface;
        candidate.hops = entry.hops;
        candidate.interface_priority = routePriority(entry.interface);
        candidate.usable = !isInterfaceUsable || isInterfaceUsable(entry.interface);
        group->candidates.push_back(candidate);
    }

    auto isBetterDiagnosticCandidate = [](const RouteDiagnosticCandidate& candidate, const RouteDiagnosticCandidate& currentBest) {
        if (candidate.hops != currentBest.hops) {
            return candidate.hops < currentBest.hops;
        }
        if (candidate.interface_priority != currentBest.interface_priority) {
            return candidate.interface_priority > currentBest.interface_priority;
        }
        return candidate.age_ms < currentBest.age_ms;
    };

    for (auto& group : groups) {
        RouteDiagnosticCandidate* selectedCandidate = nullptr;
        for (auto& candidate : group.candidates) {
            if (!candidate.usable) {
                continue;
            }
            if (!selectedCandidate || isBetterDiagnosticCandidate(candidate, *selectedCandidate)) {
                selectedCandidate = &candidate;
            }
        }

        if (selectedCandidate) {
            selectedCandidate->selected = true;
        }
    }

    return groups;
}

// --- Announce Forwarding Prevention ---
bool RoutingTable::shouldForwardAnnounce(const uint8_t packet_hash[32]) {
    if (!packet_hash) return false;
    pruneRecentAnnounces();
    RecentAnnounceKey key;
    memcpy(key.packet_hash.data(), packet_hash, key.packet_hash.size());
    return _recentAnnounces.find(key) == _recentAnnounces.end();
}

void RoutingTable::markAnnounceForwarded(const uint8_t packet_hash[32]) {
    if (!packet_hash) return;
    RecentAnnounceKey key;
    memcpy(key.packet_hash.data(), packet_hash, key.packet_hash.size());
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

    // A burst of fresh, valid announces must not grow this cache without
    // bound. Expiry alone cannot enforce the configured memory ceiling.
    while (_recentAnnounces.size() > MAX_RECENT_ANNOUNCES) {
        auto oldest = _recentAnnounces.begin();
        for (auto it = std::next(_recentAnnounces.begin()); it != _recentAnnounces.end(); ++it) {
            if ((now - it->second) > (now - oldest->second)) oldest = it;
        }
        _recentAnnounces.erase(oldest);
    }
    _last_recent_announce_prune = now;
}
