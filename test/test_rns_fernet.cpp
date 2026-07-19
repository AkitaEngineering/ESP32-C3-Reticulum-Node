#include <Arduino.h>
#include <unity.h>

#include "RNSFernet.h"

void test_rns_token_roundtrip_and_authentication(void) {
    uint8_t key[64];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i);
    const uint8_t plaintext[] = {'a', 'u', 't', 'h', 'e', 'n', 't', 'i', 'c', 'a', 't', 'e', 'd'};

    RNSToken token;
    TEST_ASSERT_TRUE(token.init(key));
    std::vector<uint8_t> encrypted = token.encrypt(plaintext, sizeof(plaintext));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(64, encrypted.size());
    std::vector<uint8_t> decrypted = token.decrypt(encrypted.data(), encrypted.size());
    TEST_ASSERT_EQUAL_UINT(sizeof(plaintext), decrypted.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, decrypted.data(), sizeof(plaintext));

    encrypted[20] ^= 0x80;
    TEST_ASSERT_TRUE(token.decrypt(encrypted.data(), encrypted.size()).empty());
    TEST_ASSERT_TRUE(token.decrypt(encrypted.data(), encrypted.size() - 1).empty());

    std::vector<uint8_t> emptyEncrypted = token.encrypt(nullptr, 0);
    std::vector<uint8_t> emptyPlaintext = {'x'};
    TEST_ASSERT_FALSE(emptyEncrypted.empty());
    TEST_ASSERT_TRUE(token.decrypt(emptyEncrypted.data(), emptyEncrypted.size(), emptyPlaintext));
    TEST_ASSERT_TRUE(emptyPlaintext.empty());
}
