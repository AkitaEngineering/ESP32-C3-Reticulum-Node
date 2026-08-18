#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h> // For Stream
#include <cstddef>   // For size_t
#include <cstdint>   // For uint8_t
#include "Config.h"  // For RNS_ADDRESS_SIZE

namespace Utils {
    // Prints byte buffer as hex
    void printBytes(const uint8_t* buffer, size_t len, Stream& output);
    // Compares two RNS addresses
    bool compareAddresses(const uint8_t* addr1, const uint8_t* addr2, size_t len = RNS_ADDRESS_SIZE);
    // Returns true if all bytes in the buffer are zero
    bool isAllZeros(const uint8_t* buf, size_t len);
    // Returns true if every byte equals the given value
    bool isAllBytes(const uint8_t* buf, size_t len, uint8_t value);
    // IEEE CRC-32 (same polynomial as ESP-NOW fragment frames)
    uint32_t crc32(const uint8_t* data, size_t len);
}

#endif // UTILS_H
