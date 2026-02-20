# ESP32 Reticulum Gateway KISS Interface Specification
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-KISS

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for the KISS (Keep It Simple, Stupid) protocol interface implementation in the ESP32 Reticulum Network Stack Gateway Node system. The KISS interface provides reliable packet framing for serial communication links.

### 1.2 Applicability
This specification applies to all KISS protocol operations including frame encoding, decoding, and transmission over serial interfaces (UART and Bluetooth Classic Serial Profile).

---

## 2.0 KISS PROTOCOL OVERVIEW

### 2.1 Protocol Description
KISS is a simple datagram framing protocol designed for serial communication links. It provides reliable packet boundary detection through special character encoding and frame delimiters.

### 2.2 Standard Compliance
- **Standard**: RFC 1055 (KISS Protocol)
- **Compliance**: Full compliance with extensions
- **Extensions**: Additional command bytes for TNC configuration (not used in this implementation)

### 2.3 Interface Applications
- **Serial UART**: Primary KISS interface over hardware UART
- **Bluetooth Classic**: KISS framing over Serial Port Profile (SPP)
- **HAM Modem**: KISS framing for TNC communication

---

## 3.0 KISS FRAME STRUCTURE

### 3.1 Frame Format
```
[FEND] [CMD] [DATA...] [FEND]
```

### 3.2 Frame Components

#### 3.2.1 Frame Delimiter (FEND)
- **Value**: 0xC0
- **Usage**: Marks frame boundaries (opening and closing)
- **Multiple FENDs**: Ignored (padding between frames)

#### 3.2.2 Command Byte
- **Position**: Immediately after opening FEND
- **Size**: 1 byte
- **Purpose**: Indicates frame type

#### 3.2.3 Data Field
- **Position**: After command byte, before closing FEND
- **Size**: Variable (0 to maximum packet size)
- **Encoding**: Special characters escaped

#### 3.2.4 Closing Delimiter
- **Value**: 0xC0 (FEND)
- **Usage**: Marks end of frame

---

## 4.0 SPECIAL CHARACTER DEFINITIONS

### 4.1 Special Character Table
```
Character  Hex Value  Name              Purpose
─────────────────────────────────────────────────────────
FEND       0xC0       Frame End         Frame delimiter
FESC       0xDB       Frame Escape      Escape sequence indicator
TFEND      0xDC       Transposed FEND    Escaped FEND character
TFESC      0xDD       Transposed FESC    Escaped FESC character
```

### 4.2 Special Character Usage
- **FEND (0xC0)**: Frame boundary marker
- **FESC (0xDB)**: Escape sequence prefix
- **TFEND (0xDC)**: Represents literal FEND in data
- **TFESC (0xDD)**: Represents literal FESC in data

---

## 5.0 COMMAND BYTE SPECIFICATIONS

### 5.1 Command Byte Values
```
Value  Command Type        Description
─────────────────────────────────────────────────────────
0x00   Data Frame         Normal packet data (used)
0x01   TX Delay           TNC configuration (not used)
0x02   Persistence        TNC configuration (not used)
0x03   Slot Time          TNC configuration (not used)
0x04   TX Tail           TNC configuration (not used)
0x05   Full Duplex        TNC configuration (not used)
0x06   Set Hardware       TNC configuration (not used)
0x0F   Return             TNC configuration (not used)
```

### 5.2 Command Byte Processing
- **0x00**: Data frame, process payload
- **0x01-0x0F**: TNC configuration commands, ignored in this implementation
- **Invalid**: Protocol error, frame discarded

---

## 6.0 ENCODING PROCEDURES

### 6.1 Encoding Rules
1. **FEND in Data**: Replace with FESC (0xDB) + TFEND (0xDC)
2. **FESC in Data**: Replace with FESC (0xDB) + TFESC (0xDD)
3. **All Other Bytes**: Transmit as-is

### 6.2 Encoding Algorithm
```
FOR each byte in packet data:
    IF byte == FEND (0xC0):
        OUTPUT FESC (0xDB)
        OUTPUT TFEND (0xDC)
    ELSE IF byte == FESC (0xDB):
        OUTPUT FESC (0xDB)
        OUTPUT TFESC (0xDD)
    ELSE:
        OUTPUT byte
```

### 6.3 Frame Construction
1. Output FEND (0xC0)
2. Output command byte (0x00 for data)
3. Output encoded data (per encoding rules)
4. Output FEND (0xC0)

---

## 7.0 DECODING PROCEDURES

### 7.1 Decoding State Machine
The decoder maintains the following state:
- **Expecting Command**: After FEND, before data
- **In Data**: Processing data bytes
- **In Escape**: After FESC, expecting escape sequence

### 7.2 Decoding Rules
1. **FEND Received**:
   - If buffer not empty: Process complete frame
   - Clear buffer
   - Reset to "Expecting Command" state

2. **FESC Received**:
   - Enter "In Escape" state
   - Wait for next byte

3. **In Escape State**:
   - **TFEND (0xDC)**: Output FEND (0xC0)
   - **TFESC (0xDD)**: Output FESC (0xDB)
   - **Other**: Protocol error, discard frame

4. **Normal Byte**:
   - Add to buffer
   - Continue processing

### 7.3 Decoding Algorithm
```
State = EXPECTING_COMMAND
Buffer = []

FOR each received byte:
    IF byte == FEND:
        IF Buffer not empty:
            Process frame (Buffer)
        Clear Buffer
        State = EXPECTING_COMMAND
    ELSE IF State == EXPECTING_COMMAND:
        Command = byte
        State = IN_DATA
    ELSE IF State == IN_ESCAPE:
        IF byte == TFEND:
            Buffer.append(FEND)
        ELSE IF byte == TFESC:
            Buffer.append(FESC)
        ELSE:
            Error: Invalid escape sequence
        State = IN_DATA
    ELSE IF byte == FESC:
        State = IN_ESCAPE
    ELSE:
        Buffer.append(byte)
```

---

## 8.0 INTERFACE CONFIGURATION

### 8.1 Serial UART Configuration
- **Baud Rate**: 115200 (default, configurable)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Flow Control**: None

### 8.2 Bluetooth Configuration
- **Profile**: Serial Port Profile (SPP)
- **Baud Rate**: 115200 (logical)
- **Framing**: KISS protocol
- **Connection**: Automatic pairing

### 8.3 Pin Configuration
Platform-specific UART pin assignments:
- **ESP32-C3**: RX=18, TX=19 (UART1)
- **ESP32-S2**: RX=33, TX=34 (UART2)
- **ESP32-S3**: RX=17, TX=18 (UART2)
- **ESP32**: RX=16, TX=17 (UART2)

---

## 9.0 OPERATIONAL PROCEDURES

### 9.1 Transmission Procedure
1. Construct Reticulum packet
2. Encode packet with KISS framing
3. Transmit encoded frame over serial interface
4. Verify transmission (if possible)

### 9.2 Reception Procedure
1. Receive bytes from serial interface
2. Process bytes through KISS decoder
3. Extract complete frames
4. Decode frames to Reticulum packets
5. Process packets through Reticulum stack

### 9.3 Error Handling
- **Invalid Escape Sequence**: Frame discarded, error logged
- **Frame Too Large**: Frame discarded, error logged
- **Timeout**: No frame received within timeout period
- **Buffer Overflow**: Frame discarded, error logged

---

## 10.0 PERFORMANCE CHARACTERISTICS

### 10.1 Throughput
- **Maximum**: Limited by serial baud rate
- **At 115200 baud**: ~11.5 KB/s theoretical, ~8-10 KB/s practical
- **Overhead**: ~2 bytes per frame (FEND + command)

### 10.2 Latency
- **Frame Encoding**: <1 ms (typical)
- **Frame Decoding**: <1 ms (typical)
- **Serial Transmission**: Baud rate dependent

### 10.3 Reliability
- **Frame Detection**: 100% (FEND delimiters)
- **Error Detection**: Escape sequence validation
- **Error Recovery**: Automatic (discard invalid frames)

---

## 11.0 TESTING AND VERIFICATION

### 11.1 Frame Encoding Test
1. Create test packet with all byte values (0x00-0xFF)
2. Encode packet
3. Verify FEND and FESC are properly escaped
4. Verify frame structure (FEND + CMD + DATA + FEND)

### 11.2 Frame Decoding Test
1. Create encoded test frame
2. Decode frame byte-by-byte
3. Verify decoded data matches original
4. Verify escape sequences handled correctly

### 11.3 Round-Trip Test
1. Encode packet
2. Transmit over serial
3. Receive and decode
4. Verify packet matches original

---

## 12.0 TROUBLESHOOTING

### 12.1 Common Issues

#### 12.1.1 Frames Not Received
- **Cause**: Incorrect baud rate
- **Solution**: Verify baud rate configuration matches on both ends

#### 12.1.2 Corrupted Data
- **Cause**: Electrical interference, incorrect wiring
- **Solution**: Check connections, add shielding if needed

#### 12.1.3 Incomplete Frames
- **Cause**: Buffer overflow, timeout
- **Solution**: Increase buffer size, adjust timeout values

### 12.2 Diagnostic Procedures
1. Enable debug output
2. Monitor serial data (hex dump)
3. Verify frame boundaries (FEND bytes)
4. Check escape sequences
5. Validate command bytes

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
