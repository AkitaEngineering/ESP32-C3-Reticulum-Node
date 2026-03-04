#ifndef AX25_H
#define AX25_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include <cstring>

// AX.25 Protocol Implementation
// Based on AX.25 specification for amateur packet radio

class AX25 {
public:
    // AX.25 Frame Types
    enum class FrameType {
        I,      // Information frame
        S,      // Supervisory frame
        U       // Unnumbered frame
    };
    
    // AX.25 Control Field Types
    enum class ControlType {
        I_FRAME = 0x00,           // Information frame
        S_RR = 0x01,              // Receive Ready
        S_RNR = 0x05,             // Receive Not Ready
        S_REJ = 0x09,             // Reject
        U_SABM = 0x2F,            // Set Asynchronous Balanced Mode
        U_DISC = 0x43,            // Disconnect
        U_DM = 0x0F,              // Disconnected Mode
        U_UA = 0x63,              // Unnumbered Acknowledge
        U_FRMR = 0x87,            // Frame Reject
        U_UI = 0x03               // Unnumbered Information
    };
    
    // AX.25 Address Structure
    struct Address {
        char callsign[6];         // 6-character callsign (padded with spaces)
        uint8_t ssid;             // SSID (0-15)
        bool command;             // Command/Response bit
        bool hasBeenRepeated;     // Has Been Repeated bit
        
        Address() : ssid(0), command(false), hasBeenRepeated(false) {
            memset(callsign, ' ', 6);
        }
        
        Address(const char* call, uint8_t ssid_val = 0) : ssid(ssid_val), command(false), hasBeenRepeated(false) {
            memset(callsign, ' ', 6);
            size_t len = strlen(call);
            if (len > 6) len = 6;
            memcpy(callsign, call, len);
        }
    };
    
    // AX.25 Frame Structure
    struct Frame {
        Address source;
        Address destination;
        std::vector<Address> digipeaters;  // Repeater path
        ControlType control;
        uint8_t pid;              // Protocol ID (0xF0 for no layer 3)
        std::vector<uint8_t> info; // Information field (for I and UI frames)
        uint16_t fcs;             // Frame Check Sequence
        
        Frame() : control(ControlType::U_UI), pid(0xF0), fcs(0) {}
    };
    
    // Encode AX.25 address to bytes
    static void encodeAddress(const Address& addr, std::vector<uint8_t>& output, bool isLast = false);
    
    // Decode AX.25 address from bytes
    static bool decodeAddress(const uint8_t* data, size_t& offset, Address& addr);
    
    // Encode complete AX.25 frame
    static bool encodeFrame(const Frame& frame, std::vector<uint8_t>& output);
    
    // Decode AX.25 frame from bytes
    static bool decodeFrame(const uint8_t* data, size_t len, Frame& frame);
    
    // Calculate FCS (Frame Check Sequence) - CRC-16 CCITT
    static uint16_t calculateFCS(const uint8_t* data, size_t len);
    
    // Verify FCS
    static bool verifyFCS(const uint8_t* data, size_t len, uint16_t receivedFCS);
    
    // Convert callsign to AX.25 format (shift left by 1 bit)
    static void callsignToAX25(const char* callsign, uint8_t* output);
    
    // Convert AX.25 format to callsign (shift right by 1 bit)
    static void ax25ToCallsign(const uint8_t* ax25, char* output);
    
private:
    static const uint16_t FCS_POLYNOMIAL = 0x8408;  // CRC-16 CCITT reversed
    static const uint8_t FLAG = 0x7E;                // AX.25 flag byte
};

#endif // AX25_H

