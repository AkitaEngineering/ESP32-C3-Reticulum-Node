#include <Arduino.h>
#include <unity.h>

#include "EspNowFraming.h"

void test_espnow_fragment_crc_roundtrip(void) {
    uint8_t payload[300];
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i * 3);
    const uint32_t crc = EspNowFraming::crc32(payload, sizeof(payload));

    uint8_t frame[EspNowFraming::MAX_PAYLOAD_LEN];
    const size_t firstLen = EspNowFraming::CHUNK_DATA_LEN;
    const size_t packed = EspNowFraming::packFragment(
        frame, sizeof(frame), 7, 0, 2, crc, true, payload, firstLen);
    TEST_ASSERT_EQUAL_UINT(EspNowFraming::HEADER_LEN + firstLen, packed);

    uint16_t messageId = 0;
    uint8_t chunkIndex = 0;
    uint8_t totalChunks = 0;
    bool hasCrc = false;
    uint32_t expectedCrc = 0;
    const uint8_t *chunk = nullptr;
    size_t chunkLen = 0;
    TEST_ASSERT_TRUE(EspNowFraming::parseFragment(
        frame, static_cast<int>(packed), messageId, chunkIndex, totalChunks,
        hasCrc, expectedCrc, chunk, chunkLen));
    TEST_ASSERT_EQUAL_UINT16(7, messageId);
    TEST_ASSERT_EQUAL_UINT8(0, chunkIndex);
    TEST_ASSERT_EQUAL_UINT8(2, totalChunks);
    TEST_ASSERT_TRUE(hasCrc);
    TEST_ASSERT_EQUAL_UINT32(crc, expectedCrc);
    TEST_ASSERT_EQUAL_UINT(firstLen, chunkLen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, chunk, firstLen);

    frame[8] ^= 0xFF;
    TEST_ASSERT_TRUE(EspNowFraming::parseFragment(
        frame, static_cast<int>(packed), messageId, chunkIndex, totalChunks,
        hasCrc, expectedCrc, chunk, chunkLen));
    TEST_ASSERT_NOT_EQUAL(crc, expectedCrc);
}

void test_espnow_store_forward_drops_oldest(void) {
    EspNowFraming::StoreQueue queue(2);
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {4, 5};
    const uint8_t third[] = {6, 7, 8, 9};
    TEST_ASSERT_TRUE(queue.enqueue(first, sizeof(first), nullptr, 1000, 300));
    TEST_ASSERT_TRUE(queue.enqueue(second, sizeof(second), nullptr, 1000, 300));
    TEST_ASSERT_FALSE(queue.droppedOldest());
    TEST_ASSERT_TRUE(queue.enqueue(third, sizeof(third), nullptr, 1000, 300));
    TEST_ASSERT_TRUE(queue.droppedOldest());
    TEST_ASSERT_EQUAL_UINT(2, queue.size());

    TEST_ASSERT_NULL(queue.frontReady(1200));
    EspNowFraming::StoreItem *ready = queue.frontReady(1400);
    TEST_ASSERT_NOT_NULL(ready);
    TEST_ASSERT_EQUAL_UINT(sizeof(second), ready->payload.size());
    TEST_ASSERT_EQUAL_UINT8(4, ready->payload[0]);
}
