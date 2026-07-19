#include <Arduino.h>
#include <unity.h>

#include "AX25.h"

void test_ax25_frame_roundtrip(void) {
    AX25::Frame original;
    original.destination = AX25::Address("APZ001", 0);
    original.source = AX25::Address("VE3ABC", 7);
    original.digipeaters.push_back(AX25::Address("WIDE1", 1));
    original.digipeaters.push_back(AX25::Address("WIDE2", 2));
    original.digipeaters[0].hasBeenRepeated = true;
    original.info = {'!', '4', '5', '3', '0', '.', '0', '0', 'N'};

    std::vector<uint8_t> encoded;
    TEST_ASSERT_TRUE(AX25::encodeFrame(original, encoded));
    TEST_ASSERT_EQUAL_HEX8(0x7e, encoded.front());
    TEST_ASSERT_EQUAL_HEX8(0x7e, encoded.back());

    AX25::Frame decoded;
    TEST_ASSERT_TRUE(AX25::decodeFrame(encoded.data(), encoded.size(), decoded));
    TEST_ASSERT_EQUAL_STRING("APZ001", decoded.destination.callsign);
    TEST_ASSERT_EQUAL_STRING("VE3ABC", decoded.source.callsign);
    TEST_ASSERT_EQUAL_UINT8(7, decoded.source.ssid);
    TEST_ASSERT_EQUAL_UINT(2, decoded.digipeaters.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.info.data(), decoded.info.data(), original.info.size());

    // Decoders commonly receive frames with the HDLC flags already removed.
    TEST_ASSERT_TRUE(AX25::decodeFrame(encoded.data() + 1, encoded.size() - 2, decoded));
}
void test_ax25_six_character_callsign(void) {
    AX25::Address address("ABC123", 15);
    std::vector<uint8_t> encoded;
    AX25::encodeAddress(address, encoded, true);
    TEST_ASSERT_EQUAL_UINT(7, encoded.size());

    size_t offset = 0;
    bool moreAddresses = true;
    AX25::Address decoded;
    TEST_ASSERT_TRUE(AX25::decodeAddress(encoded.data(), encoded.size(), offset, decoded, moreAddresses));
    TEST_ASSERT_EQUAL_STRING("ABC123", decoded.callsign);
    TEST_ASSERT_EQUAL_UINT8(15, decoded.ssid);
    TEST_ASSERT_FALSE(moreAddresses);
}

void test_ax25_rejects_bad_fcs_and_truncation(void) {
    AX25::Frame frame;
    frame.destination = AX25::Address("APZ001");
    frame.source = AX25::Address("N0CALL");
    frame.info = {'t', 'e', 's', 't'};

    std::vector<uint8_t> encoded;
    TEST_ASSERT_TRUE(AX25::encodeFrame(frame, encoded));
    encoded[encoded.size() - 3] ^= 0x01;
    AX25::Frame decoded;
    TEST_ASSERT_FALSE(AX25::decodeFrame(encoded.data(), encoded.size(), decoded));
    TEST_ASSERT_FALSE(AX25::decodeFrame(encoded.data(), 16, decoded));

    size_t offset = 0;
    bool moreAddresses = false;
    AX25::Address address;
    TEST_ASSERT_FALSE(AX25::decodeAddress(encoded.data(), 6, offset, address, moreAddresses));
}
