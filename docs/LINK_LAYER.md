# Reticulum Link Sessions

**Document version:** 3.1
**Updated:** 2026-08-18

This document describes the link functionality that is actually implemented in `Link.cpp` and `LinkManager.cpp`. The implementation establishes authenticated, encrypted Reticulum link sessions. After the proof is verified, LRRTT, keepalive, close, and application data are Fernet-encrypted like reference RNS. It does not currently implement Reticulum resource transfer, packet delivery proofs, request/channel APIs, or automatic data retransmission.

## Establishment

The supported state sequence is:

```text
initiator PENDING --LINKREQUEST--> responder HANDSHAKE
initiator PENDING <--LRPROOF------ responder HANDSHAKE
initiator ACTIVE  --LRRTT--------> responder ACTIVE
ACTIVE ----------LINKCLOSE------> CLOSED
```

1. The initiator sends a `LINKREQUEST` packet containing its ephemeral X25519 public key, ephemeral Ed25519 public key, and three signalling bytes.
2. The link ID is the 16-byte truncated hash of the request's hashable part without the signalling bytes.
3. The responder performs X25519 key agreement and derives 64 bytes with HKDF-SHA256 using the link ID as salt.
4. The responder signs `link_id || responder_x25519_public || responder_ed25519_public || signalling` with its announced identity and returns an `LRPROOF`.
5. The initiator verifies the proof against the destination's announced Ed25519 key before deriving or accepting session keys.
6. The initiator returns an encrypted `LRRTT` MessagePack float and both ends enter `ACTIVE`.

All-zero X25519 shared secrets, unsupported modes, malformed proof sizes, invalid signatures, non-finite RTT values, and invalid MTUs are rejected. A successfully authenticated session is bound to the physical/logical interface on which the handshake completed; subsequent packets arriving through another interface are discarded.

## Wire data

| Operation | Packet type | Destination type | Context | Payload |
|---|---:|---:|---:|---|
| Link request | `LINKREQ` | `SINGLE` | `0x00` | X25519 pub (32), Ed25519 pub (32), signalling (3) |
| Link proof | `PROOF` | `LINK` | `0xFF` | signature (64), responder X25519 pub (32), optional signalling (3) |
| RTT | `DATA` | `LINK` | `0xFE` | Fernet(MessagePack float32/float64) |
| Application data | `DATA` | `LINK` | `0x00` | authenticated encrypted token |
| Keepalive | `DATA` | `LINK` | `0xFA` | Fernet(one byte: `0xFF` request or `0xFE` response) |
| Close | `DATA` | `LINK` | `0xFC` | encrypted 16-byte link ID |

The signalling field encodes the negotiated MTU and mode. This firmware accepts AES-256-CBC mode and an MTU no larger than the 500-byte Reticulum MTU. Outgoing encrypted packets are rejected if their complete Reticulum packet would exceed the negotiated MTU.

Session payloads use the Reticulum Fernet variant:

```text
[IV:16][AES-256-CBC ciphertext, PKCS#7 padded][HMAC-SHA256:32]
```

The first 32 derived bytes are the HMAC key and the next 32 are the AES key. HMAC is checked in constant time before decryption. Valid zero-length plaintext can be authenticated and received, although the local `sendLinkData()` application API intentionally requires non-empty data.

## Lifecycle and limits

- Maximum concurrent sessions: `LINK_MAX_ACTIVE` (10 by default).
- Pending/handshake timeout: 21 seconds with the current constants.
- Active inactivity timeout: 210 seconds with the default announce interval.
- Key material is wiped when a link closes or is destroyed.
- A close notification is authenticated but is not acknowledged.

## Delivery semantics

`sendLinkData(link, data, len)` is still a single encrypted send: a true return means the local transport accepted the packet. `sendLinkData(..., confirm=true)` stores that exact wire frame and retries it until a matching encrypted `LINKPROOF` (32-byte packet hash) arrives or `LINK_DATA_MAX_ATTEMPTS` is exhausted. Receivers emit that proof automatically after a successful decrypt. There is still no multi-packet window, resource transfer, or official RNS delivery-proof/resource protocol. PLAIN mesh chat (`say <text>` on the debug UART) uses a separate application ACK/retry.

Hardware acceptance must exercise link establishment, forged-proof rejection, cross-interface injection rejection, negotiated-MTU rejection, encrypted payload exchange, inactivity cleanup, and link close on two physical devices.
