#include <Arduino.h>
#include <unity.h>
#include <cstring>

#include "RoutingTable.h"

static RnsPacketInfo makeAnnounce(const uint8_t dest[16], uint8_t hops) {
    RnsPacketInfo info;
    info.valid = true;
    info.packet_type = RNS_PACKET_ANNOUNCE;
    info.destination_type = RNS_DEST_SINGLE;
    info.hops = hops;
    memcpy(info.destination_hash, dest, 16);
    return info;
}

void test_route_failover_prefers_hops_then_usable_interface(void) {
    RoutingTable table;
    uint8_t dest[16] = {0};
    dest[0] = 0x42;
    uint8_t macSlow[6] = {1, 2, 3, 4, 5, 6};
    uint8_t macFast[6] = {9, 8, 7, 6, 5, 4};

    table.update(makeAnnounce(dest, 4), InterfaceType::ESP_NOW, macSlow, IPAddress(), 0, nullptr);
    table.update(makeAnnounce(dest, 1), InterfaceType::WIFI_UDP, nullptr, IPAddress(10, 0, 0, 2), 4242, nullptr);
    TEST_ASSERT_EQUAL_UINT(1, table.getRouteCount());
    TEST_ASSERT_EQUAL_UINT(2, table.getRouteCandidateCount());

    RouteEntry *best = table.findRoute(dest);
    TEST_ASSERT_NOT_NULL(best);
    TEST_ASSERT_EQUAL_UINT8(1, best->hops);
    TEST_ASSERT_TRUE(best->interface == InterfaceType::WIFI_UDP);

    RouteEntry *failover = table.findRoute(dest, InterfaceType::WIFI_UDP);
    TEST_ASSERT_NOT_NULL(failover);
    TEST_ASSERT_EQUAL_UINT8(4, failover->hops);
    TEST_ASSERT_TRUE(failover->interface == InterfaceType::ESP_NOW);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(macSlow, failover->next_hop_mac, 6);

    RouteEntry *usableOnly = table.findRoute(dest, InterfaceType::UNKNOWN, [](InterfaceType ifType) {
        return ifType == InterfaceType::ESP_NOW;
    });
    TEST_ASSERT_NOT_NULL(usableOnly);
    TEST_ASSERT_TRUE(usableOnly->interface == InterfaceType::ESP_NOW);
}

void test_routing_table_holds_many_destinations(void) {
    RoutingTable table;
    uint8_t dest[16] = {0};
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
    const size_t destinations = 32;

    for (size_t node = 1; node <= destinations; ++node) {
        dest[0] = static_cast<uint8_t>(node);
        dest[1] = 0xF1;
        mac[5] = static_cast<uint8_t>(node);
        table.update(makeAnnounce(dest, 1), InterfaceType::ESP_NOW, mac, IPAddress(), 0, nullptr);
    }

    TEST_ASSERT_EQUAL_UINT32(destinations, table.getRouteCount());
    TEST_ASSERT_EQUAL_UINT32(destinations, table.getRouteCandidateCount());

    dest[0] = 1;
    dest[1] = 0xF1;
    RouteEntry *first = table.findRoute(dest);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_UINT8(1, first->destination_addr[0]);

    dest[0] = static_cast<uint8_t>(destinations);
    RouteEntry *last = table.findRoute(dest);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(destinations), last->destination_addr[0]);
}

void test_recent_announce_cache_is_strictly_bounded(void) {
    RoutingTable table;
    for (size_t index = 0; index < MAX_RECENT_ANNOUNCES + 25; ++index) {
        uint8_t hash[32] = {0};
        hash[0] = static_cast<uint8_t>(index);
        hash[1] = static_cast<uint8_t>(index >> 8);
        hash[2] = 0xA5;
        table.markAnnounceForwarded(hash);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(MAX_RECENT_ANNOUNCES, table.getRecentAnnounceCount());
    }

    TEST_ASSERT_EQUAL_UINT32(MAX_RECENT_ANNOUNCES, table.getRecentAnnounceCount());
}
