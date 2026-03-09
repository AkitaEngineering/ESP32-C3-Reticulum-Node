#include "AX25.h"
#include <Arduino.h>

// AX.25 Protocol Implementation

void AX25::callsignToAX25(const char* callsign, uint8_t* output) {
    memset(output, 0x20 << 1, 6);  // Fill with spaces, shifted left
    size_t len = strlen(callsign);
    if (len > 6) len = 6;
    for (size_t i = 0; i < len; i++) {
        output[i] = (callsign[i] << 1) & 0xFE;
    }
}

void AX25::ax25ToCallsign(const uint8_t* ax25, char* output) {
    for (int i = 0; i < 6; i++) {
        output[i] = (ax25[i] >> 1) & 0x7F;
        if (output[i] == 0x20) output[i] = '\0';
    }
    output[6] = '\0';
}

void AX25::encodeAddress(const Address& addr, std::vector<uint8_t>& output, bool isLast) {
    uint8_t ax25Addr[7];
    callsignToAX25(addr.callsign, ax25Addr);
    
    // Set SSID and control bits
    uint8_t ssidByte = (addr.ssid << 1) & 0x1E;
    if (addr.command) ssidByte |= 0x80;
    if (addr.hasBeenRepeated) ssidByte |= 0x40;
    if (isLast) ssidByte |= 0x01;  // Address extension bit
    
    for (int i = 0; i < 6; i++) {
        output.push_back(ax25Addr[i]);
    }
    output.push_back(ssidByte);
}

bool AX25::decodeAddress(const uint8_t* data, size_t& offset, Address& addr) {
    if (offset + 7 > 330) return false;  // Max frame size
    
    uint8_t ax25Addr[6];
    memcpy(ax25Addr, data + offset, 6);
    ax25ToCallsign(ax25Addr, addr.callsign);
    
    uint8_t ssidByte = data[offset + 6];
    addr.ssid = (ssidByte >> 1) & 0x0F;
    addr.command = (ssidByte & 0x80) != 0;
    addr.hasBeenRepeated = (ssidByte & 0x40) != 0;
    
    offset += 7;
    return (ssidByte & 0x01) == 0;  // Return true if more addresses follow
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
    
    // Calculate and append FCS
    uint16_t fcs = calculateFCS(output.data() + 1, output.size() - 1);
    output.push_back(fcs & 0xFF);
    output.push_back((fcs >> 8) & 0xFF);
    
    output.push_back(FLAG);  // Closing flag
    
    return true;
}

bool AX25::decodeFrame(const uint8_t* data, size_t len, Frame& frame) {
    if (len < 18) return false;  // Minimum frame size
    
    size_t offset = 0;
    
    // Skip opening flag if present (may have been stripped by the modem layer)
    if (data[offset] == FLAG) {
        offset++;
    }
    
    // Decode destination
    if (!decodeAddress(data, offset, frame.destination)) return false;
    
    // Decode source
    bool moreAddresses = decodeAddress(data, offset, frame.source);
    
    // Decode digipeaters
    frame.digipeaters.clear();
    while (moreAddresses && offset + 7 <= len) {
        Address digi;
        moreAddresses = decodeAddress(data, offset, digi);
        frame.digipeaters.push_back(digi);
    }
    
    if (offset >= len) return false;
    
    // Control field
    frame.control = static_cast<ControlType>(data[offset++]);
    
    // PID field
    if (frame.control == ControlType::I_FRAME || frame.control == ControlType::U_UI) {
        if (offset >= len) return false;
        frame.pid = data[offset++];
    }
    
    // Information field
    frame.info.clear();
    while (offset + 3 < len) {  // Leave room for FCS (2 bytes) + closing flag (1 byte)
        frame.info.push_back(data[offset++]);
    }
    
    // Extract and verify FCS
    if (offset + 2 > len) return false;
    frame.fcs = data[offset] | (data[offset + 1] << 8);
    
    // Verify FCS (excluding flags and FCS itself)
    uint16_t calculatedFCS = calculateFCS(data + 1, offset - 1);
    return verifyFCS(data + 1, offset - 1, frame.fcs);
}

