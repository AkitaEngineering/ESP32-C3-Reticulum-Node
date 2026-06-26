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
- `sign_firmware.sh` — Sign `SHA-512(firmware.bin)` with an Ed25519 private key for OTA.
  - Usage: `./tools/sign_firmware.sh <firmware.bin> <ed25519-private-key.pem> <signature.hex>`
- `ota_upload.sh` — Upload a signed binary to `/api/v1/ota`.
  - Usage: `./tools/ota_upload.sh <device-ip> <port> <token> <firmware.bin> <signature.hex>`
- `fleet_status.py` — Poll `/api/v1/status` across a small fleet and emit JSONL.
  - Usage: `./tools/fleet_status.py --inventory provisioning/fleet_inventory.csv`
- `postman_collection.json` — Minimal Postman collection generated from `docs/openapi.yaml` for quick import into Postman.

Examples

Provisioning (bash):
```bash
./tools/make_device_config.py --device-id RNS-000001 --api-public-key "$OTA_PUBLIC_KEY"
./tools/provision.sh 192.168.4.1 80 "$TOKEN" myconfig.json
```

OTA upload (bash):
```bash
./tools/sign_firmware.sh .pio/build/esp32-c3-prod-usb/firmware.bin keys/ota-ed25519.pem signature.hex
./tools/ota_upload.sh 192.168.4.1 80 "$TOKEN" firmware.bin signature.hex
```

Windows PowerShell (equivalent):
```powershell
# Set variables
$token = "<token>"
$device = "192.168.4.1"

# POST config
Get-Content -Raw .\myconfig.json | Invoke-RestMethod -Uri "http://$device:80/api/v1/config" -Method Post -Headers @{ Authorization = "Bearer $token" } -ContentType 'application/json'

# OTA upload
.\tools\sign_firmware.ps1 .\firmware.bin .\keys\ota-ed25519.pem .\signature.hex
Invoke-RestMethod -Uri "http://$device:80/api/v1/ota" -Method Post -InFile .\firmware.bin -Headers @{ Authorization = "Bearer $token"; 'X-Signature-Ed25519' = (Get-Content .\signature.hex -Raw).Trim() } -ContentType 'application/octet-stream'
```

Notes
- The server may require authentication depending on configuration; if no `api.token` is set in runtime config, the API allows initial bootstrap without auth.
- Signing contract: the OTA signature is Ed25519 over the 64-byte `SHA-512(firmware.bin)` digest, hex-encoded as 128 characters.
