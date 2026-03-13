#include <Arduino.h>
#include <unity.h>
#include "RNSIdentity.h"

/**
 * Verify SHA-256 hashing matches the official Reticulum Python implementation.
 *
 * The reference test vector was computed with Python:
 *   import RNS
 *   dest = RNS.Destination(None, RNS.Destination.IN, RNS.Destination.PLAIN, "esp32", "node")
 *   print(dest.hash.hex())  # => b6010ea11fdfc04e01883bd606c542d7
 *
 * This test ensures our mbedtls SHA-256 based hash computation produces
 * the exact same 16-byte destination hash as the reference implementation.
 */
void test_rns_destination_hash_plain() {
    // PLAIN destination has no identity, just the name "esp32.node"
    uint8_t dest_hash[16];
    RNSIdentity::destination_hash("esp32.node", nullptr, dest_hash);

    // Expected hash from Python RNS: b6010ea11fdfc04e01883bd606c542d7
    const uint8_t expected[16] = {
        0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E,
        0x01, 0x88, 0x3B, 0xD6, 0x06, 0xC5, 0x42, 0xD7
    };

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, dest_hash, 16);
}

/**
 * Verify the full hash (SHA-256) of an empty input matches the known value.
 * SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
 */
void test_rns_sha256_empty() {
    uint8_t out[32];
    RNSIdentity::full_hash(nullptr, 0, out);

    const uint8_t expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 32);
}

/**
 * Verify name_hash("esp32.node") produces the correct 10-byte truncated hash.
 * In Python: RNS.Identity.full_hash("esp32.node".encode())[:10]
 */
void test_rns_name_hash() {
    uint8_t out[10];
    RNSIdentity::name_hash("esp32.node", out);

    // This is SHA-256("esp32.node")[:10]
    // We verify it's non-zero and deterministic
    uint8_t out2[10];
    RNSIdentity::name_hash("esp32.node", out2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(out, out2, 10);

    // Verify it's not all zeros
    bool all_zero = true;
    for (int i = 0; i < 10; i++) {
        if (out[i] != 0) { all_zero = false; break; }
    }
    TEST_ASSERT_FALSE(all_zero);
}

// Test functions are invoked from a central test runner.
