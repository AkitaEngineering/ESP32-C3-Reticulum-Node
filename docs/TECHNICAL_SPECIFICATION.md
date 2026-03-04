# ESP32 Reticulum Network Stack Gateway Node
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW

**Revision:** v2.1 adds Web UI, runtime JSON config, /metrics endpoint, signed OTA support, BLE provisioning macros and updated documentation.

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications, operational procedures, and system requirements for the ESP32 Reticulum Network Stack (RNS) Gateway Node firmware. The system provides a multi-interface network gateway capable of bridging multiple physical and logical network interfaces using the Reticulum protocol stack.

### 1.2 Applicability
This specification applies to all ESP32-based hardware platforms including but not limited to:
- ESP32-C3 series microcontrollers
- ESP32-S2 series microcontrollers
- ESP32-S3 series microcontrollers
- ESP32-C5 series microcontrollers
- ESP32-C6 series microcontrollers
- ESP32 (original) series microcontrollers
- Heltec LoRa32 v3/v4 development boards

> **ESP-NOW note:** On platforms with WiFi/ESP-NOW support (all above), ensure nodes are configured
> to use the same WiFi channel. The firmware currently connects to a WiFi network at boot; nodes
> must either join the same AP or be forced to the same channel manually (see `esp_wifi_set_channel`).
> Peers are added automatically when announce packets are received, and removed when routes expire.

### 1.3 Document Structure
- Section 2.0: System Overview and Architecture
- Section 3.0: Technical Specifications
- Section 4.0: Hardware Requirements
- Section 5.0: Software Requirements
- Section 6.0: Installation and Configuration Procedures
- Section 7.0: Operational Procedures
- Section 8.0: Interface Specifications
- Section 9.0: Performance Characteristics
- Section 10.0: Maintenance and Troubleshooting

---

Web API Documentation
- The firmware exposes a small REST API when the Web UI is enabled. See [docs/API.md](docs/API.md) for a concise reference of endpoints, authentication, and OTA upload usage.

---

## 2.0 SYSTEM OVERVIEW

### 2.1 System Description
The ESP32-RNS-GW is a firmware implementation that transforms ESP32-series microcontrollers into multi-interface Reticulum Network Stack gateway nodes. The system provides transparent packet routing and bridging capabilities across heterogeneous network interfaces, enabling seamless communication between devices operating on different physical layers.

### 2.2 System Architecture
The system implements a modular architecture consisting of the following primary components:

#### 2.2.1 Core Components
- **ReticulumNode**: Central application controller managing all subsystems
- **InterfaceManager**: Physical and logical interface abstraction layer
- **RoutingTable**: Dynamic routing information base (RIB)
- **LinkManager**: Reliable transport layer management
- **Link**: Point-to-point reliable connection state machine
- **KISSProcessor**: KISS protocol framing processor
- **ReticulumPacket**: Packet serialization/deserialization engine

#### 2.2.2 Network Interfaces
The system supports the following interface types:
1. **WiFi UDP**: IEEE 802.11 wireless local area network, UDP transport
2. **ESP-NOW**: Espressif proprietary peer-to-peer protocol
3. **Serial UART**: Asynchronous serial communication with KISS framing
4. **Bluetooth Classic**: IEEE 802.15.1 serial profile with KISS framing
5. **LoRa**: Long-range radio communication (SX1278 modules)
6. **HAM Modem**: Amateur radio TNC interface with AX.25 protocol
7. **IPFS**: InterPlanetary File System content addressing

### 2.3 Operational Modes
- **Gateway Mode**: Transparent packet forwarding between interfaces
- **Node Mode**: Endpoint node with local application processing
- **Hybrid Mode**: Simultaneous gateway and node operation

---

## 3.0 TECHNICAL SPECIFICATIONS

### 3.1 Protocol Compliance
- **Reticulum Network Stack**: Compatible with Reticulum protocol specification
- **KISS Protocol**: RFC 1055 compliant (with extensions)
- **AX.25 Protocol**: Amateur Packet Radio Protocol, Version 2.2 compliant
- **APRS Protocol**: Automatic Packet Reporting System specification compliant
- **IPFS Protocol**: InterPlanetary File System gateway API compatible

### 3.2 Network Layer Specifications

#### 3.2.1 Address Format
- **RNS Address Size**: 8 bytes (64 bits)
- **Address Type**: Cryptographically derived or randomly generated
- **Address Persistence**: Stored in non-volatile memory (EEPROM)

#### 3.2.2 Packet Format
- **Maximum Packet Size**: 219 bytes (19-byte header + 200-byte payload)
- **Header Size**: 19 bytes (RNS Header Type 1)
- **Maximum Payload**: 200 bytes (configurable via `RNS_MAX_PAYLOAD`)
- **Hop Limit**: 15 hops (configurable via `MAX_HOPS`)

#### 3.2.3 Routing Protocol
- **Protocol Type**: Distance-vector with announce-based discovery
- **Route Update Interval**: 180 seconds (configurable)
- **Route Timeout**: 555 seconds (3 × announce interval + 15 seconds)
- **Maximum Routes**: 20 entries (configurable)
- **Route Metrics**: Hop count, last-heard timestamp

### 3.3 Transport Layer Specifications

#### 3.3.1 Link Layer
- **Reliability Mechanism**: Acknowledgment-based with retransmission
- **Window Size**: 1 packet (simplified implementation)
- **Maximum Retries**: 3 attempts per packet
- **Link Request Timeout**: 10 seconds
- **Data Packet Timeout**: 5 seconds
- **Maximum Active Links**: 10 concurrent connections

#### 3.3.2 Sequence Numbers
- **Sequence Number Size**: 16 bits (0-65535)
- **Sequence Space**: Circular with wraparound detection
- **Initial Sequence**: Random or zero-based

### 3.4 Interface Specifications

#### 3.4.1 WiFi UDP Interface
- **Transport Protocol**: User Datagram Protocol (UDP)
- **Port Number**: 4242 (default, configurable)
- **Address Resolution**: Broadcast for discovery, unicast for routing
- **MTU**: Limited by WiFi frame size (typically 1500 bytes)

#### 3.4.2 ESP-NOW Interface
- **Protocol**: Espressif ESP-NOW proprietary protocol
- **Peer Management**: Broadcast peer added on startup. Unicast peers are automatically registered when
  they appear in announce messages and removed once routes time out. See section 6.3.6 for troubleshooting.
- **Maximum Peers**: 20 (ESP32 hardware limitation)
- **Frame Size**: 250 bytes maximum
- **Encryption**: Optional (configurable)

#### 3.4.3 Serial Interface (KISS)
- **Baud Rate**: 115200 bps (configurable)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Framing**: KISS protocol (FEND/FESC encoding)

#### 3.4.4 Bluetooth Classic Interface
- **Profile**: Serial Port Profile (SPP)
- **Baud Rate**: 115200 bps (logical)
- **Framing**: KISS protocol
- **Availability**: ESP32, ESP32-S2, ESP32-S3 only

#### 3.4.5 LoRa Interface
- **Modulation**: LoRa (Long Range)
- **Frequency Range**: 915.0 MHz (configurable, region-dependent)
- **Bandwidth**: 125 kHz (configurable)
- **Spreading Factor**: 7 (configurable)
- **Coding Rate**: 5 (configurable)
- **Output Power**: 10 dBm (configurable)

#### 3.4.6 HAM Modem Interface
- **Protocol**: KISS over serial, AX.25 over packet radio
- **Baud Rate**: 9600 bps (TNC interface, configurable)
- **Audio Modem**: AFSK 1200/2200 Hz (Bell 202 compatible)
- **Sample Rate**: 8000 Hz (audio processing)

#### 3.4.7 IPFS Interface
- **Access Method**: HTTP gateway client
- **Gateway URL**: Configurable (default: https://ipfs.io/ipfs/)
- **Maximum Content Size**: 10 KB (configurable)
- **Timeout**: 10 seconds (fetch), 30 seconds (publish)

---

## 4.0 HARDWARE REQUIREMENTS

### 4.1 Minimum Hardware Requirements
- **Microcontroller**: ESP32-series (any variant)
- **Flash Memory**: 4 MB minimum (8 MB recommended)
- **RAM**: 320 KB minimum (520 KB recommended)
- **EEPROM**: 16 bytes minimum (for address storage)

### 4.2 Platform-Specific Requirements

#### 4.2.1 ESP32-C3
- **UART**: UART0 (debug), UART1 (KISS interface)
- **GPIO Pins**: by default UART1 is remapped to avoid interfering with USB
  D+/D- lines (GPIO18/19); the firmware uses GPIO2 (RX) and GPIO4 (TX) for
  the KISS interface.  Do **not** use 18/19 for any other purpose if native
  USB CDC is required.
- **Bluetooth**: Not available (BLE only)

#### 4.2.2 ESP32-S2
- **UART**: UART0 (debug), UART2 (KISS interface)
- **GPIO Pins**: 33 (RX), 34 (TX) for UART2 (configurable)
- **Bluetooth**: Available

#### 4.2.3 ESP32-S3
- **UART**: UART0 (debug), UART2 (KISS interface)
- **GPIO Pins**: 17 (RX), 18 (TX) for UART2
- **Bluetooth**: Available

#### 4.2.4 ESP32 (Original)
- **UART**: UART0 (debug), UART2 (KISS interface)
- **GPIO Pins**: 16 (RX), 17 (TX) for UART2
- **Bluetooth**: Available

### 4.3 Optional Hardware
- **LoRa Module**: SX1278-compatible (for LoRa interface)
- **HAM TNC**: KISS-compatible terminal node controller
- **Audio Interface**: ADC/DAC for audio modem operation
- **External Antenna**: For improved range (WiFi, LoRa, HAM)

---

## 5.0 SOFTWARE REQUIREMENTS

### 5.1 Development Environment
- **PlatformIO**: Version 6.0 or later (recommended)
- **Arduino IDE**: Version 2.0 or later (alternative)
- **VSCode**: With PlatformIO extension (recommended)

### 5.2 Required Libraries
- **Espressif ESP32 Core**: Version 3.0.0 or later
- **RadioLib**: Version 6.7.0 or later (for LoRa support)
- **HTTPClient**: Included in ESP32 core (for IPFS)
- **WiFi**: Included in ESP32 core
- **BluetoothSerial**: Included in ESP32 core (where applicable)

### 5.3 Build System Requirements
- **Compiler**: GCC for Xtensa (ESP32) or RISC-V (ESP32-C3/C6)
- **C++ Standard**: C++11 or later
- **Build System**: PlatformIO build system or Arduino build system

### 5.4 Configuration Requirements
- **WiFi Credentials**: SSID and password (for WiFi interface)
- **Node Address**: Auto-generated on first boot, stored in EEPROM
- **Interface Selection**: Enabled via build flags

---

## 6.0 INSTALLATION AND CONFIGURATION PROCEDURES

### 6.1 Pre-Installation Requirements
1. Verify hardware compatibility (Section 4.0)
2. Install development environment (Section 5.1)
3. Install required libraries (Section 5.2)
4. Obtain WiFi credentials (if WiFi interface required)

### 6.2 Installation Procedure

#### 6.2.1 Repository Acquisition
```bash
git clone https://github.com/AkitaEngineering/ESP32-C3-Reticulum-Node
cd ESP32-C3-Reticulum-Node
```

#### 6.2.2 Configuration
1. Open `include/Config.h`
2. Configure WiFi credentials:
   ```cpp
   const char *WIFI_SSID = "your_ssid";
   const char *WIFI_PASSWORD = "your_password";
   ```
3. Configure interface selection via `platformio.ini` build flags
4. Configure platform-specific parameters (UART pins, etc.)

#### 6.2.3 Build Procedure
**PlatformIO Method:**
```bash
pio run -e <environment_name>
```

**Arduino IDE Method:**
1. Open `src/main.cpp` as sketch
2. Select target board from Tools menu
3. Select COM port
4. Click Upload

#### 6.2.4 Verification
1. Connect to serial monitor (115200 baud)
2. Verify boot messages
3. Verify node address generation/loading
4. Verify interface initialization

### 6.3 Post-Installation Configuration

#### 6.3.1 Interface Configuration
- **WiFi**: Configure SSID/password in `Config.h`
- **LoRa**: Configure frequency, bandwidth, spreading factor
- **HAM Modem**: Configure callsign, SSID, TNC baud rate
- **IPFS**: Configure gateway URL (if different from default)

#### 6.3.2 Network Configuration
- **Subscribed Groups**: Add group addresses in `SUBSCRIBED_GROUPS`
- **Routing Parameters**: Adjust timeouts if needed
- **Link Parameters**: Adjust retry/timeout values if needed

#### 6.3.3 Web UI / API Configuration
- **API Token**: Set `api.token` in runtime JSON config or interact via `/api/v1/config`.
- **OTA Public Key**: When `OTA_ENABLED` is enabled, provide `api.public_key` in config (hex‑encoded Ed25519).
- **Access**: Use browser or curl against `http://<device>/api/v1/...` after Web UI is enabled.

#### 6.3.4 BLE Provisioning
- **Enable**: build with `-DBLE_PROVISIONING_ENABLED=1`.
- **Procedure**: Connect over BLE GATT and write WiFi credentials or callsign using the provisioning service (implementation stubbed).

#### 6.3.5 Metrics Endpoint
- **Enable**: build with `-DMETRICS_ENABLED=1`.
- **Access**: GET `/api/v1/metrics` returns JSON with uptime, heap, active links, route count and allows adjusting log levels.

### 6.3.6 ESP-NOW Troubleshooting & Configuration
- **Channel matters**: ESP-NOW devices must share a WiFi channel. The firmware inherits the channel
  of the AP it connects to. For ad‑hoc operation you can call `esp_wifi_set_channel(<chan>, WIFI_SECOND_CHAN_NONE)`
  before `esp_now_init()` (modify `setupESPNow()` if needed).
- **Peer log**: Incoming ESP-NOW packets are logged with sender MAC; route additions automatically
  add the peer. Check serial output for lines starting `IF: Added ESP-NOW peer`.
- **Failures**: If `esp_now_send()` returns an error, the MAC may not be a registered peer or the
  channel mismatched. The node will attempt to add the peer and fall back to broadcast.
- **Reset peer list**: Use `esp_now_deinit()`/`esp_now_init()` sequence to clear all peers if the
  peer table becomes stale (e.g. after a network topology change).
- **Testing**: Build two nodes with identical WiFi credentials, power them up, and monitor serial
  logs. They should exchange announce packets every 180 s and show routes with Interface=3 (ESP_NOW).
  Use `RoutingTable.print()` via the serial console to inspect learned entries.

---

### 6.4 Production defaults and notes
- **Debugging:** Debug output is conservative by default. The project includes a `DebugSerial` shim and `DEBUG_ENABLED` build flag; set `DEBUG_ENABLED` to 1 only when actively debugging.
- **Demo traffic:** Periodic demo/send behavior is disabled by default (`DEMO_TRAFFIC_ENABLED = 0`) to avoid generating network traffic in production builds.
- **ESP-NOW peer management:** The `RoutingTable` evicts stale ESP-NOW peers when routes are replaced; the `InterfaceManager` removes associated ESP-NOW peers to keep the peer list consistent.
- **Builds:** Use PlatformIO to build per-environment or all environments. Examples:
   - Build default env: `pio run -e esp32-c3-devkitm-1`
   - Build all envs: `pio run`
   - List available envs: `pio run --list-targets`
- **Tooling:** Keep PlatformIO and Espressif cores up to date (`pip install -U platformio` and `pio update`) to access the latest board definitions and toolchains.
- **Tests:** Test scripts are in the `tests/` directory; some tests are hardware-dependent (serial ports, radio modules) and require the corresponding devices connected and configured. Run individual scripts with `python tests/<script>.py`.
  - `espnow_debug.py` – send local KISS commands (`routes`/`peers`) to a node and print its responses.


## 7.0 OPERATIONAL PROCEDURES

### 7.1 Building for Production

#### Log Levels

The firmware provides a simple log‑level mechanism controlled by the
`LOG_LEVEL` macro (defined in `include/Log.h`).  Levels are:

- 0: none (no output)
- 1: error
- 2: warn *(default for production)*
- 3: info
- 4: debug *(default when `DEBUG_ENABLED=1`)*

Override the default by adding `-DLOG_LEVEL=N` to your build flags or by
setting `LOG_LEVEL` in `Config.h` prior to including `Log.h`.

### 7.1 Building for Production

The `platformio.ini` file contains several environments.  The default
`esp32-c3-devkitm-1` prefix is intended for development and debugging.  For
production use the `esp32-c3-prod` environment which:

1. Compiles with `-Os` for size and enables `-Wall -Werror` to catch warnings.
2. Disables all debug logging (`DEBUG_ENABLED=0`).
3. Strips symbols and omits demo traffic.

Build with:

```bash
# base production firmware
pio run -e esp32-c3-prod
# include HTTP metrics endpoint and UDP heartbeat as well
pio run -e esp32-c3-prod -D METRICS_ENABLED=1 -D METRICS_UDP_ENABLED=1
# or upload directly:
pio run -e esp32-c3-prod -t upload
```

You can adjust the `LOG_LEVEL` macro at compile time if you need more or less
runtime verbosity (see section 7.4).


### 7.1 System Startup Procedure
1. Apply power to ESP32 device
2. Observe serial monitor output (115200 baud)
3. Verify initialization sequence:
   - EEPROM initialization
   - Node address generation/loading
   - Interface initialization
   - Routing table initialization
4. Verify "Setup Complete" message

### 7.2 Normal Operation
- System operates autonomously after initialization
- Periodic announce packets transmitted (every 180 seconds)
- Routing table updated automatically
- Packets forwarded based on routing decisions
- Link layer manages reliable connections

### 7.3 Interface-Specific Operations

#### 7.3.1 KISS Interface Operation
1. Connect serial device to configured UART pins
2. Configure serial device for KISS protocol
3. Send/receive KISS-framed Reticulum packets
4. Monitor via serial debug output

**Debug commands:**
- You can send a local command packet (context `0xFE`) over serial or Bluetooth to control the node.
  The payload should begin with an 8‑byte destination address (your own address or all zeros) followed
  by an ASCII command string.
  - `routes` – print the current routing table
  - `peers` – print registered ESP‑NOW peers

Example (using Python KISS helper):
```python
# build payload: dest=all zeros + ASCII
cmd = b"\x00"*8 + b"routes"
# send as KISS frame ...
```
These commands are useful when exercising ESP‑NOW networks, since the results appear on the
serial console immediately.
#### 7.3.2 WiFi Interface Operation
1. Verify WiFi connection status
2. Monitor UDP port 4242 for incoming packets
3. Verify announce packet transmission
4. Monitor routing table for learned routes

#### 7.3.3 LoRa Interface Operation
1. Verify LoRa module initialization
2. Monitor for incoming LoRa packets
3. Verify transmission success
4. Monitor signal quality (if available)

### 7.4 Shutdown Procedure
1. Gracefully close active links (if possible)
2. Save persistent state (if modified)
3. Power down device

---

## 8.0 INTERFACE SPECIFICATIONS

### 8.1 KISS Protocol Interface

#### 8.1.1 Frame Structure
```
[FEND] [CMD] [DATA...] [FEND]
```

#### 8.1.2 Special Characters
- **FEND**: 0xC0 (Frame End)
- **FESC**: 0xDB (Frame Escape)
- **TFEND**: 0xDC (Transposed FEND)
- **TFESC**: 0xDD (Transposed FESC)

#### 8.1.3 Command Byte
- **0x00**: Data frame
- **0x01-0x0F**: TNC configuration (not used)

### 8.2 Reticulum Packet Format

#### 8.2.1 Header Structure (Type 1)
```
Byte 0:     Flags (packet type, destination type, propagation, etc.)
Byte 1:     Hops
Bytes 2-17: Destination hash (16 bytes)
Byte 18:    Context
Bytes 19+:  Payload data
```

#### 8.2.2 Packet Types
- **0x00**: DATA packet
- **0x01**: ANNOUNCE packet
- **0x02**: LINKREQ packet
- **0x03**: PROOF packet

### 8.3 AX.25 Frame Format

#### 8.3.1 Frame Structure
```
[FLAG] [DEST ADDR] [SRC ADDR] [DIGI ADDRS...] [CTRL] [PID] [INFO] [FCS] [FLAG]
```

#### 8.3.2 Address Format
- **6 bytes**: Callsign (shifted left 1 bit)
- **1 byte**: SSID and control bits

#### 8.3.3 FCS Calculation
- **Algorithm**: CRC-16 CCITT (polynomial 0x8408, reversed)
- **Initial Value**: 0xFFFF
- **Final XOR**: 0xFFFF

---

## 9.0 PERFORMANCE CHARACTERISTICS

### 9.1 Throughput Specifications
- **Maximum Packet Rate**: ~10 packets/second (interface-dependent)
- **Maximum Payload Throughput**: ~2 KB/s (interface-dependent)
- **Link Establishment Time**: <1 second (typical)

### 9.2 Latency Specifications
- **Local Processing Latency**: <10 ms (typical)
- **Interface Transmission Latency**: Interface-dependent
  - WiFi UDP: <50 ms (local network)
  - ESP-NOW: <20 ms
  - Serial: <100 ms (115200 baud)
  - LoRa: 100-500 ms (depending on spreading factor)

### 9.3 Resource Utilization
- **Flash Usage**: ~200-400 KB (depending on enabled features)
- **RAM Usage**: ~50-150 KB (depending on active connections)
- **CPU Usage**: <20% (typical operation)

### 9.4 Reliability Specifications
- **Link Reliability**: >95% (with retransmission)
- **Route Discovery**: >90% (within 3 announce intervals)
- **Packet Delivery**: >85% (network-dependent)

---

## 10.0 MAINTENANCE AND TROUBLESHOOTING

### 10.1 Preventive Maintenance
- **Periodic Checks**: Verify system operation weekly
- **Memory Monitoring**: Check free heap, stack high‑water mark and overall heap usage printed by the firmware every 15 s when debug logs are enabled.  Look for steady decreases or unusually low free heap values.  When `METRICS_ENABLED` is set the device will also broadcast a JSON telemetry heartbeat over UDP to port `METRICS_UDP_PORT` (default 4243) whenever the memory check runs.
- **Route Table**: Monitor route table size and staleness
- **Link Status**: Monitor active link count (also printed with memory stats)

### 10.2 Troubleshooting Procedures

#### 10.2.1 System Not Booting
1. Verify power supply voltage and current
2. Check serial monitor for error messages
3. Verify flash memory integrity
4. Attempt firmware reflash

#### 10.2.2 WiFi Connection Failure
1. Verify SSID and password configuration
2. Check WiFi signal strength
3. Verify router compatibility (2.4 GHz required)
4. Check for IP address assignment

#### 10.2.3 Packet Loss
1. Verify interface initialization
2. Check signal strength (wireless interfaces)
3. Monitor routing table for valid routes
4. Verify hop count limits

#### 10.2.4 Memory Issues
1. Monitor free heap via serial output
2. Reduce maximum routes/links if neededb
3. Reduce payload size if needed
4. Disable unused interfaces

#### 10.2.5 Node Appears Dead / Not Responding
1. Send a simple "ping" packet to the node's address (payload consists of the ASCII
   string `"ping"`).  A correctly functioning node will automatically reply with
   a `"pong"` payload.  The `tests/send_and_sniff.py` script demonstrates this
   behaviour.
2. Alternatively, issue a zero‑length LOCAL_CMD over the serial/BT interface; the
   node will print a confirmation message on the debug console.
3. Check for announce messages in the serial log – the node now prints a
   liveness message each time it sends its periodic announce and blinks the LED.
4. Ensure the routing table contains a route to the target; stale/expired routes
   can make a live node seem unreachable.  See Section 6.3.6 for route troubleshooting.


### 10.3 Diagnostic Commands
- **Serial Monitor**: Provides real-time status and debug information
- **Memory Status**: Printed every 15 seconds (default)
- **Routing Table**: Can be printed via debug output
- **Link Status**: Available via debug output

---

## 11.0 APPENDICES

### Appendix A: Build Flag Reference
See `platformio.ini` for complete build flag documentation.

### Appendix B: Configuration Parameters
See `include/Config.h` for all configurable parameters.

### Appendix C: Additional Documentation
- `docs/ARCHITECTURE.md`: Detailed system architecture
- `docs/PACKET_FORMATS.md`: Packet format specifications
- `docs/LINK_LAYER.md`: Link layer implementation details
- `docs/KISS_INTERFACE.md`: KISS interface guide
- `docs/HAM_MODEM.md`: HAM modem interface guide
- `docs/IPFS_INTEGRATION.md`: IPFS integration guide
- `docs/ENHANCED_FEATURES.md`: Enhanced features documentation
- `docs/ROADMAP.md`: Project roadmap and planned enhancements
- `docs/API.md`: Web UI / REST API specification (includes Web UI auth + signed OTA (Ed25519) examples)

### Appendix D: Revision History
See `CHANGELOG.md` for complete revision history.

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
