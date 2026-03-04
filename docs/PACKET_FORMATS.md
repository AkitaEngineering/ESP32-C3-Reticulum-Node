# ESP32 Reticulum Gateway Packet Format Specifications
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-PKT

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for all packet formats used within the ESP32 Reticulum Network Stack Gateway Node system. This includes Reticulum protocol packets, KISS frames, AX.25 frames, and link layer packet structures.

### 1.2 Applicability
This specification applies to all packet encoding, decoding, and processing operations within the ESP32-RNS-GW system.

---

## 2.0 RETICULUM PROTOCOL PACKET FORMAT

### 2.1 Packet Structure Overview
Reticulum packets utilize a variable-length structure with a fixed header and variable payload. The system implements Header Type 1 format.

### 2.2 Header Type 1 Format

#### 2.2.1 Header Layout
```
Offset  Size  Field Name          Description
─────────────────────────────────────────────────────────
0       1     Flags               Packet type, destination type, propagation
1       1     Hops                Hop count (0-15)
2-17    16    Destination Hash    16-byte destination address hash
18      1     Context             Context identifier
19+     N     Payload             Variable-length payload data
```

#### 2.2.2 Flags Byte Structure
```
Bit  Description
─────────────────────────────────────────────────────────
0-1  Packet Type (2 bits)
     00 = DATA
     01 = ANNOUNCE
     10 = LINKREQ
     11 = PROOF
2-3  Destination Type (2 bits)
     00 = SINGLE
     01 = GROUP
     10 = PLAIN
     11 = LINK
4    Propagation Type (1 bit)
     0 = BROADCAST
     1 = TRANSPORT
5    Context Flag (1 bit)
     0 = No context field
     1 = Context field present
6    Header Type (1 bit)
     0 = Header Type 1
     1 = Header Type 2
7    IFAC Flag (1 bit)
     0 = Standard interface
     1 = Interface-specific
```

#### 2.2.3 Packet Type Definitions
- **DATA (0x00)**: Standard data packet containing application data
- **ANNOUNCE (0x01)**: Network discovery and routing announcement
- **LINKREQ (0x02)**: Link establishment request
- **PROOF (0x03)**: Cryptographic proof packet

#### 2.2.4 Destination Type Definitions
- **SINGLE (0x00)**: Addressed to single destination node
- **GROUP (0x01)**: Addressed to group of nodes
- **PLAIN (0x02)**: Unencrypted broadcast destination
- **LINK (0x03)**: Link layer destination

### 2.3 Context Field Specifications

#### 2.3.1 Standard Context Values
```
Value  Name              Usage
─────────────────────────────────────────────────────────
0x00   RNS_CONTEXT_NONE  Default context, no special meaning
0xA1   RNS_CONTEXT_LINK_REQ    Link establishment request
0xA2   RNS_CONTEXT_LINK_CLOSE  Link termination request
0xA3   RNS_CONTEXT_LINK_DATA   Link layer data packet
0xA4   RNS_CONTEXT_ACK         Acknowledgment packet
0xFE   RNS_CONTEXT_LOCAL_CMD   Local command (non-standard) – used over KISS/serial for node-local control. Payload begins with an 8‑byte destination (use your own address or all zeros) followed by ASCII command string (e.g. "routes", "peers").
```

#### 2.3.2 Context Usage Rules
- Context field is present when Context Flag (bit 5) is set
- Context 0xFE is implementation-specific and not part of RNS standard
- Link layer contexts (0xA1-0xA4) require proper sequence number handling

### 2.4 Payload Specifications

#### 2.4.1 Maximum Payload Size
- **Default Maximum**: 200 bytes
- **Configurable**: Via `RNS_MAX_PAYLOAD` constant
- **Total Packet Size**: Header (19 bytes) + Payload (max 200 bytes) = 219 bytes

#### 2.4.2 Payload Encoding
- Payload data is binary, no encoding applied
- Application layer responsible for data encoding/decoding
- Payload may contain nested protocol structures (e.g., AX.25 frames)

---

## 3.0 LINK LAYER PACKET FORMATS

### 3.1 Link Data Packet Format

#### 3.1.1 Structure
```
Offset  Size  Field Name          Description
─────────────────────────────────────────────────────────
0-1     2     Sequence Number     16-bit sequence number (big-endian)
2+      N     Application Data    Variable-length application payload
```

#### 3.1.2 Sequence Number Specifications
- **Size**: 16 bits (0-65535)
- **Byte Order**: Network byte order (big-endian)
- **Initial Value**: 0 or random (implementation-dependent)
- **Wraparound**: Circular, with detection logic

#### 3.1.3 Acknowledgment Requirements
- Link data packets require acknowledgment
- REQ_ACK flag must be set in packet header
- Acknowledgment contains sequence number of acknowledged packet

### 3.2 Link Acknowledgment Packet Format

#### 3.2.1 Structure
```
Offset  Size  Field Name          Description
─────────────────────────────────────────────────────────
0-1     2     Sequence Number     16-bit sequence number being acknowledged
2+      N     Optional Data        Optional acknowledgment data (typically empty)
```

#### 3.2.2 Acknowledgment Rules
- Acknowledges specific sequence number
- Sent in response to link data packet
- Uses ACK header type with RNS_CONTEXT_ACK context

### 3.3 Link Request Packet Format

#### 3.3.1 Structure
- **Header Type**: DATA
- **Context**: RNS_CONTEXT_LINK_REQ (0xA1)
- **Payload**: Typically empty
- **Acknowledgment**: ACK with sequence number 0

### 3.4 Link Close Packet Format

#### 3.4.1 Structure
- **Header Type**: DATA
- **Context**: RNS_CONTEXT_LINK_CLOSE (0xA2)
- **Payload**: Typically empty
- **Acknowledgment**: ACK with sequence number 0

---

## 4.0 KISS PROTOCOL FRAME FORMAT

### 4.1 Frame Structure
```
[FEND] [CMD] [DATA...] [FEND]
```

### 4.2 Special Character Definitions
```
Character  Value  Name              Usage
─────────────────────────────────────────────────────────
FEND       0xC0   Frame End         Frame delimiter
FESC       0xDB   Frame Escape      Escape sequence indicator
TFEND      0xDC   Transposed FEND   Escaped FEND character
TFESC      0xDD   Transposed FESC   Escaped FESC character
```

### 4.3 Command Byte Specifications
```
Value  Command Type        Description
─────────────────────────────────────────────────────────
0x00   Data Frame         Normal data packet
0x01   TX Delay           TNC configuration (not used)
0x02   Persistence        TNC configuration (not used)
0x03   Slot Time          TNC configuration (not used)
0x04   TX Tail            TNC configuration (not used)
0x05   Full Duplex        TNC configuration (not used)
0x06   Set Hardware       TNC configuration (not used)
0x0F   Return             TNC configuration (not used)
```

### 4.4 Encoding Rules
1. FEND (0xC0) in data → FESC (0xDB) + TFEND (0xDC)
2. FESC (0xDB) in data → FESC (0xDB) + TFESC (0xDD)
3. All other bytes transmitted as-is

### 4.5 Decoding Rules
1. FEND indicates frame boundary
2. FESC indicates escape sequence
3. FESC + TFEND → FEND
4. FESC + TFESC → FESC
5. FESC + other → protocol error

---

## 5.0 AX.25 PROTOCOL FRAME FORMAT

### 5.1 Frame Structure
```
[FLAG] [DEST ADDR] [SRC ADDR] [DIGI ADDRS...] [CTRL] [PID] [INFO] [FCS] [FLAG]
```

### 5.2 Flag Byte
- **Value**: 0x7E
- **Usage**: Frame delimiter (opening and closing)

### 5.3 Address Field Format

#### 5.3.1 Address Structure
```
Offset  Size  Field Name          Description
─────────────────────────────────────────────────────────
0-5     6     Callsign            Callsign (shifted left 1 bit)
6       1     SSID/Control        SSID and control bits
```

#### 5.3.2 SSID/Control Byte
```
Bit  Description
─────────────────────────────────────────────────────────
0    Address Extension Bit (0 = more addresses follow)
1-4  SSID (0-15)
5    Has Been Repeated (H) bit
6    Command/Response (C) bit
7    Reserved (always 0)
```

#### 5.3.3 Callsign Encoding
- Callsign characters shifted left by 1 bit
- Unused positions filled with spaces (0x20)
- Maximum 6 characters

### 5.4 Control Field Format

#### 5.4.1 Control Field Types
```
Type  Value  Name              Description
─────────────────────────────────────────────────────────
I     0x00   Information        Data frame with sequence numbers
S     0x01   Supervisory        Control frame (RR, RNR, REJ)
U     0x03   Unnumbered         Control frame (SABM, DISC, UA, UI)
```

#### 5.4.2 Information Frame (I-Frame)
- Contains sequence numbers (N(S), N(R))
- Poll/Final bit usage
- Used for data transmission

#### 5.4.3 Supervisory Frame (S-Frame)
- **RR (0x01)**: Receive Ready
- **RNR (0x05)**: Receive Not Ready
- **REJ (0x09)**: Reject

#### 5.4.4 Unnumbered Frame (U-Frame)
- **SABM (0x2F)**: Set Asynchronous Balanced Mode
- **DISC (0x43)**: Disconnect
- **DM (0x0F)**: Disconnected Mode
- **UA (0x63)**: Unnumbered Acknowledge
- **FRMR (0x87)**: Frame Reject
- **UI (0x03)**: Unnumbered Information

### 5.5 Protocol Identifier (PID) Field
- **Size**: 1 byte
- **Default**: 0xF0 (No Layer 3 protocol)
- **Usage**: Present in I-frames and UI-frames

### 5.6 Information Field
- **Size**: Variable (0-256 bytes typically)
- **Content**: Application data or protocol data
- **Usage**: Present in I-frames and UI-frames

### 5.7 Frame Check Sequence (FCS)

#### 5.7.1 FCS Specifications
- **Algorithm**: CRC-16 CCITT
- **Polynomial**: 0x8408 (reversed)
- **Initial Value**: 0xFFFF
- **Final XOR**: 0xFFFF
- **Size**: 16 bits (2 bytes, little-endian)

#### 5.7.2 FCS Calculation
- Calculated over all bytes between flags (excluding flags and FCS)
- Includes addresses, control, PID, and information fields
- Transmitted as two bytes (LSB first)

---

## 6.0 APRS PACKET FORMATS

### 6.1 APRS Position Report Format

#### 6.1.1 Uncompressed Position Format
```
Format: !DDMM.MMNS/DDDMM.MMEW[comment]
Example: !4026.70N/07400.36W[comment text]
```

#### 6.1.2 Field Specifications
- **DD**: Degrees (latitude: 00-90, longitude: 000-180)
- **MM.MM**: Minutes with decimal (00.00-59.99)
- **N/S**: North/South indicator
- **E/W**: East/West indicator
- **Symbol**: APRS symbol character
- **Comment**: Optional text (up to 43 characters)

### 6.2 APRS Weather Report Format

#### 6.2.1 Weather Format
```
Format: _DDHHMMc...s...g...t...r...p...P...h..b...
```

#### 6.2.2 Field Specifications
- **DDHHMM**: Date and time (DD=day, HH=hour, MM=minute)
- **c**: Wind direction (3 digits, 000-360)
- **s**: Wind speed (3 digits, mph)
- **g**: Gust speed (3 digits, mph)
- **t**: Temperature (3 digits, Fahrenheit, with sign)
- **r**: Rainfall 1 hour (3 digits, hundredths of inches)
- **p**: Rainfall 24 hours (3 digits, hundredths of inches)
- **P**: Rainfall since midnight (3 digits, hundredths of inches)
- **h**: Humidity (2 digits, percent)
- **b**: Barometric pressure (5 digits, millibars × 10)

### 6.3 APRS Message Format

#### 6.3.1 Message Format
```
Format: :ADDRESSEE :message{message_id}
Example: :N0CALL    :Hello from ESP32{001}
```

#### 6.3.2 Field Specifications
- **ADDRESSEE**: 9-character callsign (padded with spaces)
- **message**: Message text (up to 67 characters)
- **message_id**: Optional message identifier (3 digits)

---

## 7.0 PACKET PROCESSING PROCEDURES

### 7.1 Reception Procedure
1. Receive raw bytes from interface
2. Decode interface-specific framing (KISS, etc.)
3. Validate packet structure
4. Parse header fields
5. Extract payload
6. Route to appropriate handler

### 7.2 Transmission Procedure
1. Construct packet header
2. Add payload data
3. Calculate checksums (if required)
4. Encode interface-specific framing
5. Transmit via physical interface

### 7.3 Validation Procedures
1. Check packet size (minimum and maximum)
2. Verify header field ranges
3. Validate checksums/FCS
4. Check hop count limits
5. Verify address formats

---

## 8.0 BYTE ORDER SPECIFICATIONS

### 8.1 Network Byte Order
- **Standard**: Big-endian (most significant byte first)
- **Application**: Multi-byte fields in network protocols
- **Examples**: Sequence numbers, packet IDs, FCS (in some protocols)

### 8.2 Little-Endian Fields
- **FCS in AX.25**: Transmitted LSB first
- **Platform-specific**: Some internal structures

---

## 9.0 ERROR DETECTION AND CORRECTION

### 9.1 Error Detection Mechanisms
- **FCS (AX.25)**: CRC-16 CCITT
- **Sequence Numbers**: Link layer sequence validation
- **Hop Count**: Maximum hop limit enforcement
- **Size Validation**: Packet size bounds checking

### 9.2 Error Handling
- Invalid packets are discarded
- Error conditions logged (if debug enabled)
- No automatic retransmission for invalid packets
- Link layer handles retransmission for valid but lost packets

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
