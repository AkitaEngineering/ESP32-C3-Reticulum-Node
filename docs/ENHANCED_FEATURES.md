# Optional and Experimental Features

**Document version:** 3.0
**Updated:** 2026-07-19

The canonical production artifact intentionally has a narrow feature set. Optional code can compile for other targets, but compile success is not a support or production claim.

| Feature | Current scope | Production status |
|---|---|---|
| LoRa | RadioLib packet transport on configured supported boards | Requires board, RF, range, coexistence, and regulatory HIL |
| AX.25/APRS | Frame/FCS and payload helpers | Experimental; excluded with HAM bundle |
| Direct AFSK | Blocking tone TX and externally sampled Goertzel RX | Experimental, no complete radio integration |
| Winlink-style adapter | Local `WL2K|...` UI-frame serialization | Not Winlink protocol interoperability |
| IPFS | HTTP gateway fetch and optional local-node publish adapter | Development only; absent from canonical target |
| BLE provisioning | Compile-time placeholder | Not implemented or enabled |

## LoRa

The Heltec LoRa32 v3 environment enables `LORA_ENABLED` and RadioLib. Generic pin/frequency defaults are examples. Before field use, set the legal regional frequency and power, validate the exact radio module/pins, handle duty-cycle constraints, verify antenna matching, and run two-node packet/route soak tests.

## Amateur radio

See `HAM_MODEM.md`. The production build rejects the bundled HAM feature because its audio, protocol, electrical, and regulatory behavior is not qualified. An external KISS TNC is the preferred development path.

## IPFS

When `IPFS_ENABLED` is compiled, the device can fetch a bounded object from an HTTP gateway and can publish multipart data only when a reachable local IPFS API is explicitly enabled. Public gateway behavior, CID validity, TLS trust, content authenticity, availability, privacy, and pinning are outside this adapter. Do not treat IPFS content addressing as authentication; authenticate application content separately.

## BLE provisioning

`BLE_PROVISIONING_ENABLED` is reserved configuration surface only. There is no GATT provisioning service in this repository. Production provisioning uses an offline generated `/config.json` written to SPIFFS before the device joins its field network.

## Acceptance rule

An optional feature can move into the supported production target only after it gains:

1. explicit product requirements and threat model;
2. unit, malformed-input, and cross-platform build tests;
3. physical HIL and interoperability tests;
4. resource/soak and recovery tests;
5. updated operator documentation and regulatory review;
6. a production build gate that prevents unsafe placeholder configuration.
