# ESP32 Reticulum Network Stack Gateway Node

A multi-interface [Reticulum](https://reticulum.network/) gateway firmware for ESP32-series microcontrollers. Routes packets transparently between WiFi (UDP), ESP-NOW, Serial KISS, Bluetooth Classic, LoRa, and amateur-radio (AX.25/APRS) interfaces.

**Status:** Software release candidate for controlled pilots. Core routing, ESP-NOW mesh, and KISS-over-USB TNC mode have been exercised on ESP32-C3 hardware. Broad or unattended production deployment remains blocked on the hardware-security, HIL, manufacturing, and regulatory gates in `docs/PRODUCTION_READINESS.md`.

---

## Features

- **Multi-interface bridging** — packets received on any interface are forwarded to all others based on routing table or broadcast
- **ESP-NOW mesh** — automatic peer discovery via announce packets; zero-config wireless mesh between ESP32 nodes
- **KISS-over-USB TNC** — use an ESP32-C3 as a USB-connected TNC for desktop Reticulum instances
- **Announce-based routing** — distance-vector routing with 16-byte destination hashes, hop counting, and automatic route expiry
- **Authenticated encrypted links** — Reticulum-compatible X25519/Ed25519 handshake and Fernet session encryption
- **LoRa support** — long-range radio via SX1278-compatible modules (Heltec LoRa32 v3)
- **Experimental HAM radio** — AX.25/APRS framing and AFSK components for development builds; not production-qualified
- **HTTP REST API** — optional runtime configuration, signed OTA updates, and metrics endpoint
- **Multi-platform** — builds for ESP32, ESP32-C3, ESP32-S2, ESP32-S3, and Heltec LoRa boards

## Supported Hardware

| Board | Environment | Notes |
|---|---|---|
| ESP32-C3-DevKitM-1 | `esp32-c3-prod-managed` | Canonical managed production target |
| ESP32-C3 (KISS TNC) | `esp32-c3-kiss-usb` | USB = KISS port, debug on UART1 GPIO2/4 |
| ESP32 (original) | `esp32dev` | Bluetooth Classic available |
| ESP32-S2-Saola-1 | `esp32-s2-devkitm-1` | |
| ESP32-S3-DevKitC-1 | `esp32-s3-devkitc-1` | |
| TTGO T-OI Plus | `ttgo-t-oi-plus` | |
| Heltec LoRa32 v3 | `heltec_wifi_lora_32_V3` | LoRa + WiFi + ESP-NOW |

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 development board
- USB cable

### Build & Flash

```bash
git clone https://github.com/AkitaEngineering/ESP32-C3-Reticulum-Node
cd ESP32-C3-Reticulum-Node

# Build the canonical managed production environment
pio run

# Flash a dev/debug build to a connected board
pio run -e esp32-c3-devkitm-1 -t upload

# Monitor serial output (115200 baud)
pio device monitor -b 115200
```

### Configure a production unit

Generate a unique per-device config with WiFi credentials, API token, and OTA public key. Provision the generated file as `/config.json` in SPIFFS before the unit is connected to its field network. Production firmware deliberately disables unauthenticated network bootstrap.

```bash
./tools/make_device_config.py --device-id RNS-000001 --node-name field-001 \
  --wifi-ssid field-net --wifi-password "$WIFI_PASSWORD" \
  --api-public-key "$OTA_PUBLIC_KEY"
```

### Two-Node ESP-NOW Mesh

1. Flash two boards with the default environment (same WiFi credentials)
2. Power both boards and open serial monitors
3. Within about 35 seconds they exchange announce packets and add each other as ESP-NOW peers
4. Packets sent to one node's destination hash are relayed wirelessly to the other

### KISS-over-USB TNC Mode

Use an ESP32-C3 as a USB-attached KISS TNC for a desktop Reticulum instance:

```bash
pio run -e esp32-c3-kiss-usb -t upload
```

After flashing, press the **physical RESET button** (the ESP32-C3 USB Serial/JTAG auto-reset always enters bootloader mode; a manual reset is required to boot into application firmware).

In this mode:
- **USB CDC** carries KISS-framed Reticulum packets (the TNC interface)
- **UART1** (GPIO2 RX / GPIO4 TX) carries debug output at 115200 baud
- Packets received via USB KISS are forwarded over ESP-NOW (and vice versa)

> **Important:** On the host side, you must configure Reticulum to use a **KISSInterface**, not a `SerialInterface`.
> `SerialInterface` expects HDLC framing (0x7E), while the ESP32 firmware sends KISS framing (0xC0). If you use `SerialInterface` you will see "malformed packet" errors.

Example Reticulum configuration (`~/.reticulum/config`):

```ini
[[ESP32_KISS]]
  type = KISSInterface
  enabled = yes
  port = /dev/ttyACM0
  speed = 115200
```

> **ESP32-C3 USB Note:** The HWCDC `connected` flag starts `false`. The host must write at least one byte to the device before `Serial.write()` output will flow. Most serial tools do this automatically; for raw `open()` calls, write a newline after connecting.

## Build Environments

| Environment | Purpose |
|---|---|
| `esp32-c3-prod-managed` | **Default/canonical:** USB KISS, ESP-NOW, WiFi UDP, authenticated API, metrics, signed OTA |
| `esp32-c3-devkitm-1` | Default dev/debug build (USB CDC, debug enabled) |
| `esp32-c3-kiss-usb` | KISS TNC over USB CDC |
| `esp32-c3-demo-sender` | Sends periodic test packets (demo mode) |
| `esp32-c3-prod` | Production build (`-Os`, no debug, no demo traffic) |
| `esp32-c3-prod-usb` | Production USB/KISS build with runtime JSON naming config enabled |
| `esp32-c3-prod-metrics` | Production + metrics/heartbeat endpoint |
| `esp32-c3-web` | HTTP REST API, OTA, JSON config |
| `esp32-c3-debug` | WiFi disabled, `-Og` debug optimizations |
| `esp32-c3-minimal` | Minimal main (build sanity check) |
| `esp32-c3-kiss-usb-test` | Progressive USB subsystem isolation test |
| `esp32dev` | Original ESP32 target |
| `esp32-s2-devkitm-1` | ESP32-S2 target |
| `esp32-s3-devkitc-1` | ESP32-S3 target |
| `ttgo-t-oi-plus` | TTGO T-OI Plus (web + OTA) |
| `heltec_wifi_lora_32_V3` | Heltec LoRa32 v3 (LoRa enabled) |

Build the default environment: `pio run`

Build a specific environment:

```bash
pio run -e esp32-c3-devkitm-1
```

### Runtime JSON Config

The default production target is `esp32-c3-prod-managed`. It reads `/config.json` from SPIFFS before network initialization.

- `node_name`: required for managed production; use a unique safe name of 1–47 characters. Offline builds may omit it to use the MAC-derived default such as `rns-6497AC`.
- `rns_app_name`: required for managed production; use a safe Reticulum application name of 1–63 characters.
- `wifi.ssid` / `wifi.password`: boot-time WiFi station credentials.
- `api.token`: required per-device API token (at least 32 characters).
- `api.public_key`: required 32-byte Ed25519 OTA public key in hex.
- `data/config.example.json`: editable template only; its `CHANGE_ME` values and empty OTA key are deliberately rejected by managed production firmware.

For a factory filesystem upload, temporarily stage the generated file as the ignored `data/config.json`, upload it, and remove the staged copy:

```bash
cp provisioning/devices/RNS-000001.config.json data/config.json
pio run -e esp32-c3-prod-managed -t uploadfs --upload-port /dev/serial/by-id/<board>
rm data/config.json
```

Do not commit generated configs or manifests; both contain credentials and are ignored by Git.

## Project Structure

```
include/              Header files (Config.h, all module headers)
src/                  Firmware source code
  main.cpp            Entry point (setup/loop)
  ReticulumNode.cpp   Core application controller
  InterfaceManager.cpp Interface abstraction & packet routing
  KISS.cpp            KISS protocol encoder/decoder
  ReticulumPacket.cpp Packet serialization/deserialization
  RoutingTable.cpp    Route storage & announce-based discovery
  Link.cpp            Authenticated encrypted link sessions
  LinkManager.cpp     Link lifecycle management
  AX25.cpp            AX.25 frame encoding
  AudioModem.cpp      AFSK audio modem
  WebServer.cpp       HTTP REST API and signed OTA
  Config.cpp          Runtime JSON configuration
  Utils.cpp           Utility functions
  Winlink.cpp         Experimental Winlink-style message adapter

test/                 PlatformIO unit tests
tests/                Hardware integration & debug scripts (Python)
  kiss_usb_inject.py  KISS packet injector (raw fd, no DTR/RTS reset)
  espnow_debug.py     Send debug commands to a node
  kiss_roundtrip.py   KISS encode/decode round-trip test
  send_and_sniff.py   Packet TX/RX verification
  ...

docs/                 Technical documentation
tools/                Build, provisioning, and OTA scripts
data/                 Example configuration files
```

## Configuration

All compile-time settings are in `include/Config.h`. Key parameters:

| Parameter | Default | Description |
|---|---|---|
| `wifi.ssid` / `wifi.password` | — | Runtime WiFi credentials in `/config.json` |
| `RNS_UDP_PORT` | 4242 | Reticulum UDP transport port |
| `ANNOUNCE_INTERVAL_MS` | 30000 | Announce broadcast interval (ms) |
| `MAX_HOPS` | 128 | Maximum packet hop count |
| `MAX_ROUTES` | 20 | Maximum routing table entries |
| `KISS_SERIAL_SPEED` | 115200 | KISS UART baud rate |

Build flags control feature inclusion:

| Flag | Effect |
|---|---|
| `-DDEBUG_ENABLED=1` | Enable verbose serial debug output |
| `-DKISS_OVER_USB=1` | USB CDC = KISS port, debug on UART1 |
| `-DDEMO_TRAFFIC_ENABLED=1` | Send periodic test packets |
| `-DWEBSERVER_ENABLED=1` | Enable the HTTP REST API |
| `-DJSON_CONFIG_ENABLED=1` | Runtime JSON config (SPIFFS) |
| `-DOTA_ENABLED=1` | Signed OTA firmware updates |
| `-DLORA_ENABLED=1` | Enable LoRa radio interface |
| `-DHAM_MODEM_ENABLED=1` | Enable experimental HAM components; rejected by `PRODUCTION_BUILD` |
| `-DMETRICS_ENABLED=1` | Enable /metrics endpoint |
| `-DBLE_PROVISIONING_ENABLED=1` | Reserved for BLE GATT provisioning; not included in production builds |

## Debug Commands

Send a local command packet (context `0xB0`) over the serial or Bluetooth KISS interface. This node-specific context is consumed locally and is never forwarded. Payload format: `[8-byte dest address (zeros = self)] [ASCII command]`

| Command | Response |
|---|---|
| `routes` | Print the current routing table |
| `peers` | Print registered ESP-NOW peers |
| *(empty payload)* | Ping — node prints "alive" confirmation |

## Documentation

| Document | Description |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture and data flows |
| [docs/PACKET_FORMATS.md](docs/PACKET_FORMATS.md) | Reticulum wire format specification |
| [docs/KISS_INTERFACE.md](docs/KISS_INTERFACE.md) | KISS protocol implementation details |
| [docs/LINK_LAYER.md](docs/LINK_LAYER.md) | Authenticated encrypted link sessions and limitations |
| [docs/API.md](docs/API.md) | HTTP REST API reference |
| [docs/HAM_MODEM.md](docs/HAM_MODEM.md) | Amateur radio interface guide |
| [docs/IPFS_INTEGRATION.md](docs/IPFS_INTEGRATION.md) | IPFS content addressing |
| [docs/ENHANCED_FEATURES.md](docs/ENHANCED_FEATURES.md) | Advanced features documentation |
| [docs/TECHNICAL_SPECIFICATION.md](docs/TECHNICAL_SPECIFICATION.md) | Full technical specification |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Project roadmap |

## Known Quirks

### ESP32-C3 USB Serial/JTAG

The ESP32-C3's built-in USB Serial/JTAG controller has some important behaviors:

- **Auto-reset enters bootloader:** Toggling DTR/RTS (as PlatformIO and most serial tools do) resets the chip into download/bootloader mode, not application mode. After flashing, you **must press the physical RESET button** to run the firmware.
- **HWCDC connected flag:** The USB CDC `connected` flag starts `false`. Output via `Serial.write()` is silently discarded until the host writes data to the device (triggering the `OUT_RECV_PKT` interrupt). Most serial monitors do this automatically on open.
- **Port numbering:** USB port numbers (`/dev/ttyACMx`) may change after re-enumeration. Use `udevadm info` or check the MAC-based serial number to identify boards.

### pyserial and ESP32-C3

For host-side capture, pyserial is more reliable than a raw `open()` because asserting DTR makes the USB CDC connection become active consistently. Keep RTS low to avoid unwanted reset behavior:

```python
import serial
ser = serial.Serial(port, 115200, dsrdtr=False, rtscts=False)
ser.dtr = True
ser.rts = False
```

## Testing

Compile unit-test firmware without an attached board:
```bash
pio test -e test --without-uploading --without-testing
```

Run unit tests on a connected board:
```bash
pio test -e test --test-port /dev/serial/by-id/<board>
```

Integration tests (require connected hardware):
```bash
python tests/kiss_usb_inject.py          # KISS relay: PC -> USB -> Board A -> ESP-NOW -> Board B
python tests/espnow_debug.py             # Send debug commands to a node
python tests/kiss_roundtrip.py           # KISS encode/decode verification
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines. PRs should be small and focused, include tests where applicable, and compile with `pio run -e esp32-c3-devkitm-1` before submission.

## Security

See [SECURITY.md](SECURITY.md) for responsible disclosure policy and deployment guidance (Secure Boot, Flash Encryption, signed OTA).

## License

See [LICENSE](LICENSE) for details.

---

**Akita Engineering** — [github.com/AkitaEngineering](https://github.com/AkitaEngineering)
