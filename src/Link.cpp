#include "Link.h"
#include "LinkManager.h"
#include "RNSCrypto.h"
#include "ReticulumPacket.h"
#include "Utils.h"
#include <Arduino.h>

// ========================================================================
// Signalling byte helpers (matches RNS/Link.py signalling_bytes / mode_from_*)
// ========================================================================

void RNSLink::buildSignallingBytes(uint8_t out[3], uint32_t mtu, uint8_t mode) {
    // signalling_value = (mtu & 0x1FFFFF) + (((mode<<5) & 0xE0) << 16)
    uint32_t val = (mtu & 0x1FFFFF) | (((uint32_t)(mode << 5) & 0xE0) << 16);
    // Pack as 4-byte big-endian then take last 3 bytes
    out[0] = (val >> 16) & 0xFF;
    out[1] = (val >> 8) & 0xFF;
    out[2] = val & 0xFF;
}

uint32_t RNSLink::mtuFromSignalling(const uint8_t sig[3]) {
    uint32_t val = ((uint32_t)sig[0] << 16) | ((uint32_t)sig[1] << 8) | sig[2];
    return val & 0x1FFFFF;
}

uint8_t RNSLink::modeFromSignalling(const uint8_t sig[3]) {
    uint32_t val = ((uint32_t)sig[0] << 16) | ((uint32_t)sig[1] << 8) | sig[2];
    return (uint8_t)((val >> 16) & 0xE0) >> 5;
}

// ========================================================================
// Constructors
// ========================================================================

// RESPONDER constructor: created when an incoming LINKREQUEST is received
RNSLink::RNSLink(const uint8_t link_id[16],
                  const uint8_t peer_pub[32],
                  const uint8_t peer_sig_pub[32],
                  LinkManager& owner,
                  RNSCrypto& identity)
    : _initiator(false), _state(RNSLinkState::PENDING),
      _ownerRef(owner), _identityRef(identity),
      _keys_derived(false), _mtu(RNS_MTU),
      _mode(RNS_LINK_MODE_AES256_CBC),
      _lastActivityTime(millis()), _requestTime(millis()),
      _rtt(0), _rttMeasured(false)
{
    memcpy(_link_id, link_id, 16);
    memcpy(_peer_x25519_pub, peer_pub, 32);
    memcpy(_peer_sig_pub, peer_sig_pub, 32);
    memset(_dest_hash, 0, 16);
    memset(_dest_pub_key, 0, 64);

    // Generate ephemeral X25519 keypair for this link
    esp_fill_random(_x25519_priv, 32);
    crypto_x25519_public_key(_x25519_pub, _x25519_priv);

    // Responder uses node identity's Ed25519 key for signing the proof
    // (stored in _sig_priv/_sig_pub but populated from RNSCrypto)
    // We'll use _identityRef.sign() directly in prove()
    memset(_sig_priv, 0, 64);
    memset(_sig_pub, 0, 32);
    memset(_sig_seed, 0, 32);
}

// INITIATOR constructor: created when we want to establish a link to a destination
RNSLink::RNSLink(const uint8_t dest_hash[16],
                  const uint8_t dest_pub_key[64],
                  LinkManager& owner,
                  RNSCrypto& identity)
    : _initiator(true), _state(RNSLinkState::PENDING),
      _ownerRef(owner), _identityRef(identity),
      _keys_derived(false), _mtu(RNS_MTU),
      _mode(RNS_LINK_MODE_AES256_CBC),
      _lastActivityTime(millis()), _requestTime(0),
      _rtt(0), _rttMeasured(false)
{
    memset(_link_id, 0, 16);  // Will be computed after packing
    memset(_peer_x25519_pub, 0, 32);
    memset(_peer_sig_pub, 0, 32);
    memcpy(_dest_hash, dest_hash, 16);
    memcpy(_dest_pub_key, dest_pub_key, 64);

    // Generate ephemeral X25519 keypair
    esp_fill_random(_x25519_priv, 32);
    crypto_x25519_public_key(_x25519_pub, _x25519_priv);

    // Generate ephemeral Ed25519 keypair (initiator uses random, not node identity)
    esp_fill_random(_sig_seed, 32);
    crypto_eddsa_key_pair(_sig_priv, _sig_pub, _sig_seed);
}

RNSLink::~RNSLink() {
    wipeKeys();
}

void RNSLink::wipeKeys() {
    crypto_wipe(_x25519_priv, 32);
    crypto_wipe(_sig_priv, 64);
    crypto_wipe(_sig_seed, 32);
    crypto_wipe(_derived_key, 64);
    _keys_derived = false;
}

// ========================================================================
// Initiator: establish() — send LINKREQUEST packet
// ========================================================================

bool RNSLink::establish() {
    if (!_initiator) return false;
    if (_state != RNSLinkState::PENDING) return false;

    // Build link request data: [X25519_pub 32][Ed25519_sig_pub 32][signalling 3]
    uint8_t request_data[RNS_LINK_REQUEST_SIZE];
    memcpy(request_data, _x25519_pub, 32);
    memcpy(request_data + 32, _sig_pub, 32);
    buildSignallingBytes(request_data + 64, _mtu, _mode);

    // Serialize as LINKREQUEST packet (packet_type=0x02)
    std::vector<uint8_t> payload(request_data, request_data + RNS_LINK_REQUEST_SIZE);
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t len = 0;
    bool ok = ReticulumPacket::serialize(buffer, len,
        _dest_hash,
        RNS_PACKET_LINKREQ,    // packet_type = LINKREQUEST
        RNS_DEST_SINGLE,       // destination type
        RNS_PROPAGATION_BROADCAST,
        RNS_CONTEXT_NONE,      // No special context for LINKREQUEST
        0,                     // hops
        payload);

    if (!ok) {
        DebugSerial.println("! RNSLink::establish: serialize failed");
        _state = RNSLinkState::CLOSED;
        return false;
    }

    // Compute link_id from the hashable part of the raw packet
    // hashable_part = (flags & 0x0F) + raw[2:]  (for Header Type 1)
    // But we must strip the MTU signalling bytes from the end
    // link_id = truncated_hash(hashable_part without last LINK_MTU_SIZE bytes)
    {
        uint8_t hashable[MAX_PACKET_SIZE];
        hashable[0] = buffer[0] & 0x0F;  // flags masked
        size_t hashable_len = 1 + (len - 2);  // skip flags+hops bytes
        memcpy(hashable + 1, buffer + 2, len - 2);

        // Strip MTU signalling (last 3 bytes of data = last LINK_MTU_SIZE of hashable)
        hashable_len -= RNS_LINK_MTU_SIZE;

        RNSIdentity::truncated_hash(hashable, hashable_len, _link_id);
    }

    _ownerRef.sendPacketRaw(buffer, len, _dest_hash);
    _requestTime = millis();
    updateActivity();

    DebugSerial.print("[Link] LINKREQUEST sent, link_id=");
    Utils::printBytes(_link_id, 16, DebugSerial);
    DebugSerial.println();

    return true;
}

// ========================================================================
// Handshake: X25519 ECDH + HKDF key derivation
// ========================================================================

bool RNSLink::handshake() {
    if (_state != RNSLinkState::PENDING) return false;
    _state = RNSLinkState::HANDSHAKE;

    // X25519 ECDH: shared_key = X25519(our_priv, peer_pub)
    uint8_t shared_key[32];
    crypto_x25519(shared_key, _x25519_priv, _peer_x25519_pub);

    // Derive 64-byte key via HKDF-SHA256
    // salt = link_id, context = empty (matching RNS get_salt()/get_context())
    if (!rns_hkdf_sha256(_derived_key, 64,
                         shared_key, 32,
                         _link_id, 16,
                         nullptr, 0)) {
        DebugSerial.println("! RNSLink::handshake: HKDF failed");
        crypto_wipe(shared_key, 32);
        _state = RNSLinkState::CLOSED;
        return false;
    }

    crypto_wipe(shared_key, 32);

    // Initialize Fernet token with derived key
    _token.init(_derived_key);
    _keys_derived = true;

    return true;
}

// ========================================================================
// Responder: prove() — sign and send link proof
// ========================================================================

bool RNSLink::prove() {
    if (!_keys_derived) return false;

    // Build signed_data: link_id + our_x25519_pub + our_sig_pub + signalling_bytes
    uint8_t signalling[3];
    buildSignallingBytes(signalling, _mtu, _mode);

    // Responder's sig_pub = node identity's Ed25519 public key
    const uint8_t* our_sig_pub = _identityRef.getPublicKey() + 32;  // Ed25519 pub is second 32 bytes

    size_t signed_len = 16 + 32 + 32 + 3;  // link_id + pub + sig_pub + signalling
    uint8_t signed_data[16 + 32 + 32 + 3];
    size_t off = 0;
    memcpy(signed_data + off, _link_id, 16); off += 16;
    memcpy(signed_data + off, _x25519_pub, 32); off += 32;
    memcpy(signed_data + off, our_sig_pub, 32); off += 32;
    memcpy(signed_data + off, signalling, 3);

    // Sign with node identity's Ed25519 key
    uint8_t signature[64];
    _identityRef.sign(signature, signed_data, signed_len);

    // Proof payload: [signature 64][X25519_pub 32][signalling 3] = 99 bytes
    uint8_t proof_data[RNS_LINK_PROOF_SIZE];
    memcpy(proof_data, signature, 64);
    memcpy(proof_data + 64, _x25519_pub, 32);
    memcpy(proof_data + 96, signalling, 3);

    // Send as PROOF packet with LRPROOF context
    // For LRPROOF, destination field = link_id, dest_type = LINK
    std::vector<uint8_t> payload(proof_data, proof_data + RNS_LINK_PROOF_SIZE);
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t len = 0;
    bool ok = ReticulumPacket::serialize(buffer, len,
        _link_id,                // destination = link_id
        RNS_PACKET_PROOF,       // packet_type = PROOF
        RNS_DEST_LINK,          // dest_type = LINK
        RNS_PROPAGATION_BROADCAST,
        RNS_CONTEXT_LRPROOF,    // context = LRPROOF (0xFF)
        0,
        payload);

    if (!ok) {
        DebugSerial.println("! RNSLink::prove: serialize failed");
        return false;
    }

    _ownerRef.sendPacketRaw(buffer, len, _link_id);
    updateActivity();
    DebugSerial.println("[Link] LRPROOF sent");
    return true;
}

// ========================================================================
// Initiator: validateProof() — verify proof and complete handshake
// ========================================================================

bool RNSLink::validateProof(const uint8_t* proof_data, size_t proof_len,
                             const uint8_t* /*raw_packet*/, size_t /*raw_len*/) {
    if (!_initiator || _state != RNSLinkState::PENDING) return false;

    // Proof can be 64+32 = 96 bytes (no signalling) or 64+32+3 = 99 bytes
    if (proof_len != RNS_LINK_PROOF_SIZE && proof_len != 96) {
        DebugSerial.print("! RNSLink::validateProof: invalid proof size ");
        DebugSerial.println(proof_len);
        return false;
    }

    const uint8_t* signature = proof_data;
    const uint8_t* peer_pub_bytes = proof_data + 64;

    // Check mode from signalling if present
    uint8_t signalling[3] = {0};
    if (proof_len == RNS_LINK_PROOF_SIZE) {
        memcpy(signalling, proof_data + 96, 3);
        uint8_t proof_mode = modeFromSignalling(signalling);
        if (proof_mode != _mode) {
            DebugSerial.println("! RNSLink::validateProof: mode mismatch");
            return false;
        }
        uint32_t confirmed_mtu = mtuFromSignalling(signalling);
        if (confirmed_mtu > 0 && confirmed_mtu <= RNS_MTU) {
            _mtu = confirmed_mtu;
        }
    }

    // Load peer keys
    memcpy(_peer_x25519_pub, peer_pub_bytes, 32);
    // Peer sig pub = destination's Ed25519 public key (second 32 bytes of dest_pub_key)
    memcpy(_peer_sig_pub, _dest_pub_key + 32, 32);

    // Perform ECDH handshake
    if (!handshake()) return false;

    // Verify signature: signed_data = link_id + peer_x25519_pub + peer_sig_pub + signalling
    uint8_t signed_data[16 + 32 + 32 + 3];
    size_t signed_len = 16 + 32 + 32;
    size_t off = 0;
    memcpy(signed_data + off, _link_id, 16); off += 16;
    memcpy(signed_data + off, _peer_x25519_pub, 32); off += 32;
    memcpy(signed_data + off, _peer_sig_pub, 32); off += 32;
    if (proof_len == RNS_LINK_PROOF_SIZE) {
        memcpy(signed_data + off, signalling, 3);
        signed_len += 3;
    }

    // Verify with destination's Ed25519 public key
    if (!RNSCrypto::verify(signature, _peer_sig_pub, signed_data, signed_len)) {
        DebugSerial.println("! RNSLink::validateProof: signature verification failed");
        _state = RNSLinkState::CLOSED;
        return false;
    }

    // Link is now ACTIVE
    _state = RNSLinkState::ACTIVE;
    _rtt = (float)(millis() - _requestTime) / 1000.0f;
    _rttMeasured = true;
    updateActivity();

    DebugSerial.print("[Link] ACTIVE, RTT=");
    DebugSerial.print(_rtt * 1000.0f, 1);
    DebugSerial.println("ms");

    // Send LRRTT packet: msgpack-encoded float RTT
    // Simple msgpack float32: [0xCA][4 bytes big-endian IEEE 754]
    uint8_t rtt_data[5];
    rtt_data[0] = 0xCA;  // msgpack float32 tag
    union { float f; uint32_t u; } conv;
    conv.f = _rtt;
    rtt_data[1] = (conv.u >> 24) & 0xFF;
    rtt_data[2] = (conv.u >> 16) & 0xFF;
    rtt_data[3] = (conv.u >> 8) & 0xFF;
    rtt_data[4] = conv.u & 0xFF;

    std::vector<uint8_t> rtt_payload(rtt_data, rtt_data + 5);
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t len = 0;
    ReticulumPacket::serialize(buffer, len,
        _link_id,
        RNS_PACKET_DATA,
        RNS_DEST_LINK,
        RNS_PROPAGATION_BROADCAST,
        RNS_CONTEXT_LRRTT,
        0,
        rtt_payload);

    _ownerRef.sendPacketRaw(buffer, len, _link_id);

    return true;
}

// ========================================================================
// Responder: handleRTT() — process RTT packet from initiator
// ========================================================================

void RNSLink::handleRTT(const uint8_t* data, size_t len) {
    if (_state != RNSLinkState::HANDSHAKE) return;

    // Decode msgpack float32: [0xCA][4 bytes]
    if (len >= 5 && data[0] == 0xCA) {
        union { float f; uint32_t u; } conv;
        conv.u = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                 ((uint32_t)data[3] << 8) | data[4];
        _rtt = conv.f;
        _rttMeasured = true;
    }
    // Also accept msgpack float64: [0xCB][8 bytes] — truncate to float
    else if (len >= 9 && data[0] == 0xCB) {
        // Read as double, store as float
        uint64_t u64 = 0;
        for (int i = 0; i < 8; i++) u64 = (u64 << 8) | data[1 + i];
        union { double d; uint64_t u; } dconv;
        dconv.u = u64;
        _rtt = (float)dconv.d;
        _rttMeasured = true;
    }

    _state = RNSLinkState::ACTIVE;
    updateActivity();
    DebugSerial.print("[Link] ACTIVE (responder), RTT=");
    DebugSerial.print(_rtt * 1000.0f, 1);
    DebugSerial.println("ms");
}

// ========================================================================
// Encryption / Decryption for link data
// ========================================================================

std::vector<uint8_t> RNSLink::encrypt(const uint8_t* plaintext, size_t len) {
    if (!_keys_derived) return {};
    return _token.encrypt(plaintext, len);
}

std::vector<uint8_t> RNSLink::decrypt(const uint8_t* ciphertext, size_t len) {
    if (!_keys_derived) return {};
    return _token.decrypt(ciphertext, len);
}

void RNSLink::sign(uint8_t signature[64], const uint8_t* message, size_t msg_len) {
    // Link signing uses the Ed25519 key associated with this link's role
    if (_initiator) {
        crypto_eddsa_sign(signature, _sig_priv, message, msg_len);
    } else {
        _identityRef.sign(signature, message, msg_len);
    }
}

// ========================================================================
// Send encrypted data over active link
// ========================================================================

bool RNSLink::sendData(const uint8_t* data, size_t len) {
    if (_state != RNSLinkState::ACTIVE || !_keys_derived) {
        DebugSerial.println("! RNSLink::sendData: link not active");
        return false;
    }

    auto encrypted = encrypt(data, len);
    if (encrypted.empty()) {
        DebugSerial.println("! RNSLink::sendData: encryption failed");
        return false;
    }

    // Send as DATA packet with NONE context, dest_type=LINK, destination=link_id
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t pkt_len = 0;
    bool ok = ReticulumPacket::serialize(buffer, pkt_len,
        _link_id,
        RNS_PACKET_DATA,
        RNS_DEST_LINK,
        RNS_PROPAGATION_BROADCAST,
        RNS_CONTEXT_NONE,
        0,
        encrypted);

    if (!ok) {
        DebugSerial.println("! RNSLink::sendData: serialize failed");
        return false;
    }

    _ownerRef.sendPacketRaw(buffer, pkt_len, _link_id);
    updateActivity();
    return true;
}

// ========================================================================
// Handle incoming packets on this link
// ========================================================================

void RNSLink::handlePacket(const RnsPacketInfo& packetInfo) {
    if (!packetInfo.valid) return;
    updateActivity();

    uint8_t ctx = packetInfo.context;

    switch (_state) {
        case RNSLinkState::PENDING:
            // Initiator waiting for proof
            if (ctx == RNS_CONTEXT_LRPROOF && packetInfo.packet_type == RNS_PACKET_PROOF) {
                validateProof(packetInfo.data.data(), packetInfo.data.size(), nullptr, 0);
            }
            break;

        case RNSLinkState::HANDSHAKE:
            // Responder waiting for LRRTT
            if (ctx == RNS_CONTEXT_LRRTT) {
                handleRTT(packetInfo.data.data(), packetInfo.data.size());
            }
            break;

        case RNSLinkState::ACTIVE: {
            if (ctx == RNS_CONTEXT_NONE && packetInfo.packet_type == RNS_PACKET_DATA) {
                // Encrypted data — decrypt and deliver
                auto plaintext = decrypt(packetInfo.data.data(), packetInfo.data.size());
                if (!plaintext.empty()) {
                    _ownerRef.processReceivedLinkData(_link_id, plaintext);
                }
            }
            else if (ctx == RNS_CONTEXT_LINKCLOSE) {
                // Peer closing link — decrypt and verify it contains our link_id
                auto plaintext = decrypt(packetInfo.data.data(), packetInfo.data.size());
                if (!plaintext.empty() && plaintext.size() >= 16 &&
                    memcmp(plaintext.data(), _link_id, 16) == 0) {
                    _state = RNSLinkState::CLOSED;
                    wipeKeys();
                    DebugSerial.println("[Link] Closed by peer");
                }
            }
            else if (ctx == RNS_CONTEXT_KEEPALIVE) {
                // Keepalive: if 0xFF from initiator, respond with 0xFE
                if (!_initiator && packetInfo.data.size() == 1 && packetInfo.data[0] == 0xFF) {
                    uint8_t resp = 0xFE;
                    std::vector<uint8_t> kp = {resp};
                    uint8_t buffer[MAX_PACKET_SIZE];
                    size_t len = 0;
                    ReticulumPacket::serialize(buffer, len, _link_id,
                        RNS_PACKET_DATA, RNS_DEST_LINK, RNS_PROPAGATION_BROADCAST,
                        RNS_CONTEXT_KEEPALIVE, 0, kp);
                    _ownerRef.sendPacketRaw(buffer, len, _link_id);
                }
            }
            else if (ctx == RNS_CONTEXT_LRRTT) {
                // RTT measurement in active state (from initiator → responder)
                handleRTT(packetInfo.data.data(), packetInfo.data.size());
            }
            break;
        }

        case RNSLinkState::STALE:
        case RNSLinkState::CLOSED:
            break;
    }
}

// ========================================================================
// Close link
// ========================================================================

void RNSLink::close(bool notifyPeer) {
    if (_state == RNSLinkState::CLOSED) return;

    if (notifyPeer && _state == RNSLinkState::ACTIVE && _keys_derived) {
        // Send encrypted link_id as LINKCLOSE
        auto encrypted = encrypt(_link_id, 16);
        if (!encrypted.empty()) {
            uint8_t buffer[MAX_PACKET_SIZE];
            size_t len = 0;
            ReticulumPacket::serialize(buffer, len, _link_id,
                RNS_PACKET_DATA, RNS_DEST_LINK, RNS_PROPAGATION_BROADCAST,
                RNS_CONTEXT_LINKCLOSE, 0, encrypted);
            _ownerRef.sendPacketRaw(buffer, len, _link_id);
        }
    }

    _state = RNSLinkState::CLOSED;
    wipeKeys();
    DebugSerial.println("[Link] Closed");
}

// ========================================================================
// Timeout handling
// ========================================================================

void RNSLink::checkTimeouts() {
    if (_state == RNSLinkState::CLOSED) return;

    unsigned long now = millis();

    if (_state == RNSLinkState::PENDING || _state == RNSLinkState::HANDSHAKE) {
        // Establishment timeout
        unsigned long timeout = RNS_LINK_EST_TIMEOUT_PER_HOP_MS + RNS_LINK_KEEPALIVE_MIN_MS;
        if (now - _requestTime > timeout) {
            DebugSerial.println("! RNSLink: establishment timed out");
            _state = RNSLinkState::CLOSED;
            wipeKeys();
        }
    }
    else if (_state == RNSLinkState::ACTIVE) {
        // Inactivity timeout
        if (now - _lastActivityTime > LINK_INACTIVITY_TIMEOUT_MS) {
            DebugSerial.println("! RNSLink: inactivity timeout");
            close(true);
        }
    }
}
