#include <Arduino.h>
#include <unity.h>
#include <cstring>
#include <EEPROM.h>

#include "RNSCrypto.h"
#include "Utils.h"

void test_identity_persists_and_rejects_corrupt_crc(void) {
    TEST_ASSERT_TRUE(EEPROM.begin(EEPROM_SIZE_WITH_IDENTITY));
    for (int i = 0; i < EEPROM_SIZE_WITH_IDENTITY; ++i) EEPROM.write(i, 0xFF);
    TEST_ASSERT_TRUE(EEPROM.commit());

    RNSCrypto first;
    TEST_ASSERT_TRUE(first.begin());
    TEST_ASSERT_TRUE(first.isReady());
    uint8_t hash1[16];
    memcpy(hash1, first.getIdentityHash(), 16);

    RNSCrypto reload;
    TEST_ASSERT_TRUE(reload.begin());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(hash1, reload.getIdentityHash(), 16);

    uint8_t crcByte = EEPROM.read(EEPROM_ADDR_IDENTITY_CRC);
    EEPROM.write(EEPROM_ADDR_IDENTITY_CRC, static_cast<uint8_t>(crcByte ^ 0xFF));
    TEST_ASSERT_TRUE(EEPROM.commit());

    RNSCrypto recovered;
    TEST_ASSERT_TRUE(recovered.begin());
    TEST_ASSERT_TRUE(recovered.isReady());
    TEST_ASSERT_TRUE(memcmp(hash1, recovered.getIdentityHash(), 16) != 0);
}

void test_identity_clone_mac_is_rejected(void) {
    TEST_ASSERT_TRUE(EEPROM.begin(EEPROM_SIZE_WITH_IDENTITY));
    for (int i = 0; i < EEPROM_SIZE_WITH_IDENTITY; ++i) EEPROM.write(i, 0xFF);
    TEST_ASSERT_TRUE(EEPROM.commit());

    RNSCrypto original;
    TEST_ASSERT_TRUE(original.begin());
    uint8_t hash1[16];
    memcpy(hash1, original.getIdentityHash(), 16);

    uint8_t mac[6];
    for (int i = 0; i < 6; ++i) mac[i] = EEPROM.read(EEPROM_ADDR_IDENTITY_MAC + i);
    mac[0] ^= 0x01;

    uint8_t payload[4 + 32 + 32 + 6] = {0};
    uint32_t magic = IDENTITY_MAGIC_V2;
    payload[0] = static_cast<uint8_t>(magic);
    payload[1] = static_cast<uint8_t>(magic >> 8);
    payload[2] = static_cast<uint8_t>(magic >> 16);
    payload[3] = static_cast<uint8_t>(magic >> 24);
    for (int i = 0; i < 32; ++i) {
        payload[4 + i] = EEPROM.read(EEPROM_ADDR_X25519_PRIV + i);
        payload[36 + i] = EEPROM.read(EEPROM_ADDR_ED25519_SEED + i);
    }
    memcpy(payload + 68, mac, 6);
    const uint32_t crc = Utils::crc32(payload, sizeof(payload));
    for (int i = 0; i < 6; ++i) EEPROM.write(EEPROM_ADDR_IDENTITY_MAC + i, mac[i]);
    for (int i = 0; i < 4; ++i) EEPROM.write(EEPROM_ADDR_IDENTITY_CRC + i, (crc >> (i * 8)) & 0xFF);
    TEST_ASSERT_TRUE(EEPROM.commit());

    RNSCrypto cloned;
    TEST_ASSERT_TRUE(cloned.begin());
    TEST_ASSERT_TRUE(memcmp(hash1, cloned.getIdentityHash(), 16) != 0);
}
