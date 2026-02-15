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

// Test functions are invoked from a central test runner.
