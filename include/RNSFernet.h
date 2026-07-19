#ifndef RNS_FERNET_H
#define RNS_FERNET_H

/**
 * RNS-compatible Fernet token and HKDF-SHA256 implementation.
 *
 * Implements the modified Fernet spec used by Reticulum:
 *   - NO version byte or timestamp (stripped per RNS Token.py)
 *   - Token = [IV 16][AES-256-CBC ciphertext][HMAC-SHA256 32]
 *   - 48 bytes overhead per token
 *
 * Also provides:
 *   - HKDF-SHA256 (RFC 5869) key derivation
 *   - HMAC-SHA256
 *   - PKCS7 padding
 *   - AES-256-CBC encrypt/decrypt
 *
 * Uses mbedtls (bundled with ESP-IDF) for all crypto operations.
 *
 * Reference: RNS/Cryptography/Token.py, HKDF.py, PKCS7.py
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <monocypher.h>

// --- HMAC-SHA256 ---

/**
 * Compute HMAC-SHA256(key, data).
 * @param key      HMAC key
 * @param key_len  key length in bytes
 * @param data     input data
 * @param data_len data length
 * @param out      32-byte output buffer
 * @return true on success
 */
inline bool rns_hmac_sha256(const uint8_t* key, size_t key_len,
                            const uint8_t* data, size_t data_len,
                            uint8_t out[32]) {
    if (!out || (!key && key_len != 0) || (!data && data_len != 0)) return false;
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) return false;
    return mbedtls_md_hmac(md_info, key, key_len, data, data_len, out) == 0;
}

// --- HKDF-SHA256 (RFC 5869) ---

/**
 * HKDF key derivation using SHA-256.
 *
 * Matches RNS/Cryptography/HKDF.py exactly:
 *   - Extract: PRK = HMAC-SHA256(salt, ikm)
 *   - Expand:  T(i) = HMAC-SHA256(PRK, T(i-1) || context || byte(i))
 *
 * @param out         output buffer (must be >= length bytes)
 * @param length      desired output length (max 255*32 = 8160)
 * @param ikm         input keying material
 * @param ikm_len     IKM length
 * @param salt        optional salt (NULL = 32 zero bytes)
 * @param salt_len    salt length (ignored if salt is NULL)
 * @param context     optional context/info (NULL = empty, matching RNS default)
 * @param context_len context length
 * @return true on success
 */
inline bool rns_hkdf_sha256(uint8_t* out, size_t length,
                            const uint8_t* ikm, size_t ikm_len,
                            const uint8_t* salt, size_t salt_len,
                            const uint8_t* context, size_t context_len) {
    if (!out || !ikm || ikm_len == 0 || length == 0 || length > 255 * 32 ||
        (!salt && salt_len != 0) || (!context && context_len != 0)) return false;

    // Extract phase: PRK = HMAC-SHA256(salt, ikm)
    uint8_t default_salt[32] = {0};
    const uint8_t* actual_salt = salt;
    size_t actual_salt_len = salt_len;
    if (!salt || salt_len == 0) {
        actual_salt = default_salt;
        actual_salt_len = 32;
    }

    uint8_t prk[32];
    if (!rns_hmac_sha256(actual_salt, actual_salt_len, ikm, ikm_len, prk)) {
        return false;
    }

    // Expand phase
    uint8_t block[32];
    size_t block_len = 0;
    size_t derived_len = 0;
    size_t n_blocks = (length + 31) / 32;

    for (size_t i = 0; i < n_blocks; i++) {
        // T(i) = HMAC-SHA256(PRK, T(i-1) || context || byte(i+1))
        // Build input: previous_block + context + counter_byte
        size_t ctx_len = (context ? context_len : 0);
        size_t input_len = block_len + ctx_len + 1;
        std::vector<uint8_t> input(input_len);
        size_t off = 0;
        if (block_len > 0) {
            memcpy(input.data(), block, block_len);
            off += block_len;
        }
        if (context && ctx_len > 0) {
            memcpy(input.data() + off, context, ctx_len);
            off += ctx_len;
        }
        // Counter byte: (i+1) % 256, matching Python's bytes([(i + 1)%(0xFF+1)])
        input[off] = (uint8_t)((i + 1) & 0xFF);

        if (!rns_hmac_sha256(prk, 32, input.data(), input_len, block)) {
            crypto_wipe(prk, sizeof(prk));
            crypto_wipe(block, sizeof(block));
            return false;
        }
        block_len = 32;

        size_t to_copy = length - derived_len;
        if (to_copy > 32) to_copy = 32;
        memcpy(out + derived_len, block, to_copy);
        derived_len += to_copy;
    }
    crypto_wipe(prk, sizeof(prk));
    crypto_wipe(block, sizeof(block));
    return true;
}

// --- PKCS7 Padding ---

/** PKCS7 pad data to 16-byte blocks. Returns padded data. */
inline std::vector<uint8_t> rns_pkcs7_pad(const uint8_t* data, size_t len) {
    if (!data && len != 0) return {};
    size_t n = 16 - (len % 16);
    std::vector<uint8_t> padded(len + n);
    if (len != 0) memcpy(padded.data(), data, len);
    memset(padded.data() + len, (uint8_t)n, n);
    return padded;
}

/** Validate PKCS7 padding and return the unpadded length. */
inline bool rns_pkcs7_unpad(const uint8_t* data, size_t len, size_t& unpadded_len) {
    unpadded_len = 0;
    if (!data || len == 0) return false;
    uint8_t n = data[len - 1];
    if (n == 0 || n > 16 || n > len) return false;
    // Verify all padding bytes
    for (size_t i = 0; i < n; i++) {
        if (data[len - 1 - i] != n) return false;
    }
    unpadded_len = len - n;
    return true;
}

// --- AES-256-CBC ---

/**
 * AES-256-CBC encrypt (no padding — caller must PKCS7-pad first).
 * @param out         ciphertext output buffer (must be >= in_len)
 * @param out_len     actual ciphertext length written
 * @param plaintext   padded plaintext
 * @param in_len      plaintext length (must be multiple of 16)
 * @param key         32-byte AES key
 * @param iv          16-byte IV
 * @return true on success
 */
inline bool rns_aes256_cbc_encrypt(uint8_t* out, size_t& out_len,
                                   const uint8_t* plaintext, size_t in_len,
                                   const uint8_t key[32], const uint8_t iv[16]) {
    out_len = 0;
    if (!out || !plaintext || !key || !iv || in_len == 0 || (in_len % 16) != 0) return false;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    bool ok = false;

    const mbedtls_cipher_info_t* info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if (!info) goto cleanup;
    if (mbedtls_cipher_setup(&ctx, info) != 0) goto cleanup;
    if (mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_NONE) != 0) goto cleanup;
    if (mbedtls_cipher_setkey(&ctx, key, 256, MBEDTLS_ENCRYPT) != 0) goto cleanup;
    if (mbedtls_cipher_set_iv(&ctx, iv, 16) != 0) goto cleanup;
    if (mbedtls_cipher_reset(&ctx) != 0) goto cleanup;
    {
        size_t olen = 0, finish_len = 0;
        if (mbedtls_cipher_update(&ctx, plaintext, in_len, out, &olen) != 0) goto cleanup;
        if (mbedtls_cipher_finish(&ctx, out + olen, &finish_len) != 0) goto cleanup;
        out_len = olen + finish_len;
        ok = true;
    }

cleanup:
    mbedtls_cipher_free(&ctx);
    return ok;
}

/**
 * AES-256-CBC decrypt (no unpadding — caller must PKCS7-unpad after).
 */
inline bool rns_aes256_cbc_decrypt(uint8_t* out, size_t& out_len,
                                   const uint8_t* ciphertext, size_t in_len,
                                   const uint8_t key[32], const uint8_t iv[16]) {
    out_len = 0;
    if (!out || !ciphertext || !key || !iv || in_len == 0 || (in_len % 16) != 0) return false;
    mbedtls_cipher_context_t ctx;
    mbedtls_cipher_init(&ctx);
    bool ok = false;

    const mbedtls_cipher_info_t* info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_256_CBC);
    if (!info) goto cleanup;
    if (mbedtls_cipher_setup(&ctx, info) != 0) goto cleanup;
    if (mbedtls_cipher_set_padding_mode(&ctx, MBEDTLS_PADDING_NONE) != 0) goto cleanup;
    if (mbedtls_cipher_setkey(&ctx, key, 256, MBEDTLS_DECRYPT) != 0) goto cleanup;
    if (mbedtls_cipher_set_iv(&ctx, iv, 16) != 0) goto cleanup;
    if (mbedtls_cipher_reset(&ctx) != 0) goto cleanup;
    {
        size_t olen = 0, finish_len = 0;
        if (mbedtls_cipher_update(&ctx, ciphertext, in_len, out, &olen) != 0) goto cleanup;
        if (mbedtls_cipher_finish(&ctx, out + olen, &finish_len) != 0) goto cleanup;
        out_len = olen + finish_len;
        ok = true;
    }

cleanup:
    mbedtls_cipher_free(&ctx);
    return ok;
}

// --- Fernet Token (RNS modified: no version/timestamp) ---

static constexpr size_t RNS_FERNET_OVERHEAD = 48;  // IV(16) + min_pad(16) + HMAC(32) - but actual overhead depends on data alignment
static constexpr size_t RNS_FERNET_IV_SIZE = 16;
static constexpr size_t RNS_FERNET_HMAC_SIZE = 32;

/**
 * RNS Fernet Token — manages a 64-byte derived key split into:
 *   signing_key    = derived_key[0:32]
 *   encryption_key = derived_key[32:64]
 *
 * Token format: [IV 16][AES-256-CBC ciphertext][HMAC-SHA256 32]
 */
class RNSToken {
public:
    RNSToken() : _valid(false), _signing_key{0}, _encryption_key{0} {}
    ~RNSToken() { clear(); }

    RNSToken(const RNSToken&) = delete;
    RNSToken& operator=(const RNSToken&) = delete;

    /** Initialize from a 64-byte derived key. */
    bool init(const uint8_t derived_key[64]) {
        if (!derived_key) {
            clear();
            return false;
        }
        memcpy(_signing_key, derived_key, 32);
        memcpy(_encryption_key, derived_key + 32, 32);
        _valid = true;
        return true;
    }

    bool isValid() const { return _valid; }

    void clear() {
        crypto_wipe(_signing_key, sizeof(_signing_key));
        crypto_wipe(_encryption_key, sizeof(_encryption_key));
        _valid = false;
    }

    /**
     * Encrypt plaintext into a Fernet token.
     * Token = [IV 16][ciphertext][HMAC-SHA256 32]
     *
     * @return encrypted token as vector, empty on failure
     */
    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t plaintext_len) const {
        if (!_valid || (!plaintext && plaintext_len != 0)) return {};

        // Generate random IV
        uint8_t iv[16];
        esp_fill_random(iv, 16);

        // PKCS7 pad
        auto padded = rns_pkcs7_pad(plaintext, plaintext_len);

        // Encrypt
        std::vector<uint8_t> ciphertext(padded.size());
        size_t ct_len = 0;
        if (!rns_aes256_cbc_encrypt(ciphertext.data(), ct_len,
                                     padded.data(), padded.size(),
                                     _encryption_key, iv)) {
            crypto_wipe(padded.data(), padded.size());
            return {};
        }
        crypto_wipe(padded.data(), padded.size());

        // Build token: IV + ciphertext + HMAC
        std::vector<uint8_t> token(16 + ct_len + 32);
        memcpy(token.data(), iv, 16);
        memcpy(token.data() + 16, ciphertext.data(), ct_len);

        // HMAC over (IV + ciphertext)
        if (!rns_hmac_sha256(_signing_key, 32,
                             token.data(), 16 + ct_len,
                             token.data() + 16 + ct_len)) {
            return {};
        }

        return token;
    }

    /** Decrypt and authenticate a Fernet token, including valid empty plaintexts. */
    bool decrypt(const uint8_t* token, size_t token_len, std::vector<uint8_t>& plaintext) const {
        plaintext.clear();
        if (!_valid || !token) return false;
        // Minimum: IV(16) + one_block(16) + HMAC(32) = 64
        if (token_len < 64) return false;

        const uint8_t* iv = token;
        size_t ct_len = token_len - 16 - 32;
        if ((ct_len % 16) != 0) return false;
        const uint8_t* ciphertext = token + 16;
        const uint8_t* received_hmac = token + 16 + ct_len;

        // Verify HMAC
        uint8_t expected_hmac[32];
        if (!rns_hmac_sha256(_signing_key, 32, token, 16 + ct_len, expected_hmac)) {
            return false;
        }

        // Constant-time comparison
        const bool authenticated = crypto_verify32(received_hmac, expected_hmac) == 0;
        crypto_wipe(expected_hmac, sizeof(expected_hmac));
        if (!authenticated) return false;

        // Decrypt
        std::vector<uint8_t> plaintext_padded(ct_len);
        size_t pt_len = 0;
        if (!rns_aes256_cbc_decrypt(plaintext_padded.data(), pt_len,
                                     ciphertext, ct_len,
                                     _encryption_key, iv)) {
            return false;
        }

        // PKCS7 unpad
        size_t unpadded_len = 0;
        if (!rns_pkcs7_unpad(plaintext_padded.data(), pt_len, unpadded_len)) {
            crypto_wipe(plaintext_padded.data(), plaintext_padded.size());
            return false;
        }

        plaintext.assign(plaintext_padded.begin(), plaintext_padded.begin() + unpadded_len);
        crypto_wipe(plaintext_padded.data(), plaintext_padded.size());
        return true;
    }

    /** Compatibility wrapper; an empty vector can represent success or failure. */
    std::vector<uint8_t> decrypt(const uint8_t* token, size_t token_len) const {
        std::vector<uint8_t> plaintext;
        (void)decrypt(token, token_len, plaintext);
        return plaintext;
    }

private:
    bool _valid;
    uint8_t _signing_key[32];
    uint8_t _encryption_key[32];
};

#endif // RNS_FERNET_H
