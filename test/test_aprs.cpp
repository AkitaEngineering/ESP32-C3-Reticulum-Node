#include <Arduino.h>
#include <unity.h>
#include "APRS.h"

void test_aprs_uncompressed_position_roundtrip(void) {
    APRS::Position pos;
    pos.latitude = 45.5017f;
    pos.longitude = -73.5673f;
    pos.symbolTable = '/';
    pos.symbol = '>';
    pos.course = 270;
    pos.speed = 12;

    char encoded[64] = {0};
    APRS::formatPosition(pos, encoded, false);

    APRS::Position parsed;
    TEST_ASSERT_TRUE(APRS::parsePosition(encoded, parsed));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos.latitude, parsed.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos.longitude, parsed.longitude);
    TEST_ASSERT_EQUAL_UINT8(pos.symbolTable, parsed.symbolTable);
    TEST_ASSERT_EQUAL_UINT8(pos.symbol, parsed.symbol);
    TEST_ASSERT_EQUAL_UINT16(pos.course, parsed.course);
    TEST_ASSERT_EQUAL_UINT16(pos.speed, parsed.speed);
}

void test_aprs_compressed_position_format(void) {
    APRS::Position pos;
    pos.latitude = 0.0f;
    pos.longitude = 0.0f;
    pos.symbolTable = '/';
    pos.symbol = '>';

    char encoded[32] = {0};
    APRS::formatPosition(pos, encoded, true);

    TEST_ASSERT_EQUAL_UINT8('!', encoded[0]);
    TEST_ASSERT_EQUAL_UINT8('/', encoded[1]);
    TEST_ASSERT_EQUAL_UINT8('>', encoded[10]);
    TEST_ASSERT_EQUAL_UINT8(13, strlen(encoded));
}

void test_aprs_rejects_malformed_positions(void) {
    APRS::Position parsed;
    TEST_ASSERT_FALSE(APRS::parsePosition("!4560.00N/07334.04W>", parsed));
    TEST_ASSERT_FALSE(APRS::parsePosition("!9000.01N/07334.04W>", parsed));
    TEST_ASSERT_FALSE(APRS::parsePosition("!4530.10X/07334.04W>", parsed));
    TEST_ASSERT_FALSE(APRS::parsePosition("!4530.10N/18100.00W>", parsed));
    TEST_ASSERT_FALSE(APRS::parsePosition("!4530.10N/07334.04W>361/001", parsed));
    TEST_ASSERT_FALSE(APRS::parsePosition("!4530.10N/07334.04W>12A/001", parsed));
}

void test_aprs_accepts_zero_coordinates(void) {
    APRS::Position parsed;
    TEST_ASSERT_TRUE(APRS::parsePosition("!0000.00N/00000.00E>", parsed));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, parsed.latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, parsed.longitude);
}

// Test functions are invoked from a central test runner.
