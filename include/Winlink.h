#ifndef WINLINK_H
#define WINLINK_H

#include <Arduino.h>
#include <vector>
#include <cstdint>
#include <string>
#include "AX25.h"

// Winlink Integration for HAM Email
// Supports Winlink 2000 protocol over packet radio

class Winlink {
public:
    // Winlink Message Structure
    struct Message {
        String to;              // Recipient email address
        String from;            // Sender email address
        String subject;         // Message subject
        String body;            // Message body
        String bbsCallsign;     // BBS callsign
        uint16_t messageId;     // Message ID
        
        Message() : messageId(0) {}
    };
    
    // Winlink Connection State
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        AUTHENTICATING,
        AUTHENTICATED,
        SENDING,
        RECEIVING,
        ERROR
    };
    
    Winlink();

    using RawSender = bool (*)(const uint8_t* data, size_t len, void* context);
    
    // Initialize Winlink
    bool begin(const char* callsign, const char* password = "");

    // Set raw frame sender callback (AX.25 frames)
    void setRawSender(RawSender sender, void* context);
    
    // Connect to Winlink BBS
    bool connect(const char* bbsCallsign);
    
    // Disconnect from BBS
    void disconnect();
    
    // Send message
    bool sendMessage(const Message& msg);
    
    // Receive messages
    bool receiveMessages(std::vector<Message>& messages);
    
    // Check for new messages
    bool checkForMessages();
    
    // Get connection state
    ConnectionState getState() const { return _state; }
    
    // Process incoming AX.25 frame
    bool processFrame(const AX25::Frame& frame);
    
private:
    String _callsign;
    String _password;
    String _bbsCallsign;
    ConnectionState _state;
    uint16_t _sequenceNumber;

    RawSender _rawSender = nullptr;
    void* _rawSenderContext = nullptr;
    std::vector<Message> _inbox;
    std::vector<Message> _outbox;
    
    // Winlink protocol handlers
    bool handleConnect(const AX25::Frame& frame);
    bool handleDisconnect(const AX25::Frame& frame);
    bool handleAck(const AX25::Frame& frame);
    bool handleNak(const AX25::Frame& frame);
    bool handleMessage(const AX25::Frame& frame);
    
    // Encode Winlink message
    bool encodeMessage(const Message& msg, std::vector<uint8_t>& output);
    
    // Decode Winlink message
    bool decodeMessage(const uint8_t* data, size_t len, Message& msg);
    
    // Send Winlink frame
    bool sendFrame(const std::vector<uint8_t>& data, bool requiresAck = true);
};

#endif // WINLINK_H

