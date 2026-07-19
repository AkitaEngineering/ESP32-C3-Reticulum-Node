#ifndef LINK_H
#define LINK_H

/**
 * RNS-compatible Link protocol implementation.
 *
 * Implements the full Reticulum link establishment handshake:
 *   1. Initiator sends LINKREQUEST: [X25519_pub 32][Ed25519_sig_pub 32][signalling 3]
 *   2. Responder validates, performs X25519 ECDH, derives keys via HKDF-SHA256
 *   3. Responder sends LRPROOF: [signature 64][X25519_pub 32][signalling 3]
 *   4. Initiator validates proof, performs ECDH, derives shared keys
 *   5. Initiator sends LRRTT with msgpacked RTT
 *   6. Link is ACTIVE — all data encrypted with Fernet tokens
 *
 * Link ID = truncated_hash(hashable_part without MTU signalling)
 * Key derivation: HKDF-SHA256(shared_key, salt=link_id, context=empty, length=64)
 * Derived key[0:32] = signing_key, [32:64] = encryption_key
 *
 * Reference: RNS/Link.py
 */

#include <Arduino.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <monocypher.h>
#include <optional/monocypher-ed25519.h>

#include "Config.h"
#include "RNSIdentity.h"
#include "RNSFernet.h"
#include "ReticulumPacket.h"

// Forward declarations
class LinkManager;
class RNSCrypto;

// Link constants (from RNS/Link.py)
static constexpr size_t RNS_LINK_ECPUBSIZE   = 64;  // X25519_pub(32) + Ed25519_sig_pub(32)
static constexpr size_t RNS_LINK_KEYSIZE     = 32;
static constexpr size_t RNS_LINK_MTU_SIZE    = 3;
static constexpr size_t RNS_LINK_SIGBYTES    = 64;

// Link request: [X25519_pub 32][Ed25519_sig_pub 32][signalling 3] = 67 bytes
static constexpr size_t RNS_LINK_REQUEST_SIZE = 32 + 32 + 3;
// Link proof: [signature 64][X25519_pub 32][signalling 3] = 99 bytes
static constexpr size_t RNS_LINK_PROOF_SIZE   = 64 + 32 + 3;

// Link modes
static constexpr uint8_t RNS_LINK_MODE_AES256_CBC = 0x01;

// Establishment timeout per hop
static constexpr unsigned long RNS_LINK_EST_TIMEOUT_PER_HOP_MS = 6000;
static constexpr unsigned long RNS_LINK_KEEPALIVE_MIN_MS       = 15000;

// Link states
enum class RNSLinkState : uint8_t {
    PENDING    = 0x00,
    HANDSHAKE  = 0x01,
    ACTIVE     = 0x02,
    STALE      = 0x03,
    CLOSED     = 0x04
};

class RNSLink {
public:
    /**
     * Create as RESPONDER (incoming link request).
     */
    RNSLink(const uint8_t link_id[16],
            const uint8_t peer_pub[32],
            const uint8_t peer_sig_pub[32],
            InterfaceType attachedInterface,
            LinkManager& owner,
            RNSCrypto& identity);

    /**
     * Create as INITIATOR (outgoing to a known destination).
     */
    RNSLink(const uint8_t dest_hash[16],
            const uint8_t dest_pub_key[64],
            LinkManager& owner,
            RNSCrypto& identity);

    ~RNSLink();

    // --- Core operations ---
    bool establish();
    void handlePacket(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface);
    bool sendData(const uint8_t* data, size_t len);
    void close(bool notifyPeer = true);
    void checkTimeouts();

    // --- State queries ---
    RNSLinkState getState() const { return _state; }
    bool isActive() const { return _state == RNSLinkState::ACTIVE; }
    bool isInitiator() const { return _initiator; }
    const uint8_t* getLinkId() const { return _link_id; }
    unsigned long getLastActivityTime() const { return _lastActivityTime; }

    // --- Crypto operations for link data ---
    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t len);
    std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t len);
    bool decrypt(const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& plaintext);
    void sign(uint8_t signature[64], const uint8_t* message, size_t msg_len);

    // --- Handshake (called by LinkManager) ---
    bool handshake();
    bool prove();
    bool validateProof(const uint8_t* proof_data, size_t proof_len,
                       InterfaceType incomingInterface);
    bool handleRTT(const uint8_t* data, size_t len);
    bool setMtu(uint32_t mtu);

    // --- Signalling helpers (public for LinkManager) ---
    static void buildSignallingBytes(uint8_t out[3], uint32_t mtu, uint8_t mode);
    static uint32_t mtuFromSignalling(const uint8_t sig[3]);
    static uint8_t modeFromSignalling(const uint8_t sig[3]);

private:
    void updateActivity() { _lastActivityTime = millis(); }
    void wipeKeys();

    // Identity
    bool _initiator;
    RNSLinkState _state;
    LinkManager& _ownerRef;
    RNSCrypto& _identityRef;

    // Link identity
    uint8_t _link_id[16];

    // Our ephemeral X25519 keypair (per-link)
    uint8_t _x25519_priv[32];
    uint8_t _x25519_pub[32];

    // Our Ed25519 keypair (initiator: ephemeral; responder: node identity)
    uint8_t _sig_priv[64];
    uint8_t _sig_pub[32];
    uint8_t _sig_seed[32];  // Ed25519 seed for initiator's ephemeral key

    // Peer keys
    uint8_t _peer_x25519_pub[32];
    uint8_t _peer_sig_pub[32];

    // Destination info (initiator only)
    uint8_t _dest_hash[16];
    uint8_t _dest_pub_key[64];

    // Derived keys from HKDF after ECDH
    uint8_t _derived_key[64] = {0};
    bool _keys_derived;

    // Fernet token for link encryption
    RNSToken _token;

    // MTU/mode
    uint32_t _mtu;
    uint8_t _mode;
    InterfaceType _attachedInterface;

    // Timing
    unsigned long _lastActivityTime;
    unsigned long _requestTime;
    float _rtt;
    bool _rttMeasured;
};

#endif // LINK_H
