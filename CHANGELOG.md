# ESP32 Reticulum Network Stack Gateway Node
## Revision History and Change Log
**Document Version:** 2.3  
**Date:** 2026-08-18  
**System Designation:** ESP32-RNS-GW-REV

---

## Revision History

Revisions are listed in reverse chronological order (most recent first).

---

### Firmware 0.3.2 — Fail-Closed Identity and RNS Link Control (2026-08-18)

- Missing or invalid managed config now locks WiFi/HTTP only. Identity-ready units still run USB KISS + ESP-NOW mesh from two nodes upward, with no sold fleet-size cap.
- Mesh tables are resource-bounded (64 route candidates, 80 announce-cache slots, 20 concurrent links). ESP-NOW TX stays on the broadcast peer so the driver peer table is not consumed by unicast neighbors.
- Confirmed link sends retry the same wire frame until an encrypted `LINKPROOF` arrives. Debug `say <text>` does the same for PLAIN mesh chat with an application ACK.

- Encrypt LRRTT and keepalive payloads with the same Fernet token as application data and `LINKCLOSE`, matching reference RNS.
- Bind persistent identities to the chip eFuse MAC, CRC-protect EEPROM keys, migrate v1 blobs, and generate a new identity on clone or corruption.
- Fail-closed USB-only boot when identity is missing or managed production config is invalid; LED fault-blinks; `status` reports `config_error`.
- Allow read-only `GET /api/v1/status` when saved config is invalid so operators can see `config_error` / `fail_closed`.
- Disable unauthenticated UDP metrics on the managed production image.
- Pump the mesh dataplane during OTA upload/write; reject non-ESP images (`0xE9`) before `Update.begin()`.
- Preserve WiFi password and API token when a GET-redacted config is written back.
- Gate KISS `0x06` diagnostics behind `DEBUG_ENABLED`; bound ESP-NOW peers; shed store-and-forward on low heap.
- Add unit tests for encrypted link control, forged LRPROOF, identity CRC/clone, ESP-NOW fragment CRC, store-forward drop-oldest, and route failover.
- Document the C3 product boundary honestly; BLE provisioning remains a reserved compile-time error.

### Firmware 0.3.1 — Production Hardening Release Candidate (2026-07-19)

- Hardened Reticulum packet validation, signed announces, identity encryption, authenticated link establishment, negotiated MTU handling, interface binding, routing failover, and ESP-NOW fragmentation/concurrency.
- Hardened HTTP parsing, exact Bearer authentication, secret redaction, atomic validated configuration writes, version-bound streaming signed OTA, and upload limits/timeouts.
- Added per-device provisioning and release-package verification, with one raw-Ed25519 public-key ID contract.
- Added AX.25/APRS and Fernet regression tests, hardware-acceptance CI, production build gates, static checks, and managed release documentation.
- Bounded the recent-announce cache during fresh floods, made route and ESP-NOW eviction wrap-safe, and propagated transport rejection into link handshake/data results.
- Centralized validation for preprovisioned and API-written configuration, exposed config health, enforced production feature invariants, and actively rolled back unhealthy pending OTA images.
- Added Reticulum compatibility to CI, runtime-config and announce-cache regression tests, Unix announce timestamps after time sync, and C/C++ index exclusions for generated SDK/build trees.
- Removed tracked build output and example secrets from source control and excluded generated trees from editor indexing.
- Corrected the documented product boundary: link sessions are authenticated/encrypted but do not yet implement automatic delivery ACK/retry; HAM/audio/Winlink-style support remains experimental.
- Classified 0.3.1 as suitable for controlled pilots only until physical HIL, OTA recovery/power-loss, Secure Boot/Flash Encryption, manufacturing, and regulatory gates pass.

---

### 2.3 — Provisioning Status and Commercialization Strategy (2026-05-18)

#### Productization Documentation
- Added `docs/COMMERCIALIZATION_STRATEGY.md` to define the product thesis, beachhead market,
  monetization model, and engineering priorities required for commercialization.
- Updated `docs/INDEX.md` to include commercialization strategy documentation.

#### Provisioning and Fleet API
- Added provisioning-oriented fields to `GET /api/v1/status`, including:
  - Stable `device_id`
  - `config_present`
  - `bootstrap_mode`
  - WiFi connection state and IP address
  - `restart_required` and `restart_reason`
- Added `X-Restart-Required` and `X-Restart-Reason` response headers to
  `POST /api/v1/config` so provisioning tooling can distinguish live-applied
  changes from saved-but-restart-required changes.

#### Runtime Config Handling
- Reworked runtime JSON config name loading to use a refreshable cache instead of
  one-time static initialization.
- Added explicit runtime config cache reload after config writes so API responses
  and status reporting reflect the saved configuration immediately.

#### Routing Policy
- Routing table now preserves multiple candidate paths per destination instead of
  replacing them with whichever announce arrived most recently.
- Route selection is now deterministic: lower hop count wins first, then interface
  priority, then freshest route.
- When the incoming interface is excluded during forwarding, the sender now selects
  the next-best usable route instead of dropping to whichever single route entry
  happened to be stored last.

#### Route Observability
- Preserved `route_count` as the count of distinct destinations even though the
  routing table now stores multiple candidates per destination.
- Added `route_candidate_count` and `route_candidates_by_interface` to status and
  metrics reporting so fleet tooling can observe failover-ready paths explicitly.
- Added `GET /api/v1/routes` to expose grouped per-destination route diagnostics,
  including candidate paths, the selected path, hop count, age, and next-hop data.

#### Interface Health Telemetry
- Added per-interface health snapshots to the status and metrics APIs.
- Each interface now reports support state, current usability, last RX/TX uptime
  timestamps, packet counters, and byte counters.
- Added interface activity accounting in the real ingress/egress paths for serial,
  ESP-NOW, WiFi UDP, LoRa, HAM modem, Bluetooth, and IPFS.

#### Runtime Route Policy
- Added `routing.interface_priority` runtime config keys so deployments can change
  interface tie-break priorities without recompiling firmware.
- `RoutingTable` now uses the effective runtime route-priority policy instead of
  compile-time macros alone.
- Added `route_priority_by_interface` to `/api/v1/status` so operators can verify
  the active routing policy being applied by a node.

#### API Contract Updates
- Updated `docs/API.md` and `docs/openapi.yaml` to document the expanded status
  schema, route diagnostics payloads, interface health payloads, and restart-required
  config write signaling.

### 2.2 — KISS-over-USB, ESP-NOW Mesh Confirmed, Repo Cleanup (2026-03-04)

#### KISS-over-USB TNC Fix
- **Root cause:** `ARDUINO_USB_MODE=1` in build flags disables the Arduino framework's
  auto-`Serial.begin()` in `app_main()` (guarded by `!ARDUINO_USB_MODE`). Without an
  explicit `Serial.begin()`, `HWCDC::begin()` never runs — no D+ pullup, no ring buffers,
  no ISR — and the USB device never enumerates on the host.
- **Fix:** Added explicit `Serial.begin()` call in the `KISS_OVER_USB` code path in `main.cpp`.
- **Verified:** Full relay chain PC → USB KISS → Board A → ESP-NOW → Board B confirmed
  working with `tests/kiss_usb_inject.py`.

#### ESP-NOW Mesh Confirmed
- Bidirectional ESP-NOW peer-to-peer communication tested on two ESP32-C3-DevKitM-1 boards.
- Announce-based automatic peer discovery working — nodes add each other's MACs as ESP-NOW
  peers when announce packets are received.
- Packets sent via KISS-over-USB on one node are relayed wirelessly to the other.

#### Build Fixes (14/15 environments pass)
- Fixed `Serial1` GPIO18/19 conflict on ESP32-C3 — redirected KISS UART to GPIO2 (RX) / GPIO4 (TX).
- Fixed HWCDC::begin() single-argument signature for KISS_OVER_USB mode.
- Fixed multiple build errors across ESP32, ESP32-S2, ESP32-S3, Heltec, and TTGO environments.
- Only `esp32-c3-bare` (ESP-IDF framework, not Arduino) does not build — this is expected.

#### New Files
- `src/kiss_usb_test.cpp` — progressive subsystem isolation test firmware for debugging USB issues.
- `tests/kiss_usb_inject.py` — raw fd KISS packet injector (no DTR/RTS reset) for testing KISS relay.
- `tests/espnow_relay_monitor.py` — ESP-NOW relay monitor for verifying wireless mesh.
- `tests/mesh_demo.py` — mesh demonstration script.

#### Repository Cleanup
- Removed tracked build logs, boot captures, temp scripts, and autogenerated sdkconfig files.
- Removed obsolete Windows COM16 debug scripts and `.tmp/` directory from tracking.
- Removed `src/main.original.cpp` backup and `temp_blink/` test project.
- Updated `.gitignore` to exclude build logs, sdkconfig files, Python caches, and OS files.
- Moved the old 600+ line README (technical specification) to `docs/TECHNICAL_SPECIFICATION.md`.
- Rewrote `README.md` as a practical GitHub-style project README with quick-start guide,
  hardware table, build environment reference, and known-quirks section.

#### ESP32-C3 USB Quirks Documented
- DTR/RTS auto-reset always enters bootloader mode — physical RESET required for app mode.
- HWCDC `connected` flag starts false — host must write data to trigger output.
- pyserial workarounds for avoiding board reset on port open.

---

### 2.1 Documentation Update (2026-02-20)
- Bumped document versions and dates across repository
- Added notes about Web UI, metrics endpoint, OTA support, BLE provisioning
- Marked v2.1 milestone items complete in roadmap
- Editorial corrections and accuracy updates

## 2.0 REVISION HISTORY

## Major Changes

### 1. Multi-Platform Support
- **Added support for ESP32-S2, ESP32-S3, ESP32-C5, and ESP32-C6**
  - Updated `platformio.ini` with build environments for all platforms
  - Added platform-specific UART pin configurations in `Config.h`
  - Fixed Bluetooth Classic availability detection (C3/C5/C6 don't support it)

### 2. Heltec LoRa32 v3 and v4 Support
- **Added dedicated build environments** for Heltec LoRa32 v3 (ESP32-S3) and v4 (ESP32-C6)
- **Configured LoRa pin definitions** specific to Heltec boards
- **Automatic board detection** via build flags (`HELTEC_LORA32_V3`, `HELTEC_LORA32_V4`)

### 3. LoRa Interface Support
- **Integrated RadioLib library** for LoRa communication
- **Added LoRa interface** to `InterfaceManager`:
  - `setupLoRa()` - Initializes SX1278 LoRa module
  - `processLoRaInput()` - Handles incoming LoRa packets
  - `sendPacketViaLoRa()` - Sends packets over LoRa
- **LoRa configuration** in `Config.h`:
  - Frequency: 915.0 MHz (configurable)
  - Bandwidth: 125 kHz
  - Spreading Factor: 7
  - Coding Rate: 5
  - Sync Word: 0x12
  - Output Power: 10 dBm
- **LoRa routing integration** - LoRa packets are included in routing and forwarding logic

### 4. Code Fixes and Improvements

#### Platform Compatibility
- **Fixed random seed generation** - Replaced `analogRead(A0)` with `esp_random()` for better cross-platform compatibility
- **Fixed UART pin definitions** - Platform-specific pin assignments for all ESP32 variants
- **Conditional Bluetooth compilation** - Bluetooth Classic code only compiles on supported platforms

#### Memory Management
- **Improved buffer allocation** - Using `std::unique_ptr` for automatic memory management
- **Better error handling** - Added null checks and error reporting

#### Code Quality
- **Fixed duplicate code** - Removed duplicate debug print statement
- **Fixed syntax error** - Corrected missing opening brace in `saveNodeAddress()`
- **Improved error messages** - More descriptive error reporting

### 5. Build System Updates

#### platformio.ini
- **Unified build configuration** - Common settings in `[env]` section
- **Multiple environments**:
  - `esp32-c3-devkitm-1` (default)
  - `esp32dev` (original ESP32)
  - `esp32-s2-devkitm-1`
  - `esp32-s3-devkitc-1`
  - `esp32-c5-devkitm-1`
  - `esp32-c6-devkitc-1`
  - `heltec_wifi_lora_32_V3`
  - `heltec_wifi_lora_32_V4`
- **Added RadioLib dependency** - Automatically included when LoRa is enabled

### 6. Configuration Enhancements

#### Config.h Updates
- **Platform detection macros** - Automatic platform-specific configuration
- **LoRa configuration section** - Comprehensive LoRa settings
- **Interface type enum** - Added `LORA` interface type
- **Bluetooth availability flag** - `BLUETOOTH_CLASSIC_AVAILABLE` macro

## Technical Details

### LoRa Implementation
- Uses RadioLib library (v6.7.0+) for SX1278 module control
- SPI communication via HSPI (ESP32-S2/S3/C3/C5/C6) or VSPI (ESP32)
- Automatic initialization on supported boards
- Broadcast-based communication (can be extended for addressing)

### Platform-Specific Notes

#### ESP32-C3, C5, C6
- No Bluetooth Classic support (BLE only)
- UART1 used for KISS interface
- Default pins: RX=18, TX=19

#### ESP32-S2
- Bluetooth Classic available
- UART2 used for KISS interface
- Default pins: RX=33, TX=34 (may need adjustment)

#### ESP32-S3
- Bluetooth Classic available
- UART2 used for KISS interface
- Default pins: RX=17, TX=18

#### ESP32 (Original)
- Bluetooth Classic available
- UART2 used for KISS interface
- Default pins: RX=16, TX=17

### Heltec LoRa32 Boards
- **V3 (ESP32-S3)**: LoRa pins pre-configured
- **V4 (ESP32-C6)**: LoRa pins pre-configured
- Both boards have integrated SX1278 LoRa modules

## Testing Recommendations

1. **Platform Testing**: Test on each ESP32 variant to verify UART and Bluetooth functionality
2. **LoRa Testing**: Verify LoRa communication on Heltec boards
3. **Multi-Interface**: Test packet forwarding between different interfaces (WiFi, ESP-NOW, LoRa, Serial)
4. **Memory Usage**: Monitor heap usage on memory-constrained variants (C3, C5, C6)

## Known Limitations

1. **LoRa Addressing**: Currently uses broadcast mode; point-to-point addressing can be added
2. **LoRa Range**: Limited by LoRa module power and antenna
3. **Platform-Specific Pins**: Some UART pins may need adjustment for custom boards
4. **Memory Constraints**: C3/C5/C6 have less RAM; monitor usage carefully

## Future Enhancements

1. LoRa addressing and routing
2. Dynamic LoRa parameter adjustment
3. RSSI-based routing metrics for LoRa
4. LoRa mesh networking support
5. Additional board support (TTGO, LilyGO, etc.)
6. Full AX.25 protocol support for HAM modem
7. Audio modem support (AFSK, Bell 202)
8. IPFS publishing support
9. IPFS content caching

## New Features Added (Latest Update)

### HAM Modem Support
- **KISS Protocol**: Full KISS framing support for HAM TNCs
- **APRS Support**: Basic APRS packet generation and transmission
- **TNC Integration**: Seamless integration with standard HAM radio TNCs
- **Multi-Interface Routing**: HAM modem packets integrated into Reticulum routing

### IPFS Integration
- **Lightweight Client**: Gateway-based IPFS content fetching
- **Content Addressing**: Support for IPFS hash references
- **HTTP Gateway Access**: Fetch content from public IPFS gateways
- **Reticulum Integration**: IPFS hashes can be embedded in Reticulum packets

### Enhanced Features (Latest)

#### Audio Modem Support
- **AFSK Modulation**: Audio Frequency Shift Keying for direct audio connection
- **Bell 202 Support**: Standard 1200 baud Bell 202 modem
- **Direct Radio Interface**: Bypass TNC, connect directly to radio audio
- **Real-time Processing**: Audio sample processing for modulation/demodulation

#### Full AX.25 Protocol
- **Complete Frame Encoding/Decoding**: Full AX.25 frame structure support
- **Address Encoding**: Proper callsign and SSID encoding
- **FCS Calculation**: Frame Check Sequence (CRC-16 CCITT)
- **Digipeater Support**: Repeater path encoding in frames
- **Control Frames**: I, S, and U frame types supported

#### Enhanced APRS
- **Position Reporting**: Send GPS coordinates with altitude
- **Weather Data**: Transmit temperature, humidity, pressure, and more
- **Messaging**: Send APRS messages to other stations
- **Status Reports**: Send status updates
- **Proper Formatting**: Correct APRS packet format encoding

#### IPFS Publishing
- **Local Node API**: Publish content via local IPFS node HTTP API
- **Content Upload**: Upload data and receive IPFS hash
- **JSON Response Parsing**: Automatic hash extraction from API response
- **Timeout Handling**: Configurable timeout for large uploads

#### Winlink Integration
- **HAM Email**: Send and receive email over packet radio
- **BBS Connection**: Connect to Winlink BBS stations
- **Message Queuing**: Automatic message queuing and delivery
- **Protocol Support**: Winlink protocol over AX.25

See `docs/HAM_MODEM.md`, `docs/IPFS_INTEGRATION.md`, and `docs/ENHANCED_FEATURES.md` for detailed documentation.

## Breaking Changes

None - All changes are backward compatible with existing ESP32-C3 configurations.

## Migration Guide

For existing users:
1. Update `platformio.ini` if using custom build environments
2. No code changes required for ESP32-C3
3. For other platforms, select appropriate environment in PlatformIO
4. For LoRa support, ensure `LORA_ENABLED` is defined in build flags

