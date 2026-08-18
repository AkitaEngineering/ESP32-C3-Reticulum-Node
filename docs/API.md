## Web API (REST) - ESP32 Reticulum Gateway

**Document version:** 3.1  
**Updated:** 2026-08-18

This document describes the HTTP API exposed by the device when `WEBSERVER_ENABLED=1`. The firmware does not serve a browser UI.

All endpoints are served on HTTP (port defined by `WEBSERVER_PORT`) and use simple JSON where applicable.

Authentication
- The API uses an opaque Bearer token when `WEBSERVER_AUTH_ENABLED=1`. The scheme must be exactly `Authorization: Bearer <token>`; a raw token is rejected.
- Development builds may allow selected first-boot endpoints without authentication. `PRODUCTION_BUILD=1` disables this path.
- If `/config.json` exists with an empty token or the placeholder token `CHANGE_ME_generate_a_strong_token`, write endpoints fail closed. `GET /api/v1/status` remains available as a read-only diagnostic so `config_error` / `fail_closed` can be observed. Replace the placeholder with a strong per-device token before enabling writes.
- The API is plaintext HTTP. Production deployments must terminate TLS on a VPN or management gateway; never expose port 80 to an untrusted network.
- Header: `Authorization: Bearer <token>`
- Duplicate `Authorization`, `X-Signature-Ed25519`, `X-Firmware-Version`, or `Content-Length` headers are rejected. Folded headers and `Transfer-Encoding` are not supported.

Common response codes
- `200 OK` – success
- `201 Created` – created
- `400 Bad Request` – malformed input
- `401 Unauthorized` – missing/invalid auth
- `403 Forbidden` – invalid signature or forbidden action
- `404 Not Found` – resource not present
- `408 Request Timeout` – request body or OTA stream incomplete/inactive
- `409 Conflict` – OTA version is not newer
- `411 Length Required` – POST omitted `Content-Length`
- `413 Content Too Large` – non-OTA body exceeds the configured limit
- `500 Internal Server Error` – server error

Endpoints

- `GET /api/v1/status`
  - Returns JSON with node status metrics, provisioning state, and per-interface health snapshots (uptime, free heap, link counts, route count, stable device ID, config presence/validity, identity readiness, fail-closed state, mesh enablement, bootstrap mode, WiFi state, restart-required state, interface support/usability, packet counters, and last RX/TX uptime timestamps). `config_error` is present when validation fails. `api_transport` is `http`; `management_requires_tls_proxy` is always true.
  - Auth: required when a valid token exists. If the saved config is invalid or the token is a placeholder, this endpoint is allowed as a read-only diagnostic.

- `GET /api/v1/routes`
  - Returns grouped route diagnostics by destination, including all candidate paths, the currently selected path, and the effective interface-priority policy used for tie-breaks.
  - Auth: required.

- `GET /api/v1/config`
  - Returns runtime configuration with `wifi.password` and `api.token` redacted.
  - Auth: always required in production.
  - Response: JSON config document.
  - Routing policy: `routing.interface_priority` can override interface tie-break priorities at runtime without rebuilding firmware.

- `POST /api/v1/config`
  - Accepts a complete JSON config object and writes it to `/config.json` when `JSON_CONFIG_ENABLED=1`.
  - If `wifi` credentials are present in the JSON they will be applied immediately (`WiFi.begin(...)`).
  - Response headers:
    - `X-Restart-Required: true|false` — indicates whether the saved configuration includes changes that require a reboot to take full effect.
    - `X-Restart-Reason: <reason>` — currently returned when `rns_app_name` changed and the running node still uses the previous application name.
  - Auth: required when a token exists. Managed production configuration requires `api.auth_enabled=true`, a non-placeholder 32–128 character token, and a non-zero 32-byte Ed25519 public key.
  - WiFi passwords must be empty (explicit open network), an 8–63 character printable WPA passphrase, or a 64-digit hexadecimal PSK. Managed production also requires a non-empty SSID.
  - Production requires factory provisioning and rejects unauthenticated first-time bootstrap.
  - Writes are validated and committed through a verified temporary file; the response never echoes secrets.

- `POST /api/v1/config/save`
  - Confirms the committed `/config.json` still validates. Returns `{"saved":true,"already_committed":true}` or an error if the file is missing/invalid. There is no separate staging document; `POST /api/v1/config` is the write path.
  - Auth: required.

- `POST /api/v1/restart`
  - Persists the node address and packet counter, returns `restarting`, then performs a controlled restart.
  - Auth: required. Send `Content-Length: 0` when no body is needed.

- `POST /api/v1/ota`
  - Signed OTA upload endpoint (enabled with `OTA_ENABLED=1`).
  - Uploads are streamed to a temporary file in SPIFFS, hashed with SHA-512 while streaming, verified using Ed25519 signature, then flashed via `Update`.
  - Required headers:
    - `Content-Length: <bytes>`
    - `X-Firmware-Version: <MAJOR.MINOR.PATCH>` — must be newer than the running version
    - `X-Signature-Ed25519: <hex_signature>` — Ed25519 signature of `SHA-512("RNS-OTA-V1\\0" || version || "\\0" || firmware.bin)`
    - Authorization: Bearer token (if enabled)
  - The public key used to verify signatures is read from the runtime JSON config (`api.public_key`).
  - On signature verification failure the endpoint returns `403 Invalid signature`.
  - The upload permits at most 10 seconds without receiving bytes and at most 5 minutes total; an incomplete/timed-out upload returns `408` and the staging file is removed.

Notes & Examples
- To GET the config (example):

```bash
curl -s -H "Authorization: Bearer ${TOKEN}" http://<device-ip>:<port>/api/v1/config
```

- To POST a new config (example):

```bash
curl -X POST -H "Content-Type: application/json" -H "Authorization: Bearer ${TOKEN}" \
  --data @myconfig.json http://<device-ip>:<port>/api/v1/config
```

- To upload signed OTA (high-level example):

```bash
# Build and version-bind the signed firmware digest
./tools/sign_firmware.sh 0.3.2 firmware.bin keys/ota-ed25519.pem signature.hex

curl -X POST \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "X-Firmware-Version: 0.3.2" \
  -H "X-Signature-Ed25519: $(cat signature.hex)" \
  --data-binary @firmware.bin \
  http://<device-ip>:<port>/api/v1/ota
```

Implementation notes
- The implementation verifies uploaded image size and hashes the upload as it streams, avoiding full-image RAM allocation on ESP32-C3.
- `api.public_key_id` is the first 16 hexadecimal characters of SHA-256 over the raw 32-byte Ed25519 public key. `tools/make_device_config.py` and `tools/package_release.py` use the same rule.
- The API is plaintext HTTP. Put it behind a trusted, encrypted management boundary and never expose it directly to an untrusted LAN or the Internet.

OpenAPI and CLI helpers
- An OpenAPI v3 description is provided in `docs/openapi.yaml` for tooling and SDK generation.
- CLI helper scripts for quick provisioning and signed OTA uploads are available in `tools/`:
  - `tools/provision.sh` — POST a JSON config to `/api/v1/config` and print the response (requires `jq` for pretty output).
  - `tools/make_device_config.py` — Generate per-device config files and a provisioning manifest.
  - `tools/sign_firmware.sh` — Generate the OTA signature expected by `/api/v1/ota`.
  - `tools/ota_upload.sh` — Upload a signed binary to `/api/v1/ota` with `X-Signature-Ed25519` header.

API request/response schemas
- `GET /api/v1/status` response:

```json
{
  "node_name": "rns-6497AC",
  "device_id": "rns-6497AC",
  "rns_app_name": "esp32.node",
  "uptime_s": 123,
  "free_heap": 34567,
  "active_links": 2,
  "route_count": 12,
  "route_candidate_count": 16,
  "config_present": true,
  "config_valid": true,
  "bootstrap_mode": false,
  "wifi_connected": true,
  "wifi_ip": "192.168.1.42",
  "route_candidates_by_interface": {
    "serial_port": 0,
    "esp_now": 8,
    "wifi_udp": 6,
    "bluetooth": 0,
    "lora": 2,
    "ham_modem": 0,
    "ipfs": 0
  },
  "route_priority_by_interface": {
    "serial_port": 15,
    "esp_now": 40,
    "wifi_udp": 50,
    "bluetooth": 10,
    "lora": 30,
    "ham_modem": 25,
    "ipfs": 5
  },
  "interfaces": {
    "serial_port": {
      "supported": true,
      "usable": true,
      "last_rx_uptime_ms": 8112,
      "last_tx_uptime_ms": 8120,
      "rx_packets": 14,
      "tx_packets": 21,
      "rx_bytes": 1710,
      "tx_bytes": 2483
    },
    "esp_now": {
      "supported": true,
      "usable": true,
      "last_rx_uptime_ms": 7901,
      "last_tx_uptime_ms": 8044,
      "rx_packets": 9,
      "tx_packets": 11,
      "rx_bytes": 1092,
      "tx_bytes": 1340
    },
    "wifi_udp": {
      "supported": true,
      "usable": false,
      "last_rx_uptime_ms": 0,
      "last_tx_uptime_ms": 0,
      "rx_packets": 0,
      "tx_packets": 0,
      "rx_bytes": 0,
      "tx_bytes": 0
    }
  },
  "restart_required": false
}
```

- `route_count` remains the count of distinct reachable destinations.
- `route_candidate_count` reports the total number of stored route candidates across all interfaces.
- `route_candidates_by_interface` breaks candidate-path counts down per interface for fleet diagnostics and failover visibility.
- `route_priority_by_interface` reports the effective tie-break policy currently used by the router after applying defaults and any runtime overrides from `config.json`.
- `interfaces.<name>.last_rx_uptime_ms` and `interfaces.<name>.last_tx_uptime_ms` are monotonic milliseconds since boot, not wall-clock timestamps.
- `interfaces.<name>.supported` indicates whether the interface is compiled into the current firmware image.
- `interfaces.<name>.usable` indicates whether the interface is presently available for routing or transmit activity.

- `GET /api/v1/routes` response (example abridged):

```json
{
  "route_count": 2,
  "route_candidate_count": 3,
  "route_priority_by_interface": {
    "wifi_udp": 50,
    "esp_now": 40,
    "lora": 30
  },
  "destinations": [
    {
      "destination": "A1B2C3D4E5F60708",
      "candidate_count": 2,
      "selected_interface": "wifi_udp",
      "selected_hops": 2,
      "selected_priority": 50,
      "candidates": [
        {
          "interface": "wifi_udp",
          "selected": true,
          "usable": true,
          "hops": 2,
          "interface_priority": 50,
          "age_ms": 1400,
          "last_heard_uptime_ms": 42100,
          "next_hop_mac": "000000000000",
          "next_hop_ip": "192.168.1.20",
          "next_hop_port": 4242
        },
        {
          "interface": "esp_now",
          "selected": false,
          "usable": true,
          "hops": 2,
          "interface_priority": 40,
          "age_ms": 900,
          "last_heard_uptime_ms": 42600,
          "next_hop_mac": "AABBCCDDEEFF",
          "next_hop_ip": "",
          "next_hop_port": 0
        }
      ]
    }
  ]
}
```

- `GET /api/v1/routes` is the diagnostics view for route choice. If no candidate for a destination is currently usable, `selected_interface` will be empty and all candidates for that destination will have `selected: false`.

- `GET /api/v1/config` response (example minimal):

```json
{
  "node_name": "",
  "rns_app_name": "esp32.node",
  "wifi": {"ssid":"","password":""},
  "routing": {
    "interface_priority": {
      "wifi_udp": 50,
      "esp_now": 40
    }
  },
  "api": {"token":"","public_key":""}
}
```

- `POST /api/v1/config` request: any valid JSON object matching your runtime config. Empty `wifi.password` or `api.token` fields keep the previously stored secrets. Response is `{"saved":true,"restart_required":...}` and never echoes secrets.
- `POST /api/v1/config` also returns `X-Restart-Required` and, when applicable, `X-Restart-Reason` so provisioning tools can prompt for a controlled restart instead of assuming every saved change applied live.

- `POST /api/v1/ota` request: binary body with required `X-Firmware-Version` and `X-Signature-Ed25519` headers. The signature is Ed25519 over `SHA-512("RNS-OTA-V1\\0" || version || "\\0" || firmware.bin)`. Responses: `200` (ok), `400` (bad upload/version), `403` (invalid signature), or `409` (same/older version).

- `GET /api/v1/metrics` returns a compact JSON metrics payload including heap, uptime, link counts, route counts, and the same `interfaces` health snapshot object used by `/api/v1/status` when `METRICS_ENABLED=1`.

Examples
- Provisioning

```bash
./tools/provision.sh 192.168.4.1 80 "$TOKEN" myconfig.json
```

- OTA upload

```bash
./tools/ota_upload.sh 192.168.4.1 80 "$TOKEN" 0.3.2 firmware.bin signature.hex
```


---
