#include <Arduino.h>
#include <unity.h>

#include "Link.h"

void test_link_signalling_roundtrip(void) {
    uint8_t signalling[3] = {0};
    RNSLink::buildSignallingBytes(signalling, RNS_MTU, RNS_LINK_MODE_AES256_CBC);
    TEST_ASSERT_EQUAL_UINT32(RNS_MTU, RNSLink::mtuFromSignalling(signalling));
    TEST_ASSERT_EQUAL_UINT8(RNS_LINK_MODE_AES256_CBC, RNSLink::modeFromSignalling(signalling));

    RNSLink::buildSignallingBytes(signalling, 218, 7);
    TEST_ASSERT_EQUAL_UINT32(218, RNSLink::mtuFromSignalling(signalling));
    TEST_ASSERT_EQUAL_UINT8(7, RNSLink::modeFromSignalling(signalling));
}
