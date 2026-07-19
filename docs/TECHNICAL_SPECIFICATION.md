# ESP32 Reticulum Gateway Technical Specification

**Document version:** 3.0
**Updated:** 2026-07-19
**Canonical target:** `esp32-c3-prod-managed`

## Product status

The firmware is a software release candidate for controlled pilots. It is not approved for unattended or physically exposed production until the hardware-security, hardware-in-the-loop, manufacturing, recovery, and regulatory gates in `PRODUCTION_READINESS.md` pass.

## Canonical build

| Item | Value |
|---|---|
| MCU/board | ESP32-C3-DevKitM-1 class, 160 MHz, 4 MB flash |
| Framework | Arduino-ESP32 from pinned PlatformIO `espressif32@6.12.0` |
| Optimization | `-Os`, `-Wall`, `-Werror` |
| Firmware version | `0.3.1` for the current production environment |
| Interfaces | USB KISS, ESP-NOW, WiFi UDP |
| Management | Runtime JSON, authenticated HTTP API, metrics, signed OTA |
| Debug/demo traffic | Disabled |

Other PlatformIO environments are compatibility, development, test, or optional hardware targets. A successful compile does not certify an untested board or radio configuration.

## Network parameters

| Parameter | Default |
|---|---:|
| Reticulum MTU | 500 bytes |
| Header Type 1 / Type 2 | 19 / 35 bytes |
| Conservative maximum payload | 465 bytes |
| UDP port | 4242 |
| Announce interval | 30 seconds |
| Route timeout | 105 seconds |
| Maximum hops | 128 |
| Maximum stored route candidates | 20 |
| Maximum simultaneous links | 10 |
| Link inactivity timeout | 210 seconds |
| KISS rate | 115200 baud |

The firmware supports normal and transport headers and rejects IFAC packets. Routing learns only from valid signed announces, stores candidate paths by interface, prefers fewer hops, then configured interface priority and freshness, and expires stale paths.

## Cryptographic behavior

- Identity public key: X25519 public key (32 bytes) plus Ed25519 public key (32 bytes).
- Identity hash: first 16 bytes of SHA-256 over the 64-byte public key.
- Announce validation: destination hash, name hash, and Ed25519 signature are checked before learning or forwarding.
- Announce random hashes use Unix time after SNTP synchronization and an uptime fallback while offline or unsynchronized.
- SINGLE encryption: ephemeral X25519, HKDF-SHA256, then authenticated Fernet-style AES-256-CBC/HMAC-SHA256.
- Link establishment: ephemeral X25519 with an Ed25519-authenticated responder proof; active links bind to the handshake interface and enforce negotiated MTU.
- OTA: Ed25519 signature over `SHA-512("RNS-OTA-V1\0" || version || "\0" || firmware)`.

The firmware does not implement GROUP keys, IFAC, Reticulum resources, delivery proofs, or automatic reliable link delivery. See `LINK_LAYER.md`.

## Runtime configuration

The production unit reads `/config.json` from SPIFFS before network initialization. Required managed-production values are:

- non-empty `node_name` and `rns_app_name` within documented length limits;
- a WiFi SSID, with an empty password for an explicitly open network, an 8–63 printable-character WPA passphrase, or a 64-digit hexadecimal PSK;
- `api.auth_enabled: true`;
- a non-placeholder per-device API token of 32–128 safe characters;
- a non-zero 32-byte Ed25519 OTA public key in hexadecimal.

Use `tools/make_device_config.py`; generated configs and manifests contain secrets and must remain outside Git. The OTA `public_key_id` is the first 16 hexadecimal characters of SHA-256 over the raw 32-byte Ed25519 public key.

The same validator is used when loading a preprovisioned file and accepting an API update. Production startup and pending-OTA health checks reject a missing or invalid file; `/api/v1/status` reports `config_valid` and an explanatory `config_error` when applicable.

## Management API

The device exposes HTTP endpoints for status, route diagnostics, redacted configuration, configuration writes, metrics, and signed OTA. Authentication requires the exact scheme `Authorization: Bearer <token>`. Duplicate authorization/signature/version headers, folded headers, transfer encoding, invalid lengths, oversized bodies, and incomplete transfers are rejected.

HTTP is not encrypted. Deploy it only on a trusted management VLAN carried through a TLS VPN/gateway, never directly on an untrusted LAN or the Internet. Endpoint schemas and response details are in `API.md` and `openapi.yaml`.

## Optional interfaces

- ESP32 original can provide Bluetooth Classic; S2/S3/C3 are not advertised as Bluetooth Classic targets by this firmware.
- Heltec LoRa v3 builds enable RadioLib LoRa support. Frequency, output power, antenna, duty cycle, and certification are deployment-specific.
- HAM/AX.25/APRS/audio/Winlink code is experimental. `PRODUCTION_BUILD` rejects `HAM_MODEM_ENABLED`.
- IPFS support is an optional development adapter and is absent from the canonical target.

## Verification requirements

Every release must pass:

1. the CI PlatformIO build matrix and production `-Werror` build;
2. embedded unit-test image compilation and physical execution;
3. host provisioning/release tests, Python/shell/config syntax checks, and static analysis;
4. two-node ESP-NOW/KISS/link HIL, long-duration heap/queue soak, and OTA failure/power-loss recovery;
5. per-unit provisioning validation and, for broad deployment, Secure Boot/Flash Encryption verification;
6. applicable FCC/ISED/CE and amateur-radio operational requirements.

Software validation alone cannot establish RF performance, electrical safety, regulatory conformity, power-loss resilience, or irreversible eFuse correctness.

## Operational diagnostics

- `/api/v1/status` reports version, identity/config state, heap, routes, links, WiFi, and per-interface counters.
- `/api/v1/routes` explains all candidates and the selected route.
- `/api/v1/metrics` provides a compact health snapshot when metrics are enabled.
- Local serial/Bluetooth KISS commands use context `0xB0`, are never forwarded, and support `routes`, `peers`, and `status`.
- The node announces every 30 seconds and checks memory every 15 seconds by default.

For packet structure, link semantics, API behavior, and release decisions, the normative repository documents are `PACKET_FORMATS.md`, `LINK_LAYER.md`, `API.md`, and `PRODUCTION_READINESS.md`.
