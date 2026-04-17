#include <Arduino.h>
#include <unity.h>
#include "KISS.h"

void test_kiss_encode_escape() {
    const uint8_t input[] = {0x01, KISS_FEND, 0x02, KISS_FESC, 0x03};
    std::vector<uint8_t> out;
    KISSProcessor::encode(input, sizeof(input), out);

    TEST_ASSERT_GREATER_THAN(4, out.size());
    TEST_ASSERT_EQUAL_UINT8(KISS_FEND, out.front());
    TEST_ASSERT_EQUAL_UINT8(KISS_FEND, out.back());

    bool hasTFEND = false;
    bool hasTFESC = false;
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == KISS_FESC && out[i+1] == KISS_TFEND) hasTFEND = true;
        if (out[i] == KISS_FESC && out[i+1] == KISS_TFESC) hasTFESC = true;
    }

    TEST_ASSERT_TRUE(hasTFEND);
    TEST_ASSERT_TRUE(hasTFESC);
}

void test_kiss_ignores_non_data_commands() {
    int packets_seen = 0;
    std::vector<uint8_t> last_packet;

    KISSProcessor processor([&](const std::vector<uint8_t>& packetData, InterfaceType) {
        packets_seen++;
        last_packet = packetData;
    });

    const uint8_t config_frame[] = {KISS_FEND, 0x01, 0x41, 0x42, KISS_FEND};
    for (uint8_t byte : config_frame) {
        processor.decodeByte(byte, InterfaceType::SERIAL_PORT);
    }

    TEST_ASSERT_EQUAL_INT(0, packets_seen);

    const uint8_t data_frame[] = {KISS_FEND, 0x00, 0x11, 0x22, KISS_FEND};
    for (uint8_t byte : data_frame) {
        processor.decodeByte(byte, InterfaceType::SERIAL_PORT);
    }

    TEST_ASSERT_EQUAL_INT(1, packets_seen);
    TEST_ASSERT_EQUAL_UINT32(2, last_packet.size());
    TEST_ASSERT_EQUAL_HEX8(0x11, last_packet[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, last_packet[1]);
}

// Test functions are invoked from a central test runner.
