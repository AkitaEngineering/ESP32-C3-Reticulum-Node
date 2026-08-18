#include "Utils.h"
#include "Config.h" // For RNS_ADDRESS_SIZE
#include <cstring> // For memcmp

namespace Utils {

void printBytes(const uint8_t* buffer, size_t len, Stream& output) {
    if (!buffer) {
        output.print("[NULL]");
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] < 0x10) output.print("0");
        output.print(buffer[i], HEX);
    }
}

bool compareAddresses(const uint8_t* addr1, const uint8_t* addr2, size_t len) {
    if (!addr1 || !addr2) return false;
    return memcmp(addr1, addr2, len) == 0;
}

bool isAllZeros(const uint8_t* buf, size_t len) {
    return isAllBytes(buf, len, 0);
}

bool isAllBytes(const uint8_t* buf, size_t len, uint8_t value) {
    if (!buf) return false;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != value) return false;
    }
    return true;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    if (!data && len != 0) return crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

} // namespace Utils
