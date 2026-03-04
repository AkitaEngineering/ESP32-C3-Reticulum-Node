# ESP32 Reticulum Network Stack Gateway Node
## Complete Technical Specification
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-TECH

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides comprehensive technical specifications for the ESP32 Reticulum Network Stack Gateway Node system, including all interfaces, protocols, performance characteristics, and operational requirements.

### 1.2 Document Structure
This specification is organized into the following sections:
- Section 2.0: System Overview
- Section 3.0: Hardware Specifications
- Section 4.0: Software Specifications
- Section 5.0: Interface Specifications
- Section 6.0: Protocol Specifications
- Section 7.0: Performance Specifications
- Section 8.0: Environmental Specifications
- Section 9.0: Compliance and Standards

---

## 2.0 SYSTEM OVERVIEW

### 2.1 System Identification
- **System Name**: ESP32 Reticulum Network Stack Gateway Node
- **System Designation**: ESP32-RNS-GW
- **Version**: 2.0
- **Manufacturer**: Akita Engineering

### 2.2 System Function
The ESP32-RNS-GW provides a multi-interface network gateway capable of:
- Bridging heterogeneous network interfaces
- Routing packets across network boundaries
- Providing reliable transport services
- Supporting multiple physical and logical interfaces simultaneously

---

## 3.0 HARDWARE SPECIFICATIONS

### 3.1 Processor Specifications
- **Architecture**: Xtensa (ESP32) or RISC-V (ESP32-C3/C6)
- **Clock Speed**: 80-240 MHz (platform-dependent)
- **Instruction Set**: 32-bit
- **Endianness**: Little-endian (native), big-endian (network)

### 3.2 Memory Specifications
- **Flash Memory**: 4-16 MB (platform-dependent)
- **SRAM**: 320-520 KB (platform-dependent)
- **EEPROM**: 16 bytes minimum (for configuration)
- **PSRAM**: Optional (platform-dependent)

### 3.3 Peripheral Specifications
- **UART**: 2-3 channels (platform-dependent)
- **SPI**: 2-4 channels (platform-dependent)
- **I2C**: 1-2 channels (platform-dependent)
- **ADC**: 6-18 channels (platform-dependent)
- **DAC**: 0-2 channels (platform-dependent)
- **WiFi**: IEEE 802.11 b/g/n (2.4 GHz)
- **Bluetooth**: Classic and/or BLE (platform-dependent)

---

## 4.0 SOFTWARE SPECIFICATIONS

### 4.1 Operating System
- **Type**: FreeRTOS (ESP-IDF)
- **Version**: ESP-IDF 4.4+ or Arduino Core 3.0+
- **Scheduler**: Preemptive multitasking
- **Task Priorities**: Configurable

### 4.2 Programming Language
- **Primary**: C++
- **Standard**: C++11 or later
- **Compiler**: GCC for Xtensa/RISC-V

### 4.3 Library Dependencies
- **ESP32 Core**: Espressif ESP32 Arduino Core or ESP-IDF
- **RadioLib**: Version 6.7.0+ (for LoRa)
- **Standard C++ Library**: STL containers and algorithms

---

## 5.0 INTERFACE SPECIFICATIONS

### 5.1 WiFi Interface
- **Standard**: IEEE 802.11 b/g/n
- **Frequency**: 2.4 GHz
- **Modes**: Station (STA) mode
- **Security**: WPA/WPA2
- **Transport**: UDP/IP
- **Port**: 4242 (default, configurable)
- **MTU**: 1500 bytes (typical)

### 5.2 ESP-NOW Interface
- **Protocol**: Espressif proprietary
- **Range**: 200-1000 meters (line-of-sight)
- **Data Rate**: Up to 250 bytes per packet
- **Peers**: Maximum 20
- **Encryption**: Optional

### 5.3 Serial Interface
- **Type**: UART
- **Baud Rate**: 115200 (default, configurable)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Framing**: KISS protocol

### 5.4 Bluetooth Interface
- **Type**: Bluetooth Classic (Serial Port Profile)
- **Baud Rate**: 115200 (logical)
- **Range**: 10-100 meters
- **Availability**: ESP32, ESP32-S2, ESP32-S3 only

### 5.5 LoRa Interface
- **Module**: SX1278-compatible
- **Frequency**: 915.0 MHz (configurable, region-dependent)
- **Bandwidth**: 125 kHz (configurable)
- **Spreading Factor**: 7 (configurable)
- **Coding Rate**: 5 (configurable)
- **Output Power**: 10 dBm (configurable)
- **Range**: 2-15 km (line-of-sight)

### 5.6 HAM Modem Interface
- **Protocol**: KISS over serial, AX.25 over radio
- **Baud Rate**: 9600 (TNC interface, configurable)
- **Audio Modem**: AFSK 1200/2200 Hz (Bell 202)
- **Sample Rate**: 8000 Hz

### 5.7 IPFS Interface
- **Access Method**: HTTP gateway client
- **Protocol**: HTTP/HTTPS
- **Gateway**: Configurable URL
- **Timeout**: 10 seconds (fetch), 30 seconds (publish)

---

## 6.0 PROTOCOL SPECIFICATIONS

### 6.1 Reticulum Protocol
- **Version**: Compatible with Reticulum specification
- **Address Size**: 8 bytes
- **Maximum Packet Size**: 219 bytes (19-byte header + 200-byte payload)
- **Hop Limit**: 15 hops
- **Announce Interval**: 180 seconds

### 6.2 KISS Protocol
- **Standard**: RFC 1055 (with extensions)
- **Frame Format**: [FEND][CMD][DATA...][FEND]
- **Special Characters**: FEND (0xC0), FESC (0xDB)

### 6.3 AX.25 Protocol
- **Version**: 2.2
- **Frame Format**: [FLAG][ADDRS][CTRL][PID][INFO][FCS][FLAG]
- **FCS Algorithm**: CRC-16 CCITT

### 6.4 APRS Protocol
- **Standard**: APRS specification
- **Position Format**: Uncompressed or compressed
- **Weather Format**: Standard APRS weather format
- **Message Format**: Standard APRS message format

---

## 7.0 PERFORMANCE SPECIFICATIONS

### 7.1 Processing Performance
- **Packet Processing Latency**: <10 ms (typical)
- **Routing Decision Time**: <1 ms (typical)
- **CPU Utilization**: 10-20% (typical operation)
- **Memory Usage**: 50-150 KB (runtime, interface-dependent)

### 7.2 Network Performance
- **Maximum Packet Rate**: ~10 packets/second
- **Maximum Throughput**: ~2 KB/s (interface-dependent)
- **Link Establishment Time**: <1 second
- **Route Discovery Time**: <3 minutes (within announce interval)

### 7.3 Reliability Specifications
- **Link Reliability**: >95% (with retransmission)
- **Route Discovery**: >90% (within 3 announce intervals)
- **Packet Delivery**: >85% (network-dependent)

---

## 8.0 ENVIRONMENTAL SPECIFICATIONS

### 8.1 Operating Temperature
- **Range**: -40°C to +85°C (typical ESP32 specification)
- **Storage Temperature**: -40°C to +125°C

### 8.2 Operating Humidity
- **Range**: 10% to 90% RH (non-condensing)

### 8.3 Power Requirements
- **Voltage**: 3.0V to 3.6V (typical)
- **Current**: 80-240 mA (active, platform-dependent)
- **Sleep Current**: <10 µA (deep sleep mode)

---

## 9.0 COMPLIANCE AND STANDARDS

### 9.1 Protocol Compliance
- **Reticulum**: Compatible with Reticulum Network Stack specification
- **KISS**: RFC 1055 compliant
- **AX.25**: Version 2.2 compliant
- **APRS**: APRS specification compliant
- **IEEE 802.11**: WiFi standard compliant
- **IEEE 802.15.1**: Bluetooth standard compliant

### 9.2 Regulatory Compliance
- **FCC**: Compliance required for operation in United States
- **CE**: Compliance required for operation in European Union
- **HAM Radio**: License required for amateur radio frequencies

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*

