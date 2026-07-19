#include "AX25.h"
#include <Arduino.h>

constexpr uint16_t AX25::FCS_POLYNOMIAL;
constexpr uint8_t AX25::FLAG;

// AX.25 Protocol Implementation

void AX25::callsignToAX25(const char* callsign, uint8_t* output) {
    if (!output) return;
    memset(output, 0x20 << 1, 6);  // Fill with spaces, shifted left
    if (!callsign) return;
    size_t len = strnlen(callsign, 6);
    for (size_t i = 0; i < len; i++) {
        output[i] = (callsign[i] << 1) & 0xFE;
    }
}

void AX25::ax25ToCallsign(const uint8_t* ax25, char* output) {
    if (!ax25 || !output) return;
    size_t lastNonSpace = 0;
    for (int i = 0; i < 6; i++) {
        output[i] = (ax25[i] >> 1) & 0x7F;
        if (output[i] != ' ') lastNonSpace = static_cast<size_t>(i + 1);
    }
    output[lastNonSpace] = '\0';
}

void AX25::encodeAddress(const Address& addr, std::vector<uint8_t>& output, bool isLast) {
    uint8_t ax25Addr[7];
    callsignToAX25(addr.callsign, ax25Addr);
    
    // Set SSID and control bits
    uint8_t ssidByte = 0x60 | ((addr.ssid << 1) & 0x1E); // AX.25 reserved bits set
    if (addr.command || addr.hasBeenRepeated) ssidByte |= 0x80;
    if (isLast) ssidByte |= 0x01;  // Address extension bit
    
    for (int i = 0; i < 6; i++) {
        output.push_back(ax25Addr[i]);
    }
    output.push_back(ssidByte);
}

bool AX25::decodeAddress(const uint8_t* data, size_t len, size_t& offset,
                         Address& addr, bool& moreAddresses) {
    if (!data || offset > len || len - offset < 7) return false;
    
    uint8_t ax25Addr[6];
    memcpy(ax25Addr, data + offset, 6);
    ax25ToCallsign(ax25Addr, addr.callsign);
    
    uint8_t ssidByte = data[offset + 6];
    addr.ssid = (ssidByte >> 1) & 0x0F;
    addr.command = (ssidByte & 0x80) != 0;
    addr.hasBeenRepeated = (ssidByte & 0x80) != 0;
    
    offset += 7;
    moreAddresses = (ssidByte & 0x01) == 0;
    return true;
}

uint16_t AX25::calculateFCS(const uint8_t* data, size_t len) {
    uint16_t fcs = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        fcs ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (fcs & 0x0001) {
                fcs = (fcs >> 1) ^ FCS_POLYNOMIAL;
            } else {
                fcs >>= 1;
            }
        }
    }
    
    return fcs ^ 0xFFFF;
}

bool AX25::verifyFCS(const uint8_t* data, size_t len, uint16_t receivedFCS) {
    uint16_t calculatedFCS = calculateFCS(data, len);
    return calculatedFCS == receivedFCS;
}

bool AX25::encodeFrame(const Frame& frame, std::vector<uint8_t>& output) {
    output.clear();
    if (frame.digipeaters.size() > 8) return false;
    output.push_back(FLAG);  // Opening flag
    
    // Encode destination
    encodeAddress(frame.destination, output, false);
    
    // Encode source
    bool isLast = frame.digipeaters.empty();
    encodeAddress(frame.source, output, isLast);
    
    // Encode digipeaters
    for (size_t i = 0; i < frame.digipeaters.size(); i++) {
        bool last = (i == frame.digipeaters.size() - 1);
        encodeAddress(frame.digipeaters[i], output, last);
    }
    
    // Control field
    output.push_back(static_cast<uint8_t>(frame.control));
    
    // PID field (for I and UI frames)
    if (frame.control == ControlType::I_FRAME || frame.control == ControlType::U_UI) {
        output.push_back(frame.pid);
    }
    
    // Information field
    if (!frame.info.empty()) {
        for (uint8_t byte : frame.info) {
            output.push_back(byte);
        }
    }

    // AX.25 v2.x frames are limited to 330 bytes between flags, including FCS.
    if (output.size() - 1 + 2 > 330) {
        output.clear();
        return false;
    }
    
    // Calculate and append FCS
    uint16_t fcs = calculateFCS(output.data() + 1, output.size() - 1);
    output.push_back(fcs & 0xFF);
    output.push_back((fcs >> 8) & 0xFF);
    
    output.push_back(FLAG);  // Closing flag
    
    return true;
}

bool AX25::decodeFrame(const uint8_t* data, size_t len, Frame& frame) {
    if (!data || len < 17 || len > 332) return false;

    const size_t frameStart = data[0] == FLAG ? 1 : 0;
    size_t frameEnd = len;
    if (frameEnd > frameStart && data[frameEnd - 1] == FLAG) --frameEnd;
    const size_t bodyLength = frameEnd > frameStart ? frameEnd - frameStart : 0;
    if (bodyLength < 17 || bodyLength > 330) return false;

    const size_t fcsOffset = frameEnd - 2;
    size_t offset = frameStart;
    
    // Decode destination
    bool moreAddresses = false;
    if (!decodeAddress(data, fcsOffset, offset, frame.destination, moreAddresses) || !moreAddresses) return false;
    
    // Decode source
    if (!decodeAddress(data, fcsOffset, offset, frame.source, moreAddresses)) return false;
    
    // Decode digipeaters
    frame.digipeaters.clear();
    size_t digipeaterCount = 0;
    while (moreAddresses) {
        if (++digipeaterCount > 8) return false;
        Address digi;
        if (!decodeAddress(data, fcsOffset, offset, digi, moreAddresses)) return false;
        frame.digipeaters.push_back(digi);
    }
    
    if (offset >= fcsOffset) return false;
    
    // Control field
    frame.control = static_cast<ControlType>(data[offset++]);
    
    // PID field
    if (frame.control == ControlType::I_FRAME || frame.control == ControlType::U_UI) {
        if (offset >= fcsOffset) return false;
        frame.pid = data[offset++];
    }
    
    // Information field
    frame.info.clear();
    frame.info.assign(data + offset, data + fcsOffset);
    
    // Extract and verify FCS
    frame.fcs = static_cast<uint16_t>(data[fcsOffset]) |
                (static_cast<uint16_t>(data[fcsOffset + 1]) << 8);
    
    // Verify FCS (excluding flags and FCS itself)
    return verifyFCS(data + frameStart, fcsOffset - frameStart, frame.fcs);
}

