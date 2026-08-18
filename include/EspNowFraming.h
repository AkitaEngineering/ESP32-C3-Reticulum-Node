#ifndef ESPNOW_FRAMING_H
#define ESPNOW_FRAMING_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <array>
#include <vector>

#include "Config.h"

namespace EspNowFraming {

constexpr uint8_t MAGIC0 = 0x52; // 'R'
constexpr uint8_t MAGIC1 = 0x4E; // 'N'
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_LEN = 12;
constexpr size_t MAX_PAYLOAD_LEN = 250;
constexpr size_t CHUNK_DATA_LEN = MAX_PAYLOAD_LEN - HEADER_LEN;
constexpr uint8_t MAX_FRAGMENT_CHUNKS =
    static_cast<uint8_t>((RNS_MTU + CHUNK_DATA_LEN - 1) / CHUNK_DATA_LEN);

inline uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

inline bool parseFragment(const uint8_t *data, int len,
                          uint16_t &messageId, uint8_t &chunkIndex, uint8_t &totalChunks,
                          bool &hasCrc, uint32_t &expectedCrc,
                          const uint8_t *&chunkData, size_t &chunkLen) {
    if (!data || len < static_cast<int>(HEADER_LEN)) return false;
    if (data[0] != MAGIC0 || data[1] != MAGIC1 || data[2] != VERSION) return false;
    if ((data[3] & 0xFE) != 0) return false;

    hasCrc = (data[3] & 0x01) != 0;
    messageId = (static_cast<uint16_t>(data[4]) << 8) | static_cast<uint16_t>(data[5]);
    chunkIndex = data[6];
    totalChunks = data[7];
    if (totalChunks == 0 || totalChunks > MAX_FRAGMENT_CHUNKS || chunkIndex >= totalChunks) {
        return false;
    }

    expectedCrc = (static_cast<uint32_t>(data[8]) << 24)
                | (static_cast<uint32_t>(data[9]) << 16)
                | (static_cast<uint32_t>(data[10]) << 8)
                | static_cast<uint32_t>(data[11]);
    chunkData = data + HEADER_LEN;
    chunkLen = static_cast<size_t>(len) - HEADER_LEN;
    if (chunkLen == 0 || chunkLen > CHUNK_DATA_LEN) return false;
    if (chunkIndex + 1 < totalChunks && chunkLen != CHUNK_DATA_LEN) return false;
    return true;
}

inline size_t packFragment(uint8_t *out, size_t outLen,
                           uint16_t messageId, uint8_t chunkIndex, uint8_t totalChunks,
                           uint32_t fullCrc, bool withCrc,
                           const uint8_t *chunkData, size_t chunkLen) {
    if (!out || !chunkData || chunkLen == 0 || chunkLen > CHUNK_DATA_LEN) return 0;
    if (outLen < HEADER_LEN + chunkLen) return 0;
    if (totalChunks == 0 || totalChunks > MAX_FRAGMENT_CHUNKS || chunkIndex >= totalChunks) return 0;

    out[0] = MAGIC0;
    out[1] = MAGIC1;
    out[2] = VERSION;
    out[3] = withCrc ? 0x01 : 0x00;
    out[4] = static_cast<uint8_t>((messageId >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>(messageId & 0xFF);
    out[6] = chunkIndex;
    out[7] = totalChunks;
    out[8] = static_cast<uint8_t>((fullCrc >> 24) & 0xFF);
    out[9] = static_cast<uint8_t>((fullCrc >> 16) & 0xFF);
    out[10] = static_cast<uint8_t>((fullCrc >> 8) & 0xFF);
    out[11] = static_cast<uint8_t>(fullCrc & 0xFF);
    memcpy(out + HEADER_LEN, chunkData, chunkLen);
    return HEADER_LEN + chunkLen;
}

struct StoreItem {
    std::vector<uint8_t> payload;
    std::array<uint8_t, 16> destination = {0};
    bool hasDestination = false;
    uint8_t attempts = 0;
    unsigned long nextTryMs = 0;
};

class StoreQueue {
public:
    explicit StoreQueue(size_t maxSize = ESPNOW_SF_QUEUE_SIZE) : _maxSize(maxSize) {}

    bool enqueue(const uint8_t *packetBuffer, size_t packetLen,
                 const uint8_t *destinationAddr, unsigned long nowMs,
                 unsigned long retryMs) {
        if (!packetBuffer || packetLen == 0 || packetLen > RNS_MTU) return false;
        if (_items.size() >= _maxSize) {
            _items.pop_front();
            _droppedOldest = true;
        } else {
            _droppedOldest = false;
        }

        StoreItem item;
        item.payload.assign(packetBuffer, packetBuffer + packetLen);
        item.hasDestination = destinationAddr != nullptr;
        if (item.hasDestination) {
            memcpy(item.destination.data(), destinationAddr, item.destination.size());
        }
        item.attempts = 0;
        item.nextTryMs = nowMs + retryMs;
        _items.push_back(std::move(item));
        return true;
    }

    bool empty() const { return _items.empty(); }
    size_t size() const { return _items.size(); }
    bool droppedOldest() const { return _droppedOldest; }
    void clear() { _items.clear(); }

    StoreItem *frontReady(unsigned long nowMs) {
        if (_items.empty()) return nullptr;
        StoreItem &item = _items.front();
        if (static_cast<long>(nowMs - item.nextTryMs) < 0) return nullptr;
        return &item;
    }

    void popFront() {
        if (!_items.empty()) _items.pop_front();
    }

private:
    std::deque<StoreItem> _items;
    size_t _maxSize;
    bool _droppedOldest = false;
};

} // namespace EspNowFraming

#endif
