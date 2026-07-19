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

void test_header2_roundtrip_and_hash_invariance() {
    uint8_t destination[16];
    uint8_t transport[16];
    for (size_t i = 0; i < 16; ++i) {
        destination[i] = static_cast<uint8_t>(0x10 + i);
        transport[i] = static_cast<uint8_t>(0x80 + i);
    }
    const std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    uint8_t header1[MAX_PACKET_SIZE] = {0};
    uint8_t header2[MAX_PACKET_SIZE] = {0};
    size_t header1Length = 0;
    size_t header2Length = 0;

    TEST_ASSERT_TRUE(ReticulumPacket::serialize(
        header1, header1Length, destination, RNS_PACKET_DATA, RNS_DEST_PLAIN,
        RNS_PROPAGATION_BROADCAST, RNS_CONTEXT_NONE, 0, data));
    TEST_ASSERT_TRUE(ReticulumPacket::serialize(
        header2, header2Length, destination, RNS_PACKET_DATA, RNS_DEST_PLAIN,
        RNS_PROPAGATION_TRANSPORT, RNS_CONTEXT_NONE, 7, data, 0, RNS_HEADER_2, transport));

    RnsPacketInfo first;
    RnsPacketInfo second;
    TEST_ASSERT_TRUE(ReticulumPacket::deserialize(header1, header1Length, first));
    TEST_ASSERT_TRUE(ReticulumPacket::deserialize(header2, header2Length, second));
    TEST_ASSERT_EQUAL_UINT8(RNS_HEADER_2, second.header_type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(transport, second.transport_id, sizeof(transport));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.packet_hash, second.packet_hash, sizeof(first.packet_hash));

    header1[1] = 42; // Hop count is deliberately excluded from the packet hash.
    RnsPacketInfo changedHops;
    TEST_ASSERT_TRUE(ReticulumPacket::deserialize(header1, header1Length, changedHops));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.packet_hash, changedHops.packet_hash, sizeof(first.packet_hash));
}

void test_packet_parser_rejects_ifac_and_oversize() {
    uint8_t destination[16] = {0};
    std::vector<uint8_t> data = {'x'};
    uint8_t buffer[MAX_PACKET_SIZE + 1] = {0};
    size_t length = 0;
    TEST_ASSERT_TRUE(ReticulumPacket::serialize(
        buffer, length, destination, RNS_PACKET_DATA, RNS_DEST_PLAIN,
        RNS_PROPAGATION_BROADCAST, RNS_CONTEXT_NONE, 0, data));
    buffer[0] |= 0x80;
    RnsPacketInfo info;
    TEST_ASSERT_FALSE(ReticulumPacket::deserialize(buffer, length, info));
    TEST_ASSERT_FALSE(ReticulumPacket::deserialize(buffer, MAX_PACKET_SIZE + 1, info));
}

// Test functions are invoked from a central test runner.
