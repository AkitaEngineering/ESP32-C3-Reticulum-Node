#ifndef LINK_H
#define LINK_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include <list>
#include <array>
#include <memory> // Include memory for shared_ptr potentially

#include "Config.h"
#include "ReticulumPacket.h" // For RnsPacketInfo

// Forward declaration
class LinkManager;

class Link {
public:
    enum class LinkState {
        CLOSED,
        PENDING_REQ, // Waiting for ACK to our LINK_REQ
        ESTABLISHED,
        CLOSING      // Waiting for ACK to our LINK_CLOSE
    };

    // Constructor requires destination address and owner (LinkManager)
    Link(const uint8_t* destination, LinkManager& owner);
    ~Link(); // Destructor

    // Core methods
    bool establish(); // Initiate link establishment
    bool sendData(const std::vector<uint8_t>& dataPayload); // Send application data
    void handlePacket(const RnsPacketInfo& packetInfo); // Process incoming packet for this link
    void checkTimeouts(); // Called periodically to check for ACK/retransmission timeouts
    void close(bool notifyPeer = true); // Initiate link closure
    bool teardown(); // Force immediate closure and cleanup (sets state to CLOSED)

    // State checks
    bool isEstablished() const { return _state == LinkState::ESTABLISHED; }
    bool isActive() const { return _state != LinkState::CLOSED; } // Active if not fully CLOSED
    const uint8_t* getDestination() const { return _destinationAddress.data(); }
    unsigned long getLastActivityTime() const { return _lastActivityTime; }
    LinkState getState() const { return _state; } // Added getter for state


private:
    // Structure to hold packet info and retransmission state
    struct PendingPacket {
        RnsPacketInfo packetInfo; // Holds the full packet info for retransmission
        unsigned long firstSentTime;
        unsigned long lastSentTime;
        // uint8_t retryCount = 0; // Retry count specific to this packet (use _currentRetryCount for link state)
    };

    void sendLinkRequest();
    void sendLinkClose();
    void sendAck(uint16_t sequenceToAck);
    void sendPacketInternal(const RnsPacketInfo& packetInfo); // Adds to queue, serializes, sends, starts timers
    void processAck(const RnsPacketInfo& ackPacket);
    void processData(const RnsPacketInfo& dataPacket);
    void processLinkRequest(const RnsPacketInfo& reqPacket);
    void processLinkClose(const RnsPacketInfo& closePacket);
    void retransmitOldestPending();
    void clearPendingQueue();
    void updateActivity() { _lastActivityTime = millis(); } // Update timestamp


    std::array<uint8_t, RNS_ADDRESS_SIZE> _destinationAddress;
    LinkManager& _ownerRef; // Reference to owner for sending/config access
    LinkState _state = LinkState::CLOSED;
    unsigned long _lastActivityTime = 0;
    unsigned long _stateTimer = 0; // Timer for state transitions (REQ/CLOSE ACK) and retransmissions
    uint16_t _outgoingSequence = 0; // Next data sequence number to send
    uint16_t _expectedIncomingSequence = 0; // Next data sequence number expected
    uint16_t _linkReqPacketId = 0; // Packet ID of the link request we sent

    // Queue for reliable data packets awaiting ACK (simplified: size 1 initially)
    std::list<PendingPacket> _pendingOutgoingPackets;
    uint8_t _currentRetryCount = 0; // Retries for the packet/state action currently awaiting ACK/timeout


};

#endif // LINK_H
