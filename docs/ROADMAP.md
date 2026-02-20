# Project Roadmap — ESP32 Reticulum Gateway

Status (2026-02-20): roadmap updated; v2.1 features largely implemented.

## Goals
- Make the node easier to operate remotely (Web UI, runtime config, metrics)
- Improve reliability and update security (signed OTA, CI, tests)
- Add user-facing features (APRS, IPFS improvements, BLE provisioning)

## Milestones
- v2.1 — Observability & DX
  - [x] CI + unit tests
  - [x] Web UI + REST API (status, logs)
  - [x] Runtime JSON config (SPIFFS/LittleFS)
  - [x] /metrics endpoint + log levels
- v2.2 — Field features
  - [ ] Secure OTA with signature verification
  - [ ] BLE provisioning (GATT) for WiFi/callsign
  - [ ] IPFS: pinning & IPNS support
- v2.3 — Advanced routing & security
  - [ ] Adaptive routing metrics (ETX/RSSI)
  - [ ] Encrypted group messaging & key management
  - [ ] Crash reporting & remote core dumps (opt-in)

## Immediate next tasks (this repo change set)
- Track remaining roadmap features in issues
- Continue implementing features from top of checklist
- Extend unit tests and CI coverage

## How we'll work
- Features implemented via separate PRs (recommended)
- Each PR must include tests or docs where applicable
- CI must pass on every PR before merge

---

If you want, I can now:
- create GitHub issues & milestones for every unchecked item, or
- start implementing items from the top of the checklist (pick 1–3 to begin)
