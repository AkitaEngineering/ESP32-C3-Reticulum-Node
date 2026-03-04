# ESP32 Reticulum Gateway System Architecture
## Technical Architecture Document
**Document Version:** 2.1  
**Classification:** Unclassified  
**Date:** 2026-02-20  
**System Designation:** ESP32-RNS-GW-ARCH

---

## 1.0 SCOPE

### 1.1 Purpose
This document provides detailed technical architecture specifications for the ESP32 Reticulum Network Stack Gateway Node firmware system. The architecture description includes component specifications, data flow diagrams, interface definitions, and system integration requirements.

### 1.2 Applicability
This architecture document applies to all implementations of the ESP32-RNS-GW system firmware and serves as the primary reference for system design, implementation, and maintenance activities.

---

## 2.0 SYSTEM ARCHITECTURE OVERVIEW

### 2.1 Architectural Principles
The system architecture adheres to the following design principles:
- **Modularity**: Clear separation of concerns with well-defined interfaces
- **Extensibility**: Support for additional interfaces and protocols
- **Reliability**: Error handling and recovery mechanisms at all layers
- **Efficiency**: Resource-conscious design for embedded systems
- **Standards Compliance**: Adherence to published protocol specifications

### 2.2 System Hierarchy
```
┌─────────────────────────────────────────────────────────┐
│                  ReticulumNode                          │
│              (Application Controller)                   │
└──────────────┬──────────────────────────────────────────┘
               │
    ┌──────────┼──────────┬──────────────┬──────────────┐
    │          │          │              │              │
┌───▼───┐ ┌───▼───┐ ┌───▼───┐    ┌────▼────┐   ┌────▼────┐
│Interface│ │Routing│ │ Link  │    │ Reticulum│   │   KISS  │
│Manager │ │ Table │ │Manager│    │  Packet  │   │Processor│
└───┬───┘ └───┬───┘ └───┬───┘    └─────────┘   └─────────┘
    │          │          │
    │    ┌─────▼─────┐   │
    │    │   Link    │   │
    │    │  (State   │   │
    │    │  Machine) │   │
    │    └───────────┘   │
    │
┌───▼───────────────────────────────────────────────────┐
│              Physical Interfaces                       │
│  WiFi │ ESP-NOW │ Serial │ BT │ LoRa │ HAM │ IPFS     │
└───────────────────────────────────────────────────────┘
```

### 2.3 Layer Architecture
The system implements a layered architecture:
1. **Application Layer**: ReticulumNode (user application logic)
2. **Transport Layer**: LinkManager, Link (reliable delivery)
3. **Network Layer**: RoutingTable, ReticulumPacket (routing, addressing)
4. **Interface Layer**: InterfaceManager (physical interfaces)
5. **Hardware Layer**: ESP32 peripherals, radio modules

---

## 3.0 CORE COMPONENT SPECIFICATIONS

### 3.1 ReticulumNode Component

#### 3.1.1 Component Identification
- **Component Name**: ReticulumNode
- **Component Type**: Application Controller
- **Files**: `ReticulumNode.h`, `ReticulumNode.cpp`
- **Dependencies**: InterfaceManager, RoutingTable, LinkManager, ReticulumPacket

#### 3.1.2 Functional Responsibilities
1. **System Initialization**
   - EEPROM initialization and configuration loading
   - Subsystem initialization coordination
   - Node address generation/loading
   - Timer initialization

2. **Main Execution Loop**
   - Periodic task scheduling
   - Interface processing coordination
   - Link timeout management
   - Routing table maintenance

3. **Packet Processing**
   - Packet reception from InterfaceManager
   - Packet type classification and dispatch
   - Local packet processing
   - Packet forwarding decisions

4. **Application Integration**
   - Application data handler registration
   - Link data delivery to application
   - Command interface processing

#### 3.1.3 Interface Specifications
- **Public Methods**:
  - `setup()`: System initialization
  - `loop()`: Main execution loop
  - `getNodeAddress()`: Retrieve node address
  - `getNextPacketId()`: Generate unique packet identifier
  - `setAppDataHandler()`: Register application callback
  - `getInterfaceManager()`: Access interface manager

- **Private Methods**:
  - `loadConfig()`: Load configuration from EEPROM
  - `handleReceivedPacket()`: Process incoming packets
  - `processPacketForSelf()`: Handle local packets
  - `forwardPacket()`: Forward packets to other nodes
  - `sendAnnounceIfNeeded()`: Transmit periodic announces

#### 3.1.4 State Management
- **Node Address**: 8-byte RNS address (persistent)
- **Packet Counter**: 16-bit packet ID generator (persistent)
- **Subscribed Groups**: Vector of group addresses
- **Timers**: Announce timer, memory check timer

### 3.2 InterfaceManager Component

#### 3.2.1 Component Identification
- **Component Name**: InterfaceManager
- **Component Type**: Interface Abstraction Layer
- **Files**: `InterfaceManager.h`, `InterfaceManager.cpp`
- **Dependencies**: RoutingTable, KISSProcessor, WiFi, Bluetooth, ESP-NOW, LoRa (optional)

#### 3.2.2 Functional Responsibilities
1. **Interface Initialization**
   - WiFi station mode configuration (AP/STA hybrid)
   - UDP socket initialization
   - ESP-NOW initialization (optionally forcing a channel via `ESP_NOW_CHANNEL`)
   - Bluetooth Serial initialization
   - Serial UART initialization
   - LoRa module initialization (if enabled)
   - HAM modem initialization (if enabled)
   - IPFS client initialization (if enabled)

2. **Packet Reception**
   - Interface-specific input processing
   - KISS frame decoding
   - Packet validation
   - Callback invocation to ReticulumNode

3. **Packet Transmission**
   - Route lookup via RoutingTable
   - Interface selection
   - Packet encoding (KISS if required)
   - Interface-specific transmission

4. **Interface Management**
   - ESP-NOW peer management (automatic registration when routes are learned, removal on timeout)
   - Connection state monitoring
   - Error handling and recovery

#### 3.2.3 Interface Specifications
- **Public Methods**:
  - `setup()`: Initialize all interfaces
  - `loop()`: Process interface inputs
  - `sendPacket()`: Send packet via appropriate interface
  - `sendPacketVia()`: Send packet via specific interface
  - `broadcastAnnounce()`: Broadcast announce packets
  - `addEspNowPeer()`: Add ESP-NOW peer
  - `removeEspNowPeer()`: Remove ESP-NOW peer

- **Private Methods**:
  - `setupWiFi()`: WiFi initialization
  - `setupESPNow()`: ESP-NOW initialization
  - `processWiFiInput()`: Process UDP packets
  - `processSerialInput()`: Process serial input
  - `sendPacketViaWiFi()`: WiFi transmission
  - `sendPacketViaEspNow()`: ESP-NOW transmission

#### 3.2.4 Interface State
- **WiFi State**: Connection status, IP address
- **ESP-NOW Peers**: List of registered peers (dynamic, leveled by routing updates)
- **Bluetooth State**: Connection status
- **LoRa State**: Initialization status, module handle
- **HAM Modem State**: Initialization status, TNC connection

### 3.3 RoutingTable Component

#### 3.3.1 Component Identification
- **Component Name**: RoutingTable
- **Component Type**: Routing Information Base
- **Files**: `RoutingTable.h`, `RoutingTable.cpp`
- **Dependencies**: ReticulumPacket, InterfaceManager (for peer management)

#### 3.3.2 Functional Responsibilities
1. **Route Management**
   - Route entry storage and retrieval
   - Route update from announce packets
   - Route lookup by destination address
   - Route aging and pruning

2. **Loop Prevention**
   - Recent announce tracking
   - Forward decision logic
   - Announce ID deduplication

3. **Route Selection**
   - Best route selection (currently hop count)
   - Route comparison logic
   - Interface preference

#### 3.3.3 Data Structures
- **RouteEntry**: Contains destination, next hop, interface, hop count, timestamp
- **RecentAnnounceKey**: Packet ID + source address prefix
- **Route List**: std::list<RouteEntry> (ordered by last heard)
- **Recent Announces Map**: std::map<RecentAnnounceKey, timestamp>

#### 3.3.4 Interface Specifications
- **Public Methods**:
  - `update()`: Update routing table from announce
  - `findRoute()`: Lookup route for destination
  - `prune()`: Remove stale routes
  - `shouldForwardAnnounce()`: Check if announce should be forwarded
  - `markAnnounceForwarded()`: Mark announce as forwarded
  - `print()`: Debug output of routing table

### 3.4 LinkManager Component

#### 3.4.1 Component Identification
- **Component Name**: LinkManager
- **Component Type**: Transport Layer Manager
- **Files**: `LinkManager.h`, `LinkManager.cpp`
- **Dependencies**: ReticulumNode, Link, ReticulumPacket

#### 3.4.2 Functional Responsibilities
1. **Link Lifecycle Management**
   - Link creation and destruction
   - Link state tracking
   - Link timeout management
   - Link cleanup

2. **Packet Processing**
   - Link packet classification
   - Link instance routing
   - Application data delivery

3. **Reliable Transmission**
   - Reliable send initiation
   - Retransmission coordination
   - Acknowledgment processing

#### 3.4.3 Data Structures
- **Link Map**: std::map<address, LinkPtr> (keyed by destination)
- **Link State**: CLOSED, PENDING_REQ, ESTABLISHED, CLOSING

#### 3.4.4 Interface Specifications
- **Public Methods**:
  - `processPacket()`: Process link-related packet
  - `sendReliableData()`: Initiate reliable transmission
  - `checkAllTimeouts()`: Check all link timeouts
  - `removeLink()`: Remove link from manager

### 3.5 Link Component

#### 3.5.1 Component Identification
- **Component Name**: Link
- **Component Type**: Transport Connection
- **Files**: `Link.h`, `Link.cpp`
- **Dependencies**: LinkManager, ReticulumPacket

#### 3.5.2 Functional Responsibilities
1. **State Machine Management**
   - State transitions
   - State validation
   - Timeout handling

2. **Sequence Number Management**
   - Outgoing sequence generation
   - Incoming sequence validation
   - Sequence wraparound handling

3. **Reliability Mechanisms**
   - Acknowledgment generation
   - Retransmission logic
   - Timeout management

#### 3.5.3 State Machine
```
CLOSED → PENDING_REQ → ESTABLISHED → CLOSING → CLOSED
   ↑                                        ↓
   └────────────────────────────────────────┘
```

### 3.6 KISSProcessor Component

#### 3.6.1 Component Identification
- **Component Name**: KISSProcessor
- **Component Type**: Protocol Processor
- **Files**: `KISS.h`, `KISS.cpp`
- **Dependencies**: Config (for InterfaceType)

#### 3.6.2 Functional Responsibilities
1. **Frame Encoding**
   - Special character escaping
   - Frame boundary insertion
   - Command byte insertion

2. **Frame Decoding**
   - Byte stream processing
   - Escape sequence handling
   - Frame boundary detection
   - Command byte extraction

#### 3.6.3 Protocol Compliance
- **Standard**: RFC 1055 (KISS Protocol)
- **Frame Format**: [FEND][CMD][DATA...][FEND]
- **Escape Sequences**: FESC+TFEND, FESC+TFESC

### 3.7 ReticulumPacket Component

#### 3.7.1 Component Identification
- **Component Name**: ReticulumPacket
- **Component Type**: Protocol Serialization
- **Files**: `ReticulumPacket.h`, `ReticulumPacket.cpp`
- **Dependencies**: Config

#### 3.7.2 Functional Responsibilities
1. **Packet Serialization**
   - Header construction
   - Payload encoding
   - Sequence number embedding (for links)

2. **Packet Deserialization**
   - Header parsing
   - Payload extraction
   - Validation

3. **Packet Information Structure**
   - RnsPacketInfo struct definition
   - Field extraction and population

---

## 4.0 DATA FLOW SPECIFICATIONS

### 4.1 Incoming Packet Flow
```
Physical Interface
    ↓
InterfaceManager::process[Interface]Input()
    ↓
KISSProcessor::decodeByte() [if KISS interface]
    ↓
InterfaceManager::handleKissPacket()
    ↓
PacketReceiverCallback (ReticulumNode::handleReceivedPacket)
    ↓
Packet Type Classification
    ├─→ Link Packet → LinkManager::processPacket()
    ├─→ Announce Packet → RoutingTable::update() + forwardAnnounce()
    └─→ Data Packet → processPacketForSelf() OR forwardPacket()
```

### 4.2 Outgoing Packet Flow (Unreliable)
```
Application Logic
    ↓
InterfaceManager::sendPacket()
    ↓
RoutingTable::findRoute()
    ↓
Interface Selection
    ↓
InterfaceManager::sendPacketVia[Interface]()
    ↓
KISSProcessor::encode() [if KISS interface]
    ↓
Physical Interface Transmission
```

### 4.3 Outgoing Packet Flow (Reliable)
```
Application Logic
    ↓
ReticulumNode::processPacketForSelf() [LOCAL_CMD]
    ↓
LinkManager::sendReliableData()
    ↓
LinkManager::getOrCreateLink()
    ↓
Link::sendData()
    ↓
Link::sendPacketInternal()
    ↓
LinkManager::sendPacketRaw()
    ↓
InterfaceManager::sendPacket()
    ↓
[Continue with unreliable flow]
```

---

## 5.0 INTERFACE SPECIFICATIONS

### 5.1 Component Interfaces

#### 5.1.1 ReticulumNode ↔ InterfaceManager
- **Interface Type**: Callback-based
- **Data Flow**: Bidirectional
- **Protocol**: Function callbacks
- **Data**: Packet buffers, interface types, source information

#### 5.1.2 ReticulumNode ↔ RoutingTable
- **Interface Type**: Direct method calls
- **Data Flow**: Bidirectional
- **Protocol**: C++ method calls
- **Data**: Route entries, announce packets

#### 5.1.3 ReticulumNode ↔ LinkManager
- **Interface Type**: Direct method calls
- **Data Flow**: Bidirectional
- **Protocol**: C++ method calls
- **Data**: Link packets, application data

#### 5.1.4 InterfaceManager ↔ RoutingTable
- **Interface Type**: Reference-based
- **Data Flow**: InterfaceManager → RoutingTable
- **Protocol**: C++ method calls
- **Data**: Route lookups, route updates

### 5.2 External Interfaces

#### 5.2.1 Physical Interfaces
- **WiFi**: IEEE 802.11, UDP/IP
- **ESP-NOW**: Espressif proprietary
- **Serial**: UART, KISS framing
- **Bluetooth**: IEEE 802.15.1, SPP profile
- **LoRa**: SPI, SX1278 protocol
- **HAM Modem**: Serial, KISS/AX.25
- **IPFS**: HTTP, REST API

---

## 6.0 ERROR HANDLING AND RECOVERY

### 6.1 Error Categories
1. **Interface Errors**: Connection failures, transmission errors
2. **Protocol Errors**: Invalid packets, malformed frames
3. **Resource Errors**: Memory allocation failures, buffer overflows
4. **State Errors**: Invalid state transitions, timeout conditions

### 6.2 Recovery Mechanisms
- **Automatic Retry**: Link layer retransmission
- **State Reset**: Link state machine reset
- **Route Pruning**: Automatic stale route removal
- **Interface Reinitialization**: Attempted on critical failures

---

## 7.0 PERFORMANCE CHARACTERISTICS

### 7.1 Processing Latency
- **Packet Reception**: <10 ms (typical)
- **Routing Decision**: <1 ms (typical)
- **Packet Transmission**: Interface-dependent

### 7.2 Memory Utilization
- **Static Memory**: ~50 KB (code)
- **Dynamic Memory**: 50-150 KB (runtime, interface-dependent)
- **Stack Usage**: <4 KB (per task)

### 7.3 CPU Utilization
- **Idle**: <5%
- **Active Processing**: 10-20% (typical)
- **Peak**: <50% (during heavy traffic)

---

## 8.0 EXTENSIBILITY AND MAINTENANCE

### 8.1 Adding New Interfaces
1. Define interface type in Config.h
2. Add setup method to InterfaceManager
3. Add process method for input handling
4. Add send method for output handling
5. Update InterfaceManager::sendPacketVia() switch statement

### 8.2 Adding New Protocols
1. Create protocol processor class
2. Integrate with InterfaceManager or ReticulumNode
3. Define packet format structures
4. Implement serialization/deserialization

---

**Document Control:**
- **Prepared By**: Akita Engineering
- **Approved By**: [Approval Authority]
- **Distribution**: Unrestricted
- **Classification**: Unclassified

---

*End of Document*
