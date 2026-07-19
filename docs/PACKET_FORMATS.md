# Packet Formats Implemented by the Gateway

**Document version:** 3.0
**Updated:** 2026-07-19

This is an implementation reference, not a replacement for the upstream Reticulum specification. The parser accepts Reticulum Header Type 1 and Header Type 2 packets up to the 500-byte MTU and rejects IFAC packets because IFAC authentication is not implemented.

## Reticulum packet headers

Header Type 1 is 19 bytes:

```text
[flags:1][hops:1][destination_hash:16][context:1][data:0..481]
```

Header Type 2 is 35 bytes:

```text
[flags:1][hops:1][transport_id:16][destination_hash:16][context:1][data:0..465]
```

The flags byte is laid out as:

```text
bit 7    IFAC flag (rejected when set)
bit 6    header type (0 = Type 1, 1 = Type 2)
bit 5    context flag
bit 4    propagation type (0 = broadcast, 1 = transport)
bits 3:2 destination type (SINGLE, GROUP, PLAIN, LINK)
bits 1:0 packet type (DATA, ANNOUNCE, LINKREQ, PROOF)
```

The packet hash is SHA-256 over the Reticulum hashable part. Hop count is excluded as required by the protocol. The router rejects packets at or above `MAX_HOPS` (128), suppresses recently seen packet hashes, and never mutates an authenticated payload.

## Context values

Contexts `0x00` through `0x0E` and `0xFA` through `0xFF` use the Reticulum values declared in `Config.h`. The link implementation actively handles:

| Context | Meaning |
|---:|---|
| `0x00` | Generic or encrypted link data |
| `0xFA` | Link keepalive |
| `0xFC` | Link close |
| `0xFE` | Link-request RTT |
| `0xFF` | Link-request proof |

Context `0xB0` is a node-specific local command accepted only from the serial or Bluetooth KISS interfaces and never forwarded. Its data is `[destination_prefix:8][ASCII command]`; an all-zero prefix addresses the local node. It must not be confused with standard Reticulum context `0xFE`.

There is no custom ACK context and no sequence-number prefix in ordinary Reticulum data. See `LINK_LAYER.md` for the implemented link handshake and the explicit lack of automatic reliable-delivery semantics.

## Announce data

The supported fixed announce body is:

```text
[public_key:64][name_hash:10][random_hash:10][signature:64][app_data...]
```

When the context flag is set, a 32-byte ratchet public key appears before the signature. Announce validation checks the destination hash, name hash, and Ed25519 signature before a route or ESP-NOW peer is learned. The public key is X25519 public key (32 bytes) followed by Ed25519 public key (32 bytes).

## SINGLE destination encryption

Encrypted SINGLE data is:

```text
[ephemeral_x25519_public:32][Fernet token:64 or more]
```

The Fernet token is `[IV:16][ciphertext:n*16][HMAC-SHA256:32]`. The recipient rejects all-zero shared secrets and authenticates before decrypting.

## Link packets

Link requests, proofs, RTT, encrypted data, keepalive, and close formats are defined in `LINK_LAYER.md`. Link packets retain normal Reticulum headers; the destination hash field contains the 16-byte link ID after establishment.

## KISS framing

Serial, USB CDC, Bluetooth, and external TNC paths use KISS framing:

```text
[FEND 0xC0][command/port byte][escaped Reticulum or AX.25 bytes][FEND 0xC0]
```

Payload `0xC0` is escaped as `0xDB 0xDC`; payload `0xDB` is escaped as `0xDB 0xDD`. Only KISS data frames are treated as packet input. Malformed or oversized frames are discarded.

## ESP-NOW fragmentation

Reticulum packets larger than one ESP-NOW payload are split into a bounded set of fragments. Each fragment carries a versioned header with message ID, fragment index/count, total length, and—when enabled—the CRC32 of the complete packet. Reassembly is keyed by sender MAC and message ID, rejects inconsistent metadata and excessive lengths/counts, ignores repeated fragment indexes, expires incomplete assemblies, and validates CRC32 before passing the complete Reticulum packet to the parser.

## AX.25 and APRS

The experimental amateur-radio path supports AX.25 address fields, UI control/PID bytes, CRC-16/X.25 FCS, HDLC bit stuffing, and APRS position/text payload helpers. These features are not enabled in the production target. `HAM_MODEM_ENABLED` is compile-time rejected when `PRODUCTION_BUILD=1` pending hardware, interoperability, callsign, and regional RF acceptance.
