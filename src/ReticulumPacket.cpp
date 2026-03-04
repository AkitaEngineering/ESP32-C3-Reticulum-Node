#include "ReticulumPacket.h"
#include "Config.h"
#include <Arduino.h>
#include <cstring>

namespace ReticulumPacket {

// Deserialize packet from official Reticulum wire format
// Format: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
bool deserialize(const uint8_t *buffer, size_t len, RnsPacketInfo &info) {
    info.valid = false;

    if (!buffer || len < RNS_HEADER_1_SIZE) {
        DebugSerial.println("! Deserialize Error: Null buffer or packet too short.");
        return false;
    }

    // Parse header
    info.flags = buffer[0];
    info.parseFlags();  // Extract bitfields from flags byte

    info.hops = buffer[1];

    // Copy 16-byte destination hash
    memcpy(info.destination_hash, buffer + 2, RNS_TRUNCATED_HASHLENGTH_BYTES);

    // Also populate 8-byte destination field with first 8 bytes of hash
    memcpy(info.destination, buffer + 2, RNS_ADDRESS_SIZE);

    info.context = buffer[18];  // After flags(1) + hops(1) + dest_hash(16)

    // Extract data payload (everything after context byte)
    size_t data_start = 19;
    if (len > data_start) {
        info.data.assign(buffer + data_start, buffer + len);
        info.payload = info.data;  // Alias for convenience
    }

    // Source address is not present in DATA packets (official Reticulum format)
    // It would only be added by transport nodes (Header 2), which we don't handle yet
    memset(info.source, 0, RNS_ADDRESS_SIZE);

    // For announce packets, source address is carried at the start of the data payload
    // Format: [SOURCE_ADDR 8] [remaining announce data...]
    if (info.packet_type == RNS_PACKET_ANNOUNCE && info.data.size() >= RNS_ADDRESS_SIZE) {
        memcpy(info.source, info.data.data(), RNS_ADDRESS_SIZE);
        // Strip source prefix from payload alias, keep full data intact
        info.payload.assign(info.data.begin() + RNS_ADDRESS_SIZE, info.data.end());
    }

    // For link-context packets, source/packet_id/sequence_number are encoded in the payload
    // Format: [SOURCE 8] [PACKET_ID 2] [SEQ_NUM 2] [APP_DATA...]
    const size_t LINK_META_SIZE = RNS_ADDRESS_SIZE + 2 + 2; // 12 bytes
    if ((info.context == RNS_CONTEXT_LINK_REQ  || info.context == RNS_CONTEXT_LINK_DATA ||
         info.context == RNS_CONTEXT_ACK       || info.context == RNS_CONTEXT_LINK_CLOSE) &&
        info.data.size() >= LINK_META_SIZE)
    {
        memcpy(info.source, info.data.data(), RNS_ADDRESS_SIZE);
        info.packet_id       = ((uint16_t)info.data[RNS_ADDRESS_SIZE]     << 8) | info.data[RNS_ADDRESS_SIZE + 1];
        info.sequence_number = ((uint16_t)info.data[RNS_ADDRESS_SIZE + 2] << 8) | info.data[RNS_ADDRESS_SIZE + 3];
        // Strip link metadata; remaining bytes are the actual application data
        info.data.assign(info.data.begin() + LINK_META_SIZE, info.data.end());
        info.payload = info.data;
    }

    info.packet_len = len;
    info.valid = true;

    return true;
}

// Serialize packet using official Reticulum wire format
// Format: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
bool serialize(uint8_t *buffer, size_t &len,
               const uint8_t* dest_hash_16bytes,
               uint8_t packet_type,
               uint8_t dest_type,
               uint8_t propagation_type,
               uint8_t context,
               uint8_t hops,
               const std::vector<uint8_t>& data)
{
    len = 0;

    if (!buffer || !dest_hash_16bytes) {
        DebugSerial.println("! Serialize Error: Null buffer or dest hash.");
        return false;
    }

    if (data.size() > RNS_MAX_PAYLOAD) {
        DebugSerial.println("! Serialize Error: Payload exceeds max size.");
        return false;
    }

    size_t total_len = RNS_HEADER_1_SIZE + data.size();
    if (total_len > MAX_PACKET_SIZE) {
        DebugSerial.println("! Serialize Error: Total packet exceeds max size.");
        return false;
    }

    // Build flags byte
    // Format: [IFAC:1][HeaderType:1][ContextFlag:1][PropType:1][DestType:2][PacketType:2]
    uint8_t flags = (packet_type & 0b11) |
                    ((dest_type & 0b11) << 2) |
                    ((propagation_type & 0b1) << 4) |
                    (0 << 5) |  // context_flag = 0 (unused)
                    (0 << 6) |  // header_type = 0 (HEADER_1)
                    (0 << 7);   // ifac_flag = 0 (no IFAC)

    // Assemble packet
    buffer[0] = flags;
    buffer[1] = hops;
    memcpy(buffer + 2, dest_hash_16bytes, RNS_TRUNCATED_HASHLENGTH_BYTES);
    buffer[18] = context;

    // Copy data payload if present
    if (!data.empty()) {
        memcpy(buffer + 19, data.data(), data.size());
    }

    len = total_len;
    return true;
}

} // namespace ReticulumPacket
