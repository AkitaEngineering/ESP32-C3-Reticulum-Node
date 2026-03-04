#include <Arduino.h>
#include <unity.h>
#include <monocypher.h>

void test_ed25519_sign_verify(void) {
    uint8_t seed[32] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f };

    uint8_t secret_key[64];
    uint8_t pk[32];
    // Derive the eddsa key pair from a 32-byte seed
    crypto_eddsa_key_pair(secret_key, pk, seed);

    const uint8_t msg[] = "unit test message";
    uint8_t sig[64];

    // Sign the message with the 64-byte secret key
    crypto_eddsa_sign(sig, secret_key, msg, sizeof(msg)-1);

    // Verify returns 0 on success
    int ok = crypto_eddsa_check(sig, pk, msg, sizeof(msg)-1);
    TEST_ASSERT_EQUAL_INT(0, ok);
}

// Test functions are invoked from a central test runner.
