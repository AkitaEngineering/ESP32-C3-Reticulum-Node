#include <Arduino.h>
#include <unity.h>

#include "RoutingTable.h"

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
