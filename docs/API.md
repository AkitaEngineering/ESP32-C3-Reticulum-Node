## Web API (REST) - ESP32 Reticulum Gateway

This document describes the HTTP API exposed by the device when the Web UI is enabled (`WEBSERVER_ENABLED=1`).

All endpoints are served on HTTP (port defined by `WEBSERVER_PORT`) and use simple JSON where applicable.

Authentication
- The API uses Bearer token authentication when `WEBSERVER_AUTH_ENABLED=1` and a token is configured in the runtime JSON config.
- If no token is configured (first-time bootstrap) the API allows initial configuration without authentication.
- Header: `Authorization: Bearer <token>`

Common response codes
- `200 OK` – success
- `201 Created` – created
- `400 Bad Request` – malformed input
- `401 Unauthorized` – missing/invalid auth
- `403 Forbidden` – invalid signature or forbidden action
- `404 Not Found` – resource not present
- `500 Internal Server Error` – server error

Endpoints

- `GET /api/v1/status`
  - Returns JSON with node status metrics (uptime, free heap, link counts, route count).
  - Auth: optional depending on `WEBSERVER_AUTH_ENABLED`.

- `GET /api/v1/config`
  - Returns the runtime JSON configuration (from `/config.json` in SPIFFS) or a default template when not present.
  - Auth: required when a token exists.
  - Response: JSON config document.

- `POST /api/v1/config`
  - Accepts a JSON body and writes it to `/config.json` when `JSON_CONFIG_ENABLED=1`.
  - If `wifi` credentials are present in the JSON they will be applied immediately (`WiFi.begin(...)`).
  - Auth: required when a token exists.
  - Returns the saved JSON on success.

- `POST /api/v1/config/save`
  - Saves the currently staged runtime config; returns `saved` or `no config to save`.
  - Auth: required.

- `POST /api/v1/ota`
  - Signed OTA upload endpoint (enabled with `OTA_ENABLED=1`).
  - Uploads are streamed to a temporary file in SPIFFS, verified using Ed25519 signature, then flashed via `Update`.
  - Required headers:
    - `Content-Length: <bytes>`
    - `X-Signature-Ed25519: <hex_signature>` — Ed25519 signature of the uploaded binary
    - Authorization: Bearer token (if enabled)
  - The public key used to verify signatures is read from the runtime JSON config (`api.public_key`).
  - On signature verification failure the endpoint returns `403 Invalid signature`.

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
# Build firmware -> firmware.bin, sign it with your Ed25519 private key -> signature.hex
curl -X POST \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "X-Signature-Ed25519: $(cat signature.hex)" \
  --data-binary @firmware.bin \
  http://<device-ip>:<port>/api/v1/ota
```

Implementation notes
- The implementation verifies uploaded image size and bounds the verification buffer to avoid large RAM allocations (see `MAX_OTA_VERIFY_SIZE` in `WebServer.cpp`).
- If you plan to integrate tooling for OTA automation, use the `api.public_key` field in your device config for signature verification.

Next steps (optional)
- Provide an OpenAPI (Swagger) JSON/YAML export to enable automatic SDK generation and interactive docs.
- Add example Postman collection or CLI helper scripts to streamline provisioning.

OpenAPI and CLI helpers
- An OpenAPI v3 description is provided in `docs/openapi.yaml` for tooling and SDK generation.
- CLI helper scripts for quick provisioning and signed OTA uploads are available in `tools/`:
  - `tools/provision.sh` — POST a JSON config to `/api/v1/config` and print the response (requires `jq` for pretty output).
  - `tools/ota_upload.sh` — Upload a signed binary to `/api/v1/ota` with `X-Signature-Ed25519` header.

API request/response schemas
- `GET /api/v1/status` response:

```json
{
  "uptime_ms": 123456,
  "free_heap": 34567,
  "active_links": 2,
  "route_count": 12
}
```

- `GET /api/v1/config` response (example minimal):

```json
{
  "node_name": "esp32-rns-node",
  "wifi": {"ssid":"","password":""},
  "api": {"token":"","public_key":""}
}
```

- `POST /api/v1/config` request: any valid JSON object matching your runtime config. Response is the saved JSON document on success.

- `POST /api/v1/ota` request: binary body, header `X-Signature-Ed25519` required with hex signature. Responses: `200` (ok), `400` (bad upload), `403` (invalid signature).

Examples
- Provisioning

```bash
./tools/provision.sh 192.168.4.1 80 "$TOKEN" myconfig.json
```

- OTA upload

```bash
./tools/ota_upload.sh 192.168.4.1 80 "$TOKEN" firmware.bin signature.hex
```


---
Generated docs: concise reference for the Web UI endpoints implemented in `src/WebServer.cpp`.
# Web UI & REST API — Spec (draft)

This document describes the initial REST API and WebSocket endpoints for the planned Web UI.
All endpoints require authentication when enabled (future: token / password protected).

## Endpoints (HTTP, JSON)
- GET /api/v1/status
  - Returns device status, uptime, heap, active links, routing table summary.
- GET /api/v1/config
  - Returns current runtime config (from JSON config store if enabled).
- POST /api/v1/config
  - Accepts JSON body to update runtime config (validated); responds with saved config.
- POST /api/v1/config/save
  - Persist current runtime config to SPIFFS/LittleFS (if JSON_CONFIG_ENABLED).
- POST /api/v1/restart
  - Request soft restart of node (admin only).
- GET /api/v1/metrics
  - Prometheus-style or JSON metrics endpoint (if METRICS_ENABLED).
  - When `METRICS_UDP_ENABLED` is also set the node will emit the same
    metrics as a JSON string over UDP broadcast every `METRICS_INTERVAL_MS`.
- POST /api/v1/ota
  - Upload a signed firmware image for OTA. Requires `OTA_ENABLED` and an Ed25519 `public_key` in `/config.json` under `api.public_key`.
  - Headers: `X-Signature-Ed25519: <hex-signature>` (64-byte signature, hex-encoded)
  - Body: raw firmware binary (application/octet-stream)

Example (host):

curl -X POST \
  -H "Authorization: Bearer <API_TOKEN>" \
  -H "X-Signature-Ed25519: <hex-signature>" \
  --data-binary @firmware.bin \
  http://<node-ip>/api/v1/ota

Security notes:
- When `api.token` is set in config, all sensitive endpoints require `Authorization: Bearer <token>`.
- Keep the private signing key offline and only sign release artifacts.

## WebSocket
- /ws/console — real-time log stream and packet events
  - Messages: { "type": "log", "level": "info", "msg": "..." }
  - Messages: { "type": "packet", "dir": "rx|tx", "hex": "...", "decoded": {...} }

## Security & Auth (design)
- Web UI must be behind a password or token by default.
- Use HTTPS for remote access; otherwise restrict to local networks.
- Future: integrate signed JWT tokens and role-based control.

## Implementation notes
- Minimal server will be implemented as optional module compiled when `WEBSERVER_ENABLED`.
- Use AsyncWebServer (recommended) or WebServer for smaller footprint.
- Config persistence stored in `/config.json` when `JSON_CONFIG_ENABLED`.

---

This is a draft — endpoints and payloads will be finalized while implementing the Web UI feature.