#include "Winlink.h"

Winlink::Winlink()
: _state(ConnectionState::DISCONNECTED),
  _sequenceNumber(0)
{}

bool Winlink::begin(const char* callsign, const char* password) {
    _callsign = callsign ? callsign : "";
    _password = password ? password : "";
    _state = _callsign.length() > 0 ? ConnectionState::DISCONNECTED : ConnectionState::ERROR;
    return _state != ConnectionState::ERROR;
}

void Winlink::setRawSender(RawSender sender, void* context) {
    _rawSender = sender;
    _rawSenderContext = context;
}

bool Winlink::connect(const char* bbsCallsign) {
    _bbsCallsign = bbsCallsign ? bbsCallsign : "";
    if (_callsign.isEmpty() || _bbsCallsign.isEmpty()) {
        _state = ConnectionState::ERROR;
        return false;
    }
    _state = ConnectionState::CONNECTED;
    return true;
}

void Winlink::disconnect() {
    _state = ConnectionState::DISCONNECTED;
}

bool Winlink::sendMessage(const Message& msg) {
    if (_state != ConnectionState::CONNECTED && _state != ConnectionState::AUTHENTICATED) {
        return false;
    }

    Message toSend = msg;
    if (toSend.messageId == 0) {
        toSend.messageId = ++_sequenceNumber;
    }
    if (toSend.bbsCallsign.length() == 0) {
        toSend.bbsCallsign = _bbsCallsign;
    }

    std::vector<uint8_t> payload;
    if (!encodeMessage(toSend, payload)) {
        return false;
    }
    _bbsCallsign = toSend.bbsCallsign;
    if (!sendFrame(payload, true)) {
        return false;
    }

    _outbox.push_back(toSend);
    return true;
}

bool Winlink::receiveMessages(std::vector<Message>& messages) {
    messages = _inbox;
    _inbox.clear();
    return !messages.empty();
}

bool Winlink::checkForMessages() {
    return !_inbox.empty();
}

bool Winlink::processFrame(const AX25::Frame& frame) {
    if (frame.control != AX25::ControlType::U_UI || frame.pid != 0xF0) {
        return false;
    }

    Message msg;
    if (!decodeMessage(frame.info.data(), frame.info.size(), msg)) {
        return false;
    }
    _inbox.push_back(msg);
    return true;
}

bool Winlink::handleConnect(const AX25::Frame& frame) {
    (void)frame;
    _state = ConnectionState::CONNECTED;
    return true;
}
bool Winlink::handleDisconnect(const AX25::Frame& frame) { (void)frame; _state = ConnectionState::DISCONNECTED; return true; }
bool Winlink::handleAck(const AX25::Frame& frame) { (void)frame; return true; }
bool Winlink::handleNak(const AX25::Frame& frame) { (void)frame; return true; }
bool Winlink::handleMessage(const AX25::Frame& frame) { return processFrame(frame); }

bool Winlink::encodeMessage(const Message& msg, std::vector<uint8_t>& output) {
    String safeTo = msg.to; safeTo.replace("|", "/");
    String safeFrom = msg.from; safeFrom.replace("|", "/");
    String safeSubject = msg.subject; safeSubject.replace("|", "/");
    String safeBody = msg.body; safeBody.replace("|", "/");

    String payload = String("WL2K|") + safeTo + "|" + safeFrom + "|" + safeSubject + "|" + String(msg.messageId) + "|" + safeBody;
    output.assign(payload.begin(), payload.end());
    return !output.empty();
}

bool Winlink::decodeMessage(const uint8_t* data, size_t len, Message& msg) {
    if (!data || len == 0) return false;
    String payload;
    payload.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        payload += static_cast<char>(data[i]);
    }

    if (!payload.startsWith("WL2K|")) return false;

    std::vector<String> parts;
    int start = 0;
    while (true) {
        int idx = payload.indexOf('|', start);
        if (idx < 0) {
            parts.push_back(payload.substring(start));
            break;
        }
        parts.push_back(payload.substring(start, idx));
        start = idx + 1;
    }

    if (parts.size() < 6) return false;

    msg.to = parts[1];
    msg.from = parts[2];
    msg.subject = parts[3];
    msg.messageId = static_cast<uint16_t>(parts[4].toInt());
    String body = parts[5];
    for (size_t i = 6; i < parts.size(); ++i) {
        body += "|" + parts[i];
    }
    msg.body = body;
    return true;
}

bool Winlink::sendFrame(const std::vector<uint8_t>& data, bool requiresAck) {
    (void)requiresAck;
    if (!_rawSender) return false;

    AX25::Frame frame;
    frame.source = AX25::Address(_callsign.c_str(), 0);
    frame.destination = AX25::Address(_bbsCallsign.c_str(), 0);
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0;
    frame.info = data;

    std::vector<uint8_t> encoded;
    if (!AX25::encodeFrame(frame, encoded)) {
        return false;
    }

    return _rawSender(encoded.data(), encoded.size(), _rawSenderContext);
}
