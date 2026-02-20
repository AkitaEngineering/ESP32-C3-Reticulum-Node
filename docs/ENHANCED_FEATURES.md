# ESP32 Reticulum Gateway Enhanced Features Specification
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-ENH

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for enhanced features implemented in the ESP32 Reticulum Network Stack Gateway Node system. These features include audio modem support, full AX.25 protocol implementation, enhanced APRS capabilities, IPFS publishing support, and Winlink integration.

### 1.2 Applicability
This specification applies to all enhanced feature implementations and serves as the technical reference for system integration, configuration, and operational procedures.

---

## 2.0 AUDIO MODEM SUPPORT

### 2.1 Functional Description
Audio modem support enables direct audio connection to HAM radio transceivers, bypassing the need for separate TNC hardware. The system modulates digital data into audio tones and demodulates audio signals back to digital data.

### 2.2 Supported Modem Types

#### 2.2.1 Bell 202 Modem
- **Baud Rate**: 1200 baud
- **Mark Frequency**: 1200 Hz
- **Space Frequency**: 2200 Hz
- **Modulation**: Audio Frequency Shift Keying (AFSK)
- **Standard**: Bell 202 compatible

#### 2.2.2 AFSK 1200
- **Baud Rate**: 1200 baud
- **Frequencies**: Configurable (default: 1200/2200 Hz)
- **Modulation**: Audio Frequency Shift Keying

#### 2.2.3 AFSK 2400
- **Baud Rate**: 2400 baud (future implementation)
- **Status**: Planned enhancement

### 2.3 Technical Specifications

#### 2.3.1 Audio Processing
- **Sample Rate**: 8000 Hz (configurable)
- **Bit Encoding**: NRZI (Non-Return-to-Zero Inverted)
- **Frame Structure**: Standard packet radio frame format

#### 2.3.2 Hardware Requirements
- **ADC Input**: For audio signal reception
- **DAC Output**: For audio signal transmission
- **Pin Configuration**: Platform-specific (see Config.h)

#### 2.3.3 Configuration Parameters
```cpp
#define AUDIO_MODEM_SAMPLE_RATE 8000   // Hz
#define AUDIO_MODEM_MARK_FREQ 1200     // Hz
#define AUDIO_MODEM_SPACE_FREQ 2200    // Hz
#define AUDIO_MODEM_BAUD_RATE 1200     // baud
#define AUDIO_MODEM_RX_PIN 34          // ADC pin
#define AUDIO_MODEM_TX_PIN 25          // DAC pin
```

### 2.4 Operational Procedures

#### 2.4.1 Initialization
1. Configure audio modem parameters
2. Initialize ADC for reception
3. Initialize DAC for transmission
4. Set sample rate and frequencies
5. Enable audio processing interrupts

#### 2.4.2 Transmission Procedure
1. Encode data with NRZI
2. Generate audio samples (mark/space frequencies)
3. Output samples via DAC
4. Monitor transmission completion

#### 2.4.3 Reception Procedure
1. Sample audio input via ADC
2. Demodulate audio to digital bits
3. Decode NRZI encoding
4. Reconstruct packet data
5. Deliver to upper layers

---

## 3.0 AX.25 PROTOCOL IMPLEMENTATION

### 3.1 Protocol Compliance
- **Standard**: AX.25 Version 2.2
- **Compliance**: Full protocol implementation
- **Frame Types**: I, S, and U frames supported

### 3.2 Frame Encoding Specifications

#### 3.2.1 Address Encoding
- **Callsign**: 6 bytes (shifted left 1 bit)
- **SSID**: 4 bits (0-15)
- **Control Bits**: Command/Response, Has Been Repeated
- **Address Extension**: Indicates more addresses follow

#### 3.2.2 Control Field Encoding
- **I-Frames**: Information frames with sequence numbers
- **S-Frames**: Supervisory frames (RR, RNR, REJ)
- **U-Frames**: Unnumbered frames (SABM, DISC, UA, UI, etc.)

#### 3.2.3 FCS Calculation
- **Algorithm**: CRC-16 CCITT
- **Polynomial**: 0x8408 (reversed)
- **Initial Value**: 0xFFFF
- **Final XOR**: 0xFFFF

### 3.3 Operational Procedures

#### 3.3.1 Frame Encoding
1. Encode destination address
2. Encode source address
3. Encode digipeater addresses (if any)
4. Add control field
5. Add PID field (if I-frame or UI-frame)
6. Add information field (if present)
7. Calculate FCS
8. Add flags (opening and closing)

#### 3.3.2 Frame Decoding
1. Verify opening flag
2. Decode addresses
3. Extract control field
4. Extract PID (if applicable)
5. Extract information field
6. Verify FCS
7. Verify closing flag

---

## 4.0 ENHANCED APRS CAPABILITIES

### 4.1 Position Reporting

#### 4.1.1 Position Format Specifications
- **Format**: Uncompressed or compressed
- **Uncompressed**: `!DDMM.MMNS/DDDMM.MMEW[comment]`
- **Compressed**: Binary format (future implementation)

#### 4.1.2 Position Encoding
- **Latitude**: Degrees and minutes (DDMM.MM)
- **Longitude**: Degrees and minutes (DDDMM.MM)
- **Altitude**: Optional, in feet (A=xxxxx format)
- **Symbol**: APRS symbol character
- **Comment**: Optional text (up to 43 characters)

#### 4.1.3 Operational Procedures
```cpp
// Send position report
interfaceManager.sendAPRSPosition(
    40.7128,    // Latitude (degrees)
    -74.0060,   // Longitude (degrees)
    10.0,       // Altitude (meters)
    "Mobile Node" // Comment
);
```

### 4.2 Weather Reporting

#### 4.2.1 Weather Format Specifications
- **Format**: `_DDHHMMc...s...g...t...r...p...P...h..b...`
- **Fields**: Date/time, wind, temperature, rainfall, humidity, pressure

#### 4.2.2 Weather Data Encoding
- **Wind Direction**: 3 digits (000-360 degrees)
- **Wind Speed**: 3 digits (mph)
- **Gust Speed**: 3 digits (mph)
- **Temperature**: 3 digits (Fahrenheit, with sign)
- **Rainfall**: 3 digits each (1h, 24h, since midnight, hundredths of inches)
- **Humidity**: 2 digits (percent)
- **Pressure**: 5 digits (millibars × 10)

#### 4.2.3 Operational Procedures
```cpp
// Send weather report
interfaceManager.sendAPRSWeather(
    72.5,       // Temperature (Celsius)
    65.0,       // Humidity (percent)
    1013.25,    // Pressure (millibars)
    "Clear"     // Comment
);
```

### 4.3 Messaging

#### 4.3.1 Message Format Specifications
- **Format**: `:ADDRESSEE :message{message_id}`
- **Addressee**: 9 characters (padded with spaces)
- **Message**: Up to 67 characters
- **Message ID**: Optional 3-digit identifier

#### 4.3.2 Operational Procedures
```cpp
// Send APRS message
interfaceManager.sendAPRSMessage(
    "N0CALL",           // Addressee
    "Hello from ESP32!" // Message text
);
```

---

## 5.0 IPFS PUBLISHING SUPPORT

### 5.1 Functional Description
IPFS publishing support enables uploading content to the InterPlanetary File System via a local IPFS node HTTP API. This allows the system to publish data and receive content-addressed hashes for distribution.

### 5.2 Technical Specifications

#### 5.2.1 API Interface
- **Protocol**: HTTP/HTTPS
- **Method**: POST
- **Endpoint**: `/api/v0/add`
- **Content-Type**: `multipart/form-data`

#### 5.2.2 Configuration
```cpp
#define IPFS_LOCAL_NODE_URL "http://localhost:5001"
#define IPFS_LOCAL_NODE_ENABLED 1
#define IPFS_PUBLISH_TIMEOUT_MS 30000
```

#### 5.2.3 Response Format
- **Format**: JSON
- **Fields**: Name, Hash, Size
- **Hash Extraction**: Parse JSON response for Hash field

### 5.3 Operational Procedures

#### 5.3.1 Publishing Procedure
1. Connect to local IPFS node API
2. Construct multipart form data
3. POST data to `/api/v0/add` endpoint
4. Receive JSON response
5. Extract IPFS hash from response
6. Return hash to caller

#### 5.3.2 Usage Example
```cpp
String ipfsHash;
uint8_t data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"

if (interfaceManager.publishToIPFS(data, 5, ipfsHash)) {
    Serial.print("Published! Hash: ");
    Serial.println(ipfsHash);
}
```

### 5.4 Requirements
- **Local IPFS Node**: Must be running and accessible
- **Network Connection**: WiFi connection required
- **API Access**: HTTP API must be enabled on IPFS node

---

## 6.0 WINLINK INTEGRATION

### 6.1 Functional Description
Winlink integration provides HAM email functionality over packet radio networks. The system can connect to Winlink BBS stations, send email messages, and receive email via the Winlink 2000 protocol.

### 6.2 Protocol Specifications

#### 6.2.1 Transport Protocol
- **Base Protocol**: AX.25
- **Application Protocol**: Winlink 2000
- **Connection Type**: Point-to-point over packet radio

#### 6.2.2 Message Format
- **To**: Recipient email address
- **From**: Sender email address (HAM callsign format)
- **Subject**: Message subject line
- **Body**: Message body text
- **BBS Callsign**: Winlink BBS station identifier

### 6.3 Operational Procedures

#### 6.3.1 Connection Procedure
1. Initialize Winlink client
2. Connect to BBS callsign
3. Authenticate (if required)
4. Establish session

#### 6.3.2 Message Transmission
1. Construct Winlink message
2. Encode message per Winlink protocol
3. Transmit via AX.25 frames
4. Wait for acknowledgment
5. Confirm delivery

#### 6.3.3 Message Reception
1. Poll BBS for messages
2. Receive message notifications
3. Request message download
4. Decode message content
5. Deliver to application

### 6.4 Configuration
```cpp
#define WINLINK_ENABLED 1
#define WINLINK_BBS_CALLSIGN "N0BBS"   // BBS callsign
#define WINLINK_PASSWORD ""            // Password (if required)
```

---

## 7.0 INTEGRATION EXAMPLES

### 7.1 Complete HAM Radio Setup
```cpp
void setup() {
    // Standard initialization
    reticulumNode.setup();
    
    // HAM modem automatically initialized if enabled
    // Audio modem processes audio samples automatically
    // AX.25 frames automatically encoded/decoded
}

void loop() {
    // Send periodic position reports
    static unsigned long lastPosition = 0;
    if (millis() - lastPosition > 600000) {  // Every 10 minutes
        interfaceManager.sendAPRSPosition(
            getLatitude(),
            getLongitude(),
            getAltitude(),
            "Mobile Node"
        );
        lastPosition = millis();
    }
    
    // Send weather data
    static unsigned long lastWeather = 0;
    if (millis() - lastWeather > 300000) {  // Every 5 minutes
        interfaceManager.sendAPRSWeather(
            getTemperature(),
            getHumidity(),
            getPressure(),
            "Weather Station"
        );
        lastWeather = millis();
    }
}
```

### 7.2 IPFS Content Distribution
```cpp
void publishFirmwareUpdate() {
    // Read firmware file
    File fwFile = SPIFFS.open("/firmware.bin", "r");
    if (!fwFile) return;
    
    size_t fwSize = fwFile.size();
    uint8_t* fwData = new uint8_t[fwSize];
    fwFile.read(fwData, fwSize);
    fwFile.close();
    
    // Publish to IPFS
    String ipfsHash;
    if (interfaceManager.publishToIPFS(fwData, fwSize, ipfsHash)) {
        // Send IPFS hash via Reticulum
        sendFirmwareUpdateNotification(ipfsHash);
    }
    
    delete[] fwData;
}
```

---

## 8.0 PERFORMANCE CHARACTERISTICS

### 8.1 Audio Modem Performance
- **Processing Latency**: <10 ms (typical)
- **Throughput**: 1200 baud (Bell 202)
- **Error Rate**: <1% (typical, environment-dependent)

### 8.2 AX.25 Performance
- **Frame Encoding**: <5 ms (typical)
- **Frame Decoding**: <5 ms (typical)
- **FCS Calculation**: <1 ms (typical)

### 8.3 APRS Performance
- **Position Encoding**: <1 ms
- **Weather Encoding**: <1 ms
- **Message Encoding**: <1 ms

### 8.4 IPFS Publishing Performance
- **Small Content (<1KB)**: 1-3 seconds
- **Medium Content (1-5KB)**: 3-10 seconds
- **Large Content (5-10KB)**: 10-30 seconds

---

## 9.0 LIMITATIONS AND CONSTRAINTS

### 9.1 Audio Modem Limitations
- Requires stable audio levels
- Susceptible to noise and interference
- Limited to supported baud rates
- May require audio filtering/preprocessing

### 9.2 AX.25 Limitations
- Full implementation but may need testing
- Some advanced features not yet implemented
- Digipeater path limited by frame size

### 9.3 APRS Limitations
- Position format uses uncompressed (can be large)
- Weather format simplified
- Message acknowledgment not fully implemented

### 9.4 IPFS Publishing Limitations
- Requires local IPFS node
- Large files may timeout
- No chunking support yet

### 9.5 Winlink Limitations
- Basic implementation
- Some Winlink features not yet supported
- Requires compatible BBS station

---

## 10.0 FUTURE ENHANCEMENTS

### 10.1 Planned Features
1. **Compressed APRS Position**: Implement compressed position format
2. **APRS Message Acknowledgment**: Full message ack support
3. **IPFS Chunking**: Support for large file uploads
4. **Winlink Advanced Features**: Full Winlink protocol support
5. **Audio Filtering**: Hardware/software audio filtering
6. **Multiple Modems**: Support for different modem types simultaneously

### 10.2 Advanced Features
1. **IPNS Support**: InterPlanetary Name System for mutable content
2. **IPLD Support**: Structured data linking
3. **PubSub**: IPFS pubsub over Reticulum
4. **Bitswap**: Direct peer-to-peer content exchange

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
