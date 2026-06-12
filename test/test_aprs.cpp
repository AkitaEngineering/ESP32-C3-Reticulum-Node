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

// Test functions are invoked from a central test runner.
