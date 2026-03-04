# ESP32 Reticulum Network Stack Gateway Node
## Revision History and Change Log
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-REV

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides a complete revision history and change log for the ESP32 Reticulum Network Stack Gateway Node firmware system. All modifications, enhancements, and fixes are documented with version numbers, dates, and detailed descriptions.

### 1.2 Document Structure
Revisions are listed in reverse chronological order (most recent first). Each revision entry includes version number, date, description, and affected components.

---

## 2.1 REVISION HISTORY

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

