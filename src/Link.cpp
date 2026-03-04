#include "Link.h"
#include "LinkManager.h" // Include owner class definition
#include "ReticulumPacket.h"
#include "Utils.h"
#include <Arduino.h> // For millis(), Serial, isprint

Link::Link(const uint8_t* destination, LinkManager& owner) :
    _ownerRef(owner), _state(LinkState::CLOSED), _lastActivityTime(0), _stateTimer(0),
    _outgoingSequence(0), _expectedIncomingSequence(0), _linkReqPacketId(0), _currentRetryCount(0)
{
    if(destination){
        memcpy(_destinationAddress.data(), destination, RNS_ADDRESS_SIZE);
    } else {
        DebugSerial.println("! FATAL: Link created with null destination!");
        // Ensure state reflects error
        _state = LinkState::CLOSED;
        memset(_destinationAddress.data(), 0xFF, RNS_ADDRESS_SIZE); // Mark as invalid?
    }
    _lastActivityTime = millis(); // Initialize activity time
     // Initialize sequence numbers randomly? Recommended by RNS spec.
    // randomSeed(analogRead(A0) ^ millis()); // Ensure seeded somewhere
    // _outgoingSequence = random(0, 0xFFFF);
    // Start at 0 for easier debugging.
}

Link::~Link() {
    // DebugSerial.print("Link destructor: "); Utils::printBytes(_destinationAddress.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
}

// Public method to initiate link establishment
bool Link::establish() {
    if (_state != LinkState::CLOSED) {
        // If already pending or established, report success (or no-op)
        if(_state == LinkState::PENDING_REQ || _state == LinkState::ESTABLISHED) return true;
        DebugSerial.println("Link::establish called but state not CLOSED.");
        return false;
    }
    DebugSerial.print("Link::establish to "); Utils::printBytes(_destinationAddress.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
    sendLinkRequest(); // Send the actual request packet
    // Return value indicates if sending was attempted, not if established yet
    return (_state == LinkState::PENDING_REQ); // Should be PENDING_REQ if sendLinkRequest succeeded
}

// Internal: Sends the LINK_REQ packet
void Link::sendLinkRequest() {
    // State should be CLOSED before calling, set PENDING now
    _state = LinkState::PENDING_REQ;
    _linkReqPacketId = _ownerRef.getNextPacketId();

    uint8_t buffer[RNS_MIN_HEADER_SIZE]; // Control packets have no payload usually
    size_t len = 0;
    // Link Request uses DATA type, specific context
    bool ok = ReticulumPacket::serialize_control(buffer, len,
        _destinationAddress.data(), _ownerRef.getNodeAddress(),
        RNS_HEADER_TYPE_DATA,
        RNS_CONTEXT_LINK_REQ,
        _linkReqPacketId,
        0); // No sequence number needed in REQ payload

    if (ok) {
        _ownerRef.sendPacketRaw(buffer, len, _destinationAddress.data());
        _stateTimer = millis(); // Start timeout timer for REQ ACK
        _currentRetryCount = 0;
        updateActivity();
        // DebugSerial.println("Link Request sent."); // Verbose
    } else {
        DebugSerial.println("! ERROR: Link::sendLinkRequest serialize failed!");
        teardown(); // Failed to send, cannot establish
    }
}

// Public method to send application data reliably
bool Link::sendData(const std::vector<uint8_t>& dataPayload) {
    if (_state != LinkState::ESTABLISHED) {
         DebugSerial.println("! Link::sendData failed: Link not established.");
         return false;
    }
    // Enforce simplified window size of 1
    if (!_pendingOutgoingPackets.empty()) {
         DebugSerial.println("! Link::sendData failed: Link busy (awaiting ACK).");
         return false; // Wait for previous ACK
    }
    if (dataPayload.size() > RNS_MAX_PAYLOAD - RNS_SEQ_SIZE) {
        DebugSerial.println("! Link::sendData failed: Payload too large.");
        return false;
    }

    // Prepare packet info
    RnsPacketInfo packetInfo;
    packetInfo.context = RNS_CONTEXT_LINK_DATA;
    // Data requires ACK
    packetInfo.header_type = RNS_HEADER_TYPE_DATA | RNS_HEADER_FLAG_REQUEST_ACK_MASK;
    memcpy(packetInfo.destination, _destinationAddress.data(), RNS_ADDRESS_SIZE);
    packetInfo.data = dataPayload; // Store actual data payload
    packetInfo.sequence_number = _outgoingSequence; // Use current sequence number

    // Increment sequence number *after* assigning it for this packet
    _outgoingSequence++;

    // Add to queue, serialize, send, start timers
    sendPacketInternal(packetInfo);
    return true; // Indicates send attempt was initiated
}

// Internal: Adds packet to queue, sends, starts timers
void Link::sendPacketInternal(const RnsPacketInfo& packetInfo) {
     if (_pendingOutgoingPackets.size() >= 1) { // Re-check window size
        DebugSerial.println("! Link::sendPacketInternal failed: Window full.");
        return;
     }

    PendingPacket pending;
    pending.packetInfo = packetInfo; // Copy packet info
    pending.packetInfo.packet_id = _ownerRef.getNextPacketId(); // Unique ID for *this* transmission
    pending.firstSentTime = millis();
    pending.lastSentTime = pending.firstSentTime;
    // pending.retryCount = 0; // Retry count is tracked by _currentRetryCount

    uint8_t buffer[MAX_PACKET_SIZE];
    size_t len = 0;
    bool ok = ReticulumPacket::serialize(buffer, len,
        pending.packetInfo.destination, _ownerRef.getNodeAddress(),
        RNS_DST_TYPE_SINGLE, pending.packetInfo.header_type, pending.packetInfo.context,
        pending.packetInfo.packet_id, 0, // Hops = 0 initially
        pending.packetInfo.data,          // Pass actual data
        pending.packetInfo.sequence_number); // Pass sequence number

    if (ok) {
        _pendingOutgoingPackets.push_back(pending);
        _ownerRef.sendPacketRaw(buffer, len, pending.packetInfo.destination);
        _stateTimer = millis(); // Start/reset retransmission timer
        _currentRetryCount = 0; // Reset overall retry count for this attempt
        updateActivity();
        // DebugSerial.print("Link::sendPacketInternal sent seq "); DebugSerial.println(pending.packetInfo.sequence_number); // Verbose
    } else {
         DebugSerial.println("! ERROR: Link::sendPacketInternal serialize failed!");
         // Consider tearing down the link if core serialization fails
         teardown();
    }
}

// Main state machine for processing incoming packets relevant to this link
void Link::handlePacket(const RnsPacketInfo& packetInfo) {
    if (!packetInfo.valid) {
        DebugSerial.println("! Link::handlePacket received invalid packet info. Ignoring.");
        return;
    }
    updateActivity(); // Mark link as active

    // --- Handle ACKs ---
    // ACKs have specific header type and context
    if (packetInfo.header_type == RNS_HEADER_TYPE_ACK && packetInfo.context == RNS_CONTEXT_ACK) {
        processAck(packetInfo);
        return; // ACK processing is terminal for this packet
    }

    // --- State-Specific Handling ---
    switch (_state) {
        case LinkState::CLOSED:
            // Only respond to Link Requests when closed
            if (packetInfo.context == RNS_CONTEXT_LINK_REQ) {
                processLinkRequest(packetInfo);
            } else { /* Ignore other packets */ }
            break;

        case LinkState::PENDING_REQ:
            // Waiting for ACK to our Link Request (handled by processAck)
            // If we receive another Link Request, peer might not have received ours or ACK lost
            if (packetInfo.context == RNS_CONTEXT_LINK_REQ) {
                 DebugSerial.println("Link(PENDING): Received concurrent LINK_REQ. Sending ACK.");
                 sendAck(0); // ACK their REQ (seq 0 for control packets)
                 // Should we transition to ESTABLISHED here? RNS spec suggests yes.
                 _state = LinkState::ESTABLISHED;
                 _expectedIncomingSequence = 0; // Assume peer starts at 0
                 _outgoingSequence = 0;         // Reset our sequence too
                 clearPendingQueue();
                 _stateTimer = 0; // Stop REQ timer
                 DebugSerial.println("Link Established (from Pending by concurrent REQ).");
            } // Ignore other packets like DATA until established
            break;

        case LinkState::ESTABLISHED:
            // Handle Data, new REQ (peer reset?), or Close request
            if (packetInfo.context == RNS_CONTEXT_LINK_DATA) {
                processData(packetInfo);
            } else if (packetInfo.context == RNS_CONTEXT_LINK_REQ) {
                 DebugSerial.println("Link(ESTABLISHED): Received LINK_REQ. Re-sending ACK.");
                 sendAck(0); // Re-ACK their REQ
                 // Maybe reset expected sequence? Assume peer restarted.
                 _expectedIncomingSequence = 0;
            } else if (packetInfo.context == RNS_CONTEXT_LINK_CLOSE) {
                 processLinkClose(packetInfo);
            } // Ignore other unexpected packets
            break;

        case LinkState::CLOSING:
             // Waiting for ACK to our Link Close request (handled by processAck)
             // Ignore anything else.
             break;
    }
}

// Handle incoming LINK_REQ packet
void Link::processLinkRequest(const RnsPacketInfo& reqPacket) {
     // Can be received in CLOSED or ESTABLISHED state
     DebugSerial.print("Link::processLinkRequest from "); Utils::printBytes(reqPacket.source, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
     sendAck(0); // ACK the control packet (seq 0)

     // Transition to ESTABLISHED
     if (_state == LinkState::CLOSED) {
         _expectedIncomingSequence = 0;
         _outgoingSequence = 0;
         clearPendingQueue();
         _state = LinkState::ESTABLISHED;
         DebugSerial.println("Link Established (from Closed by REQ).");
     } else { // Was ESTABLISHED already
          DebugSerial.println("Link Re-Established (ACKed REQ).");
          // Optionally reset sequences if needed based on protocol interpretation
          // _expectedIncomingSequence = 0;
          // _outgoingSequence = 0;
          // clearPendingQueue();
     }
}

// Handle incoming ACK packet
void Link::processAck(const RnsPacketInfo& ackPacket) {
    uint16_t ackedSequence = ackPacket.sequence_number; // Seq num is in payload for ACKs

    if (_state == LinkState::PENDING_REQ) {
        // Expecting ACK for LINK_REQ (which used packet_id matching, conceptually seq 0)
        if (ackedSequence == 0) { // Check if ACK matches the control packet pseudo-sequence
             DebugSerial.println("Link(PENDING): Link Request ACK received.");
             _state = LinkState::ESTABLISHED;
             _expectedIncomingSequence = 0;
             _outgoingSequence = 0;
             clearPendingQueue();
             _stateTimer = 0; // Stop REQ timer
             DebugSerial.println("Link Established.");
        } else {
             DebugSerial.print("! Link(PENDING): Received ACK with unexpected seq: "); DebugSerial.println(ackedSequence);
        }
    } else if (_state == LinkState::ESTABLISHED) {
        // Expecting ACK for a data packet
        if (!_pendingOutgoingPackets.empty()) {
            PendingPacket& frontPacket = _pendingOutgoingPackets.front();
            if (ackedSequence == frontPacket.packetInfo.sequence_number) {
                 // Correct ACK received
                 // DebugSerial.print("Link(ESTABLISHED): ACK received for data seq: "); DebugSerial.println(ackedSequence); // Verbose
                 _pendingOutgoingPackets.pop_front(); // Remove acknowledged packet
                 _currentRetryCount = 0; // Reset overall retries for the link
                 _stateTimer = 0; // Stop retransmission timer until next send
                 // If windowing > 1, might send next packet here
            } else {
                 // Wrong sequence number ACKed - could be duplicate ACK or error
                 DebugSerial.print("! Link(ESTABLISHED): Received ACK for wrong seq (Expected: ");
                 DebugSerial.print(frontPacket.packetInfo.sequence_number);
                 DebugSerial.print(", Got: "); DebugSerial.print(ackedSequence); DebugSerial.println("). Ignoring.");
            }
        } else {
             // Received an ACK but queue is empty - likely duplicate ACK, ignore.
             // DebugSerial.println("Link(ESTABLISHED): Received unexpected ACK (queue empty). Ignoring."); // Verbose
        }
    } else if (_state == LinkState::CLOSING) {
         // Expecting ACK for LINK_CLOSE (conceptually seq 0)
         if (ackedSequence == 0) {
            DebugSerial.println("Link(CLOSING): Link Close ACK received.");
            _state = LinkState::CLOSED; // Final state
            clearPendingQueue();
            _stateTimer = 0;
            // Notify manager to remove this link instance
             _ownerRef.removeLink(_destinationAddress.data());
         } else {
              DebugSerial.print("! Link(CLOSING): Received ACK with unexpected seq: "); DebugSerial.println(ackedSequence);
         }
    }
     // Ignore ACKs in CLOSED state
}

// Handle incoming LINK_DATA packet
void Link::processData(const RnsPacketInfo& dataPacket) {
     if (_state != LinkState::ESTABLISHED) return; // Should not happen

     // DebugSerial.print("Link(ESTABLISHED): Received Data seq: "); DebugSerial.println(dataPacket.sequence_number); // Verbose

     if (dataPacket.sequence_number == _expectedIncomingSequence) {
          // Correct sequence - Process data and send ACK
          _ownerRef.processReceivedLinkData(dataPacket.source, dataPacket.data);
          _expectedIncomingSequence++;
          sendAck(dataPacket.sequence_number);
     } else if (dataPacket.sequence_number < _expectedIncomingSequence) {
          // Duplicate packet - Resend ACK for the duplicate's sequence number
          DebugSerial.print("Link(ESTABLISHED): Duplicate data seq "); DebugSerial.print(dataPacket.sequence_number); DebugSerial.print(" (expected "); DebugSerial.print(_expectedIncomingSequence); DebugSerial.println("). Resending ACK.");
          sendAck(dataPacket.sequence_number);
     } else {
          // Out of order - Ignore (simple strategy)
          DebugSerial.print("! Link(ESTABLISHED): Out-of-order seq "); DebugSerial.print(dataPacket.sequence_number); DebugSerial.print(" (expected "); DebugSerial.print(_expectedIncomingSequence); DebugSerial.println("). Ignoring.");
     }
}

// Internal: Send ACK packet
void Link::sendAck(uint16_t sequenceToAck) {
     uint16_t ackPacketId = _ownerRef.getNextPacketId();
     uint8_t buffer[RNS_MIN_HEADER_SIZE + RNS_SEQ_SIZE]; // ACK payload is just the sequence number
     size_t len = 0;
     // ACK uses ACK header type, ACK context
     bool ok = ReticulumPacket::serialize_control(buffer, len,
        _destinationAddress.data(), _ownerRef.getNodeAddress(),
        RNS_HEADER_TYPE_ACK,
        RNS_CONTEXT_ACK,
        ackPacketId,
        sequenceToAck); // Sequence being ACKed goes in payload

      if (ok) {
         // DebugSerial.print("Link Sending ACK for seq: "); DebugSerial.println(sequenceToAck); // Verbose
         _ownerRef.sendPacketRaw(buffer, len, _destinationAddress.data());
         updateActivity();
      } else {
         DebugSerial.println("! ERROR: Link::sendAck serialize failed!");
      }
}

// Check for timeouts (ACK for REQ/CLOSE, retransmission for DATA)
void Link::checkTimeouts() {
    // Don't check timeouts if link is cleanly closed or already established with nothing pending
    if (_state == LinkState::CLOSED || (_state == LinkState::ESTABLISHED && _pendingOutgoingPackets.empty())) {
        _stateTimer = 0; // Ensure timer is off
        return;
    }

    unsigned long now = millis();
    unsigned long timeoutDuration = LINK_RETRY_TIMEOUT_MS; // Default data retry timeout

    if (_state == LinkState::PENDING_REQ) {
        timeoutDuration = LINK_REQ_TIMEOUT_MS;
    } else if (_state == LinkState::CLOSING) {
         timeoutDuration = LINK_RETRY_TIMEOUT_MS; // Reuse data timeout for close ACK
    }
    // For ESTABLISHED state with pending packets, timeoutDuration remains LINK_RETRY_TIMEOUT_MS

    if (_stateTimer != 0 && now - _stateTimer > timeoutDuration) {
        // Timeout occurred!
        if (_state == LinkState::PENDING_REQ) {
             DebugSerial.println("! Link Request timed out.");
             teardown(); // Give up establishing
        } else if (_state == LinkState::ESTABLISHED && !_pendingOutgoingPackets.empty()) {
             // Data ACK timeout
             if (_currentRetryCount < LINK_MAX_RETRIES) {
                 _currentRetryCount++;
                 DebugSerial.print("! Link ACK timeout. Retrying packet (Attempt ");
                 DebugSerial.print(_currentRetryCount); DebugSerial.print("/"); DebugSerial.print(LINK_MAX_RETRIES); DebugSerial.println(")...");
                 retransmitOldestPending(); // Retransmit and resets _stateTimer
             } else {
                  DebugSerial.println("! Link max retries reached. Tearing down link.");
                  teardown(); // Give up after max retries
             }
        } else if (_state == LinkState::CLOSING) {
             // Close ACK timeout
             DebugSerial.println("! Link Close ACK timed out. Force closing.");
             teardown(); // Force close locally, manager will prune
        }
    }
}

// Retransmit the packet at the front of the queue
void Link::retransmitOldestPending() {
     if (_pendingOutgoingPackets.empty()) return;

     PendingPacket& pending = _pendingOutgoingPackets.front();
     pending.lastSentTime = millis();
     // uint16_t oldPacketId = pending.packetInfo.packet_id; // unused
     pending.packetInfo.packet_id = _ownerRef.getNextPacketId(); // Use new packet ID

     DebugSerial.print("Link Retransmitting seq "); DebugSerial.print(pending.packetInfo.sequence_number);
     DebugSerial.print(" ID "); DebugSerial.print(pending.packetInfo.packet_id); DebugSerial.print(" (Retry "); DebugSerial.print(_currentRetryCount); DebugSerial.println(")");

     uint8_t buffer[MAX_PACKET_SIZE];
     size_t len = 0;
     // Serialize using the info stored in the pending packet
      bool ok = ReticulumPacket::serialize(buffer, len,
        pending.packetInfo.destination, _ownerRef.getNodeAddress(),
        RNS_DST_TYPE_SINGLE, pending.packetInfo.header_type, pending.packetInfo.context,
        pending.packetInfo.packet_id, 0, // Hops = 0
        pending.packetInfo.data,
        pending.packetInfo.sequence_number);

      if (ok) {
         _ownerRef.sendPacketRaw(buffer, len, pending.packetInfo.destination);
         _stateTimer = millis(); // Reset retransmission timer
         updateActivity();
      } else {
          DebugSerial.println("! ERROR: Link::retransmit serialize failed! Tearing down.");
          teardown();
      }
}

// Initiate link closure process
void Link::close(bool notifyPeer) {
     if (_state == LinkState::CLOSED) return; // Already closed

     DebugSerial.print("Link::close requested for "); Utils::printBytes(_destinationAddress.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
     // Clear any pending packets immediately when close is initiated
     clearPendingQueue();

     if (notifyPeer && _state != LinkState::CLOSING) {
         sendLinkClose(); // Tell peer we are closing
         _state = LinkState::CLOSING;
         _stateTimer = millis(); // Start timer for close ACK
         _currentRetryCount = 0; // Reset retries for close state
     } else {
          // Force close without notification or if already closing
         _state = LinkState::CLOSED; // Set state directly
         // Let manager handle removal via inactivity or explicit call
     }
     updateActivity();
}

// Internal: Send the LINK_CLOSE packet
void Link::sendLinkClose() {
     uint16_t closePacketId = _ownerRef.getNextPacketId();
     uint8_t buffer[RNS_MIN_HEADER_SIZE]; // No payload
     size_t len = 0;
     bool ok = ReticulumPacket::serialize_control(buffer, len,
        _destinationAddress.data(), _ownerRef.getNodeAddress(),
        RNS_HEADER_TYPE_DATA, // Close uses DATA type
        RNS_CONTEXT_LINK_CLOSE,
        closePacketId, 0); // Sequence 0 for control
      if (ok) { _ownerRef.sendPacketRaw(buffer, len, _destinationAddress.data()); }
      else { DebugSerial.println("! ERROR: Link::sendLinkClose serialize failed!"); }
}

// Handle incoming LINK_CLOSE packet from peer
void Link::processLinkClose(const RnsPacketInfo& closePacket) {
    DebugSerial.print("Link::processLinkClose received from: "); Utils::printBytes(closePacket.source, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
    sendAck(0); // ACK the close request (seq 0)
    _state = LinkState::CLOSED; // Transition to closed state immediately
    clearPendingQueue();
    // Ask manager to remove this link instance now
    _ownerRef.removeLink(_destinationAddress.data());
}

// Force immediate closure and state change (called on critical error or timeout)
// Does NOT notify the peer.
bool Link::teardown() {
    if (_state == LinkState::CLOSED) return false;
    DebugSerial.print("! Link::teardown invoked for "); Utils::printBytes(_destinationAddress.data(), RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println();
    _state = LinkState::CLOSED; // Set state directly
    clearPendingQueue();
    // DO NOT call removeLink here - let the owner (LinkManager) manage removal
    // based on the CLOSED state during its prune/timeout checks.
    return true;
}

// Clear pending packet queue and associated timers/counters
void Link::clearPendingQueue() {
    _pendingOutgoingPackets.clear();
    _currentRetryCount = 0;
    _stateTimer = 0; // Stop timers related to pending packets/state waits
}
