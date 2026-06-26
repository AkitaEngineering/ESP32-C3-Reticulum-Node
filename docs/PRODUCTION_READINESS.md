# Production Readiness Plan

**Document Version:** 1.0
**Date:** 2026-06-26
**System Designation:** ESP32-RNS-GW-PROD

---

## 1.0 PURPOSE

This document defines the release, provisioning, manufacturing, and pilot gates required to sell the ESP32 Reticulum Gateway as a supported field communications appliance.

The goal is to make every shipped unit traceable, recoverable, updateable, and supportable.

---

## 2.0 PRODUCT RELEASE GATES

### 2.1 Firmware Gate

A firmware build can be released only when:

1. `pio run` passes for the default production target.
2. `pio run -e esp32-c3-web` passes for the managed/API target.
3. `pio test -e test --without-uploading --without-testing` passes in CI.
4. A hardware-in-the-loop smoke test passes on at least two ESP32-C3 nodes.
5. KISS-over-USB and ESP-NOW relay are verified with real packet traffic.
6. No placeholder API token or empty OTA public key is shipped in field config.

### 2.2 OTA Gate

OTA is considered production-capable when:

1. Firmware upload is streamed to SPIFFS.
2. Signature verification does not allocate the full firmware image in RAM.
3. The signature is Ed25519 over `SHA-512(firmware.bin)`.
4. The device verifies the signature before calling `Update.begin()`.
5. The release package contains the firmware binary, signature, public key ID, version, and changelog.
6. Rollback/recovery behavior has been tested on a failed update and a power-loss simulation.

### 2.3 Provisioning Gate

Each shipped device must have:

1. A unique device ID or serial number.
2. A per-device API token.
3. A known OTA verification public key.
4. A generated config file archived in the production manifest.
5. A visible label or QR code that maps to the device record.
6. A recorded acceptance-test result.

---

## 3.0 MANUFACTURING WORKFLOW

### 3.1 Prepare Release Artifacts

Build production firmware:

```bash
pio run
```

Sign the binary:

```bash
./tools/sign_firmware.sh .pio/build/esp32-c3-prod-usb/firmware.bin keys/ota-ed25519.pem release/signature.hex
```

The signing helper signs `SHA-512(firmware.bin)`, not the raw firmware stream. This matches the constant-memory verifier in the device OTA endpoint.

### 3.2 Generate Per-Device Config

```bash
./tools/make_device_config.py \
  --device-id RNS-000001 \
  --node-name field-node-001 \
  --api-public-key "<64_hex_chars>" \
  --out-dir provisioning/devices \
  --manifest provisioning/device_manifest.csv
```

The generated config contains a strong per-device API token and is recorded in the manifest. Treat the manifest as sensitive operational data.

### 3.3 Flash And Provision

1. Flash firmware with PlatformIO.
2. Upload the per-device filesystem config or POST it through the bootstrap API.
3. Restart the unit if the provisioning response returns `X-Restart-Required: true`.
4. Query `/api/v1/status`.
5. Confirm `config_present=true`, `bootstrap_mode=false`, expected `device_id`, expected `node_name`, and valid interface health.

### 3.4 Acceptance Test

For every unit:

1. USB enumerates reliably after reset.
2. KISS frame round trip works on USB.
3. ESP-NOW announce is observed.
4. Two-node relay succeeds.
5. `/api/v1/status` responds with no placeholder token behavior.
6. Signed OTA rejects a bad signature.
7. Signed OTA accepts a known-good signature on a test image or staging unit.

---

## 4.0 PILOT READINESS

A paid or design-partner pilot should start with 5 to 20 nodes and one narrow operational workflow.

Track:

1. Time to provision each node.
2. Time to install each node.
3. Message delivery success under normal and degraded connectivity.
4. Mean time to detect route or link failure.
5. Percentage of issues resolved remotely.
6. OTA success rate.
7. Support interventions per node per week.

---

## 5.0 COMMERCIALIZATION CHECKLIST

Before broad sales:

1. Choose one hardware SKU and enclosure.
2. Use pre-certified radio modules where possible.
3. Complete FCC/ISED/CE review for the finished product and target regions.
4. Finalize labels, user manual, warranty, return process, and safety notes.
5. Decide GPL source-distribution process or dual-license strategy.
6. Create a support playbook for provisioning, update failure, lost token, and field replacement.
7. Publish customer-facing installation and quick-start guides.

---

## 6.0 CONTROL PLANE REQUIREMENTS

The first fleet-management system should be small but real:

1. Device registry with device ID, serial, config version, firmware version, and customer ownership.
2. Token/public-key record for each unit.
3. Last-seen status and route summary.
4. OTA rollout batches with success/failure status.
5. Alerting for missed heartbeat, low heap, route loss, and repeated reboot.
6. Exportable support bundle for each device.

This can start as a local operator tool or lightweight web service. It should not block the pilot, but it is required before repeatable commercial deployment.

For the first pilots, `tools/fleet_status.py` can poll `/api/v1/status` across an inventory CSV and emit JSONL for support scripts, dashboards, or incident notes.
