#ifndef RNS_IDENTITY_H
#define RNS_IDENTITY_H

/**
 * Reticulum-compatible hashing and identity utilities.
 *
 * The official Reticulum implementation uses SHA-256 for all hashing
 * (Identity.full_hash, Identity.truncated_hash, destination hashes,
 * packet hashes, announce validation).
 *
 * This header wraps the ESP-IDF mbedtls SHA-256 so our node produces
 * hashes that match the reference Python implementation byte-for-byte.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "Config.h"

// mbedtls SHA-256 is provided by the ESP-IDF toolchain
#include <mbedtls/sha256.h>

namespace RNSIdentity {

// --- Constants matching RNS/Identity.py & RNS/Reticulum.py ---
static constexpr size_t HASHLENGTH            = 256;  // bits
static constexpr size_t HASHLENGTH_BYTES      = 32;   // 256 / 8
static constexpr size_t TRUNCATED_HASHLENGTH  = 128;  // bits
static constexpr size_t TRUNCATED_HASH_BYTES  = 16;   // 128 / 8
static constexpr size_t NAME_HASH_LENGTH      = 80;   // bits
static constexpr size_t NAME_HASH_BYTES       = 10;   // 80 / 8
static constexpr size_t KEYSIZE               = 512;  // bits (32 enc + 32 sig)
static constexpr size_t KEYSIZE_BYTES         = 64;
static constexpr size_t SIGLENGTH             = 512;  // bits
static constexpr size_t SIGLENGTH_BYTES       = 64;

/**
 * Compute SHA-256 digest (full_hash).
 * Equivalent to RNS.Identity.full_hash(data) in Python.
 */
inline void full_hash(const uint8_t* data, size_t len, uint8_t out[HASHLENGTH_BYTES]) {
    mbedtls_sha256(data, len, out, 0 /* 0 = SHA-256, 1 = SHA-224 */);
}

/**
 * Compute truncated SHA-256 digest (first 16 bytes).
 * Equivalent to RNS.Identity.truncated_hash(data) in Python.
 */
inline void truncated_hash(const uint8_t* data, size_t len, uint8_t out[TRUNCATED_HASH_BYTES]) {
    uint8_t full[HASHLENGTH_BYTES];
    full_hash(data, len, full);
    memcpy(out, full, TRUNCATED_HASH_BYTES);
}

/**
 * Compute name_hash for a destination name string.
 * Equivalent to:
 *   RNS.Identity.full_hash(name.encode("utf-8"))[:10]
 *
 * @param name  ASCII/UTF-8 name string like "esp32.node"
 * @param out   10-byte output buffer
 */
inline void name_hash(const char* name, uint8_t out[NAME_HASH_BYTES]) {
    uint8_t full[HASHLENGTH_BYTES];
    full_hash(reinterpret_cast<const uint8_t*>(name), strlen(name), full);
    memcpy(out, full, NAME_HASH_BYTES);
}

/**
 * Compute destination hash matching the official Reticulum algorithm:
 *
 *   name_hash = SHA256(app_name.aspect1.aspect2...)[:10]
 *   addr_hash_material = name_hash + identity_hash[:16]
 *   destination_hash = SHA256(addr_hash_material)[:16]
 *
 * For PLAIN destinations (no identity), identity_hash is omitted:
 *   destination_hash = SHA256(name_hash)[:16]
 *
 * @param app_name       Dot-separated name, e.g. "esp32.node"
 * @param identity_hash  Truncated identity hash (16 bytes), or nullptr for PLAIN
 * @param out            16-byte destination hash output
 */
inline void destination_hash(const char* app_name,
                             const uint8_t* identity_hash,  // 16 bytes, or nullptr
                             uint8_t out[TRUNCATED_HASH_BYTES])
{
    // Step 1: name_hash = SHA256(name)[:10]
    uint8_t nh[NAME_HASH_BYTES];
    name_hash(app_name, nh);

    // Step 2: addr_hash_material = name_hash [+ identity_hash]
    uint8_t material[NAME_HASH_BYTES + TRUNCATED_HASH_BYTES];
    memcpy(material, nh, NAME_HASH_BYTES);
    size_t material_len = NAME_HASH_BYTES;
    if (identity_hash) {
        memcpy(material + NAME_HASH_BYTES, identity_hash, TRUNCATED_HASH_BYTES);
        material_len += TRUNCATED_HASH_BYTES;
    }

    // Step 3: destination_hash = SHA256(addr_hash_material)[:16]
    truncated_hash(material, material_len, out);
}

/**
 * Compute the identity hash from a 64-byte public key.
 * identity_hash = SHA256(public_key)[:16]
 */
inline void identity_hash(const uint8_t* public_key_64, uint8_t out[TRUNCATED_HASH_BYTES]) {
    truncated_hash(public_key_64, KEYSIZE_BYTES, out);
}

/**
 * Compute the packet hash from the hashable part of a raw packet.
 * Official Reticulum: packet_hash = SHA256(hashable_part)
 *
 * For Header Type 1: hashable_part = (flags & 0x0F) + raw[2:]
 * For Header Type 2: hashable_part = (flags & 0x0F) + raw[TRUNCATED_HASH_BYTES+2:]
 */
inline void packet_hash(const uint8_t* raw, size_t raw_len, uint8_t out[HASHLENGTH_BYTES]) {
    uint8_t header_type = (raw[0] >> 6) & 0x01;
    uint8_t masked_flags = raw[0] & 0x0F;

    size_t skip = (header_type == 1) ? (TRUNCATED_HASH_BYTES + 2) : 2;
    if (raw_len <= skip) {
        memset(out, 0, HASHLENGTH_BYTES);
        return;
    }

    // Build hashable_part in a temp buffer
    size_t hashable_len = 1 + (raw_len - skip);
    std::vector<uint8_t> hashable(hashable_len);
    hashable[0] = masked_flags;
    memcpy(hashable.data() + 1, raw + skip, raw_len - skip);

    full_hash(hashable.data(), hashable_len, out);
}

} // namespace RNSIdentity

#endif // RNS_IDENTITY_H
