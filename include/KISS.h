#ifndef KISS_H
#define KISS_H

#include <vector>
#include <cstdint>
#include <functional> // For std::function
#include "Config.h"   // For InterfaceType

// KISS Framing constants
const uint8_t KISS_FEND = 0xC0;
const uint8_t KISS_FESC = 0xDB;
const uint8_t KISS_TFEND = 0xDC;
const uint8_t KISS_TFESC = 0xDD;

class KISSProcessor {
public:
    // Callback type: void packet_handler(const std::vector<uint8_t>& packetData, InterfaceType interface)
    using PacketHandler = std::function<void(const std::vector<uint8_t>&, InterfaceType)>;

    // Constructor takes the callback to call when a full packet is decoded
    KISSProcessor(PacketHandler handler);

    // Processes a single incoming byte
    void decodeByte(uint8_t byte, InterfaceType interface);

    // Encodes a raw packet into a KISS framed packet (static method)
    static void encode(const uint8_t *input, size_t len, std::vector<uint8_t> &output);

private:
    std::vector<uint8_t> _receiveBuffer;
    bool _inEscapeState = false;
    bool _expectingCommand = true;  // After FEND, expect command byte next
    PacketHandler _packetHandler; // Stores the callback function
};

#endif // KISS_H
