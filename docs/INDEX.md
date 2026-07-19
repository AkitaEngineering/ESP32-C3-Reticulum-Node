# Documentation Index

**Updated:** 2026-07-19

Use these documents in this order:

| Document | Purpose |
|---|---|
| [README](../README.md) | Build, flash, configure, and basic operation |
| [Production readiness](PRODUCTION_READINESS.md) | Authoritative release decision, manufacturing gates, and unresolved physical controls |
| [Technical specification](TECHNICAL_SPECIFICATION.md) | Current product scope, defaults, security, and verification requirements |
| [Architecture](ARCHITECTURE.md) | Component ownership and packet/control flows |
| [Packet formats](PACKET_FORMATS.md) | Formats and contexts actually accepted/emitted |
| [Link sessions](LINK_LAYER.md) | Authenticated link handshake, encryption, lifecycle, and delivery limitations |
| [HTTP API](API.md) / [OpenAPI](openapi.yaml) | Management API contract and examples |
| [KISS interface](KISS_INTERFACE.md) | Serial/USB framing and host configuration |
| [HAM components](HAM_MODEM.md) | Experimental AX.25/APRS/audio boundaries |
| [Optional features](ENHANCED_FEATURES.md) | LoRa, IPFS, BLE placeholder, and promotion gates |
| [Roadmap](ROADMAP.md) | Planned work, not shipped behavior |
| [Security policy](../SECURITY.md) | Vulnerability reporting and security boundaries |

`PRODUCTION_READINESS.md` wins if another document appears to imply broader release approval. Source constants and the pinned `platformio.ini` environments win over prose when checking a build-specific value.

Historical military-style documents were replaced because they mixed planned behavior with implemented behavior. The current documentation explicitly distinguishes compiled capability, tested software behavior, physical validation, and production approval.
