#include <Arduino.h>
#include <unity.h>
#include <cstring>

#include "Link.h"
#include "RNSCrypto.h"
#include <optional/monocypher-ed25519.h>

void test_link_signalling_roundtrip(void) {
    uint8_t signalling[3] = {0};
    RNSLink::buildSignallingBytes(signalling, RNS_MTU, RNS_LINK_MODE_AES256_CBC);
    TEST_ASSERT_EQUAL_UINT32(RNS_MTU, RNSLink::mtuFromSignalling(signalling));
    TEST_ASSERT_EQUAL_UINT8(RNS_LINK_MODE_AES256_CBC, RNSLink::modeFromSignalling(signalling));

    RNSLink::buildSignallingBytes(signalling, 218, 7);
    TEST_ASSERT_EQUAL_UINT32(218, RNSLink::mtuFromSignalling(signalling));
    TEST_ASSERT_EQUAL_UINT8(7, RNSLink::modeFromSignalling(signalling));
}

void test_link_rtt_payload_roundtrip(void) {
    uint8_t payload[5] = {0};
    TEST_ASSERT_TRUE(RNSLink::encodeRttPayload(0.125f, payload));
    float decoded = -1.0f;
    TEST_ASSERT_TRUE(RNSLink::decodeRttPayload(payload, sizeof(payload), decoded));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.125f, decoded);

    TEST_ASSERT_FALSE(RNSLink::encodeRttPayload(-1.0f, payload));
    TEST_ASSERT_FALSE(RNSLink::decodeRttPayload(payload, 4, decoded));
}

void test_link_control_payloads_are_fernet_tokens(void) {
    uint8_t key[64];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i + 3);
    RNSToken token;
    TEST_ASSERT_TRUE(token.init(key));

    uint8_t rtt[5];
    TEST_ASSERT_TRUE(RNSLink::encodeRttPayload(0.05f, rtt));
    auto encryptedRtt = token.encrypt(rtt, sizeof(rtt));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(64, encryptedRtt.size());

    std::vector<uint8_t> decrypted;
    TEST_ASSERT_TRUE(token.decrypt(encryptedRtt.data(), encryptedRtt.size(), decrypted));
    float decoded = 0;
    TEST_ASSERT_TRUE(RNSLink::decodeRttPayload(decrypted.data(), decrypted.size(), decoded));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.05f, decoded);

    const uint8_t keepalive = 0xFF;
    auto encryptedKeepalive = token.encrypt(&keepalive, 1);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(64, encryptedKeepalive.size());
    TEST_ASSERT_TRUE(token.decrypt(encryptedKeepalive.data(), encryptedKeepalive.size(), decrypted));
    TEST_ASSERT_EQUAL_UINT(1, decrypted.size());
    TEST_ASSERT_EQUAL_UINT8(0xFF, decrypted[0]);
}

void test_link_forged_proof_is_rejected(void) {
    uint8_t linkId[16];
    uint8_t expectedSigPub[32];
    uint8_t peerX[32];
    uint8_t signalling[3];
    bool hasSignalling = false;
    for (size_t i = 0; i < sizeof(linkId); ++i) linkId[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < sizeof(expectedSigPub); ++i) expectedSigPub[i] = static_cast<uint8_t>(0xA0 + i);

    uint8_t forged[RNS_LINK_PROOF_SIZE];
    memset(forged, 0x5A, sizeof(forged));
    TEST_ASSERT_FALSE(RNSLink::verifyProofSignature(
        forged, sizeof(forged), linkId, expectedSigPub, peerX, signalling, hasSignalling));

    uint8_t seed[32];
    uint8_t secret[64];
    uint8_t pub[32];
    for (size_t i = 0; i < sizeof(seed); ++i) seed[i] = static_cast<uint8_t>(i + 1);
    crypto_ed25519_key_pair(secret, pub, seed);

    uint8_t signedData[16 + 32 + 32 + 3];
    memcpy(signedData, linkId, 16);
    memset(signedData + 16, 0x11, 32);
    memcpy(signedData + 48, pub, 32);
    memset(signedData + 80, 0x22, 3);
    uint8_t validProof[RNS_LINK_PROOF_SIZE];
    crypto_ed25519_sign(validProof, secret, signedData, sizeof(signedData));
    memcpy(validProof + 64, signedData + 16, 32);
    memcpy(validProof + 96, signedData + 80, 3);

    TEST_ASSERT_TRUE(RNSLink::verifyProofSignature(
        validProof, sizeof(validProof), linkId, pub, peerX, signalling, hasSignalling));
    TEST_ASSERT_TRUE(hasSignalling);

    validProof[0] ^= 0x01;
    TEST_ASSERT_FALSE(RNSLink::verifyProofSignature(
        validProof, sizeof(validProof), linkId, pub, peerX, signalling, hasSignalling));
}
