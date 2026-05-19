#include "InterfaceManager.h"
#include "Config.h"
#include "Utils.h"
#include "Log.h"
#include "RoutingTable.h" // Need full definition now for RouteEntry
#include "ReticulumPacket.h" // For MAX_PACKET_SIZE
#include "AX25.h"
#include <WiFi.h>
#include <esp_wifi.h> // For esp_wifi_set_ps
#include <time.h>
#ifdef LORA_ENABLED
#include <SPI.h>
#endif
#ifdef HAM_MODEM_ENABLED
  #ifdef AUDIO_MODEM_ENABLED
    #include "AudioModem.h"
  #endif
  #ifdef WINLINK_ENABLED
    #include "Winlink.h"
  #endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#ifdef IPFS_ENABLED
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

// ESP-NOW broadcast MAC address (FF:FF:FF:FF:FF:FF)
static const uint8_t espnow_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint32_t espnow_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// Define static instance pointer
InterfaceManager* InterfaceManager::_instance = nullptr;

InterfaceManager::InterfaceManager(PacketReceiverCallback receiver, RoutingTable& routingTable) :
    _packetReceiver(receiver),
    _routingTableRef(routingTable),
    _espNowInitialized(false),
    // Use lambda to capture 'this' for the member function callback
    _serialKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
#if BLUETOOTH_CLASSIC_AVAILABLE
    , _bluetoothKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
#endif
#ifdef LORA_ENABLED
    , _lora(nullptr), _loraInitialized(false)
#endif
#ifdef HAM_MODEM_ENABLED
    , _hamModemKissProcessor([this](const std::vector<uint8_t>& data, InterfaceType iface){ this->handleKissPacket(data, iface); })
    , _hamModemInitialized(false)
    #ifdef AUDIO_MODEM_ENABLED
    , _audioModem(nullptr)
    #endif
    #ifdef WINLINK_ENABLED
    , _winlink(nullptr)
    #endif
#endif
#ifdef IPFS_ENABLED
    , _ipfsInitialized(false)
#endif
{
    if (_instance != nullptr) {
         // This should not happen if InterfaceManager is instantiated only once by ReticulumNode
         DebugSerial.println("! FATAL: Multiple InterfaceManager instances detected!");
         // Handle error: abort?
         return;
    }
    _instance = this; // Set the static instance pointer
}

void InterfaceManager::recordInterfaceRx(InterfaceType ifType, size_t bytes) {
    const size_t index = interfaceStatsIndex(ifType);
    if (index >= _interfaceStats.size()) {
        return;
    }

    InterfaceCounters &stats = _interfaceStats[index];
    stats.lastRxMs = millis();
    stats.rxPackets++;
    stats.rxBytes += static_cast<uint32_t>(bytes);
}

void InterfaceManager::recordInterfaceTx(InterfaceType ifType, size_t bytes) {
    const size_t index = interfaceStatsIndex(ifType);
    if (index >= _interfaceStats.size()) {
        return;
    }

    InterfaceCounters &stats = _interfaceStats[index];
    stats.lastTxMs = millis();
    stats.txPackets++;
    stats.txBytes += static_cast<uint32_t>(bytes);
}

bool InterfaceManager::isInterfaceSupported(InterfaceType ifType) const {
    switch (ifType) {
        case InterfaceType::SERIAL_PORT:
        case InterfaceType::ESP_NOW:
        case InterfaceType::WIFI_UDP:
            return true;
#if BLUETOOTH_CLASSIC_AVAILABLE
        case InterfaceType::BLUETOOTH:
            return true;
#else
        case InterfaceType::BLUETOOTH:
            return false;
#endif
#ifdef LORA_ENABLED
        case InterfaceType::LORA:
            return true;
#else
        case InterfaceType::LORA:
            return false;
#endif
#ifdef HAM_MODEM_ENABLED
        case InterfaceType::HAM_MODEM:
            return true;
#else
        case InterfaceType::HAM_MODEM:
            return false;
#endif
#ifdef IPFS_ENABLED
        case InterfaceType::IPFS:
            return true;
#else
        case InterfaceType::IPFS:
            return false;
#endif
        default:
            return false;
    }
}

InterfaceHealthSnapshot InterfaceManager::getInterfaceHealthSnapshot(InterfaceType ifType) const {
    InterfaceHealthSnapshot snapshot;
    snapshot.supported = isInterfaceSupported(ifType);
    snapshot.usable = snapshot.supported && isInterfaceUsableForRouting(ifType);

    const size_t index = interfaceStatsIndex(ifType);
    if (index >= _interfaceStats.size()) {
        return snapshot;
    }

    const InterfaceCounters &stats = _interfaceStats[index];
    snapshot.last_rx_uptime_ms = stats.lastRxMs;
    snapshot.last_tx_uptime_ms = stats.lastTxMs;
    snapshot.rx_packets = stats.rxPackets;
    snapshot.tx_packets = stats.txPackets;
    snapshot.rx_bytes = stats.rxBytes;
    snapshot.tx_bytes = stats.txBytes;
    return snapshot;
}

void InterfaceManager::setup() {
    setupSerial(); // Assumes Serial.begin() already called

    // Initialize Bluetooth first (if available)
#if BLUETOOTH_CLASSIC_AVAILABLE
    setupBluetooth();
#endif
    
    // Always start WiFi radio (needed for ESP-NOW even without AP connection)
    setupWiFi();
    setupESPNow();
    
#ifdef LORA_ENABLED
    setupLoRa();
#endif

#ifdef HAM_MODEM_ENABLED
    setupHAMModem();
#endif

#ifdef IPFS_ENABLED
    setupIPFS();
#endif
    
    DebugSerial.println("Interface Manager Setup Complete.");
}

void InterfaceManager::loop() {
    cleanupExpiredEspNowAssemblies();
    processEspNowStoreForward();
#if defined(KISS_OVER_USB)
    flushPendingUsbKissFrames();
#endif

    // Periodic ESP-NOW diagnostic over KISS (every 10 seconds)
    unsigned long now = millis();
    if (now - _lastDiagMs >= 10000) {
        _lastDiagMs = now;
        sendEspNowDiagKiss();
    }

    // Process inputs from KISS interfaces
    processSerialInput();
#if BLUETOOTH_CLASSIC_AVAILABLE
    processBluetoothInput();
#endif

    // Process UDP input if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        processWiFiInput();
    }

#ifdef LORA_ENABLED
    processLoRaInput();
#endif

#ifdef HAM_MODEM_ENABLED
    processHAMModemInput();
#endif

#if defined(HAM_MODEM_ENABLED) && defined(AUDIO_MODEM_ENABLED)
    pollAX25FromAudioModem();
#endif
}

void InterfaceManager::setupSerial() {
    // KissSerial is started in main.cpp for KISS interface
    LOG_INFO("IF: KISS Serial interface ready (RX=%d, TX=%d, baud=%d).", KISS_UART_RX, KISS_UART_TX, KISS_SERIAL_SPEED);
}

void InterfaceManager::setupWiFi() {
    // On ESP32-C3 the USB CDC controller shares clocking with the radio
    // subsystem.  Cycling through WIFI_OFF can glitch the USB peripheral
    // and cause the host to see a disconnect.  Go straight to STA mode.
    WiFi.disconnect(false); // clear any stale connection without turning off radio
    WiFi.mode(WIFI_STA);
    delay(100);            // let the radio settle
    // ESP-NOW is latency-sensitive; modem sleep can drop/delay frames.
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Only attempt AP connection if explicitly enabled and credentials are provided.
    if (WIFI_STA_CONNECT_ENABLED && strlen(WIFI_SSID) > 0 && strlen(WIFI_PASSWORD) > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        LOG_INFO("IF: Connecting to WiFi %s", WIFI_SSID);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500); DebugSerial.print("."); attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            LOG_INFO("IF: WiFi connected.");
            LOG_INFO("IF: IP address: %s", WiFi.localIP().toString().c_str());
            setenv("TZ", "UTC0", 1);
            tzset();
            configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
            if (_udp.begin(RNS_UDP_PORT)) {
                DebugSerial.print("IF: UDP Listening on port "); DebugSerial.println(RNS_UDP_PORT);
            } else {
                DebugSerial.println("! ERROR: Failed to start UDP listener!");
            }
        } else {
            LOG_WARN("IF: WiFi connection failed after %d attempts.", attempts);
        }
    } else {
        if (!WIFI_STA_CONNECT_ENABLED) {
            LOG_INFO("IF: AP connection disabled (WIFI_STA_CONNECT_ENABLED=0). Running ESP-NOW-only mode.");
        } else {
            LOG_INFO("IF: No WiFi credentials configured, skipping AP connection.");
        }
        LOG_INFO("IF: WiFi radio active in STA mode for ESP-NOW.");
    }
}

void InterfaceManager::setupESPNow() {
     DebugSerial.print("IF: Device MAC: "); DebugSerial.println(WiFi.macAddress());

    esp_err_t pwr_err = esp_wifi_set_max_tx_power(ESP_NOW_TX_POWER_QDBM);
    if (pwr_err == ESP_OK) {
        DebugSerial.print("IF: WiFi TX power set (qdbm): ");
        DebugSerial.println(ESP_NOW_TX_POWER_QDBM);
    } else {
        DebugSerial.print("! WARN: Failed to set TX power: ");
        DebugSerial.println(esp_err_to_name(pwr_err));
    }

#if ESP_NOW_ENABLE_LR
    esp_err_t proto_err = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
    if (proto_err == ESP_OK) {
        DebugSerial.println("IF: WiFi protocol includes 11LR for ESP-NOW");
    } else {
        DebugSerial.print("! WARN: Failed to enable 11LR protocol: ");
        DebugSerial.println(esp_err_to_name(proto_err));
    }
#endif

    // Optional channel override
    if (ESP_NOW_CHANNEL != 0) {
        esp_err_t ch_err = esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        if (ch_err == ESP_OK) {
            DebugSerial.print("IF: Forced WiFi channel to "); DebugSerial.println(ESP_NOW_CHANNEL);
        } else {
            DebugSerial.print("! WARN: Failed to set WiFi channel: "); DebugSerial.println(esp_err_to_name(ch_err));
        }
    } else {
        DebugSerial.println("IF: ESP-NOW channel follows connected AP channel (ESP_NOW_CHANNEL=0).");
    }

    // Query current channel for debugging
    uint8_t curr_ch;
    wifi_second_chan_t second_ch;
    esp_wifi_get_channel(&curr_ch, &second_ch);
    DebugSerial.print("IF: Current WiFi channel: "); DebugSerial.println(curr_ch);

    if (esp_now_init() != ESP_OK) {
        DebugSerial.println("! ERROR: Initializing ESP-NOW failed!");
        return; // Cannot proceed with ESP-NOW
    }
    // Register static callback function which calls instance method
    esp_err_t result = esp_now_register_recv_cb(staticEspNowRecvCallback);
    if (result != ESP_OK) {
         DebugSerial.print("! ERROR: Failed to register ESP-NOW recv cb: "); DebugSerial.println(esp_err_to_name(result));
    }
    esp_now_register_send_cb(staticEspNowSendCallback);

    _espNowInitialized = true;

    // Add broadcast peer initially (needed to receive broadcasts)
    if (!addEspNowPeer(espnow_broadcast_mac)) {
         DebugSerial.println("! WARN: Failed to add initial ESP-NOW broadcast peer");
    }
    DebugSerial.println("IF: ESP-NOW Initialized.");
}

#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::setupBluetooth() {
    const char* deviceName = getConfiguredNodeName();
      if (!_serialBT.begin(deviceName)) {
         DebugSerial.println("! ERROR: Bluetooth Serial initialization failed!");
     } else {
        DebugSerial.print("IF: Bluetooth ready. Device Name: ");
          DebugSerial.println(deviceName);
    }
}
#endif

// --- Input Processing ---
void InterfaceManager::processWiFiInput() {
    if (WiFi.status() != WL_CONNECTED) return;
    int packetSize = _udp.parsePacket();
    if (packetSize > 0) {
        if (packetSize > MAX_PACKET_SIZE) {
             DebugSerial.print("! WARN: Oversized UDP packet received ("); DebugSerial.print(packetSize); DebugSerial.println(" bytes), discarding.");
             _udp.flush(); // Discard data
             return;
        }

        // Use unique_ptr for automatic memory management
        std::unique_ptr<uint8_t[]> udpBuffer(new (std::nothrow) uint8_t[packetSize]);
        if (!udpBuffer) {
             LOG_ERROR("new failed for UDP buffer!");
             _udp.flush();
             return;
        }

        int len = _udp.read(udpBuffer.get(), packetSize);
        if (len > 0 && _packetReceiver) {
            recordInterfaceRx(InterfaceType::WIFI_UDP, static_cast<size_t>(len));
            _packetReceiver(udpBuffer.get(), len, InterfaceType::WIFI_UDP, nullptr, _udp.remoteIP(), _udp.remotePort());
        }
    }
}

void InterfaceManager::processSerialInput() {
     while (KissSerial.available()) {
        _serialKissProcessor.decodeByte(KissSerial.read(), InterfaceType::SERIAL_PORT);
    }
}

#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::processBluetoothInput() {
     while (_serialBT.available()) {
         _bluetoothKissProcessor.decodeByte(_serialBT.read(), InterfaceType::BLUETOOTH);
    }
}
#endif

// --- KISS Packet Handling ---
void InterfaceManager::handleKissPacket(const std::vector<uint8_t>& packetData, InterfaceType interface) {
     // Debug: Print raw received packet
     DebugSerial.print("[KISS] Received ");
     DebugSerial.print(packetData.size());
     DebugSerial.print(" bytes on interface ");
     DebugSerial.print(static_cast<int>(interface));
     DebugSerial.print(": ");
     for (size_t i = 0; i < min(packetData.size(), (size_t)20); i++) {
         if (packetData[i] < 0x10) DebugSerial.print("0");
         DebugSerial.print(packetData[i], HEX);
         DebugSerial.print(" ");
     }
     if (packetData.size() > 20) DebugSerial.print("...");
     DebugSerial.println();

     if (_packetReceiver) {
         recordInterfaceRx(interface, packetData.size());
         // Pass received packet up to ReticulumNode, indicate no specific sender MAC/IP/Port
         _packetReceiver(packetData.data(), packetData.size(), interface, nullptr, IPAddress(), 0);
     }
}


// --- Sending Logic ---
void InterfaceManager::sendPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, InterfaceType excludeInterface) {
    if (!packetBuffer || packetLen == 0) return;

    // Determine target interface(s) based on routing (or broadcast if unknown)
    RouteEntry* route = _routingTableRef.findRoute(
        destinationAddr,
        excludeInterface,
        [this](InterfaceType ifType) {
            return this->isInterfaceUsableForRouting(ifType);
        }
    );

    // Send via specific interface if route found, otherwise broadcast on relevant interfaces
    if (route) {
        if (route->interface != excludeInterface) {
             // Send only via the routed interface
             sendPacketVia(route->interface, packetBuffer, packetLen, destinationAddr);
        }
        // In TNC mode, also bridge to serial so the host sees all traffic,
        // unless the packet already came from serial or is routed to serial.
        if (excludeInterface != InterfaceType::SERIAL_PORT &&
            route->interface != InterfaceType::SERIAL_PORT) {
            sendPacketViaSerial(packetBuffer, packetLen);
        }
    } else {
        // No route, broadcast on primary interfaces (excluding source)
        // DebugSerial.print("Broadcasting packet (no route found) for dest: "); Utils::printBytes(destinationAddr, RNS_ADDRESS_SIZE, DebugSerial); DebugSerial.println(); // Verbose
        if (_espNowInitialized && excludeInterface != InterfaceType::ESP_NOW) {
            sendPacketViaEspNow(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
        if (WiFi.status() == WL_CONNECTED && excludeInterface != InterfaceType::WIFI_UDP) {
             sendPacketViaWiFi(packetBuffer, packetLen, nullptr); // Broadcast = null dest for internal func
        }
#ifdef LORA_ENABLED
        if (_loraInitialized && excludeInterface != InterfaceType::LORA) {
            sendPacketViaLoRa(packetBuffer, packetLen, nullptr);
        }
#endif
        // Bridge packets to serial KISS (USB) so the host receives them
        if (excludeInterface != InterfaceType::SERIAL_PORT) { sendPacketViaSerial(packetBuffer, packetLen); }
#if BLUETOOTH_CLASSIC_AVAILABLE
        // if (_serialBT.connected() && excludeInterface != InterfaceType::BLUETOOTH) { sendPacketViaBluetooth(packetBuffer, packetLen); }
#endif
    }
}

void InterfaceManager::sendPacketVia(InterfaceType ifType, const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
     if (!packetBuffer || packetLen == 0) return;
     switch(ifType) {
        case InterfaceType::ESP_NOW:  if (_espNowInitialized) sendPacketViaEspNow(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::WIFI_UDP: if (WiFi.status() == WL_CONNECTED) sendPacketViaWiFi(packetBuffer, packetLen, destinationAddr); break;
        case InterfaceType::SERIAL_PORT:   sendPacketViaSerial(packetBuffer, packetLen); break;
#if BLUETOOTH_CLASSIC_AVAILABLE
        case InterfaceType::BLUETOOTH:sendPacketViaBluetooth(packetBuffer, packetLen); break;
#endif
#ifdef LORA_ENABLED
        case InterfaceType::LORA: sendPacketViaLoRa(packetBuffer, packetLen, destinationAddr); break;
#endif
#ifdef HAM_MODEM_ENABLED
        case InterfaceType::HAM_MODEM: sendPacketViaHAMModem(packetBuffer, packetLen); break;
#endif
#ifdef IPFS_ENABLED
        case InterfaceType::IPFS: sendPacketViaIPFS(packetBuffer, packetLen, destinationAddr); break;
#endif
        default: DebugSerial.print("! WARN: sendPacketVia unsupported interface: "); DebugSerial.println(static_cast<int>(ifType)); break;
     }
}

void InterfaceManager::broadcastAnnounce(const uint8_t *packetBuffer, size_t packetLen) {
     if (!packetBuffer || packetLen == 0) return;
     // Use nullptr destination for broadcast variants
     if (_espNowInitialized) {
         sendPacketViaEspNow(packetBuffer, packetLen, nullptr);
     }
     if (WiFi.status() == WL_CONNECTED) {
         sendPacketViaWiFi(packetBuffer, packetLen, nullptr);
     }
#ifdef LORA_ENABLED
     if (_loraInitialized) {
         sendPacketViaLoRa(packetBuffer, packetLen, nullptr);
     }
#endif
#ifdef HAM_MODEM_ENABLED
     if (_hamModemInitialized) {
         sendPacketViaHAMModem(packetBuffer, packetLen);
     }
#endif
     // Always send announces via serial KISS (USB/UART)
     sendPacketViaSerial(packetBuffer, packetLen);
}

// Internal send implementations
bool InterfaceManager::sendPacketViaEspNow(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    return sendPacketViaEspNowInternal(packetBuffer, packetLen, destinationAddr, true);
}

bool InterfaceManager::sendPacketViaEspNowInternal(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr, bool enqueueOnFailure) {
    if (!packetBuffer || packetLen == 0) return false;

    const uint8_t* targetMac = espnow_broadcast_mac; // Default to broadcast

#if ESP_NOW_INDISCRIMINATE_BROADCAST
    (void)destinationAddr;
#else
    if (destinationAddr != nullptr) { // If destination provided, try to find route
        RouteEntry* route = _routingTableRef.findRouteForInterface(destinationAddr, InterfaceType::ESP_NOW);
         if (route) {
             targetMac = route->next_hop_mac;
             // Ensure peer exists - crucial for direct send
             if (!checkEspNowPeer(targetMac)) {
                 if (!addEspNowPeer(targetMac)) {
                     targetMac = espnow_broadcast_mac; // Fallback if add fails
                 }
             }
         } else { targetMac = espnow_broadcast_mac; } // No route / wrong interface
    } // else: destinationAddr is null -> use broadcastMac
#endif

    if (packetLen <= ESPNOW_MAX_PAYLOAD_LEN) {
        esp_err_t result = esp_now_send(targetMac, packetBuffer, packetLen);
        if (result != ESP_OK) {
            DebugSerial.print("! ESP-NOW Send Error to "); Utils::printBytes(targetMac, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(result));
#if ESPNOW_STORE_FORWARD_ENABLED
            if (enqueueOnFailure) {
                enqueueEspNowPacket(packetBuffer, packetLen, destinationAddr);
            }
#endif
            return false;
        }
        recordInterfaceTx(InterfaceType::ESP_NOW, packetLen);
        return true;
    }

    const size_t maxChunk = ESPNOW_FRAG_CHUNK_DATA_LEN;
    const size_t totalChunksSz = (packetLen + maxChunk - 1) / maxChunk;
    if (totalChunksSz == 0 || totalChunksSz > 255) {
        DebugSerial.print("! ESP-NOW fragmentation failed, packet too large for chunk count: ");
        DebugSerial.println(packetLen);
#if ESPNOW_STORE_FORWARD_ENABLED
        if (enqueueOnFailure) {
            enqueueEspNowPacket(packetBuffer, packetLen, destinationAddr);
        }
#endif
        return false;
    }

    const uint8_t totalChunks = static_cast<uint8_t>(totalChunksSz);
    _espNowTxMessageId++;
    if (_espNowTxMessageId == 0) {
        _espNowTxMessageId = 1;
    }
    const uint16_t messageId = _espNowTxMessageId;
    const uint32_t fullCrc = espnow_crc32(packetBuffer, packetLen);

    for (uint8_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex) {
        const size_t offset = static_cast<size_t>(chunkIndex) * maxChunk;
        const size_t remaining = packetLen - offset;
        const size_t chunkLen = remaining > maxChunk ? maxChunk : remaining;

        uint8_t frame[ESPNOW_MAX_PAYLOAD_LEN];
        frame[0] = ESPNOW_FRAG_MAGIC0;
        frame[1] = ESPNOW_FRAG_MAGIC1;
        frame[2] = ESPNOW_FRAG_VERSION;
        frame[3] = ESPNOW_FRAGMENT_CRC32_ENABLED ? 0x01 : 0x00;
        frame[4] = static_cast<uint8_t>((messageId >> 8) & 0xFF);
        frame[5] = static_cast<uint8_t>(messageId & 0xFF);
        frame[6] = chunkIndex;
        frame[7] = totalChunks;
        frame[8] = static_cast<uint8_t>((fullCrc >> 24) & 0xFF);
        frame[9] = static_cast<uint8_t>((fullCrc >> 16) & 0xFF);
        frame[10] = static_cast<uint8_t>((fullCrc >> 8) & 0xFF);
        frame[11] = static_cast<uint8_t>(fullCrc & 0xFF);
        memcpy(frame + ESPNOW_FRAG_HEADER_LEN, packetBuffer + offset, chunkLen);

        esp_err_t result = esp_now_send(targetMac, frame, ESPNOW_FRAG_HEADER_LEN + chunkLen);
        if (result != ESP_OK) {
            DebugSerial.print("! ESP-NOW fragment send failed (msg=");
            DebugSerial.print(messageId);
            DebugSerial.print(", chunk=");
            DebugSerial.print(chunkIndex);
            DebugSerial.print("/");
            DebugSerial.print(totalChunks);
            DebugSerial.print("): ");
            DebugSerial.println(esp_err_to_name(result));
#if ESPNOW_STORE_FORWARD_ENABLED
            if (enqueueOnFailure) {
                enqueueEspNowPacket(packetBuffer, packetLen, destinationAddr);
            }
#endif
            return false;
        }
    }

    recordInterfaceTx(InterfaceType::ESP_NOW, packetLen);
    return true;
}

void InterfaceManager::sendPacketViaWiFi(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
     if (WiFi.status() != WL_CONNECTED) return;

    IPAddress targetIp;
    uint16_t targetPort = RNS_UDP_PORT;
    IPAddress broadcastIp = WiFi.broadcastIP();
    targetIp = broadcastIp; // Default to broadcast

     if (destinationAddr != nullptr) { // If destination provided, try to find route
          RouteEntry* route = _routingTableRef.findRouteForInterface(destinationAddr, InterfaceType::WIFI_UDP);
          if (route && route->next_hop_ip) {
            targetIp = route->next_hop_ip;
            // targetPort = route->next_hop_port; // Use standard port
        } // else: use broadcast IP
     } // else: destinationAddr is null -> use broadcast IP

    if (!targetIp || targetIp == INADDR_NONE) {
        DebugSerial.println("! WARN: UDP Target IP is invalid, cannot send.");
        return;
    }

    _udp.beginPacket(targetIp, targetPort);
    size_t sent = _udp.write(packetBuffer, packetLen);
    if (sent != packetLen) { DebugSerial.print("! WARN: UDP write incomplete (sent "); DebugSerial.print(sent); DebugSerial.print("/"); DebugSerial.print(packetLen); DebugSerial.println(" bytes)"); }
    if (!_udp.endPacket()) { DebugSerial.println("! ERROR: UDP endPacket failed!"); }
    else { recordInterfaceTx(InterfaceType::WIFI_UDP, packetLen); }
}

bool InterfaceManager::enqueueEspNowPacket(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
#if !ESPNOW_STORE_FORWARD_ENABLED
    (void)packetBuffer;
    (void)packetLen;
    (void)destinationAddr;
    return false;
#else
    if (!packetBuffer || packetLen == 0 || packetLen > MAX_PACKET_SIZE) {
        return false;
    }

    if (_espNowStoreQueue.size() >= ESPNOW_SF_QUEUE_SIZE) {
        DebugSerial.println("! WARN: ESP-NOW store-forward queue full, dropping oldest packet");
        _espNowStoreQueue.pop_front();
    }

    EspNowQueuedPacket queued;
    queued.payload.assign(packetBuffer, packetBuffer + packetLen);
    queued.hasDestination = (destinationAddr != nullptr);
    if (queued.hasDestination) {
        memcpy(queued.destination.data(), destinationAddr, RNS_ADDRESS_SIZE);
    }
    queued.attempts = 0;
    queued.nextTryMs = millis() + ESPNOW_SF_RETRY_MS;
    _espNowStoreQueue.push_back(std::move(queued));
    return true;
#endif
}

void InterfaceManager::processEspNowStoreForward() {
#if ESPNOW_STORE_FORWARD_ENABLED
    if (!_espNowInitialized || _espNowStoreQueue.empty()) {
        return;
    }

    unsigned long now = millis();
    EspNowQueuedPacket &queued = _espNowStoreQueue.front();
    if ((long)(now - queued.nextTryMs) < 0) {
        return;
    }

    const uint8_t *dest = queued.hasDestination ? queued.destination.data() : nullptr;
    bool ok = sendPacketViaEspNowInternal(queued.payload.data(), queued.payload.size(), dest, false);
    if (ok) {
        _espNowStoreQueue.pop_front();
        return;
    }

    queued.attempts++;
    if (queued.attempts >= ESPNOW_SF_MAX_ATTEMPTS) {
        DebugSerial.println("! WARN: ESP-NOW store-forward max attempts reached, dropping packet");
        _espNowStoreQueue.pop_front();
        return;
    }
    queued.nextTryMs = now + ESPNOW_SF_RETRY_MS;
#endif
}

#if METRICS_ENABLED && METRICS_UDP_ENABLED
void InterfaceManager::sendUdpMetrics(const String &json) {
    if (WiFi.status() != WL_CONNECTED) return;
    IPAddress bcast = WiFi.broadcastIP();
    if (!bcast || bcast == INADDR_NONE) return;
    _udp.beginPacket(bcast, METRICS_UDP_PORT);
    _udp.print(json);
    _udp.endPacket();
    LOG_DEBUG("UDP metrics sent to %s:%u", bcast.toString().c_str(), METRICS_UDP_PORT);
}
#endif

// KISS interface sends packaets over dedicated serial link
void InterfaceManager::sendPacketViaSerial(const uint8_t *packetBuffer, size_t packetLen) {
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
#if defined(KISS_OVER_USB)
    size_t written = KissSerial.write(kissEncoded.data(), kissEncoded.size());
    if (written == kissEncoded.size()) {
        recordInterfaceTx(InterfaceType::SERIAL_PORT, packetLen);
        return;
    }

    if (written > 0 && written < kissEncoded.size()) {
        kissEncoded.erase(kissEncoded.begin(), kissEncoded.begin() + written);
    }

    if (!Serial && written == 0) {
        if (_pendingUsbKissFrames.size() >= MAX_PENDING_USB_KISS_FRAMES) {
            _pendingUsbKissFrames.pop_front();
        }
        _pendingUsbKissFrames.push_back(std::move(kissEncoded));
        recordInterfaceTx(InterfaceType::SERIAL_PORT, packetLen);
        return;
    }

    if (_pendingUsbKissFrames.size() >= MAX_PENDING_USB_KISS_FRAMES) {
        _pendingUsbKissFrames.pop_front();
    }
    _pendingUsbKissFrames.push_back(std::move(kissEncoded));
    recordInterfaceTx(InterfaceType::SERIAL_PORT, packetLen);
    return;
#endif
    (void)KissSerial.write(kissEncoded.data(), kissEncoded.size()); // ignore return value
    recordInterfaceTx(InterfaceType::SERIAL_PORT, packetLen);
    // if(sent != kissEncoded.size()) { DebugSerial.println("! WARN: Serial write incomplete"); } // Optional check
}

#if defined(KISS_OVER_USB)
void InterfaceManager::flushPendingUsbKissFrames() {
    if (_pendingUsbKissFrames.empty()) {
        return;
    }

    while (!_pendingUsbKissFrames.empty()) {
        std::vector<uint8_t> &frame = _pendingUsbKissFrames.front();
        size_t written = KissSerial.write(frame.data(), frame.size());
        if (written == 0) {
            break; // TX buffer full, retry next loop
        }
        if (written < frame.size()) {
            // Partial write: remove the bytes already sent so we
            // resume cleanly on the next flush without duplicating data.
            frame.erase(frame.begin(), frame.begin() + written);
            break;
        }
        _pendingUsbKissFrames.pop_front();
    }
}
#endif

bool InterfaceManager::isInterfaceUsableForRouting(InterfaceType ifType) const {
    switch (ifType) {
        case InterfaceType::ESP_NOW:
            return _espNowInitialized;
        case InterfaceType::WIFI_UDP:
            return WiFi.status() == WL_CONNECTED;
        case InterfaceType::SERIAL_PORT:
#if defined(KISS_OVER_USB)
            return static_cast<bool>(Serial);
#else
            return true;
#endif
#if BLUETOOTH_CLASSIC_AVAILABLE
        case InterfaceType::BLUETOOTH:
            return _serialBT.connected();
#endif
#ifdef LORA_ENABLED
        case InterfaceType::LORA:
            return _loraInitialized;
#endif
#ifdef HAM_MODEM_ENABLED
        case InterfaceType::HAM_MODEM:
            return _hamModemInitialized;
#endif
#ifdef IPFS_ENABLED
        case InterfaceType::IPFS:
            return _ipfsInitialized;
#endif
        default:
            return false;
    }
}

#if BLUETOOTH_CLASSIC_AVAILABLE
void InterfaceManager::sendPacketViaBluetooth(const uint8_t *packetBuffer, size_t packetLen) {
    if (!_serialBT.connected()) return;
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    size_t sent = _serialBT.write(kissEncoded.data(), kissEncoded.size());
    if (sent > 0) {
        recordInterfaceTx(InterfaceType::BLUETOOTH, packetLen);
    }
     // if(sent != kissEncoded.size()) { DebugSerial.println("! WARN: Bluetooth write incomplete"); } // Optional check
}
#endif

// --- ESP-NOW Peer Management ---
void InterfaceManager::enableEspNow() {
    if (_espNowInitialized) {
        DebugSerial.println("IF: ESP-NOW already enabled.");
        return;
    }
    // Ensure WiFi radio is in STA mode
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
    setupESPNow();
}

void InterfaceManager::disableEspNow() {
    if (!_espNowInitialized) {
        DebugSerial.println("IF: ESP-NOW already disabled.");
        return;
    }
    esp_now_deinit();
    _espNowPeers.clear();
    _espNowInitialized = false;
    DebugSerial.println("IF: ESP-NOW disabled.");
}

bool InterfaceManager::addEspNowPeer(const uint8_t* mac_addr) {
    if (!mac_addr || !_espNowInitialized) return false;
    if (checkEspNowPeer(mac_addr)) {
        // ensure our list also contains it
        std::array<uint8_t,6> arr;
        memcpy(arr.data(), mac_addr, 6);
        if (std::find(_espNowPeers.begin(), _espNowPeers.end(), arr) == _espNowPeers.end()) {
            _espNowPeers.push_back(arr);
        }
        return true; // Already exists at driver level
    }

    esp_now_peer_info_t peerInfo = {}; // Initialize all fields to 0/false/etc.
    memcpy(peerInfo.peer_addr, mac_addr, 6);
    // peerInfo.channel = 0; // Use current channel by default
    peerInfo.encrypt = false; // Encryption disabled (requires shared keys)
    // Try adding peer. On some IDF/Arduino builds the driver requires the ifidx
    // (station vs softAP) to be set. Try without ifidx first, then retry with
    // WIFI_IF_STA if the first attempt fails.
    esp_err_t add_result = esp_now_add_peer(&peerInfo);
    if (add_result != ESP_OK) {
         DebugSerial.print("! WARN: esp_now_add_peer() initial attempt failed for "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(add_result));
         // Retry with explicit interface index
         peerInfo.ifidx = WIFI_IF_STA;
         add_result = esp_now_add_peer(&peerInfo);
         if (add_result != ESP_OK) {
             DebugSerial.print("! ERROR: Failed to add ESP-NOW peer after retry "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(add_result));
             return false;
         } else {
             DebugSerial.println("IF: esp_now_add_peer succeeded on retry with WIFI_IF_STA");
         }
    }
    DebugSerial.print("IF: Added ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.println();
    // record in our local list
    {
        std::array<uint8_t,6> arr;
        memcpy(arr.data(), mac_addr, 6);
        _espNowPeers.push_back(arr);
    }
    return true;
}

bool InterfaceManager::removeEspNowPeer(const uint8_t* mac_addr) {
     if (!mac_addr || !_espNowInitialized) return false;
     if (!checkEspNowPeer(mac_addr)) return false; // Not found

     esp_err_t del_result = esp_now_del_peer(mac_addr);
     if (del_result != ESP_OK) {
         DebugSerial.print("! WARN: Failed to delete ESP-NOW peer "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(esp_err_to_name(del_result));
         return false;
     }
     DebugSerial.print("IF: Removed ESP-NOW peer: "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.println();
     // remove from our vector as well
     std::array<uint8_t,6> arr;
     memcpy(arr.data(), mac_addr, 6);
     auto it = std::find(_espNowPeers.begin(), _espNowPeers.end(), arr);
     if (it != _espNowPeers.end()) _espNowPeers.erase(it);
     return true;
}

bool InterfaceManager::checkEspNowPeer(const uint8_t* mac_addr) {
    if (!mac_addr || !_espNowInitialized) return false;
    // esp_now_is_peer_exist() is deprecated/removed in later IDF versions.
    // Use esp_now_get_peer() and check result.
    esp_now_peer_info_t peer_info;
    return (esp_now_get_peer(mac_addr, &peer_info) == ESP_OK);
    // return esp_now_is_peer_exist(mac_addr); // Older IDF versions
}

bool InterfaceManager::parseEspNowFragment(const uint8_t *data, int len, uint16_t &messageId, uint8_t &chunkIndex, uint8_t &totalChunks, bool &hasCrc, uint32_t &expectedCrc, const uint8_t* &chunkData, size_t &chunkLen) const {
    if (!data || len <= 0) return false;
    if (len < static_cast<int>(ESPNOW_FRAG_HEADER_LEN)) return false;
    if (data[0] != ESPNOW_FRAG_MAGIC0 || data[1] != ESPNOW_FRAG_MAGIC1 || data[2] != ESPNOW_FRAG_VERSION) return false;

    hasCrc = (data[3] & 0x01) != 0;
    messageId = (static_cast<uint16_t>(data[4]) << 8) | static_cast<uint16_t>(data[5]);
    chunkIndex = data[6];
    totalChunks = data[7];
    if (totalChunks == 0 || chunkIndex >= totalChunks) return false;

    expectedCrc = (static_cast<uint32_t>(data[8]) << 24)
                | (static_cast<uint32_t>(data[9]) << 16)
                | (static_cast<uint32_t>(data[10]) << 8)
                | static_cast<uint32_t>(data[11]);

    chunkData = data + ESPNOW_FRAG_HEADER_LEN;
    chunkLen = static_cast<size_t>(len) - ESPNOW_FRAG_HEADER_LEN;
    return true;
}

InterfaceManager::EspNowRxAssembly* InterfaceManager::getEspNowAssemblySlot(const uint8_t *senderMac, uint16_t messageId, uint8_t totalChunks) {
    if (!senderMac || totalChunks == 0) return nullptr;

    for (auto &slot : _espNowRxAssemblies) {
        if (slot.active && slot.messageId == messageId && slot.totalChunks == totalChunks && memcmp(slot.senderMac.data(), senderMac, 6) == 0) {
            return &slot;
        }
    }

    for (auto &slot : _espNowRxAssemblies) {
        if (!slot.active) {
            slot.active = true;
            memcpy(slot.senderMac.data(), senderMac, 6);
            slot.messageId = messageId;
            slot.totalChunks = totalChunks;
            slot.hasCrc = false;
            slot.expectedCrc = 0;
            slot.lastUpdateMs = millis();
            slot.chunks.assign(totalChunks, std::vector<uint8_t>());
            slot.receivedChunk.assign(totalChunks, 0);
            slot.receivedCount = 0;
            return &slot;
        }
    }

    if (_espNowRxAssemblies.size() < ESPNOW_MAX_RX_ASSEMBLIES) {
        EspNowRxAssembly slot;
        slot.active = true;
        memcpy(slot.senderMac.data(), senderMac, 6);
        slot.messageId = messageId;
        slot.totalChunks = totalChunks;
        slot.hasCrc = false;
        slot.expectedCrc = 0;
        slot.lastUpdateMs = millis();
        slot.chunks.assign(totalChunks, std::vector<uint8_t>());
        slot.receivedChunk.assign(totalChunks, 0);
        _espNowRxAssemblies.push_back(slot);
        return &_espNowRxAssemblies.back();
    }

    size_t oldestIndex = 0;
    unsigned long oldestTime = _espNowRxAssemblies[0].lastUpdateMs;
    for (size_t i = 1; i < _espNowRxAssemblies.size(); ++i) {
        if (_espNowRxAssemblies[i].lastUpdateMs < oldestTime) {
            oldestTime = _espNowRxAssemblies[i].lastUpdateMs;
            oldestIndex = i;
        }
    }

    auto &slot = _espNowRxAssemblies[oldestIndex];
    slot.active = true;
    memcpy(slot.senderMac.data(), senderMac, 6);
    slot.messageId = messageId;
    slot.totalChunks = totalChunks;
    slot.hasCrc = false;
    slot.expectedCrc = 0;
    slot.lastUpdateMs = millis();
    slot.chunks.assign(totalChunks, std::vector<uint8_t>());
    slot.receivedChunk.assign(totalChunks, 0);
    slot.receivedCount = 0;
    return &slot;
}

void InterfaceManager::handleEspNowPayload(const uint8_t *senderMac, const uint8_t *incomingData, int len) {
    if (!_packetReceiver || !senderMac || !incomingData || len <= 0) return;

    uint16_t messageId = 0;
    uint8_t chunkIndex = 0;
    uint8_t totalChunks = 0;
    bool hasCrc = false;
    uint32_t expectedCrc = 0;
    const uint8_t *chunkData = nullptr;
    size_t chunkLen = 0;

    if (!parseEspNowFragment(incomingData, len, messageId, chunkIndex, totalChunks, hasCrc, expectedCrc, chunkData, chunkLen)) {
        if (len <= static_cast<int>(MAX_PACKET_SIZE)) {
            recordInterfaceRx(InterfaceType::ESP_NOW, static_cast<size_t>(len));
            _packetReceiver(incomingData, static_cast<size_t>(len), InterfaceType::ESP_NOW, senderMac, IPAddress(), 0);
        } else {
            DebugSerial.print("! WARN: Oversized raw ESP-NOW payload (");
            DebugSerial.print(len);
            DebugSerial.println(") discarded.");
        }
        return;
    }

    if (totalChunks == 1) {
        if (chunkLen <= MAX_PACKET_SIZE) {
            recordInterfaceRx(InterfaceType::ESP_NOW, chunkLen);
            _packetReceiver(chunkData, chunkLen, InterfaceType::ESP_NOW, senderMac, IPAddress(), 0);
        }
        return;
    }

    EspNowRxAssembly *slot = getEspNowAssemblySlot(senderMac, messageId, totalChunks);
    if (!slot) return;
    if (hasCrc) {
        if (slot->receivedCount == 0) {
            slot->hasCrc = true;
            slot->expectedCrc = expectedCrc;
        } else if (!slot->hasCrc || slot->expectedCrc != expectedCrc) {
            slot->active = false;
            slot->chunks.clear();
            slot->receivedChunk.clear();
            slot->receivedCount = 0;
            DebugSerial.println("! WARN: ESP-NOW fragment CRC metadata mismatch, dropped frame");
            return;
        }
    }

    if (chunkIndex >= slot->receivedChunk.size()) return;
    if (!slot->receivedChunk[chunkIndex]) {
        slot->receivedChunk[chunkIndex] = 1;
        slot->receivedCount++;
        size_t currentLen = 0;
        for (const auto &chunk : slot->chunks) {
            currentLen += chunk.size();
        }
        if (currentLen + chunkLen > MAX_PACKET_SIZE) {
            slot->active = false;
            slot->chunks.clear();
            slot->receivedChunk.clear();
            slot->receivedCount = 0;
            DebugSerial.println("! WARN: ESP-NOW reassembly exceeded MAX_PACKET_SIZE, dropped frame");
            return;
        }
        slot->chunks[chunkIndex].assign(chunkData, chunkData + chunkLen);
    }
    slot->lastUpdateMs = millis();

    if (slot->receivedCount == slot->totalChunks) {
        std::vector<uint8_t> assembled;
        assembled.reserve(MAX_PACKET_SIZE);
        for (const auto &chunk : slot->chunks) {
            assembled.insert(assembled.end(), chunk.begin(), chunk.end());
        }

        if (slot->hasCrc) {
            uint32_t computed = espnow_crc32(assembled.data(), assembled.size());
            if (computed != slot->expectedCrc) {
                DebugSerial.println("! WARN: ESP-NOW reassembly CRC32 mismatch, dropped frame");
                slot->active = false;
                slot->chunks.clear();
                slot->receivedChunk.clear();
                slot->receivedCount = 0;
                slot->hasCrc = false;
                slot->expectedCrc = 0;
                return;
            }
        }

        recordInterfaceRx(InterfaceType::ESP_NOW, assembled.size());
        _packetReceiver(assembled.data(), assembled.size(), InterfaceType::ESP_NOW, senderMac, IPAddress(), 0);
        slot->active = false;
        slot->chunks.clear();
        slot->receivedChunk.clear();
        slot->receivedCount = 0;
        slot->hasCrc = false;
        slot->expectedCrc = 0;
    }
}

void InterfaceManager::cleanupExpiredEspNowAssemblies() {
    const unsigned long now = millis();
    for (auto &slot : _espNowRxAssemblies) {
        if (slot.active && (now - slot.lastUpdateMs) > ESPNOW_REASSEMBLY_TIMEOUT_MS) {
            slot.active = false;
            slot.chunks.clear();
            slot.receivedChunk.clear();
            slot.receivedCount = 0;
            slot.hasCrc = false;
            slot.expectedCrc = 0;
        }
    }
}


// --- Debug Helpers ---
void InterfaceManager::printEspNowPeers() {
    DebugSerial.println("IF: ESP-NOW peer list:");
    if (_espNowPeers.empty()) {
        DebugSerial.println("  (none)");
        return;
    }
    for (const auto &addr : _espNowPeers) {
        DebugSerial.print("  "); Utils::printBytes(addr.data(), 6, DebugSerial); DebugSerial.println();
    }
}

// --- Static Callbacks ---
// ESP-IDF v5 changed the callback signature to esp_now_recv_info_t.
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void InterfaceManager::staticEspNowRecvCallback(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    const uint8_t* mac_addr = (recv_info != nullptr) ? recv_info->src_addr : nullptr;
#else
void InterfaceManager::staticEspNowRecvCallback(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
    if (_instance && _instance->_packetReceiver && mac_addr && incomingData && len > 0) {
         _instance->espNowRxCount++;
         DebugSerial.print("[ESP-NOW RX] "); DebugSerial.print(len); DebugSerial.print("B from ");
         Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.println();
         _instance->handleEspNowPayload(mac_addr, incomingData, len);
    }
}

/* Optional Static Send Callback
void InterfaceManager::staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (_instance && mac_addr) {
        // Could notify routing table or link manager about send status
        // DebugSerial.print("IF: ESP-NOW Send Status to MAC "); Utils::printBytes(mac_addr, 6, DebugSerial); DebugSerial.print(": "); DebugSerial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    }
}
*/

void InterfaceManager::staticEspNowSendCallback(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (_instance) {
        if (status == ESP_NOW_SEND_SUCCESS) _instance->espNowTxOk++;
        else _instance->espNowTxFail++;
    }
}

void InterfaceManager::sendEspNowDiagKiss() {
    // Emit a KISS frame with cmd=0x06 containing ESP-NOW diagnostic text
    // Format: human-readable ASCII so host monitoring scripts can parse it
    uint8_t ch = 0;
    wifi_second_chan_t sec;
    esp_wifi_get_channel(&ch, &sec);
    int8_t txPwr = 0;
    esp_wifi_get_max_tx_power(&txPwr);

    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "ESPNOW ch=%u init=%d tx_ok=%lu tx_fail=%lu rx=%lu peers=%u pwr=%d mac=%s",
        ch, _espNowInitialized ? 1 : 0,
        (unsigned long)espNowTxOk, (unsigned long)espNowTxFail,
        (unsigned long)espNowRxCount,
        (unsigned)_espNowPeers.size(), (int)txPwr,
        WiFi.macAddress().c_str());
    if (n <= 0) return;

    // Build KISS frame: FEND + cmd(0x06) + payload + FEND
    std::vector<uint8_t> frame;
    frame.reserve(n + 10);
    frame.push_back(KISS_FEND);
    frame.push_back(0x06); // KISS SetHardware / diagnostic
    for (int i = 0; i < n; ++i) {
        uint8_t b = static_cast<uint8_t>(buf[i]);
        if (b == KISS_FEND) { frame.push_back(KISS_FESC); frame.push_back(KISS_TFEND); }
        else if (b == KISS_FESC) { frame.push_back(KISS_FESC); frame.push_back(KISS_TFESC); }
        else frame.push_back(b);
    }
    frame.push_back(KISS_FEND);

#if defined(KISS_OVER_USB)
    size_t written = KissSerial.write(frame.data(), frame.size());
    if (written == 0) {
        return;
    }
#else
    KissSerial.write(frame.data(), frame.size());
#endif
}

#ifdef LORA_ENABLED
// --- LoRa Implementation ---
void InterfaceManager::setupLoRa() {
    DebugSerial.println("IF: Initializing LoRa...");
    
    // Initialize SPI for LoRa
    // Use VSPI (SPI3) for ESP32, HSPI for other variants
    #if defined(CONFIG_IDF_TARGET_ESP32)
        SPIClass* spi = new SPIClass(VSPI);
    #else
        SPIClass* spi = new SPIClass(HSPI);
    #endif
    
    spi->begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI, LORA_CS_PIN);
    
    // Create LoRa module instance with SPI settings
    Module* loraModule = new Module(LORA_CS_PIN, LORA_DIO0_PIN, LORA_RST_PIN, RADIOLIB_NC, *spi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
    
    // Create SX1278 instance
    _lora = new SX1278(loraModule);
    
    // Initialize LoRa with configuration
    int state = _lora->begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD, LORA_OUTPUT_POWER, LORA_PREAMBLE_LENGTH, LORA_GAIN);
    
    if (state == RADIOLIB_ERR_NONE) {
        // Put radio into continuous RX mode so processLoRaInput() can receive
        int rxState = _lora->startReceive();
        if (rxState != RADIOLIB_ERR_NONE) {
            DebugSerial.print("! WARN: LoRa startReceive failed: "); DebugSerial.println(rxState);
        }
        _loraInitialized = true;
        DebugSerial.println("IF: LoRa initialized successfully.");
        DebugSerial.print("IF: Frequency: "); DebugSerial.print(LORA_FREQUENCY); DebugSerial.println(" MHz");
        DebugSerial.print("IF: Bandwidth: "); DebugSerial.print(LORA_BANDWIDTH); DebugSerial.println(" kHz");
        DebugSerial.print("IF: Spreading Factor: "); DebugSerial.println(LORA_SPREADING_FACTOR);
    } else {
        _loraInitialized = false;
        DebugSerial.print("! ERROR: LoRa initialization failed with code: ");
        DebugSerial.println(state);
        delete _lora;
        _lora = nullptr;
    }
}

void InterfaceManager::processLoRaInput() {
    if (!_loraInitialized || !_lora) return;
    
    // Check if data is available
    if (_lora->available()) {
        // Determine packet size (LoRa can receive variable length packets)
        size_t packetSize = _lora->getPacketLength();
        if (packetSize == 0 || packetSize > MAX_PACKET_SIZE) {
            DebugSerial.print("! WARN: Invalid LoRa packet size: ");
            DebugSerial.println(packetSize);
            _lora->clearIrqFlags(_lora->getIrqFlags());
            return;
        }
        
        // Allocate buffer for received packet
        std::unique_ptr<uint8_t[]> loraBuffer(new (std::nothrow) uint8_t[packetSize]);
        if (!loraBuffer) {
            DebugSerial.println("! ERROR: Failed to allocate LoRa receive buffer!");
            _lora->clearIrqFlags(_lora->getIrqFlags());
            return;
        }
        
        // Read packet data
        int state = _lora->readData(loraBuffer.get(), packetSize);
        if (state == RADIOLIB_ERR_NONE) {
            // Pass received packet to packet receiver callback
            // LoRa doesn't have MAC addresses, so use nullptr
            if (_packetReceiver) {
                recordInterfaceRx(InterfaceType::LORA, packetSize);
                _packetReceiver(loraBuffer.get(), packetSize, InterfaceType::LORA, nullptr, IPAddress(), 0);
            }
        } else {
            DebugSerial.print("! WARN: LoRa read failed with code: ");
            DebugSerial.println(state);
        }
        
        // Clear IRQ flags to prepare for next packet
        _lora->clearIrqFlags(_lora->getIrqFlags());
    }
}

void InterfaceManager::sendPacketViaLoRa(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    if (!_loraInitialized || !_lora || !packetBuffer || packetLen == 0) return;
    
    // LoRa is broadcast by nature, so destinationAddr is not used here.
    
    // Send packet
    int state = _lora->transmit(packetBuffer, packetLen);
    if (state == RADIOLIB_ERR_NONE) {
        // Success - packet sent
        recordInterfaceTx(InterfaceType::LORA, packetLen);
        // DebugSerial.println("IF: LoRa packet sent successfully.");
    } else {
        DebugSerial.print("! ERROR: LoRa transmit failed with code: ");
        DebugSerial.println(state);
    }
    // Re-enter RX mode after transmit (transmit leaves radio in standby)
    _lora->startReceive();
}
#endif

#ifdef HAM_MODEM_ENABLED
// --- HAM Modem Implementation ---
void InterfaceManager::setupHAMModem() {
    DebugSerial.println("IF: Initializing HAM Modem...");
    
    // Initialize serial port for HAM modem (typically connected to TNC)
    HAM_MODEM_SERIAL.begin(HAM_MODEM_BAUD, SERIAL_8N1, HAM_MODEM_RX_PIN, HAM_MODEM_TX_PIN);

#ifdef AUDIO_MODEM_ENABLED
    if (_audioModem == nullptr) {
        _audioModem = new AudioModem(AudioModem::ModemType::BELL_202);
        if (_audioModem && !_audioModem->begin(HAM_MODEM_RX_PIN, HAM_MODEM_TX_PIN, AUDIO_MODEM_SAMPLE_RATE)) {
            DebugSerial.println("! WARN: Audio modem initialization failed.");
        } else {
            DebugSerial.println("IF: Audio modem initialized.");
        }
    }
#endif

#ifdef WINLINK_ENABLED
    if (_winlink == nullptr) {
        _winlink = new Winlink();
        if (!_winlink->begin(APRS_CALLSIGN, WINLINK_PASSWORD)) {
            DebugSerial.println("! WARN: Winlink initialization failed.");
        } else {
            _winlink->setRawSender(InterfaceManager::winlinkSendRaw, this);
            DebugSerial.println("IF: Winlink initialized.");
        }
    }
#endif

#if defined(AUDIO_MODEM_ENABLED)
    // Spawn audio capture task if not running
    if (_audioCaptureTaskHandle == nullptr && _audioModem) {
        BaseType_t r = xTaskCreatePinnedToCore(
            audioCaptureTask,
            "audio_cap",
            4096,
            this,
            1,
            &_audioCaptureTaskHandle,
            0);
        if (r != pdPASS) {
            DebugSerial.println("! WARN: Failed to start audio capture task");
            _audioCaptureTaskHandle = nullptr;
        } else {
            DebugSerial.println("IF: Audio capture task started.");
        }
    }
#endif
    
    // Most HAM TNCs use KISS protocol, which we already support
    // The KISS processor will handle incoming packets
    _hamModemInitialized = true;
    
    DebugSerial.println("IF: HAM Modem initialized (KISS protocol).");
    DebugSerial.print("IF: Baud rate: "); DebugSerial.println(HAM_MODEM_BAUD);
    DebugSerial.print("IF: Callsign: "); DebugSerial.println(APRS_CALLSIGN);
}

void InterfaceManager::processHAMModemInput() {
    if (!_hamModemInitialized) return;
    
    // Process incoming bytes from HAM modem via KISS
    while (HAM_MODEM_SERIAL.available()) {
        _hamModemKissProcessor.decodeByte(HAM_MODEM_SERIAL.read(), InterfaceType::HAM_MODEM);
    }
}

#if defined(HAM_MODEM_ENABLED) && defined(AUDIO_MODEM_ENABLED)
bool InterfaceManager::pollAX25FromAudioModem() {
    if (!_audioModem) return false;
    std::vector<uint8_t> frame;
    bool got = false;
    // Drain all available frames this loop
    while (_audioModem->receive(frame)) {
        got = true;
        // Deliver as if received over HAM modem (AX.25 over KISS)
        if (!frame.empty() && _packetReceiver) {
            // Interpret as raw AX.25 frame; wrap in KISS data frame for consistency
            recordInterfaceRx(InterfaceType::HAM_MODEM, frame.size());
            _packetReceiver(frame.data(), frame.size(), InterfaceType::HAM_MODEM, nullptr, IPAddress(), 0);
        }
    }
    return got;
}
#endif

#if defined(HAM_MODEM_ENABLED) && defined(AUDIO_MODEM_ENABLED)
// Static FreeRTOS task entry
void InterfaceManager::audioCaptureTask(void* arg) {
    InterfaceManager* self = static_cast<InterfaceManager*>(arg);
    if (self) self->audioCaptureLoop();
    vTaskDelete(nullptr);
}

void InterfaceManager::audioCaptureLoop() {
    const uint32_t samplePeriodUs = 1000000UL / AUDIO_MODEM_SAMPLE_RATE;
    while (true) {
        // Read ADC sample
        int raw = analogRead(AUDIO_MODEM_RX_PIN); // 0-4095
        int16_t centered = (int16_t)((raw - 2048) << 4); // center to signed 16-bit-ish
        if (_audioModem) {
            _audioModem->processAudioSample(centered);
        }
        delayMicroseconds(samplePeriodUs);
    }
}
#endif

void InterfaceManager::sendPacketViaHAMModem(const uint8_t *packetBuffer, size_t packetLen) {
    if (!_hamModemInitialized || !packetBuffer || packetLen == 0) return;
    
    // Encode packet with KISS framing
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(packetBuffer, packetLen, kissEncoded);
    
    // Send via HAM modem serial port
    size_t sent = HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
    if (sent != kissEncoded.size()) {
        DebugSerial.println("! WARN: HAM Modem write incomplete");
    }
    if (sent > 0) {
        recordInterfaceTx(InterfaceType::HAM_MODEM, packetLen);
    }
}

// Helper: build AX.25 UI frame for APRS (dest defaults to "APRS-0")
static bool buildAX25UIFrame(const String& sourceCall, uint8_t sourceSsid,
                             const String& destCall, uint8_t destSsid,
                             const String& info, std::vector<uint8_t>& out)
{
    AX25::Frame frame;
    frame.source = AX25::Address(sourceCall.c_str(), sourceSsid);
    frame.destination = AX25::Address(destCall.c_str(), destSsid);
    frame.control = AX25::ControlType::U_UI;
    frame.pid = 0xF0; // No layer 3 protocol
    frame.info.assign(info.begin(), info.end());
    return AX25::encodeFrame(frame, out);
}

#ifdef WINLINK_ENABLED
bool InterfaceManager::winlinkSendRaw(const uint8_t* data, size_t len, void* context) {
    InterfaceManager* self = static_cast<InterfaceManager*>(context);
    if (!self || !self->_hamModemInitialized || !data || len == 0) {
        return false;
    }
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(data, len, kissEncoded);
    size_t sent = HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
    return sent == kissEncoded.size();
}
#endif

static String buildAprsWeatherTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 1000)) {
        char buf[7];
        snprintf(buf, sizeof(buf), "%02d%02d%02d", timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min);
        return String(buf);
    }

    const uint32_t minutes = millis() / 60000UL;
    const uint32_t days = minutes / 1440UL;
    const uint8_t day = (days % 31) + 1;
    const uint8_t hour = (minutes / 60UL) % 24;
    const uint8_t minute = minutes % 60;
    char buf[7];
    snprintf(buf, sizeof(buf), "%02u%02u%02u", day, hour, minute);
    return String(buf);
}

void InterfaceManager::sendAPRSPacket(const char* destination, const char* message) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }

    String info = String(destination) + ":" + String(message);
    std::vector<uint8_t> ax25;
    if (!buildAX25UIFrame(APRS_CALLSIGN, APRS_SSID, "APRS", 0, info, ax25)) {
        DebugSerial.println("! ERROR: Failed to encode AX.25 frame for APRS packet");
        return;
    }
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(ax25.data(), ax25.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSPosition(float lat, float lon, float altitude, const char* comment) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }

    // Format: !DDMM.MMNS/DDDMM.MMEW[comment]
    char latStr[10], lonStr[11];
    int latDeg = abs((int)lat);
    int lonDeg = abs((int)lon);
    float latMin = (abs(lat) - latDeg) * 60.0f;
    float lonMin = (abs(lon) - lonDeg) * 60.0f;

    /* Format minutes without using float-format specifiers (avoid linking float printf)
       APRS wants DDMM.MM (lat) and DDDMM.MM (lon) where MM.MM has two decimals. */
    int latMin100 = (int)(latMin * 100.0f + 0.5f);
    int latMinWhole = latMin100 / 100;
    int latMinFrac = latMin100 % 100;
    snprintf(latStr, sizeof(latStr), "%02d%02d.%02d", latDeg, latMinWhole, latMinFrac);

    int lonMin100 = (int)(lonMin * 100.0f + 0.5f);
    int lonMinWhole = lonMin100 / 100;
    int lonMinFrac = lonMin100 % 100;
    snprintf(lonStr, sizeof(lonStr), "%03d%02d.%02d", lonDeg, lonMinWhole, lonMinFrac);

    String info = "!";               // Position report
    info += String(latStr);
    info += (lat >= 0) ? "N" : "S";
    info += "/";                       // Symbol table (primary table)
    info += String(lonStr);
    info += (lon >= 0) ? "E" : "W";
    info += String(APRS_SYMBOL);       // Symbol character
    if (altitude > 0) {
        info += "/A=";
        info += String((int)(altitude * 3.28084)); // meters to feet
    }
    if (comment && strlen(comment) > 0) {
        info += String(comment);
    }

    std::vector<uint8_t> ax25;
    if (!buildAX25UIFrame(APRS_CALLSIGN, APRS_SSID, "APRS", 0, info, ax25)) {
        DebugSerial.println("! ERROR: Failed to encode AX.25 frame for APRS position");
        return;
    }
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(ax25.data(), ax25.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSWeather(float temp, float humidity, float pressure, const char* comment) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }

    // APRS weather format: _DDHHMMcDDDsDDDgDDDtXXXrXXXpXXXPXXXhXXbXXXXX
    String info = "_" + buildAprsWeatherTimestamp();
    info += "c000"; // wind direction (degrees)
    info += "s000"; // wind speed (mph)
    info += "g000"; // gust speed (mph)

    int tempF = (int)(temp * 9.0 / 5.0 + 32.0);
    char tempStr[5]; snprintf(tempStr, sizeof(tempStr), "t%03d", abs(tempF));
    if (tempF < 0) tempStr[1] = '-'; // negative temps: t-XX
    info += String(tempStr);

    info += "r000"; // rain 1h (hundredths of inch)
    info += "p000"; // rain 24h (hundredths of inch)
    info += "P000"; // rain since midnight (hundredths of inch)

    char humStr[4]; snprintf(humStr, sizeof(humStr), "h%02d", (int)humidity);
    info += String(humStr);

    char pressStr[7]; snprintf(pressStr, sizeof(pressStr), "b%05d", (int)(pressure * 10));
    info += String(pressStr);

    if (comment && strlen(comment) > 0) {
        info += String(comment);
    }

    std::vector<uint8_t> ax25;
    if (!buildAX25UIFrame(APRS_CALLSIGN, APRS_SSID, "APRS", 0, info, ax25)) {
        DebugSerial.println("! ERROR: Failed to encode AX.25 frame for APRS weather");
        return;
    }
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(ax25.data(), ax25.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}

void InterfaceManager::sendAPRSMessage(const char* addressee, const char* message) {
    if (!_hamModemInitialized) {
        DebugSerial.println("! ERROR: HAM Modem not initialized for APRS");
        return;
    }

    char addr[10] = {0};
    strncpy(addr, addressee, 9);
    for (int i = strlen(addr); i < 9; i++) {
        addr[i] = ' ';
    }

    String info = ":";
    info += String(addr);
    info += ":";
    info += String(message);

    std::vector<uint8_t> ax25;
    if (!buildAX25UIFrame(APRS_CALLSIGN, APRS_SSID, "APRS", 0, info, ax25)) {
        DebugSerial.println("! ERROR: Failed to encode AX.25 frame for APRS message");
        return;
    }
    std::vector<uint8_t> kissEncoded;
    KISSProcessor::encode(ax25.data(), ax25.size(), kissEncoded);
    HAM_MODEM_SERIAL.write(kissEncoded.data(), kissEncoded.size());
}
#endif

#ifdef IPFS_ENABLED
// --- IPFS Implementation (Lightweight Client) ---
void InterfaceManager::setupIPFS() {
    DebugSerial.println("IF: Initializing IPFS client...");
    
    // IPFS client is lightweight - just needs WiFi connection
    if (WiFi.status() == WL_CONNECTED) {
        _ipfsInitialized = true;
        DebugSerial.print("IF: IPFS Gateway: ");
        DebugSerial.println(IPFS_GATEWAY_URL);
        DebugSerial.println("IF: IPFS client ready (gateway mode).");
    } else {
        _ipfsInitialized = false;
        DebugSerial.println("! WARN: IPFS requires WiFi connection. Disabled.");
    }
}

bool InterfaceManager::fetchIPFSContent(const char* ipfsHash, std::vector<uint8_t>& output) {
    if (!_ipfsInitialized || WiFi.status() != WL_CONNECTED) {
        DebugSerial.println("! ERROR: IPFS not available (WiFi not connected)");
        return false;
    }
    
    if (!ipfsHash || strlen(ipfsHash) == 0) {
        DebugSerial.println("! ERROR: Invalid IPFS hash");
        return false;
    }

    // Validate IPFS hash: must be alphanumeric (CIDv0 base58 or CIDv1 base32)
    // Reject hashes containing path traversal, query strings, or other injection chars
    for (const char* p = ipfsHash; *p; ++p) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            DebugSerial.println("! ERROR: IPFS hash contains invalid characters");
            return false;
        }
    }
    
    // Build URL: gateway + hash
    String url = String(IPFS_GATEWAY_URL) + String(ipfsHash);
    
    DebugSerial.print("IF: Fetching IPFS content: ");
    DebugSerial.println(url);
    
    _httpClient.begin(url);
    _httpClient.setTimeout(IPFS_TIMEOUT_MS);
    
    int httpCode = _httpClient.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = _httpClient.getSize();
        
        if (contentLength > 0 && contentLength <= IPFS_MAX_CONTENT_SIZE) {
            // Read content
            WiFiClient* stream = _httpClient.getStreamPtr();
            output.resize(contentLength);
            
            size_t bytesRead = 0;
            while (bytesRead < (size_t)contentLength && stream->available()) {
                bytesRead += stream->readBytes(output.data() + bytesRead, contentLength - bytesRead);
            }
            
            _httpClient.end();
            recordInterfaceRx(InterfaceType::IPFS, bytesRead);
            DebugSerial.print("IF: IPFS content fetched: ");
            DebugSerial.print(bytesRead);
            DebugSerial.println(" bytes");
            return true;
        } else {
            DebugSerial.print("! ERROR: IPFS content too large: ");
            DebugSerial.println(contentLength);
            _httpClient.end();
            return false;
        }
    } else {
        DebugSerial.print("! ERROR: IPFS fetch failed, HTTP code: ");
        DebugSerial.println(httpCode);
        _httpClient.end();
        return false;
    }
}

bool InterfaceManager::publishToIPFS(const uint8_t* data, size_t len, String& ipfsHash) {
    if (!_ipfsInitialized || WiFi.status() != WL_CONNECTED) {
        DebugSerial.println("! ERROR: IPFS not available (WiFi not connected)");
        return false;
    }

#if IPFS_LOCAL_NODE_ENABLED
    // Use local IPFS node API with proper multipart/form-data encoding
    String url = String(IPFS_LOCAL_NODE_URL) + "/api/v0/add";

    DebugSerial.print("IF: Publishing to IPFS via local node: ");
    DebugSerial.println(url);

    // Boundary for multipart form
    const String boundary = "----ESP32IPFSBoundary";
    const String contentType = "multipart/form-data; boundary=" + boundary;

    // Construct multipart body: --boundary\r\n headers \r\n\r\n <data> \r\n--boundary--\r\n
    String prefix = "--" + boundary + "\r\n";
    prefix += "Content-Disposition: form-data; name=\"file\"; filename=\"data.bin\"\r\n";
    prefix += "Content-Type: application/octet-stream\r\n\r\n";
    String suffix = "\r\n--" + boundary + "--\r\n";

    std::vector<uint8_t> body;
    body.reserve(prefix.length() + len + suffix.length());
    body.insert(body.end(), prefix.begin(), prefix.end());
    body.insert(body.end(), data, data + len);
    body.insert(body.end(), suffix.begin(), suffix.end());

    _httpClient.begin(url);
    _httpClient.setTimeout(IPFS_PUBLISH_TIMEOUT_MS);
    _httpClient.addHeader("Content-Type", contentType);

    int httpCode = _httpClient.POST(body.data(), body.size());

    if (httpCode == HTTP_CODE_OK) {
        String response = _httpClient.getString();

        // Parse JSON response to extract hash
        // Response format: {"Name":"data.bin","Hash":"Qm...","Size":"123"}
        int hashStart = response.indexOf("\"Hash\":\"");
        if (hashStart >= 0) {
            hashStart += 8;
            int hashEnd = response.indexOf("\"", hashStart);
            if (hashEnd > hashStart) {
                ipfsHash = response.substring(hashStart, hashEnd);
                _httpClient.end();
                recordInterfaceTx(InterfaceType::IPFS, len);
                DebugSerial.print("IF: IPFS content published, hash: ");
                DebugSerial.println(ipfsHash);
                return true;
            }
        }
        _httpClient.end();
        DebugSerial.println("! ERROR: Failed to parse IPFS response");
        return false;
    } else {
        DebugSerial.print("! ERROR: IPFS publish failed, HTTP code: ");
        DebugSerial.println(httpCode);
        _httpClient.end();
        return false;
    }
#else
    // No local node - suggest alternatives
    DebugSerial.println("! WARN: IPFS local node not enabled.");
    DebugSerial.println("! INFO: Set IPFS_LOCAL_NODE_ENABLED to 1 in Config.h");
    DebugSerial.println("! INFO: Or use a pinning service (Pinata, Infura, etc.)");
    return false;
#endif
}

void InterfaceManager::sendPacketViaIPFS(const uint8_t *packetBuffer, size_t packetLen, const uint8_t *destinationAddr) {
    if (!_ipfsInitialized) {
        DebugSerial.println("! WARN: IPFS not initialized, cannot send packet");
        return;
    }

    String ipfsHash;
    if (!publishToIPFS(packetBuffer, packetLen, ipfsHash)) {
        DebugSerial.println("! ERROR: Failed to publish packet to IPFS");
        return;
    }
    if (ipfsHash.length() == 0) {
        DebugSerial.println("! ERROR: IPFS hash is empty");
        return;
    }

    RnsPacketInfo info;
    bool parsed = ReticulumPacket::deserialize(packetBuffer, packetLen, info);

    uint8_t destHash[16] = {0};
    if (parsed) {
        memcpy(destHash, info.destination_hash, sizeof(destHash));
    } else if (destinationAddr) {
        memcpy(destHash, destinationAddr, RNS_ADDRESS_SIZE);
    }

    const String reference = String("ipfs:") + ipfsHash;
    std::vector<uint8_t> payload(reference.begin(), reference.end());
    if (payload.size() > RNS_MAX_PAYLOAD) {
        DebugSerial.println("! ERROR: IPFS reference payload too large");
        return;
    }

    uint8_t refBuffer[MAX_PACKET_SIZE];
    size_t refLen = 0;
    const uint8_t packetType = parsed ? info.packet_type : RNS_PACKET_DATA;
    const uint8_t destType = parsed ? info.destination_type : RNS_DEST_PLAIN;
    const uint8_t propagationType = parsed ? info.propagation_type : RNS_PROPAGATION_BROADCAST;
    const uint8_t context = parsed ? info.context : RNS_CONTEXT_NONE;
    const uint8_t hops = parsed ? info.hops : 0;

    if (!ReticulumPacket::serialize(refBuffer, refLen, destHash, packetType, destType, propagationType, context, hops, payload)) {
        DebugSerial.println("! ERROR: Failed to serialize IPFS reference packet");
        return;
    }

    RouteEntry* route = nullptr;
    if (destinationAddr) {
        route = _routingTableRef.findRoute(destinationAddr);
    }

    if (route && route->interface != InterfaceType::IPFS) {
        sendPacketVia(route->interface, refBuffer, refLen, destinationAddr);
        return;
    }

    if (destinationAddr == nullptr || !route || route->interface == InterfaceType::IPFS) {
        sendPacketViaEspNow(refBuffer, refLen, destinationAddr);
        if (WiFi.status() == WL_CONNECTED) {
            sendPacketViaWiFi(refBuffer, refLen, destinationAddr);
        }
#ifdef LORA_ENABLED
        if (_loraInitialized) {
            sendPacketViaLoRa(refBuffer, refLen, destinationAddr);
        }
#endif
#ifdef HAM_MODEM_ENABLED
        if (_hamModemInitialized) {
            sendPacketViaHAMModem(refBuffer, refLen);
        }
#endif
    }
}
#endif
