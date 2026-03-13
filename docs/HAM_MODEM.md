# ESP32 Reticulum Gateway HAM Modem Interface Specification
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-HAM

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for HAM (amateur) radio modem interface support in the ESP32 Reticulum Network Stack Gateway Node system. This includes TNC integration, APRS support, AX.25 protocol, and audio modem capabilities.

### 1.2 Applicability
This specification applies to all HAM radio interface operations including packet radio communication, APRS operations, and TNC integration.

---

## 2.0 HAM MODEM OVERVIEW

### 2.1 Functional Description
HAM modem support enables integration with amateur packet radio networks through standard TNC (Terminal Node Controller) interfaces or direct audio connections. The system supports KISS protocol for TNC communication and AX.25 protocol for packet radio.

### 2.2 Supported Interfaces
- **TNC Interface**: KISS protocol over serial
- **Audio Modem**: Direct audio connection (AFSK, Bell 202)
- **Protocol Support**: AX.25, APRS

---

## 3.0 TNC INTEGRATION

### 3.1 TNC Interface Specifications

#### 3.1.1 Physical Interface
- **Connection Type**: Serial UART
- **Baud Rate**: 9600 bps (default, configurable)
- **Data Format**: 8N1 (8 data bits, no parity, 1 stop bit)
- **Flow Control**: None

#### 3.1.2 Protocol Interface
- **Protocol**: KISS (Keep It Simple, Stupid)
- **Frame Format**: Standard KISS framing (0xC0 delimiters, 0xDB escape)
- **Command Byte**: 0x00 (data frame)

> **Reticulum Configuration Note (USB KISS)**
>
> When using the USB CDC port as a KISS interface, the Reticulum daemon must use the
> `KISSInterface` type (not `SerialInterface`). Example:
>
> ```ini
> [[My USB KISS]]
>   type = KISSInterface
>   enabled = yes
>   port = /dev/ttyACM0
>   speed = 115200
> ```
>
> Using `SerialInterface` will not work because it expects HDLC framing (0x7E) instead
> of KISS framing (0xC0).

### 3.2 Supported TNCs
Any TNC supporting KISS protocol, including:
- Kantronics KPC-3, KPC-9612
- TNC-X, TNC-Pi
- Software TNCs (Dire Wolf, SoundModem)
- Most modern packet radio TNCs

### 3.3 Hardware Configuration

#### 3.3.1 Pin Assignments
- **RX Pin**: Configurable (default: GPIO 4)
- **TX Pin**: Configurable (default: GPIO 5)
- **Ground**: Common ground required

#### 3.3.2 Connection Procedure
1. Connect TNC TX → ESP32 RX pin
2. Connect TNC RX → ESP32 TX pin
3. Connect common ground
4. Configure baud rate to match TNC
5. Verify connection via serial monitor

---

## 4.0 APRS SUPPORT

### 4.1 APRS Protocol Overview
APRS (Automatic Packet Reporting System) is a digital communications protocol used by amateur radio operators for position reporting, weather data, messaging, and telemetry.

### 4.2 APRS Packet Types

#### 4.2.1 Position Reports
- **Format**: Uncompressed or compressed
- **Frequency**: Periodic or event-driven
- **Content**: Latitude, longitude, altitude, symbol, comment

#### 4.2.2 Weather Reports
- **Format**: Standard APRS weather format
- **Frequency**: Periodic (typically every 5-10 minutes)
- **Content**: Temperature, humidity, pressure, wind, rainfall

#### 4.2.3 Messages
- **Format**: Standard APRS message format
- **Delivery**: Store-and-forward via digipeaters
- **Content**: Addressee, message text, message ID

### 4.3 APRS Configuration

#### 4.3.1 Callsign Configuration
```cpp
#define APRS_CALLSIGN "N0CALL"     // <<< CHANGE ME
#define APRS_SSID 0                // SSID (0-15)
#define APRS_SYMBOL "["            // APRS symbol
#define APRS_COMMENT "Reticulum"   // Default comment
```

**IMPORTANT**: Replace "N0CALL" with your actual amateur radio callsign. Operating without proper callsign is a violation of amateur radio regulations.

### 4.4 Operational Procedures

#### 4.4.1 Position Reporting
```cpp
// Send position report
interfaceManager.sendAPRSPosition(
    40.7128,    // Latitude (degrees)
    -74.0060,   // Longitude (degrees)
    10.0,       // Altitude (meters)
    "Mobile Node" // Comment
);
```

#### 4.4.2 Weather Reporting
```cpp
// Send weather report
interfaceManager.sendAPRSWeather(
    22.5,       // Temperature (Celsius)
    65.0,       // Humidity (percent)
    1013.25,    // Pressure (millibars)
    "Clear"     // Comment
);
```

#### 4.4.3 Messaging
```cpp
// Send APRS message
interfaceManager.sendAPRSMessage(
    "N0CALL",           // Addressee
    "Hello from ESP32!" // Message text
);
```

---

## 5.0 AX.25 PROTOCOL SUPPORT

### 5.1 Protocol Specifications
- **Version**: AX.25 Version 2.2
- **Frame Types**: I, S, and U frames
- **Addressing**: Callsign + SSID
- **Error Detection**: CRC-16 CCITT FCS

### 5.2 Frame Structure
- **Flag**: 0x7E (frame delimiter)
- **Addresses**: Destination, source, digipeaters
- **Control**: Frame type and control information
- **PID**: Protocol identifier (0xF0 = no layer 3)
- **Information**: Variable-length data
- **FCS**: Frame check sequence (2 bytes)

### 5.3 Operational Procedures
See `docs/ENHANCED_FEATURES.md` Section 3.0 for complete AX.25 specifications.

---

## 6.0 AUDIO MODEM SUPPORT

### 6.1 Functional Description
Audio modem support enables direct audio connection to HAM radio transceivers, eliminating the need for separate TNC hardware. The ESP32 modulates digital data into audio tones and demodulates audio signals back to digital data.

### 6.2 Modem Specifications
- **Type**: Bell 202 compatible
- **Baud Rate**: 1200 baud
- **Mark Frequency**: 1200 Hz
- **Space Frequency**: 2200 Hz
- **Modulation**: Audio Frequency Shift Keying (AFSK)

### 6.3 Hardware Requirements
- **ADC Input**: For audio signal reception
- **DAC Output**: For audio signal transmission
- **Audio Interface**: May require level matching circuits

### 6.4 Configuration
```cpp
#define AUDIO_MODEM_ENABLED 1
#define AUDIO_MODEM_SAMPLE_RATE 8000
#define AUDIO_MODEM_MARK_FREQ 1200
#define AUDIO_MODEM_SPACE_FREQ 2200
#define AUDIO_MODEM_BAUD_RATE 1200
#define AUDIO_MODEM_RX_PIN 34
#define AUDIO_MODEM_TX_PIN 25
```

---

## 7.0 LEGAL AND REGULATORY REQUIREMENTS

### 7.1 Amateur Radio Licensing
**CRITICAL**: Operating on amateur radio frequencies requires:
- Appropriate amateur radio license from your country's regulatory authority
- Compliance with frequency allocations
- Proper station identification
- Adherence to power limits and operating procedures

### 7.2 Regulatory Compliance
- **United States**: FCC Part 97 regulations
- **European Union**: National amateur radio regulations
- **Other Countries**: Check local amateur radio regulations

### 7.3 Operating Requirements
- **Callsign**: Must use licensed callsign (not "N0CALL")
- **Identification**: Proper station identification required
- **Frequency**: Operate only on authorized amateur frequencies
- **Power**: Comply with power limits for license class

---

## 8.0 TROUBLESHOOTING PROCEDURES

### 8.1 TNC Connection Issues

#### 8.1.1 TNC Not Responding
- **Symptom**: No response from TNC
- **Diagnosis**: 
  1. Verify serial connections (TX/RX may be swapped)
  2. Check baud rate matches TNC configuration
  3. Verify TNC power and status LEDs
  4. Use serial monitor to debug KISS frames
- **Resolution**: Correct wiring, verify baud rate, check TNC status

#### 8.1.2 Packets Not Transmitting
- **Symptom**: Packets sent but not appearing on radio
- **Diagnosis**:
  1. Verify TNC is in KISS mode (not terminal mode)
  2. Check radio is properly connected to TNC
  3. Verify frequency and power settings on radio
  4. Check for proper callsign configuration
- **Resolution**: Configure TNC mode, verify radio connections

### 8.2 APRS Issues

#### 8.2.1 APRS Not Working
- **Symptom**: APRS packets not appearing on APRS-IS
- **Diagnosis**:
  1. Ensure callsign is properly configured (not "N0CALL")
  2. Verify APRS frequency is correct
  3. Check that gateway/digipeater is in range
  4. Monitor APRS-IS for packet reception
- **Resolution**: Configure proper callsign, verify frequency, check range

#### 8.2.2 Position Reports Not Updating
- **Symptom**: Position reports not appearing on map
- **Diagnosis**:
  1. Verify GPS data is valid
  2. Check position encoding format
  3. Verify transmission frequency
  4. Check digipeater/gateway connectivity
- **Resolution**: Verify GPS, check encoding, verify transmission

---

## 9.0 PERFORMANCE CHARACTERISTICS

### 9.1 TNC Interface Performance
- **Baud Rate**: 9600 bps (typical)
- **Frame Processing**: <10 ms latency
- **Throughput**: Limited by TNC and radio link

### 9.2 APRS Performance
- **Position Report Size**: ~50-100 bytes
- **Weather Report Size**: ~50-80 bytes
- **Message Size**: ~20-90 bytes
- **Transmission Time**: 1-5 seconds (depending on baud rate)

### 9.3 Audio Modem Performance
- **Baud Rate**: 1200 baud (Bell 202)
- **Processing Latency**: <10 ms
- **Error Rate**: <1% (typical, environment-dependent)

---

## 10.0 REFERENCES

### 10.1 Standards and Specifications
- **KISS Protocol**: RFC 1055
- **AX.25 Protocol**: AX.25 Version 2.2 Specification
- **APRS Protocol**: APRS Protocol Specification
- **Bell 202**: Bell 202 Modem Standard

### 10.2 Regulatory References
- **FCC Part 97**: Amateur Radio Service Rules (United States)
- **ITU Radio Regulations**: International regulations
- **National Regulations**: Country-specific amateur radio regulations

### 10.3 Additional Resources
- **Reticulum Network Stack**: https://reticulum.network/
- **APRS Information**: http://www.aprs.org/
- **AX.25 Information**: https://www.tapr.org/

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
