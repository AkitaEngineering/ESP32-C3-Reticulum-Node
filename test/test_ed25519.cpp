#include <Arduino.h>
#include <unity.h>
#include <monocypher.h>
#include <optional/monocypher-ed25519.h>
#include "RNSCrypto.h"

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

void test_announce_validation_rejects_tampering(void) {
    uint8_t xPrivate[32] = {1};
    uint8_t xPublic[32] = {0};
    crypto_x25519_public_key(xPublic, xPrivate);
    uint8_t seed[32] = {2};
    uint8_t secret[64] = {0};
    uint8_t signingPublic[32] = {0};
    crypto_ed25519_key_pair(secret, signingPublic, seed);

    uint8_t publicKey[64] = {0};
    memcpy(publicKey, xPublic, 32);
    memcpy(publicKey + 32, signingPublic, 32);
    uint8_t identityHash[16] = {0};
    uint8_t nameHash[10] = {0};
    uint8_t destination[16] = {0};
    RNSIdentity::identity_hash(publicKey, identityHash);
    RNSIdentity::name_hash("esp32.node", nameHash);
    RNSIdentity::destination_hash_from_name_hash(nameHash, identityHash, destination);

    uint8_t randomHash[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint8_t> signedData;
    signedData.insert(signedData.end(), destination, destination + 16);
    signedData.insert(signedData.end(), publicKey, publicKey + 64);
    signedData.insert(signedData.end(), nameHash, nameHash + 10);
    signedData.insert(signedData.end(), randomHash, randomHash + 10);
    const uint8_t appData[] = {'o', 'k'};
    signedData.insert(signedData.end(), appData, appData + sizeof(appData));
    uint8_t signature[64] = {0};
    crypto_ed25519_sign(signature, secret, signedData.data(), signedData.size());

    std::vector<uint8_t> payload;
    payload.insert(payload.end(), publicKey, publicKey + 64);
    payload.insert(payload.end(), nameHash, nameHash + 10);
    payload.insert(payload.end(), randomHash, randomHash + 10);
    payload.insert(payload.end(), signature, signature + 64);
    payload.insert(payload.end(), appData, appData + sizeof(appData));
    TEST_ASSERT_TRUE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size()));
    payload.back() ^= 0x01;
    TEST_ASSERT_FALSE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size()));
    payload.back() ^= 0x01;
    destination[0] ^= 0x01;
    TEST_ASSERT_FALSE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size()));
    destination[0] ^= 0x01;

    // Context-flagged announces insert a 32-byte ratchet before the signature.
    uint8_t ratchet[32] = {0};
    for (size_t i = 0; i < sizeof(ratchet); ++i) ratchet[i] = static_cast<uint8_t>(i + 1);
    signedData.clear();
    signedData.insert(signedData.end(), destination, destination + 16);
    signedData.insert(signedData.end(), publicKey, publicKey + 64);
    signedData.insert(signedData.end(), nameHash, nameHash + 10);
    signedData.insert(signedData.end(), randomHash, randomHash + 10);
    signedData.insert(signedData.end(), ratchet, ratchet + sizeof(ratchet));
    signedData.insert(signedData.end(), appData, appData + sizeof(appData));
    crypto_ed25519_sign(signature, secret, signedData.data(), signedData.size());

    payload.clear();
    payload.insert(payload.end(), publicKey, publicKey + 64);
    payload.insert(payload.end(), nameHash, nameHash + 10);
    payload.insert(payload.end(), randomHash, randomHash + 10);
    payload.insert(payload.end(), ratchet, ratchet + sizeof(ratchet));
    payload.insert(payload.end(), signature, signature + 64);
    payload.insert(payload.end(), appData, appData + sizeof(appData));
    TEST_ASSERT_TRUE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size(), true));
    TEST_ASSERT_FALSE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size(), false));
    payload[84] ^= 0x01;
    TEST_ASSERT_FALSE(RNSCrypto::validateAnnouncePayload(destination, payload.data(), payload.size(), true));
}

// Test functions are invoked from a central test runner.
