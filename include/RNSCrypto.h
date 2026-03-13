#ifndef RNS_CRYPTO_H
#define RNS_CRYPTO_H

/**
 * Reticulum-compatible cryptographic identity for the ESP32 node.
 *
 * Implements the full RNS Identity model:
 *   - Ed25519 signing keypair (32-byte secret -> 64-byte signing key + 32-byte public)
 *   - X25519 key exchange keypair (32-byte private -> 32-byte public)
 *   - Public key = X25519_pub(32) + Ed25519_pub(32) = 64 bytes
 *   - Identity hash = SHA-256(public_key)[:16]
 *   - Destination hash = SHA-256(name_hash + identity_hash)[:16]
 *
 * Keys are persisted in EEPROM alongside the existing node address.
 *
 * Reference: RNS/Identity.py in the official Reticulum implementation.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <EEPROM.h>
#include <monocypher.h>
#include "RNSIdentity.h"
#include "Config.h"

// EEPROM layout for identity keys (placed after existing node config)
// Existing: [NODE_ADDR 8] [PKT_ID 2] = 10 bytes (EEPROM offsets 0..9)
// New:      [MAGIC 4] [X25519_PRIV 32] [ED25519_SEED 32] = 68 bytes (offsets 16..83)
static constexpr int EEPROM_ADDR_IDENTITY_MAGIC = 16;
static constexpr int EEPROM_ADDR_X25519_PRIV    = 20;   // 32 bytes
static constexpr int EEPROM_ADDR_ED25519_SEED   = 52;   // 32 bytes
static constexpr int EEPROM_IDENTITY_END         = 84;

// Magic value to detect initialized identity: "RNSI" in ASCII
static constexpr uint32_t IDENTITY_MAGIC = 0x524E5349;

// Updated EEPROM size to accommodate identity keys
static constexpr int EEPROM_SIZE_WITH_IDENTITY = 128;

class RNSCrypto {
public:
    /**
     * Initialize the identity. Loads keys from EEPROM or generates new ones.
     * Must be called after EEPROM.begin() with at least EEPROM_SIZE_WITH_IDENTITY bytes.
     *
     * @return true if identity is ready (loaded or generated)
     */
    bool begin() {
        if (loadIdentity()) {
            derivePublicKeys();
            computeHashes();
            _ready = true;
            return true;
        }

        // No valid identity found — generate fresh keys
        generateIdentity();
        derivePublicKeys();
        computeHashes();
        saveIdentity();
        _ready = true;
        return true;
    }

    bool isReady() const { return _ready; }

    // --- Key Access ---

    /** 64-byte public key: X25519_pub(32) + Ed25519_pub(32) */
    const uint8_t* getPublicKey() const { return _public_key; }

    /** 16-byte identity hash: SHA256(public_key)[:16] */
    const uint8_t* getIdentityHash() const { return _identity_hash; }

    /**
     * Compute a SINGLE destination hash for the given app name.
     * dest_hash = SHA256(name_hash + identity_hash)[:16]
     */
    void getDestinationHash(const char* app_name, uint8_t out[16]) const {
        RNSIdentity::destination_hash(app_name, _identity_hash, out);
    }

    /**
     * Get the 10-byte name_hash for a destination name.
     */
    void getNameHash(const char* app_name, uint8_t out[10]) const {
        RNSIdentity::name_hash(app_name, out);
    }

    // --- Signing ---

    /**
     * Sign data with Ed25519.
     * @param signature  64-byte output buffer for signature
     * @param message    message bytes to sign
     * @param msg_len    message length
     */
    void sign(uint8_t signature[64], const uint8_t* message, size_t msg_len) const {
        crypto_eddsa_sign(signature, _ed25519_secret, message, msg_len);
    }

    /**
     * Verify an Ed25519 signature against a public key.
     * @return true if signature is valid
     */
    static bool verify(const uint8_t signature[64], const uint8_t public_key[32],
                       const uint8_t* message, size_t msg_len) {
        return crypto_eddsa_check(signature, public_key, message, msg_len) == 0;
    }

    // --- Announce Payload Construction ---

    /**
     * Build a complete RNS announce payload.
     * Format: [PUB_KEY 64][NAME_HASH 10][RANDOM_HASH 10][SIGNATURE 64][APP_DATA...]
     *
     * signed_data = dest_hash + pub_key + name_hash + random_hash + app_data
     *
     * @param app_name   Destination name like "esp32.node"
     * @param app_data   Optional application data (can be empty)
     * @return           Complete announce payload vector
     */
    std::vector<uint8_t> buildAnnouncePayload(
        const char* app_name,
        const std::vector<uint8_t>& app_data = {}
    ) const {
        uint8_t dest_hash[16];
        getDestinationHash(app_name, dest_hash);

        uint8_t nh[10];
        getNameHash(app_name, nh);

        // random_hash = random(5) + timestamp(5)
        // Reference: RNS.Identity.get_random_hash()[0:5] + time(5 big-endian)
        uint8_t random_hash[10];
        // Use esp_random() for TRNG-quality randomness
        uint32_t r1 = esp_random();
        uint32_t r2 = esp_random();
        random_hash[0] = (r1 >> 0) & 0xFF;
        random_hash[1] = (r1 >> 8) & 0xFF;
        random_hash[2] = (r1 >> 16) & 0xFF;
        random_hash[3] = (r1 >> 24) & 0xFF;
        random_hash[4] = (r2 >> 0) & 0xFF;
        // Low 5 bytes of millis/1000 as a rough timestamp
        unsigned long ts = millis() / 1000;
        random_hash[5] = (ts >> 32) & 0xFF;
        random_hash[6] = (ts >> 24) & 0xFF;
        random_hash[7] = (ts >> 16) & 0xFF;
        random_hash[8] = (ts >> 8) & 0xFF;
        random_hash[9] = (ts >> 0) & 0xFF;

        // Build signed_data: dest_hash + pub_key + name_hash + random_hash [+ app_data]
        size_t signed_len = 16 + 64 + 10 + 10 + app_data.size();
        std::vector<uint8_t> signed_data(signed_len);
        size_t offset = 0;
        memcpy(signed_data.data() + offset, dest_hash, 16); offset += 16;
        memcpy(signed_data.data() + offset, _public_key, 64); offset += 64;
        memcpy(signed_data.data() + offset, nh, 10); offset += 10;
        memcpy(signed_data.data() + offset, random_hash, 10); offset += 10;
        if (!app_data.empty()) {
            memcpy(signed_data.data() + offset, app_data.data(), app_data.size());
        }

        // Sign
        uint8_t signature[64];
        sign(signature, signed_data.data(), signed_data.size());

        // Build announce payload: pub_key + name_hash + random_hash + signature [+ app_data]
        size_t payload_len = 64 + 10 + 10 + 64 + app_data.size();
        std::vector<uint8_t> payload(payload_len);
        offset = 0;
        memcpy(payload.data() + offset, _public_key, 64); offset += 64;
        memcpy(payload.data() + offset, nh, 10); offset += 10;
        memcpy(payload.data() + offset, random_hash, 10); offset += 10;
        memcpy(payload.data() + offset, signature, 64); offset += 64;
        if (!app_data.empty()) {
            memcpy(payload.data() + offset, app_data.data(), app_data.size());
        }

        return payload;
    }

private:
    bool _ready = false;

    // Private keys (stored in EEPROM)
    uint8_t _x25519_private[32];   // X25519 private key
    uint8_t _ed25519_seed[32];     // Ed25519 seed (32 bytes)

    // Derived keys
    uint8_t _x25519_public[32];    // X25519 public key
    uint8_t _ed25519_secret[64];   // Ed25519 expanded secret key (64 bytes, monocypher format)
    uint8_t _ed25519_public[32];   // Ed25519 public key

    // Combined public key: [X25519_pub 32][Ed25519_pub 32]
    uint8_t _public_key[64];

    // Identity hash = SHA256(public_key)[:16]
    uint8_t _identity_hash[16];

    void generateIdentity() {
        // Generate 32 bytes of random data for each key using hardware TRNG
        for (int i = 0; i < 32; i += 4) {
            uint32_t r = esp_random();
            memcpy(_x25519_private + i, &r, sizeof(r));
        }
        for (int i = 0; i < 32; i += 4) {
            uint32_t r = esp_random();
            memcpy(_ed25519_seed + i, &r, sizeof(r));
        }
    }

    void derivePublicKeys() {
        // Derive X25519 public key from private key
        crypto_x25519_public_key(_x25519_public, _x25519_private);

        // Derive Ed25519 keypair from 32-byte seed
        // crypto_eddsa_key_pair produces a 64-byte secret key and 32-byte public key
        crypto_eddsa_key_pair(_ed25519_secret, _ed25519_public, _ed25519_seed);

        // Combined public key: [X25519_pub 32][Ed25519_pub 32]
        // This matches RNS Identity.get_public_key() = pub_bytes + sig_pub_bytes
        memcpy(_public_key, _x25519_public, 32);
        memcpy(_public_key + 32, _ed25519_public, 32);
    }

    void computeHashes() {
        RNSIdentity::identity_hash(_public_key, _identity_hash);
    }

    bool loadIdentity() {
        // Check magic bytes
        uint32_t magic = 0;
        for (int i = 0; i < 4; i++) {
            magic |= ((uint32_t)EEPROM.read(EEPROM_ADDR_IDENTITY_MAGIC + i)) << (i * 8);
        }

        if (magic != IDENTITY_MAGIC) {
            return false;
        }

        // Load X25519 private key
        for (int i = 0; i < 32; i++) {
            _x25519_private[i] = EEPROM.read(EEPROM_ADDR_X25519_PRIV + i);
        }

        // Load Ed25519 seed
        for (int i = 0; i < 32; i++) {
            _ed25519_seed[i] = EEPROM.read(EEPROM_ADDR_ED25519_SEED + i);
        }

        return true;
    }

    void saveIdentity() {
        // Write magic
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_ADDR_IDENTITY_MAGIC + i, (IDENTITY_MAGIC >> (i * 8)) & 0xFF);
        }

        // Write X25519 private key
        for (int i = 0; i < 32; i++) {
            EEPROM.write(EEPROM_ADDR_X25519_PRIV + i, _x25519_private[i]);
        }

        // Write Ed25519 seed
        for (int i = 0; i < 32; i++) {
            EEPROM.write(EEPROM_ADDR_ED25519_SEED + i, _ed25519_seed[i]);
        }

        EEPROM.commit();
    }
};

#endif // RNS_CRYPTO_H
