#include <Arduino.h>

#include <vector>

#include "ReticulumNode.h"
#include "ReticulumPacket.h"
#include "RNSIdentity.h"
#include "Utils.h"

#ifndef INTEGRATION_SEND_INTERVAL_MS
#define INTEGRATION_SEND_INTERVAL_MS 5000UL
#endif

#ifndef INTEGRATION_NODE_ROLE
#define INTEGRATION_NODE_ROLE 0
#endif

namespace {

ReticulumNode reticulumNode;
uint8_t plainDestinationHash[RNSIdentity::TRUNCATED_HASH_BYTES] = {0};
uint32_t txCount = 0;
uint32_t rxCount = 0;
unsigned long lastSendMs = 0;
unsigned long lastStatusMs = 0;

const char* roleName() {
#if INTEGRATION_NODE_ROLE
    return "TX";
#else
    return "RX";
#endif
}

void printBytesLine(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (isprint(data[i])) {
            DebugSerial.print(static_cast<char>(data[i]));
        } else {
            DebugSerial.print('.');
        }
    }
}

void blinkStatus(int onMs = 80) {
    setStatusLed(true);
    delay(onMs);
    setStatusLed(false);
}

void onAppData(const uint8_t* sourceAddress, const std::vector<uint8_t>& data) {
    rxCount++;
    DebugSerial.print("[ITG RX] #");
    DebugSerial.print(rxCount);
    DebugSerial.print(" from ");
    Utils::printBytes(sourceAddress, RNS_ADDRESS_SIZE, DebugSerial);
    DebugSerial.print(" len=");
    DebugSerial.print(data.size());
    DebugSerial.print(" payload='");
    printBytesLine(data.data(), data.size());
    DebugSerial.println("'");
    blinkStatus(40);
}

void sendIntegrationMessage() {
    char payload[96];
    const unsigned long uptimeSeconds = millis() / 1000UL;
    const int written = snprintf(
        payload,
        sizeof(payload),
        "role=%s tx=%lu uptime=%lus",
        roleName(),
        static_cast<unsigned long>(txCount + 1),
        static_cast<unsigned long>(uptimeSeconds)
    );

    if (written <= 0) {
        return;
    }

    std::vector<uint8_t> body(payload, payload + written);
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t packetLen = 0;
    if (!ReticulumPacket::serialize(
            buffer,
            packetLen,
            plainDestinationHash,
            RNS_PACKET_DATA,
            RNS_DEST_PLAIN,
            RNS_PROPAGATION_BROADCAST,
            RNS_CONTEXT_NONE,
            0,
            body)) {
        DebugSerial.println("[ITG TX] serialize failed");
        return;
    }

    reticulumNode.getInterfaceManager().sendPacket(buffer, packetLen, plainDestinationHash);
    txCount++;
    DebugSerial.print("[ITG TX] #");
    DebugSerial.print(txCount);
    DebugSerial.print(" bytes=");
    DebugSerial.print(packetLen);
    DebugSerial.print(" payload='");
    DebugSerial.print(payload);
    DebugSerial.println("'");
    blinkStatus(40);
}

void printStatus() {
    DebugSerial.print("[ITG STATUS] role=");
    DebugSerial.print(roleName());
    DebugSerial.print(" routes=");
    DebugSerial.print(reticulumNode.getRoutingTable().getRouteCount());
    DebugSerial.print(" tx=");
    DebugSerial.print(txCount);
    DebugSerial.print(" rx=");
    DebugSerial.println(rxCount);
}

}  // namespace

void setup() {
    initStatusLed();
    setStatusLed(false);

    DebugSerial.begin(115200);
    Serial.begin(115200);

    const unsigned long start = millis();
    while (!Serial && millis() - start < 5000) {
        setStatusLed(((millis() / 250UL) & 1U) != 0U);
        delay(10);
    }
    setStatusLed(Serial);

    RNSIdentity::destination_hash(getConfiguredAppName(), nullptr, plainDestinationHash);

    DebugSerial.println();
    DebugSerial.println("===================================");
    DebugSerial.println(" ESP32 Integration Test Booting ");
    DebugSerial.println("===================================");
    DebugSerial.print("[ITG] Role: ");
    DebugSerial.println(roleName());
    DebugSerial.print("[ITG] App Name: ");
    DebugSerial.println(getConfiguredAppName());
    DebugSerial.print("[ITG] Dest hash: ");
    Utils::printBytes(plainDestinationHash, sizeof(plainDestinationHash), DebugSerial);
    DebugSerial.println();

    reticulumNode.setup();
    reticulumNode.setAppDataHandler(onAppData);

#if INTEGRATION_NODE_ROLE
    lastSendMs = millis() - INTEGRATION_SEND_INTERVAL_MS + 1000UL;
#else
    lastSendMs = millis();
#endif
    lastStatusMs = millis();
    printStatus();
}

void loop() {
    reticulumNode.loop();

    const unsigned long now = millis();
    if (now - lastSendMs >= INTEGRATION_SEND_INTERVAL_MS) {
        lastSendMs = now;
        sendIntegrationMessage();
    }

    if (now - lastStatusMs >= 5000UL) {
        lastStatusMs = now;
        printStatus();
    }
}