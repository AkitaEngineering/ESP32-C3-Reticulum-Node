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
#include <ctime>
#include <vector>
#include <EEPROM.h>
#include <monocypher.h>
#include <optional/monocypher-ed25519.h>
#include "RNSIdentity.h"
#include "RNSFernet.h"
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
     * Opens EEPROM, then loads an existing identity or creates and persists one.
     *
     * @return true if identity is ready (loaded or generated)
     */
    bool begin() {
        _ready = false;
        if (!EEPROM.begin(EEPROM_SIZE_WITH_IDENTITY)) return false;
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
        if (!saveIdentity()) {
            crypto_wipe(_x25519_private, sizeof(_x25519_private));
            crypto_wipe(_ed25519_seed, sizeof(_ed25519_seed));
            return false;
        }
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
        crypto_ed25519_sign(signature, _ed25519_secret, message, msg_len);
    }

    /**
     * Verify an Ed25519 signature against a public key.
     * @return true if signature is valid
     */
    static bool verify(const uint8_t signature[64], const uint8_t public_key[32],
                       const uint8_t* message, size_t msg_len) {
        if (!signature || !public_key || (!message && msg_len != 0)) return false;
        return crypto_ed25519_check(signature, public_key, message, msg_len) == 0;
    }

    /** Validate an identity announce before it is learned or forwarded. */
    static bool validateAnnouncePayload(const uint8_t destination_hash[16],
                                        const uint8_t* payload, size_t payload_len,
                                        bool has_ratchet = false) {
        static constexpr size_t RATCHET_SIZE = 32;
        const size_t ratchet_size = has_ratchet ? RATCHET_SIZE : 0;
        const size_t signature_offset = 64 + 10 + 10 + ratchet_size;
        const size_t announce_fixed_size = signature_offset + 64;
        if (!destination_hash || !payload || payload_len < announce_fixed_size) return false;

        const uint8_t* public_key = payload;
        const uint8_t* name_hash_value = payload + 64;
        const uint8_t* random_hash = payload + 74;
        const uint8_t* ratchet = payload + 84;
        const uint8_t* signature = payload + signature_offset;
        const uint8_t* app_data = payload + announce_fixed_size;
        const size_t app_data_len = payload_len - announce_fixed_size;

        uint8_t identity_hash_value[16];
        uint8_t expected_destination[16];
        RNSIdentity::identity_hash(public_key, identity_hash_value);
        RNSIdentity::destination_hash_from_name_hash(
            name_hash_value, identity_hash_value, expected_destination);
        if (crypto_verify16(destination_hash, expected_destination) != 0) return false;

        std::vector<uint8_t> signed_data(16 + 64 + 10 + 10 + ratchet_size + app_data_len);
        size_t offset = 0;
        memcpy(signed_data.data() + offset, destination_hash, 16); offset += 16;
        memcpy(signed_data.data() + offset, public_key, 64); offset += 64;
        memcpy(signed_data.data() + offset, name_hash_value, 10); offset += 10;
        memcpy(signed_data.data() + offset, random_hash, 10); offset += 10;
        if (has_ratchet) {
            memcpy(signed_data.data() + offset, ratchet, RATCHET_SIZE);
            offset += RATCHET_SIZE;
        }
        if (app_data_len != 0) memcpy(signed_data.data() + offset, app_data, app_data_len);

        return verify(signature, public_key + 32, signed_data.data(), signed_data.size());
    }

    // --- SINGLE Destination Encryption ---

    /**
     * Encrypt plaintext for this identity (SINGLE destination encryption).
     *
     * Matches RNS Identity.encrypt():
     *   1. Generate ephemeral X25519 keypair
     *   2. ECDH: shared = X25519(ephemeral_priv, recipient_x25519_pub)
     *   3. HKDF-SHA256(shared, salt=identity_hash, context=empty, length=64)
     *   4. Fernet encrypt plaintext with derived key
     *   5. Return [ephemeral_pub 32][fernet_token]
     *
     * @param plaintext       data to encrypt
     * @param len             plaintext length
     * @param recipient_pub   recipient's X25519 public key (32 bytes) — first half of their 64-byte public key
     * @param recipient_hash  recipient's 16-byte identity hash (used as HKDF salt)
     * @return                encrypted token: [ephemeral_pub 32][fernet_token]
     */
    static std::vector<uint8_t> encryptForIdentity(
        const uint8_t* plaintext, size_t len,
        const uint8_t recipient_pub[32],
        const uint8_t recipient_hash[16])
    {
        if ((!plaintext && len != 0) || !recipient_pub || !recipient_hash) return {};
        // Generate ephemeral X25519 keypair
        uint8_t eph_priv[32], eph_pub[32];
        esp_fill_random(eph_priv, 32);
        crypto_x25519_public_key(eph_pub, eph_priv);

        // ECDH
        uint8_t shared[32];
        crypto_x25519(shared, eph_priv, recipient_pub);
        crypto_wipe(eph_priv, 32);
        if (isAllZero(shared, sizeof(shared))) {
            crypto_wipe(shared, sizeof(shared));
            return {};
        }

        // HKDF: salt = identity_hash, context = empty
        uint8_t derived[64];
        if (!rns_hkdf_sha256(derived, 64, shared, 32, recipient_hash, 16, nullptr, 0)) {
            crypto_wipe(shared, 32);
            return {};
        }
        crypto_wipe(shared, 32);

        // Fernet encrypt
        RNSToken token;
        token.init(derived);
        crypto_wipe(derived, 64);

        auto fernet_token = token.encrypt(plaintext, len);
        if (fernet_token.empty()) return {};

        // Result: [ephemeral_pub 32][fernet_token]
        std::vector<uint8_t> result(32 + fernet_token.size());
        memcpy(result.data(), eph_pub, 32);
        memcpy(result.data() + 32, fernet_token.data(), fernet_token.size());
        return result;
    }

    /**
     * Decrypt a SINGLE destination ciphertext token addressed to this identity.
     *
     * Matches RNS Identity.decrypt():
     *   1. Extract ephemeral_pub (first 32 bytes)
     *   2. ECDH: shared = X25519(our_priv, ephemeral_pub)
     *   3. HKDF-SHA256(shared, salt=identity_hash, context=empty, length=64)
     *   4. Fernet decrypt remainder
     *
     * @param ciphertext_token  [ephemeral_pub 32][fernet_token]
     * @param token_len         total token length
     * @param plaintext         receives the authenticated plaintext; may be empty
     * @return                  true when authentication and decryption succeed
     */
    bool decryptForIdentity(const uint8_t* ciphertext_token, size_t token_len,
                            std::vector<uint8_t>& plaintext) const {
        plaintext.clear();
        if (!_ready || !ciphertext_token || token_len < 32 + 64) return false;

        const uint8_t* peer_pub = ciphertext_token;
        const uint8_t* fernet_data = ciphertext_token + 32;
        size_t fernet_len = token_len - 32;

        // ECDH: shared = X25519(our_priv, peer_ephemeral_pub)
        uint8_t shared[32];
        crypto_x25519(shared, _x25519_private, peer_pub);
        if (isAllZero(shared, sizeof(shared))) {
            crypto_wipe(shared, sizeof(shared));
            return false;
        }

        // HKDF: salt = our identity_hash, context = empty
        uint8_t derived[64];
        if (!rns_hkdf_sha256(derived, 64, shared, 32, _identity_hash, 16, nullptr, 0)) {
            crypto_wipe(shared, 32);
            return false;
        }
        crypto_wipe(shared, 32);

        // Fernet decrypt
        RNSToken token;
        token.init(derived);
        crypto_wipe(derived, 64);

        return token.decrypt(fernet_data, fernet_len, plaintext);
    }

    /** Compatibility wrapper; use the bool overload when empty data is valid. */
    std::vector<uint8_t> decryptForIdentity(const uint8_t* ciphertext_token, size_t token_len) const {
        std::vector<uint8_t> plaintext;
        (void)decryptForIdentity(ciphertext_token, token_len, plaintext);
        return plaintext;
    }

    /** Get X25519 private key (for link handshake — use carefully). */
    const uint8_t* getX25519Private() const { return _x25519_private; }
    /** Get X25519 public key (first 32 bytes of public_key). */
    const uint8_t* getX25519Public() const { return _x25519_public; }
    /** Get Ed25519 public key (second 32 bytes of public_key). */
    const uint8_t* getEd25519Public() const { return _ed25519_public; }

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
        // Use the reference Unix timestamp once SNTP has established a sane
        // wall clock. Offline nodes retain an uptime fallback; the preceding
        // five random bytes preserve uniqueness until time is synchronized.
        const time_t wall_clock = time(nullptr);
        const bool wall_clock_valid = wall_clock >= static_cast<time_t>(1577836800); // 2020-01-01 UTC
        uint64_t ts = wall_clock_valid
            ? static_cast<uint64_t>(wall_clock)
            : static_cast<uint64_t>(millis()) / 1000ULL;
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
    uint8_t _x25519_private[32] = {0};   // X25519 private key
    uint8_t _ed25519_seed[32] = {0};     // Ed25519 seed (32 bytes)

    // Derived keys
    uint8_t _x25519_public[32] = {0};    // X25519 public key
    uint8_t _ed25519_secret[64] = {0};   // Ed25519 expanded secret key (64 bytes, monocypher format)
    uint8_t _ed25519_public[32] = {0};   // Ed25519 public key

    // Combined public key: [X25519_pub 32][Ed25519_pub 32]
    uint8_t _public_key[64] = {0};

    // Identity hash = SHA256(public_key)[:16]
    uint8_t _identity_hash[16] = {0};

    static bool isAllZero(const uint8_t* value, size_t len) {
        uint8_t combined = 0;
        for (size_t i = 0; i < len; ++i) combined |= value[i];
        return combined == 0;
    }

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
        // crypto_ed25519_key_pair produces a 64-byte secret key and 32-byte public key
        crypto_ed25519_key_pair(_ed25519_secret, _ed25519_public, _ed25519_seed);

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
        return !isAllZero(_x25519_private, sizeof(_x25519_private)) &&
               !isAllZero(_ed25519_seed, sizeof(_ed25519_seed));
    }

    bool saveIdentity() {
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

        return EEPROM.commit();
    }
};

#endif // RNS_CRYPTO_H
