#include <Arduino.h>
#include <unity.h>
#include "ReticulumPacket.h"

void test_serialize_deserialize_roundtrip() {
    uint8_t dest_hash[16];
    for (int i = 0; i < 16; ++i) dest_hash[i] = (uint8_t)i;
    std::vector<uint8_t> data = {'H','e','l','l','o'};
    uint8_t buffer[512];
    size_t len = sizeof(buffer);
    bool ok = ReticulumPacket::serialize(buffer, len, dest_hash, RNS_PACKET_DATA, RNS_DEST_PLAIN, RNS_PROPAGATION_BROADCAST, RNS_CONTEXT_NONE, 0, data);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_GREATER_THAN(0, len);

    RnsPacketInfo info;
    bool parsed = ReticulumPacket::deserialize(buffer, len, info);
    TEST_ASSERT_TRUE(parsed);
    TEST_ASSERT_EQUAL_UINT8(RNS_PACKET_DATA, info.packet_type);
    TEST_ASSERT_EQUAL_UINT8(RNS_DEST_PLAIN, info.destination_type);
    TEST_ASSERT_EQUAL_UINT8(0, info.hops);
    TEST_ASSERT_EQUAL_UINT8(RNS_CONTEXT_NONE, info.context);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dest_hash, info.destination_hash, sizeof(dest_hash));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dest_hash, info.destination, RNS_ADDRESS_SIZE);
    TEST_ASSERT_EQUAL_UINT32(data.size(), info.data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(data[i], info.data[i]);
    }
}

// Test functions are invoked from a central test runner.
