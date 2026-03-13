#include "KISS.h"
#include "Config.h"   // For InterfaceType
#include "ReticulumPacket.h" // For MAX_PACKET_SIZE
#include <Arduino.h> // For Serial debug

KISSProcessor::KISSProcessor(PacketHandler handler) :
    _inEscapeState(false),
    _expectingCommand(true),  // Start expecting command byte after first FEND
    _packetHandler(handler)
{
     // Reserve some buffer space upfront if desired
     // _receiveBuffer.reserve(MAX_PACKET_SIZE / 2);
}

void KISSProcessor::decodeByte(uint8_t byte, InterfaceType interface) {
    // Handle FEND indicating end of packet OR start padding
    if (byte == KISS_FEND) {
        if (!_receiveBuffer.empty()) {
            // End of a packet, process it
            if (_packetHandler) {
                _packetHandler(_receiveBuffer, interface);
            }
            _receiveBuffer.clear(); // Clear buffer for next packet
        }
        // Ignore FEND if buffer is empty (start padding or multiple FENDs)
        _inEscapeState = false; // Reset escape state on FEND
        _expectingCommand = true; // Next non-FEND byte will be command byte
        return; // Done processing this FEND byte
    }

    // Handle KISS command byte (comes after FEND, before data)
    if (_expectingCommand) {
        _expectingCommand = false;
        // Strip port nibble (upper 4 bits) and keep command (lower 4 bits)
        // This matches the official Reticulum KISSInterface: byte = byte & 0x0F
        uint8_t command = byte & 0x0F;
        // Command byte: 0x00 = data frame (the only one we process)
        // Other commands (0x01-0x0F) are for TNC configuration - ignore them
        if (command != 0x00) {
            // Not a data frame; skip until next FEND
            _receiveBuffer.clear();
        }
        return;
    }

    // Handle escape sequences
    if (_inEscapeState) {
        if (byte == KISS_TFEND) {
            byte = KISS_FEND; // Store unescaped byte
        } else if (byte == KISS_TFESC) {
            byte = KISS_FESC; // Store unescaped byte
        } else {
             // Protocol error: FESC followed by invalid byte
             DebugSerial.print("! KISS Decode Error: Invalid escape sequence on interface "); DebugSerial.println(static_cast<int>(interface));
             _receiveBuffer.clear(); // Discard partial packet
             _inEscapeState = false; // Reset escape state
             _expectingCommand = true; // Reset to expecting command
             return; // Discard this byte too
        }
        _inEscapeState = false; // Handled escape sequence
        // Now process the unescaped 'byte' below
    } else if (byte == KISS_FESC) {
        // Start of an escape sequence, wait for next byte
        _inEscapeState = true;
        return; // Don't store the FESC itself
    }

    // Store regular or unescaped data byte
    // HW_MTU is 564 bytes in the official KISSInterface (allows for escape expansion)
    if (_receiveBuffer.size() < 564) {
        _receiveBuffer.push_back(byte);
    } else {
        // Buffer overflow
        DebugSerial.print("! KISS Decode Error: Packet buffer overflow on interface "); DebugSerial.println(static_cast<int>(interface));
        _receiveBuffer.clear(); // Discard oversized packet
        _inEscapeState = false; // Reset escape state
        _expectingCommand = true; // Reset to expecting command
        // If overflow occurs, the current packet is lost. FEND will reset.
    }
}

// Static encode method
void KISSProcessor::encode(const uint8_t *input, size_t len, std::vector<uint8_t> &output) {
    if (!input) return;
    output.clear();
    // output.reserve(len + 2 + (len / 50)); // Pre-allocate heuristic
    output.push_back(KISS_FEND); // Start with FEND is recommended by some KISS variants
    output.push_back(0x00);      // Command byte: 0x00 = data frame (REQUIRED by KISS protocol)

    for (size_t i = 0; i < len; ++i) {
        if (input[i] == KISS_FEND) {
            output.push_back(KISS_FESC);
            output.push_back(KISS_TFEND);
        } else if (input[i] == KISS_FESC) {
            output.push_back(KISS_FESC);
            output.push_back(KISS_TFESC);
        } else {
            output.push_back(input[i]);
        }
    }
    output.push_back(KISS_FEND); // End with FEND
}
