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
    if (!buf) return false;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] != 0) return false;
    }
    return true;
}

} // namespace Utils
