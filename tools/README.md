# Tools

This folder contains helper scripts for provisioning and OTA uploads for the ESP32 Reticulum Gateway.

Prerequisites
- `curl` (for HTTP requests)
- `jq` (optional, for pretty JSON output used by `provision.sh`)
- `openssl` and `xxd` for `sign_firmware.sh`
- Unix-like shell (Linux/macOS). Windows users can use WSL or adapt the PowerShell examples below.

Files
- `provision.sh` — POST a JSON config to `/api/v1/config`.
  - Usage: `./tools/provision.sh <device-ip> <port> <token> <config-json>`
- `make_device_config.py` — Generate a per-device config and append a manufacturing manifest row.
  - Usage: `./tools/make_device_config.py --device-id RNS-000001 --api-public-key <64_hex_chars>`
- `sign_firmware.sh` — Sign the version-bound firmware digest with an Ed25519 private key for OTA.
  - Usage: `./tools/sign_firmware.sh <version> <firmware.bin> <ed25519-private-key.pem> <signature.hex>`
- `ota_upload.sh` — Upload a signed binary to `/api/v1/ota`.
  - Usage: `./tools/ota_upload.sh <device-ip> <port> <token> <version> <firmware.bin> <signature.hex>`
- `package_release.py` — Verify a signature and create the versioned firmware, manifest, changelog, key ID, and checksums release bundle.
- `fleet_status.py` — Poll `/api/v1/status` across a small fleet and emit JSONL.
  - Usage: `./tools/fleet_status.py --inventory provisioning/fleet_inventory.csv`
- `postman_collection.json` — Minimal Postman collection generated from `docs/openapi.yaml` for quick import into Postman.

Examples

Provisioning (bash):
```bash
./tools/make_device_config.py --device-id RNS-000001 --wifi-ssid field-net --wifi-password "$WIFI_PASSWORD" --api-public-key "$OTA_PUBLIC_KEY"
./tools/provision.sh 192.168.4.1 80 "$TOKEN" myconfig.json
```

OTA upload (bash):
```bash
./tools/sign_firmware.sh 0.3.2 .pio/build/esp32-c3-prod-managed/firmware.bin keys/ota-ed25519.pem signature.hex
./tools/ota_upload.sh 192.168.4.1 80 "$TOKEN" 0.3.2 .pio/build/esp32-c3-prod-managed/firmware.bin signature.hex
```

Windows PowerShell (equivalent):
```powershell
# Set variables
$token = "<token>"
$device = "192.168.4.1"

# POST config
Get-Content -Raw .\myconfig.json | Invoke-RestMethod -Uri "http://$device:80/api/v1/config" -Method Post -Headers @{ Authorization = "Bearer $token" } -ContentType 'application/json'

# OTA upload
.\tools\sign_firmware.ps1 0.3.2 .\firmware.bin .\keys\ota-ed25519.pem .\signature.hex
Invoke-RestMethod -Uri "http://$device:80/api/v1/ota" -Method Post -InFile .\firmware.bin -Headers @{ Authorization = "Bearer $token"; 'X-Firmware-Version' = '0.3.2'; 'X-Signature-Ed25519' = (Get-Content .\signature.hex -Raw).Trim() } -ContentType 'application/octet-stream'
```

Notes
- Production builds always require a valid factory-provisioned token and do not permit unauthenticated network bootstrap.
- Signing contract: Ed25519 over `SHA-512("RNS-OTA-V1\\0" || version || "\\0" || firmware.bin)`, hex-encoded as 128 characters.
- The signing version must exactly match the version embedded in the firmware and must be newer than the version running on the target device.
- Release manifests identify the OTA key with the first 16 hexadecimal characters of SHA-256 over its raw 32-byte Ed25519 public key.
