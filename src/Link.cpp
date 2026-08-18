#include "Link.h"
#include "LinkManager.h"
#include "RNSCrypto.h"
#include "ReticulumPacket.h"
#include "Utils.h"
#include <Arduino.h>
#include <cmath>

namespace {
bool isAllZero(const uint8_t* value, size_t len) {
    uint8_t combined = 0;
    for (size_t i = 0; i < len; ++i) combined |= value[i];
    return combined == 0;
}
}

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

bool RNSLink::encodeRttPayload(float rtt, uint8_t out[5]) {
    if (!out || !std::isfinite(rtt) || rtt < 0.0f || rtt > 3600.0f) return false;
    out[0] = 0xCA;
    union { float f; uint32_t u; } conv;
    conv.f = rtt;
    out[1] = (conv.u >> 24) & 0xFF;
    out[2] = (conv.u >> 16) & 0xFF;
    out[3] = (conv.u >> 8) & 0xFF;
    out[4] = conv.u & 0xFF;
    return true;
}

bool RNSLink::decodeRttPayload(const uint8_t* data, size_t len, float& rtt) {
    if (!data) return false;
    if (len == 5 && data[0] == 0xCA) {
        union { float f; uint32_t u; } conv;
        conv.u = ((uint32_t)data[1] << 24) | ((uint32_t)data[2] << 16) |
                 ((uint32_t)data[3] << 8) | data[4];
        rtt = conv.f;
    } else if (len == 9 && data[0] == 0xCB) {
        uint64_t u64 = 0;
        for (int i = 0; i < 8; i++) u64 = (u64 << 8) | data[1 + i];
        union { double d; uint64_t u; } dconv;
        dconv.u = u64;
        rtt = static_cast<float>(dconv.d);
    } else {
        return false;
    }
    return std::isfinite(rtt) && rtt >= 0.0f && rtt <= 3600.0f;
}

bool RNSLink::verifyProofSignature(const uint8_t* proof_data, size_t proof_len,
                                   const uint8_t link_id[16],
                                   const uint8_t expected_sig_pub[32],
                                   uint8_t peer_x25519_pub[32],
                                   uint8_t signalling[3],
                                   bool& has_signalling) {
    has_signalling = false;
    if (!proof_data || !link_id || !expected_sig_pub || !peer_x25519_pub || !signalling) {
        return false;
    }
    if (proof_len != RNS_LINK_PROOF_SIZE && proof_len != 96) return false;

    memcpy(peer_x25519_pub, proof_data + 64, 32);
    memset(signalling, 0, 3);
    if (proof_len == RNS_LINK_PROOF_SIZE) {
        memcpy(signalling, proof_data + 96, 3);
        has_signalling = true;
    }

    uint8_t signed_data[16 + 32 + 32 + 3];
    size_t signed_len = 16 + 32 + 32;
    memcpy(signed_data, link_id, 16);
    memcpy(signed_data + 16, peer_x25519_pub, 32);
    memcpy(signed_data + 48, expected_sig_pub, 32);
    if (has_signalling) {
        memcpy(signed_data + 80, signalling, 3);
        signed_len += 3;
    }
    return RNSCrypto::verify(proof_data, expected_sig_pub, signed_data, signed_len);
}

// ========================================================================
// Constructors
// ========================================================================

// RESPONDER constructor: created when an incoming LINKREQUEST is received
RNSLink::RNSLink(const uint8_t link_id[16],
                  const uint8_t peer_pub[32],
                  const uint8_t peer_sig_pub[32],
                  InterfaceType attachedInterface,
                  LinkManager& owner,
                  RNSCrypto& identity)
    : _initiator(false), _state(RNSLinkState::PENDING),
      _ownerRef(owner), _identityRef(identity),
      _keys_derived(false), _mtu(RNS_MTU),
      _mode(RNS_LINK_MODE_AES256_CBC),
      _attachedInterface(attachedInterface),
      _lastActivityTime(millis()), _requestTime(millis()),
      _lastKeepaliveTime(millis()),
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
      _attachedInterface(InterfaceType::UNKNOWN),
      _lastActivityTime(millis()), _requestTime(0),
      _lastKeepaliveTime(0),
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
    crypto_ed25519_key_pair(_sig_priv, _sig_pub, _sig_seed);
}

RNSLink::~RNSLink() {
    wipeKeys();
}

void RNSLink::wipeKeys() {
    crypto_wipe(_x25519_priv, 32);
    crypto_wipe(_sig_priv, 64);
    crypto_wipe(_sig_seed, 32);
    crypto_wipe(_derived_key, 64);
    _token.clear();
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

    if (!_ownerRef.sendPacketRaw(buffer, len, _dest_hash, _attachedInterface)) {
        DebugSerial.println("! RNSLink::establish: transport rejected packet");
        _state = RNSLinkState::CLOSED;
        return false;
    }
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
    if (isAllZero(shared_key, sizeof(shared_key))) {
        DebugSerial.println("! RNSLink::handshake: invalid peer key");
        crypto_wipe(shared_key, sizeof(shared_key));
        _state = RNSLinkState::CLOSED;
        return false;
    }

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

bool RNSLink::setMtu(uint32_t mtu) {
    static constexpr uint32_t MIN_LINK_MTU = RNS_HEADER_1_SIZE + RNS_LINK_PROOF_SIZE;
    if (_state != RNSLinkState::PENDING || mtu < MIN_LINK_MTU || mtu > RNS_MTU) return false;
    _mtu = mtu;
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

    if (!_ownerRef.sendPacketRaw(buffer, len, _link_id, _attachedInterface)) {
        DebugSerial.println("! RNSLink::prove: transport rejected packet");
        return false;
    }
    updateActivity();
    DebugSerial.println("[Link] LRPROOF sent");
    return true;
}

// ========================================================================
// Initiator: validateProof() — verify proof and complete handshake
// ========================================================================

bool RNSLink::validateProof(const uint8_t* proof_data, size_t proof_len,
                            InterfaceType incomingInterface) {
    if (!_initiator || _state != RNSLinkState::PENDING || !proof_data) return false;

    memcpy(_peer_sig_pub, _dest_pub_key + 32, 32);
    uint8_t signalling[3] = {0};
    bool hasSignalling = false;
    if (!verifyProofSignature(proof_data, proof_len, _link_id, _peer_sig_pub,
                              _peer_x25519_pub, signalling, hasSignalling)) {
        DebugSerial.println("! RNSLink::validateProof: signature verification failed");
        _state = RNSLinkState::CLOSED;
        return false;
    }

    if (hasSignalling) {
        if (modeFromSignalling(signalling) != _mode) {
            DebugSerial.println("! RNSLink::validateProof: mode mismatch");
            _state = RNSLinkState::CLOSED;
            return false;
        }
        if (!setMtu(mtuFromSignalling(signalling))) {
            DebugSerial.println("! RNSLink::validateProof: invalid negotiated MTU");
            _state = RNSLinkState::CLOSED;
            return false;
        }
    }

    _attachedInterface = incomingInterface;

    // Only derive session keys after authenticating the responder proof.
    if (!handshake()) return false;

    _state = RNSLinkState::ACTIVE;
    _rtt = (float)(millis() - _requestTime) / 1000.0f;
    _rttMeasured = true;
    _lastKeepaliveTime = millis();
    updateActivity();

    DebugSerial.print("[Link] ACTIVE, RTT=");
    DebugSerial.print(_rtt * 1000.0f, 1);
    DebugSerial.println("ms");

    uint8_t rtt_data[5];
    if (!encodeRttPayload(_rtt, rtt_data) ||
        !sendEncryptedContext(RNS_CONTEXT_LRRTT, rtt_data, sizeof(rtt_data))) {
        close(false);
        return false;
    }

    return true;
}

// ========================================================================
// Responder: handleRTT() — process RTT packet from initiator
// ========================================================================

bool RNSLink::handleRTT(const uint8_t* data, size_t len) {
    if (_state != RNSLinkState::HANDSHAKE || !data) return false;

    std::vector<uint8_t> plaintext;
    if (!unwrapLinkPayload(data, len, plaintext)) return false;
    if (!decodeRttPayload(plaintext.data(), plaintext.size(), _rtt)) return false;

    _rttMeasured = true;
    _state = RNSLinkState::ACTIVE;
    _lastKeepaliveTime = millis();
    updateActivity();
    DebugSerial.print("[Link] ACTIVE (responder), RTT=");
    DebugSerial.print(_rtt * 1000.0f, 1);
    DebugSerial.println("ms");
    return true;
}

bool RNSLink::sendEncryptedContext(uint8_t context, const uint8_t* plaintext, size_t len) {
    if (!_keys_derived || !plaintext || len == 0) return false;
    auto encrypted = encrypt(plaintext, len);
    if (encrypted.empty()) return false;
    if (RNS_HEADER_1_SIZE + encrypted.size() > _mtu) return false;

    uint8_t buffer[MAX_PACKET_SIZE];
    size_t pkt_len = 0;
    if (!ReticulumPacket::serialize(buffer, pkt_len, _link_id,
                                    RNS_PACKET_DATA, RNS_DEST_LINK,
                                    RNS_PROPAGATION_BROADCAST, context, 0,
                                    encrypted)) {
        return false;
    }
    return _ownerRef.sendPacketRaw(buffer, pkt_len, _link_id, _attachedInterface);
}

bool RNSLink::unwrapLinkPayload(const uint8_t* data, size_t len, std::vector<uint8_t>& plaintext) const {
    plaintext.clear();
    return data && decrypt(data, len, plaintext);
}

bool RNSLink::sendKeepalive() {
    if (_state != RNSLinkState::ACTIVE || !_keys_derived || !_initiator) return false;
    const uint8_t request = 0xFF;
    if (!sendEncryptedContext(RNS_CONTEXT_KEEPALIVE, &request, 1)) return false;
    _lastKeepaliveTime = millis();
    return true;
}

// ========================================================================
// Encryption / Decryption for link data
// ========================================================================

std::vector<uint8_t> RNSLink::encrypt(const uint8_t* plaintext, size_t len) {
    if (!_keys_derived) return {};
    return _token.encrypt(plaintext, len);
}

std::vector<uint8_t> RNSLink::decrypt(const uint8_t* ciphertext, size_t len) {
    std::vector<uint8_t> plaintext;
    (void)decrypt(ciphertext, len, plaintext);
    return plaintext;
}

bool RNSLink::decrypt(const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& plaintext) const {
    plaintext.clear();
    return _keys_derived && _token.decrypt(ciphertext, len, plaintext);
}

void RNSLink::sign(uint8_t signature[64], const uint8_t* message, size_t msg_len) {
    // Link signing uses the Ed25519 key associated with this link's role
    if (_initiator) {
        crypto_ed25519_sign(signature, _sig_priv, message, msg_len);
    } else {
        _identityRef.sign(signature, message, msg_len);
    }
}

// ========================================================================
// Send encrypted data over active link
// ========================================================================

bool RNSLink::sendData(const uint8_t* data, size_t len, bool confirm) {
    if (_state != RNSLinkState::ACTIVE || !_keys_derived) {
        DebugSerial.println("! RNSLink::sendData: link not active");
        return false;
    }
    if (!data || len == 0) return false;
    if (confirm && _inflight.active) {
        DebugSerial.println("! RNSLink::sendData: waiting for previous proof");
        return false;
    }

    auto encrypted = encrypt(data, len);
    if (encrypted.empty()) {
        DebugSerial.println("! RNSLink::sendData: encryption failed");
        return false;
    }
    if (RNS_HEADER_1_SIZE + encrypted.size() > _mtu) {
        DebugSerial.println("! RNSLink::sendData: payload exceeds negotiated MTU");
        return false;
    }

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

    if (!_ownerRef.sendPacketRaw(buffer, pkt_len, _link_id, _attachedInterface)) {
        DebugSerial.println("! RNSLink::sendData: transport rejected packet");
        return false;
    }
    updateActivity();

    if (confirm) {
        _inflight.active = true;
        memcpy(_inflight.wire, buffer, pkt_len);
        _inflight.wireLen = pkt_len;
        RNSIdentity::packet_hash(buffer, pkt_len, _inflight.hash);
        _inflight.attempts = 1;
        _inflight.nextTryMs = millis() + LINK_DATA_RETRY_MS;
    }
    return true;
}

void RNSLink::sendDataProof(const uint8_t packetHash[32]) {
    if (!packetHash || _state != RNSLinkState::ACTIVE) return;
    sendEncryptedContext(RNS_CONTEXT_LINKPROOF, packetHash, 32);
}

void RNSLink::handleDataProof(const uint8_t* data, size_t len) {
    std::vector<uint8_t> plaintext;
    if (!unwrapLinkPayload(data, len, plaintext) || plaintext.size() != 32) return;
    if (_inflight.active && memcmp(plaintext.data(), _inflight.hash, 32) == 0) {
        _inflight.active = false;
        _inflight.wireLen = 0;
        updateActivity();
        DebugSerial.println("[Link] Delivery proof received");
    }
}

void RNSLink::retryInflight(unsigned long now) {
    if (!_inflight.active || _state != RNSLinkState::ACTIVE) return;
    if (static_cast<long>(now - _inflight.nextTryMs) < 0) return;

    if (_inflight.attempts >= LINK_DATA_MAX_ATTEMPTS) {
        DebugSerial.println("! RNSLink: confirmed send timed out");
        _inflight.active = false;
        return;
    }

    if (_ownerRef.sendPacketRaw(_inflight.wire, _inflight.wireLen, _link_id, _attachedInterface)) {
        _inflight.attempts++;
        _inflight.nextTryMs = now + LINK_DATA_RETRY_MS;
        DebugSerial.print("[Link] Retrying confirmed send, attempt ");
        DebugSerial.println(_inflight.attempts);
    } else {
        _inflight.nextTryMs = now + LINK_DATA_RETRY_MS;
    }
}

// ========================================================================
// Handle incoming packets on this link
// ========================================================================

void RNSLink::handlePacket(const RnsPacketInfo& packetInfo, InterfaceType incomingInterface) {
    if (!packetInfo.valid) return;

    if (_attachedInterface != InterfaceType::UNKNOWN && incomingInterface != _attachedInterface) {
        DebugSerial.println("! Link packet received on unexpected interface");
        return;
    }

    uint8_t ctx = packetInfo.context;

    switch (_state) {
        case RNSLinkState::PENDING:
            // Initiator waiting for proof
            if (ctx == RNS_CONTEXT_LRPROOF && packetInfo.packet_type == RNS_PACKET_PROOF) {
                validateProof(packetInfo.data.data(), packetInfo.data.size(), incomingInterface);
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
                std::vector<uint8_t> plaintext;
                if (decrypt(packetInfo.data.data(), packetInfo.data.size(), plaintext)) {
                    updateActivity();
                    sendDataProof(packetInfo.packet_hash);
                    _ownerRef.processReceivedLinkData(_link_id, plaintext);
                }
            }
            else if (ctx == RNS_CONTEXT_LINKPROOF) {
                handleDataProof(packetInfo.data.data(), packetInfo.data.size());
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
                std::vector<uint8_t> plaintext;
                if (!unwrapLinkPayload(packetInfo.data.data(), packetInfo.data.size(), plaintext) ||
                    plaintext.size() != 1) {
                    break;
                }
                if (!_initiator && plaintext[0] == 0xFF) {
                    updateActivity();
                    const uint8_t resp = 0xFE;
                    sendEncryptedContext(RNS_CONTEXT_KEEPALIVE, &resp, 1);
                } else if (_initiator && plaintext[0] == 0xFE) {
                    updateActivity();
                }
            }
            else if (ctx == RNS_CONTEXT_LRRTT) {
                // RTT measurement in active state (from initiator → responder)
                // Ignore duplicate RTT messages after activation.
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
            if (ReticulumPacket::serialize(buffer, len, _link_id,
                RNS_PACKET_DATA, RNS_DEST_LINK, RNS_PROPAGATION_BROADCAST,
                RNS_CONTEXT_LINKCLOSE, 0, encrypted)) {
                _ownerRef.sendPacketRaw(buffer, len, _link_id, _attachedInterface);
            }
        }
    }

    _state = RNSLinkState::CLOSED;
    _inflight.active = false;
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
        retryInflight(now);
        if (_initiator && (now - _lastKeepaliveTime) >= RNS_LINK_KEEPALIVE_MIN_MS) {
            sendKeepalive();
        }
        if (now - _lastActivityTime > LINK_INACTIVITY_TIMEOUT_MS) {
            DebugSerial.println("! RNSLink: inactivity timeout");
            close(true);
        }
    }
}
