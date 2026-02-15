# Tools

This folder contains helper scripts for provisioning and OTA uploads for the ESP32 Reticulum Gateway.

Prerequisites
- `curl` (for HTTP requests)
- `jq` (optional, for pretty JSON output used by `provision.sh`)
- `openssl` or other Ed25519 signing tool to produce the signature.hex file (not provided here)
- Unix-like shell (Linux/macOS). Windows users can use WSL or adapt the PowerShell examples below.

Files
- `provision.sh` — POST a JSON config to `/api/v1/config`.
  - Usage: `./tools/provision.sh <device-ip> <port> <token> <config-json>`
- `ota_upload.sh` — Upload a signed binary to `/api/v1/ota`.
  - Usage: `./tools/ota_upload.sh <device-ip> <port> <token> <firmware.bin> <signature.hex>`
- `postman_collection.json` — Minimal Postman collection generated from `docs/openapi.yaml` for quick import into Postman.

Examples

Provisioning (bash):
```bash
./tools/provision.sh 192.168.4.1 80 "$TOKEN" myconfig.json
```

OTA upload (bash):
```bash
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
Invoke-RestMethod -Uri "http://$device:80/api/v1/ota" -Method Post -InFile .\firmware.bin -Headers @{ Authorization = "Bearer $token"; 'X-Signature-Ed25519' = (Get-Content .\signature.hex -Raw).Trim() } -ContentType 'application/octet-stream'
```

Notes
- The server may require authentication depending on configuration; if no `api.token` is set in runtime config, the API allows initial bootstrap without auth.
- Signing tool: ensure your Ed25519 signature is hex-encoded and exactly matches the uploaded binary.
