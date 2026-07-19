# Firmware Architecture

**Document version:** 3.0
**Updated:** 2026-07-19

## Runtime composition

`ReticulumNode` owns the routing table, interface manager, cryptographic identity, and link manager. `main.cpp` calls `setup()` once and `loop()` continuously; the firmware does not run a general-purpose operating-system service model.

```text
                         +------------------+
                         |  ReticulumNode   |
                         +---+---+---+---+--+
                             |   |   |   |
             +---------------+   |   |   +----------------+
             |                   |   |                    |
      InterfaceManager      RoutingTable             LinkManager
             |                   |                        |
    +--------+---------+   candidate routes           RNSLink (0..10)
    |   |    |    |    |   per destination           authenticated,
  KISS UDP ESP-NOW LoRa optional HAM/IPFS             interface-bound
```

The canonical `esp32-c3-prod-managed` artifact enables USB KISS, ESP-NOW, WiFi UDP, runtime JSON configuration, authenticated HTTP API, metrics, and signed OTA. LoRa, HAM/audio, Bluetooth Classic, and IPFS depend on target/build flags and are not all present on ESP32-C3.

## Packet receive path

1. An interface produces one complete packet. KISS input is unescaped first; ESP-NOW fragments are bounded and reassembled first.
2. `ReticulumPacket::deserialize()` validates the 19- or 35-byte header, flag ranges, packet length, and unsupported IFAC state, then calculates the official packet hash.
3. Node-local `LOCAL_CMD` packets are accepted only from serial or Bluetooth KISS and are consumed without forwarding.
4. Link requests and link-addressed traffic go to `LinkManager`.
5. Announces are cryptographically validated before they update routes or peer state.
6. SINGLE data for this identity is authenticated and decrypted before delivery. GROUP encryption is not implemented. PLAIN data can be delivered locally according to configuration.
7. Other traffic is loop-suppressed, hop-limited, reserialized with only the hop count changed, and sent through route selection or broadcast fallback.

## Routing model

`RoutingTable` stores bounded candidate paths per destination. Selection is ordered by hop count, configured interface priority, and freshness. A candidate records its ingress interface and interface-specific next-hop metadata such as ESP-NOW MAC or UDP address/port. Interface health is considered during selection. Routes expire after three missed 30-second announces plus 15 seconds (105 seconds by default).

Announces are forwarded only once within the recent-announce window. Normal packet forwarding uses the packet hash for duplicate suppression and preserves Header Type 1/2, propagation, destination, context flag, context, and payload.

## Interface behavior

- USB/UART and Bluetooth Classic use KISS framing. Debug output is separated from KISS when USB owns the data channel.
- WiFi UDP listens on port 4242 and tracks sender endpoint metadata for return routes.
- ESP-NOW uses broadcast discovery, bounded peer management, optional long-range PHY, fragmentation with CRC32, and a bounded store-and-forward queue for link-layer send failures.
- LoRa uses RadioLib on supported builds and must be configured for the board and legal regional frequency/power limits.
- HAM/audio and Winlink-style components are experimental and compile-time excluded from production builds.
- IPFS is an optional HTTP-gateway adapter, not a production transport in the canonical artifact.

## Identity and session security

The persistent identity contains X25519 and Ed25519 private material in EEPROM. Announce identities are verified before being trusted. SINGLE destination encryption derives a one-time shared key from an ephemeral X25519 public key. Link sessions authenticate the responder's announced Ed25519 identity before accepting derived keys and bind the active session to its handshake interface.

Fernet-style tokens authenticate IV and ciphertext with HMAC-SHA256 and use AES-256-CBC with PKCS#7 padding. Authentication occurs before decryption; sensitive derived material is wiped when possible. The current Arduino build does not provide Secure Boot or Flash Encryption, so physical extraction resistance is a release gate rather than an implemented guarantee.

## Control and update plane

The HTTP server is a small synchronous REST implementation, not a browser UI. It limits request/header/body sizes, rejects ambiguous security headers, requires exact Bearer authentication, redacts secrets from reads, validates complete production configuration, and commits configuration through a verified temporary file.

OTA uploads are streamed to SPIFFS, hashed with a version-bound domain separator, verified with the provisioned Ed25519 public key, and only then passed to the ESP update API. Version comparison rejects replay and downgrade. The HTTP plane is plaintext and must live behind an encrypted trusted management boundary.

## Concurrency and resource bounds

ESP-NOW callbacks exchange data with the main loop through protected bounded queues. Routing and link collections have configured maxima. Packet and fragment sizes are checked against the 500-byte Reticulum MTU. Dynamic allocations remain in packet, JSON, and link paths, so hardware soak tests and heap telemetry are required before a release.

## Intentionally unsupported or incomplete

- IFAC packet authentication
- GROUP destination decryption/key management
- Reticulum resource transfer, request/channel APIs, delivery proofs, and automatic link data retries
- production-qualified direct audio modem, Winlink protocol, or IPFS transport
- TLS termination on the device
- Secure Boot and Flash Encryption in the pinned prebuilt framework

These boundaries are release facts, not implied roadmap promises. See `PRODUCTION_READINESS.md` for the gates that remain outside source-level validation.
