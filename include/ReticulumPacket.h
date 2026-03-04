#ifndef RETICULUM_PACKET_H
#define RETICULUM_PACKET_H

#include "Config.h" // Includes size constants, contexts etc.
#include <vector>
#include <cstdint>
#include <cstring> // For memcpy

// --- Reticulum Official Wire Format Defines ---
// Based on official Reticulum source code

// Packet Format: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]

// Packet Types (bits 0-1 of flags byte)
#define RNS_PACKET_DATA      0x00
#define RNS_PACKET_ANNOUNCE  0x01
#define RNS_PACKET_LINKREQ   0x02
#define RNS_PACKET_PROOF     0x03

// Destination Types (bits 2-3 of flags byte)
#define RNS_DEST_SINGLE  0x00
#define RNS_DEST_GROUP   0x01
#define RNS_DEST_PLAIN   0x02
#define RNS_DEST_LINK    0x03

// Propagation Types (bit 4 of flags byte)
#define RNS_PROPAGATION_BROADCAST  0x00
#define RNS_PROPAGATION_TRANSPORT  0x01

// Header Types (bit 6 of flags byte)
#define RNS_HEADER_1  0x00  // Normal header
#define RNS_HEADER_2  0x01  // Transport header (includes transport_id)

// Constants
const size_t RNS_TRUNCATED_HASHLENGTH_BYTES = 16;  // 128 bits / 8
const size_t RNS_HEADER_1_SIZE = 2 + 16 + 1;  // flags + hops + dest_hash + context = 19 bytes
const size_t RNS_HEADER_2_SIZE = 2 + 16 + 16 + 1;  // flags + hops + transport_id + dest_hash + context = 35 bytes
const size_t MAX_PACKET_SIZE = RNS_HEADER_1_SIZE + RNS_MAX_PAYLOAD;


// --- Decoded Packet Info Structure ---
struct RnsPacketInfo {
    // Flags byte decomposed
    uint8_t flags = 0;
    uint8_t packet_type = 0;      // bits 0-1
    uint8_t destination_type = 0;  // bits 2-3
    uint8_t propagation_type = 0;  // bit 4
    bool context_flag = false;     // bit 5
    uint8_t header_type = 0;       // bit 6 (RNS_HEADER_1 or RNS_HEADER_2)
    bool ifac_flag = false;        // bit 7

    uint8_t hops = 0;
    uint8_t destination_hash[RNS_TRUNCATED_HASHLENGTH_BYTES] = {0};  // 16 bytes
    uint8_t context = RNS_CONTEXT_NONE;

    std::vector<uint8_t> data;    // Data payload
    size_t packet_len = 0;
    bool valid = false;

    // Convenience fields populated by deserialize()
    uint8_t destination[RNS_ADDRESS_SIZE] = {0};  // First 8 bytes of destination_hash
    uint8_t source[RNS_ADDRESS_SIZE] = {0};       // Extracted from payload prefix (announce/link)
    uint16_t packet_id = 0;                        // For link-context packets
    uint16_t sequence_number = 0;                  // For link-context packets
    std::vector<uint8_t> payload;                  // App data (after metadata stripped)

    RnsPacketInfo() : packet_len(0), valid(false) {}

    // Helper to parse flags byte
    void parseFlags() {
        packet_type = flags & 0b11;
        destination_type = (flags >> 2) & 0b11;
        propagation_type = (flags >> 4) & 0b1;
        context_flag = (flags >> 5) & 0b1;
        header_type = (flags >> 6) & 0b1;
        ifac_flag = (flags >> 7) & 0b1;
    }

    // Helper to build flags byte
    void buildFlags() {
        flags = (packet_type & 0b11) |
                ((destination_type & 0b11) << 2) |
                ((propagation_type & 0b1) << 4) |
                ((context_flag ? 1 : 0) << 5) |
                ((header_type & 0b1) << 6) |
                ((ifac_flag ? 1 : 0) << 7);
    }
};

// --- Serialization/Deserialization Functions ---
namespace ReticulumPacket {
    // Official Reticulum wire format deserialize
    bool deserialize(const uint8_t *buffer, size_t len, RnsPacketInfo &info);

    // Official Reticulum wire format serialize
    // Format: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
    bool serialize(uint8_t *buffer, size_t &len,
                   const uint8_t* dest_hash_16bytes,  // Full 16-byte destination hash
                   uint8_t packet_type,                // RNS_PACKET_DATA, etc.
                   uint8_t dest_type,                  // RNS_DEST_SINGLE, PLAIN, etc.
                   uint8_t propagation_type,           // RNS_PROPAGATION_BROADCAST, etc.
                   uint8_t context,                    // RNS_CONTEXT_NONE, etc.
                   uint8_t hops,
                   const std::vector<uint8_t>& data);  // Payload data (unencrypted for PLAIN)
}

#endif // RETICULUM_PACKET_H
