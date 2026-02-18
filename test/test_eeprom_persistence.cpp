#include <Arduino.h>
#include <unity.h>
#include <EEPROM.h>

#include "ReticulumNode.h"
#include "Config.h"

// Ensure DISABLE_WIFI is active in test env to avoid network init side-effects

void test_eeprom_persistence(void) {
    // Clear EEPROM to simulate first boot
    TEST_ASSERT_TRUE(EEPROM.begin(EEPROM_SIZE));
    for (int i = 0; i < EEPROM_SIZE; ++i) EEPROM.write(i, 0xFF);
    TEST_ASSERT_TRUE(EEPROM.commit());

    // First instance: will generate address + packet counter and save to EEPROM
    ReticulumNode node1;
    node1.loadConfigFromEEPROM(); // load/generate and save address/packet counter

    uint8_t addr1[RNS_ADDRESS_SIZE];
    memcpy(addr1, node1.getNodeAddress(), RNS_ADDRESS_SIZE);
    uint16_t pkt1 = node1.getPacketCounter();

    // Persist explicitly and assert success
    node1.saveConfigNow();

    // New instance simulating restart
    ReticulumNode node2;
    node2.loadConfigFromEEPROM();

    uint8_t addr2[RNS_ADDRESS_SIZE];
    memcpy(addr2, node2.getNodeAddress(), RNS_ADDRESS_SIZE);
    uint16_t pkt2 = node2.getPacketCounter();

    // Addresses and packet counter must match after restart
    TEST_ASSERT_EQUAL_UINT8_ARRAY(addr1, addr2, RNS_ADDRESS_SIZE);
    TEST_ASSERT_EQUAL_UINT16(pkt1, pkt2);
}
