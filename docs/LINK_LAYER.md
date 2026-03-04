# ESP32 Reticulum Gateway Link Layer Specification
## Technical Specification Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-LINK

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides complete technical specifications for the reliable transport layer (Link Layer) implementation in the ESP32 Reticulum Network Stack Gateway Node system. The Link Layer provides guaranteed, in-order delivery of application data between two directly communicating nodes.

### 1.2 Applicability
This specification applies to all Link Layer operations including link establishment, data transmission, acknowledgment processing, and link termination.

---

## 2.0 LINK LAYER OVERVIEW

### 2.1 Functional Description
The Link Layer implements a reliable transport mechanism providing:
- **Guaranteed Delivery**: Acknowledgment-based delivery confirmation
- **In-Order Delivery**: Sequence number-based ordering
- **Error Detection**: Timeout-based loss detection
- **Error Recovery**: Automatic retransmission of lost packets
- **Flow Control**: Window-based transmission control (simplified)

### 2.2 Design Characteristics
- **Window Size**: 1 packet (simplified implementation)
- **Acknowledgment**: Explicit ACK packets
- **Retransmission**: Timeout-based automatic retransmission
- **State Management**: Finite state machine implementation

---

## 3.0 LINK STATE MACHINE

### 3.1 State Definitions

#### 3.1.1 CLOSED State
- **Description**: Link does not exist or has been terminated
- **Entry Conditions**: Initial state, after link closure, after error
- **Exit Conditions**: Link establishment initiated
- **Actions**: None

#### 3.1.2 PENDING_REQ State
- **Description**: Link request sent, awaiting acknowledgment
- **Entry Conditions**: Link establishment initiated
- **Exit Conditions**: ACK received (→ ESTABLISHED) or timeout (→ CLOSED)
- **Actions**: 
  - Send LINK_REQ packet
  - Start timeout timer
  - Retransmit on timeout (up to max retries)

#### 3.1.3 ESTABLISHED State
- **Description**: Link active, data transmission possible
- **Entry Conditions**: LINK_REQ ACK received, or incoming LINK_REQ processed
- **Exit Conditions**: Link closure initiated, timeout, error
- **Actions**:
  - Send/receive data packets
  - Process acknowledgments
  - Manage sequence numbers
  - Monitor timeouts

#### 3.1.4 CLOSING State
- **Description**: Link closure in progress
- **Entry Conditions**: LINK_CLOSE sent or received
- **Exit Conditions**: ACK received (→ CLOSED) or timeout (→ CLOSED)
- **Actions**:
  - Send LINK_CLOSE packet
  - Wait for acknowledgment
  - Clean up resources

### 3.2 State Transition Diagram
```
         ┌─────────┐
         │ CLOSED  │
         └────┬────┘
              │
              │ establish()
              ▼
      ┌───────────────┐
      │ PENDING_REQ  │
      └───────┬───────┘
             │
             │ ACK received
             ▼
      ┌──────────────┐
      │ ESTABLISHED  │
      └──────┬───────┘
             │
             │ close()
             ▼
      ┌──────────┐
      │ CLOSING  │
      └─────┬────┘
            │
            │ ACK received or timeout
            ▼
         ┌─────────┐
         │ CLOSED  │
         └─────────┘
```

---

## 4.0 PACKET TYPES AND FORMATS

All link-layer packets use the official Reticulum wire format:

```
[FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA...]
```

Link metadata (source address, packet ID, sequence number) is encoded in
the DATA payload prefix:

```
DATA = [SOURCE 8] [PACKET_ID 2] [SEQ_NUM 2] [APP_DATA...]
```

- **Flags**: packet_type = `RNS_PACKET_DATA`, dest_type = `RNS_DEST_LINK`
- **Dest Hash**: 16-byte hash (first 8 bytes = target node address, remainder zero-padded)

### 4.1 Link Request Packet (LINK_REQ)

#### 4.1.1 Packet Structure
- **Context**: RNS_CONTEXT_LINK_REQ (0xA1)
- **Data payload**: `[SOURCE 8] [PACKET_ID 2] [SEQ_NUM=0 2]`

#### 4.1.2 Processing Rules
- Receiver must respond with ACK
- ACK contains sequence number 0
- Link enters ESTABLISHED state upon ACK receipt
- Timeout triggers retransmission (max 3 retries)

### 4.2 Link Data Packet (LINK_DATA)

#### 4.2.1 Packet Structure
- **Context**: RNS_CONTEXT_LINK_DATA (0xA3)
- **Data payload**: `[SOURCE 8] [PACKET_ID 2] [SEQ_NUM 2] [APP_DATA...]`

#### 4.2.2 Processing Rules
- Sequence number must be expected value
- Out-of-order packets are discarded
- ACK must be sent upon receipt
- Timeout triggers retransmission

### 4.3 Acknowledgment Packet (ACK)

#### 4.3.1 Packet Structure
- **Context**: RNS_CONTEXT_ACK (0xA4)
- **Data payload**: `[SOURCE 8] [PACKET_ID 2] [ACKED_SEQ 2]`

#### 4.3.2 Processing Rules
- Acknowledges specific sequence number
- Received ACKs remove packets from retransmission queue
- Duplicate ACKs are ignored
- ACK timeout triggers retransmission

### 4.4 Link Close Packet (LINK_CLOSE)

#### 4.4.1 Packet Structure
- **Context**: RNS_CONTEXT_LINK_CLOSE (0xA2)
- **Data payload**: `[SOURCE 8] [PACKET_ID 2] [SEQ_NUM=0 2]`

#### 4.4.2 Processing Rules
- Receiver must respond with ACK
- ACK contains sequence number 0
- Link enters CLOSED state upon ACK receipt
- Timeout triggers forced closure

---

## 5.0 SEQUENCE NUMBER MANAGEMENT

### 5.1 Sequence Number Specifications
- **Size**: 16 bits (0-65535)
- **Initial Value**: 0 (or random, implementation-dependent)
- **Increment**: +1 per data packet sent
- **Wraparound**: Circular (65535 → 0)

### 5.2 Outgoing Sequence Management
- **Variable**: `_outgoingSequence`
- **Usage**: Assigned to each LINK_DATA packet
- **Update**: Incremented after transmission
- **Range**: 0-65535

### 5.3 Incoming Sequence Management
- **Variable**: `_expectedIncomingSequence`
- **Usage**: Expected sequence number of next packet
- **Update**: Incremented after valid packet receipt
- **Validation**: Packets with unexpected sequence numbers are discarded

### 5.4 Sequence Number Comparison
- **Expected Packet**: sequence == expected
- **Duplicate Packet**: sequence < expected (discard)
- **Out-of-Order Packet**: sequence > expected (discard, wait for retransmission)

---

## 6.0 TIMEOUT AND RETRANSMISSION MECHANISMS

### 6.1 Timeout Specifications

#### 6.1.1 Link Request Timeout
- **Duration**: 10 seconds (LINK_REQ_TIMEOUT_MS)
- **Trigger**: LINK_REQ sent, no ACK received
- **Action**: Retransmit LINK_REQ (up to max retries)

#### 6.1.2 Data Packet Timeout
- **Duration**: 5 seconds (LINK_RETRY_TIMEOUT_MS)
- **Trigger**: LINK_DATA sent, no ACK received
- **Action**: Retransmit LINK_DATA (up to max retries)

#### 6.1.3 Link Inactivity Timeout
- **Duration**: 1110 seconds (LINK_INACTIVITY_TIMEOUT_MS)
- **Trigger**: No activity on link
- **Action**: Close link

### 6.2 Retransmission Specifications

#### 6.2.1 Maximum Retries
- **Default**: 3 retries (LINK_MAX_RETRIES)
- **Application**: Per packet (LINK_REQ, LINK_DATA)
- **After Max Retries**: Link closure or error handling

#### 6.2.2 Retransmission Queue
- **Implementation**: Single packet queue (window size 1)
- **Management**: Oldest unacknowledged packet retransmitted
- **Cleanup**: Packet removed upon ACK receipt

### 6.3 Timeout Processing
- **Check Interval**: Every main loop iteration
- **Method**: `Link::checkTimeouts()`
- **Efficiency**: O(1) per link (constant time)

---

## 7.0 LINK ESTABLISHMENT PROCEDURE

### 7.1 Initiator Procedure
1. Create Link object (state: CLOSED)
2. Call `Link::establish()`
3. Transition to PENDING_REQ state
4. Send LINK_REQ packet
5. Start timeout timer
6. Wait for ACK:
   - **ACK Received**: Transition to ESTABLISHED
   - **Timeout**: Retransmit (if retries remaining) or close link

### 7.2 Responder Procedure
1. Receive LINK_REQ packet
2. Create or retrieve Link object
3. Transition to ESTABLISHED state
4. Send ACK packet (sequence 0)
5. Link ready for data transmission

---

## 8.0 DATA TRANSMISSION PROCEDURE

### 8.1 Transmission Procedure
1. Application calls `LinkManager::sendReliableData()`
2. LinkManager retrieves or creates Link
3. Link enters ESTABLISHED state (if not already)
4. Link assigns sequence number
5. Link constructs LINK_DATA packet
6. Link sends packet via InterfaceManager
7. Link starts timeout timer
8. Link waits for ACK:
   - **ACK Received**: Remove from queue, continue
   - **Timeout**: Retransmit (if retries remaining)

### 8.2 Reception Procedure
1. Receive LINK_DATA packet
2. Validate sequence number
3. If valid:
   - Extract application data
   - Send ACK packet
   - Deliver data to application
   - Update expected sequence number
4. If invalid:
   - Discard packet
   - (Optionally send ACK for last valid sequence)

---

## 9.0 LINK TERMINATION PROCEDURE

### 9.1 Graceful Closure (Initiator)
1. Application or system initiates closure
2. Link enters CLOSING state
3. Send LINK_CLOSE packet
4. Start timeout timer
5. Wait for ACK:
   - **ACK Received**: Transition to CLOSED, cleanup
   - **Timeout**: Force closure, cleanup

### 9.2 Graceful Closure (Responder)
1. Receive LINK_CLOSE packet
2. Send ACK packet
3. Transition to CLOSED state
4. Cleanup resources
5. Deliver closure notification to application

### 9.3 Forced Closure
- **Conditions**: Timeout exceeded, max retries exceeded, error condition
- **Actions**: Immediate state transition to CLOSED, resource cleanup
- **Notification**: Application notified of closure reason

---

## 10.0 PERFORMANCE CHARACTERISTICS

### 10.1 Throughput
- **Maximum**: Limited by window size (1 packet) and timeout values
- **Typical**: ~1-2 packets per second (depending on RTT)
- **Bottleneck**: Window size and acknowledgment latency

### 10.2 Latency
- **Establishment**: <1 second (typical)
- **Data Transmission**: RTT + processing time
- **Closure**: <1 second (typical)

### 10.3 Reliability
- **Delivery Guarantee**: >95% (with retransmission)
- **Ordering Guarantee**: 100% (sequence number enforcement)
- **Failure Modes**: Timeout, max retries exceeded, network partition

---

## 11.0 LIMITATIONS AND FUTURE ENHANCEMENTS

### 11.1 Current Limitations
- **Window Size**: Fixed at 1 (no pipelining)
- **Flow Control**: None (receiver may be overwhelmed)
- **Congestion Control**: None
- **Dynamic Timeout**: Fixed timeout values

### 11.2 Planned Enhancements
- **Sliding Window**: Variable window size
- **Selective Acknowledgment**: SACK support
- **Adaptive Timeout**: RTT-based timeout calculation
- **Flow Control**: Receiver window advertisement

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
