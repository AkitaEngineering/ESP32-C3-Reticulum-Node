#include "ReticulumPacket.h"
#include "Config.h"
#include "RNSIdentity.h"
#include <Arduino.h>
#include <cstring>

namespace ReticulumPacket {

// Deserialize packet from official Reticulum wire format
// Header Type 1: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
// Header Type 2: [FLAGS 1] [HOPS 1] [TRANSPORT_ID 16] [DEST_HASH 16] [CONTEXT 1] [DATA]
bool deserialize(const uint8_t *buffer, size_t len, RnsPacketInfo &info) {
    info = RnsPacketInfo{};

    if (!buffer || len < RNS_HEADER_1_SIZE || len > MAX_PACKET_SIZE) {
        DebugSerial.println("! Deserialize Error: Null buffer or packet too short.");
        return false;
    }

    // Parse header
    info.flags = buffer[0];
    info.parseFlags();  // Extract bitfields from flags byte
    if (info.ifac_flag) {
        DebugSerial.println("! Deserialize Error: IFAC packets are not supported.");
        return false;
    }

    info.hops = buffer[1];

    size_t data_start;

    if (info.header_type == RNS_HEADER_2) {
        // Header Type 2 (transport): transport_id + dest_hash
        if (len < RNS_HEADER_2_SIZE) {
            DebugSerial.println("! Deserialize Error: Header Type 2 packet too short.");
            return false;
        }
        // transport_id at bytes 2..17, dest_hash at bytes 18..33, context at byte 34
        memcpy(info.transport_id, buffer + 2, RNS_TRUNCATED_HASHLENGTH_BYTES);
        memcpy(info.destination_hash, buffer + 2 + RNS_TRUNCATED_HASHLENGTH_BYTES, RNS_TRUNCATED_HASHLENGTH_BYTES);
        memcpy(info.destination, buffer + 2 + RNS_TRUNCATED_HASHLENGTH_BYTES, RNS_ADDRESS_SIZE);
        info.context = buffer[2 + 2 * RNS_TRUNCATED_HASHLENGTH_BYTES]; // byte 34
        data_start = RNS_HEADER_2_SIZE; // 35
    } else {
        // Header Type 1 (normal)
        memcpy(info.destination_hash, buffer + 2, RNS_TRUNCATED_HASHLENGTH_BYTES);
        memcpy(info.destination, buffer + 2, RNS_ADDRESS_SIZE);
        info.context = buffer[18];
        data_start = RNS_HEADER_1_SIZE; // 19
    }

    // Extract data payload (everything after context byte)
    if (len > data_start) {
        info.data.assign(buffer + data_start, buffer + len);
        info.payload = info.data;  // Alias for convenience
    } else {
        info.data.clear();
        info.payload.clear();
    }

    // Source address is not present in DATA packets (official Reticulum format)
    memset(info.source, 0, RNS_ADDRESS_SIZE);

    // For announce packets, the payload contains cryptographic announce data.
    // Official RNS format (148+ bytes):
    //   [PUB_KEY 64][NAME_HASH 10][RANDOM_HASH 10][SIGNATURE 64][APP_DATA...]
    // The "source" for routing purposes is derived from the destination hash
    // in the packet header. For announces, destination_hash IS the identity
    // of the announcer. Copy the first 8 bytes of destination_hash as source
    // address for backward compatibility with internal routing table.
    if (info.packet_type == RNS_PACKET_ANNOUNCE) {
        // Use destination_hash (from header) as the source identity for routing
        memcpy(info.source, info.destination_hash, RNS_ADDRESS_SIZE);
        // payload = full announce data (kept intact for validation)
        info.payload = info.data;
    }

    info.packet_len = len;
    RNSIdentity::packet_hash(buffer, len, info.packet_hash);
    info.valid = true;

    return true;
}

// Serialize packet using official Reticulum wire format
// Header Type 1: [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]
// Header Type 2: [FLAGS 1] [HOPS 1] [TRANSPORT_ID 16] [DEST_HASH 16] [CONTEXT 1] [DATA]
bool serialize(uint8_t *buffer, size_t &len,
               const uint8_t* dest_hash_16bytes,
               uint8_t packet_type,
               uint8_t dest_type,
               uint8_t propagation_type,
               uint8_t context,
               uint8_t hops,
               const std::vector<uint8_t>& data,
               uint8_t context_flag,
               uint8_t header_type,
               const uint8_t* transport_id)
{
    len = 0;

    if (!buffer || !dest_hash_16bytes) {
        DebugSerial.println("! Serialize Error: Null buffer or dest hash.");
        return false;
    }

    if (packet_type > 0x03 || dest_type > 0x03 || propagation_type > 0x01 ||
        context_flag > 0x01 || header_type > RNS_HEADER_2) {
        DebugSerial.println("! Serialize Error: Invalid packet flags.");
        return false;
    }

    size_t header_size = (header_type == RNS_HEADER_2) ? RNS_HEADER_2_SIZE : RNS_HEADER_1_SIZE;
    size_t total_len = header_size + data.size();

    if (total_len > MAX_PACKET_SIZE) {
        DebugSerial.println("! Serialize Error: Total packet exceeds MTU.");
        return false;
    }

    if (header_type == RNS_HEADER_2 && !transport_id) {
        DebugSerial.println("! Serialize Error: Header Type 2 requires transport_id.");
        return false;
    }

    // Build flags byte (matches official Reticulum: Packet.py get_packed_flags())
    // Bits: [7:IFAC][6:HeaderType][5:ContextFlag][4:PropType][3-2:DestType][1-0:PacketType]
    uint8_t flags = (packet_type & 0b11) |
                    ((dest_type & 0b11) << 2) |
                    ((propagation_type & 0b1) << 4) |
                    ((context_flag & 0b1) << 5) |
                    ((header_type & 0b1) << 6); // ifac_flag = 0 (no IFAC)

    // Assemble packet
    buffer[0] = flags;
    buffer[1] = hops;

    if (header_type == RNS_HEADER_2) {
        memcpy(buffer + 2, transport_id, RNS_TRUNCATED_HASHLENGTH_BYTES);
        memcpy(buffer + 2 + RNS_TRUNCATED_HASHLENGTH_BYTES, dest_hash_16bytes, RNS_TRUNCATED_HASHLENGTH_BYTES);
        buffer[2 + 2 * RNS_TRUNCATED_HASHLENGTH_BYTES] = context;
    } else {
        memcpy(buffer + 2, dest_hash_16bytes, RNS_TRUNCATED_HASHLENGTH_BYTES);
        buffer[18] = context;
    }

    // Copy data payload if present
    if (!data.empty()) {
        memcpy(buffer + header_size, data.data(), data.size());
    }

    len = total_len;
    return true;
}

} // namespace ReticulumPacket
