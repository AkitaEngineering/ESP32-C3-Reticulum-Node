#include "WebServer.h"
#include "Config.h"
#include "Utils.h"
#include "Log.h"

#if WEBSERVER_ENABLED

#include "ReticulumNode.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <monocypher.h>
#include <optional/monocypher-ed25519.h>

extern ReticulumNode reticulumNode;

// Sanity checks: if WebServer is enabled, ensure JSON config / OTA flags are intentionally set
#if WEBSERVER_ENABLED && !JSON_CONFIG_ENABLED
#warning "WEBSERVER_ENABLED=1 but JSON_CONFIG_ENABLED=0 — runtime JSON config will be disabled"
#endif
#if WEBSERVER_ENABLED && !OTA_ENABLED
#warning "WEBSERVER_ENABLED=1 but OTA_ENABLED=0 — OTA endpoints will be disabled"
#endif

static WiFiServer _server(WEBSERVER_PORT);
static bool _serverStarted = false;
static const char* CONFIG_PATH = "/config.json";
static const char* CONFIG_NEW_PATH = "/config.new";
static const char* CONFIG_BACKUP_PATH = "/config.backup";
static constexpr bool ALLOW_NETWORK_BOOTSTRAP = !PRODUCTION_BUILD;

static bool hexToBin(const String &hex, uint8_t *out, size_t expectedLen) {
    if ((size_t)hex.length() != expectedLen * 2) return false;
    for (size_t i = 0; i < expectedLen; ++i) {
        const char h = hex.charAt(2 * i);
        const char l = hex.charAt(2 * i + 1);
        auto value = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = value(h);
        const int lo = value(l);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parseSemanticVersion(const String &version, int parts[3]) {
    int start = 0;
    for (int i = 0; i < 3; ++i) {
        const int end = (i < 2) ? version.indexOf('.', start) : version.length();
        if (end <= start) return false;
        long value = 0;
        for (int pos = start; pos < end; ++pos) {
            const char c = version.charAt(pos);
            if (c < '0' || c > '9') return false;
            value = value * 10 + (c - '0');
            if (value > 65535) return false;
        }
        parts[i] = static_cast<int>(value);
        start = end + 1;
    }
    return start == version.length() + 1;
}

static bool isNewerFirmwareVersion(const String &candidate) {
    int incoming[3] = {0, 0, 0};
    int current[3] = {0, 0, 0};
    if (!parseSemanticVersion(candidate, incoming) || !parseSemanticVersion(String(FIRMWARE_VERSION), current)) return false;
    for (int i = 0; i < 3; ++i) {
        if (incoming[i] != current[i]) return incoming[i] > current[i];
    }
    return false;
}

static void sendResponse(WiFiClient &client, int code, const char *contentType, const String &body, const String &extraHeaders = String()) {
    const char *statusText = "OK";
    switch (code) {
        case 200: statusText = "OK"; break;
        case 201: statusText = "Created"; break;
        case 400: statusText = "Bad Request"; break;
        case 405: statusText = "Method Not Allowed"; break;
        case 408: statusText = "Request Timeout"; break;
        case 411: statusText = "Length Required"; break;
        case 413: statusText = "Payload Too Large"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 409: statusText = "Conflict"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "OK"; break;
    }

    client.print("HTTP/1.1 ");
    client.print(code);
    client.print(" "); client.print(statusText); client.print("\r\n");
    client.print("Content-Type: "); client.print(contentType); client.print("\r\n");
    client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
    client.print("Cache-Control: no-store\r\n");
    client.print("X-Content-Type-Options: nosniff\r\n");
    if (extraHeaders.length() > 0) {
        client.print(extraHeaders);
    }
    client.print("Connection: close\r\n\r\n");
    client.print(body);
}

static void sendUnauthorized(WiFiClient &client) {
    const String body = "Unauthorized";
    client.print("HTTP/1.1 401 Unauthorized\r\n");
    client.print("WWW-Authenticate: Bearer realm=\"Reticulum\"\r\n");
    client.print("Content-Type: text/plain\r\n");
    client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
    client.print("Cache-Control: no-store\r\n");
    client.print("X-Content-Type-Options: nosniff\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(body);
}

static bool hasSavedConfig() {
#if JSON_CONFIG_ENABLED
    return SPIFFS.exists(CONFIG_PATH);
#else
    return false;
#endif
}

static bool isPlaceholderApiToken(const String &token) {
    return token.length() == 0 || token == "CHANGE_ME_generate_a_strong_token";
}

#if JSON_CONFIG_ENABLED
static String getSavedApiToken() {
    if (!SPIFFS.exists(CONFIG_PATH)) return String();
    File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
    if (!f) return String();
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return String();
    if (!doc.containsKey("api")) return String();
    JsonObject api = doc["api"];
    if (!api.containsKey("token")) return String();
    return api["token"].as<String>();
}
#endif

static bool isBootstrapMode() {
#if WEBSERVER_AUTH_ENABLED
    #if JSON_CONFIG_ENABLED
    return ALLOW_NETWORK_BOOTSTRAP && !hasSavedConfig();
    #else
    return false;
    #endif
#else
    return false;
#endif
}

static bool getRestartRequiredReason(String &reason) {
    if (strcmp(reticulumNode.getRuntimeAppName(), getConfiguredAppName()) != 0) {
        reason = "rns_app_name_changed";
        return true;
    }

    const char* configuredSsid = getConfiguredWiFiSsid();
    if (strlen(configuredSsid) > 0 && (WiFi.status() != WL_CONNECTED || WiFi.SSID() != configuredSsid)) {
        reason = "wifi_config_changed";
        return true;
    }

    reason = "";
    return false;
}

static void appendRouteCandidateCounts(JsonObject routeCounts, RoutingTable &routingTable) {
    routeCounts["serial_port"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::SERIAL_PORT);
    routeCounts["esp_now"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::ESP_NOW);
    routeCounts["wifi_udp"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::WIFI_UDP);
    routeCounts["bluetooth"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::BLUETOOTH);
    routeCounts["lora"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::LORA);
    routeCounts["ham_modem"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::HAM_MODEM);
    routeCounts["ipfs"] = (int)routingTable.getRouteCandidateCountForInterface(InterfaceType::IPFS);
}

static void appendRoutePriorityConfig(JsonObject routePriorities) {
    const RoutePriorityConfig& priorities = getConfiguredRoutePriorities();
    routePriorities["serial_port"] = priorities.serial_port;
    routePriorities["esp_now"] = priorities.esp_now;
    routePriorities["wifi_udp"] = priorities.wifi_udp;
    routePriorities["bluetooth"] = priorities.bluetooth;
    routePriorities["lora"] = priorities.lora;
    routePriorities["ham_modem"] = priorities.ham_modem;
    routePriorities["ipfs"] = priorities.ipfs;
}

static void appendInterfaceHealthObject(JsonObject interfaces, const char* name, const InterfaceHealthSnapshot &snapshot) {
    JsonObject interfaceDoc = interfaces.createNestedObject(name);
    interfaceDoc["supported"] = snapshot.supported;
    interfaceDoc["usable"] = snapshot.usable;
    interfaceDoc["last_rx_uptime_ms"] = snapshot.last_rx_uptime_ms;
    interfaceDoc["last_tx_uptime_ms"] = snapshot.last_tx_uptime_ms;
    interfaceDoc["rx_packets"] = snapshot.rx_packets;
    interfaceDoc["tx_packets"] = snapshot.tx_packets;
    interfaceDoc["rx_bytes"] = snapshot.rx_bytes;
    interfaceDoc["tx_bytes"] = snapshot.tx_bytes;
}

static void appendInterfaceHealth(JsonObject interfaces, InterfaceManager &interfaceManager) {
    appendInterfaceHealthObject(interfaces, "serial_port", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::SERIAL_PORT));
    appendInterfaceHealthObject(interfaces, "esp_now", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::ESP_NOW));
    appendInterfaceHealthObject(interfaces, "wifi_udp", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::WIFI_UDP));
    appendInterfaceHealthObject(interfaces, "bluetooth", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::BLUETOOTH));
    appendInterfaceHealthObject(interfaces, "lora", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::LORA));
    appendInterfaceHealthObject(interfaces, "ham_modem", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::HAM_MODEM));
    appendInterfaceHealthObject(interfaces, "ipfs", interfaceManager.getInterfaceHealthSnapshot(InterfaceType::IPFS));
}

static String bytesToHexString(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[(data[i] >> 4) & 0x0F];
        out += hex[data[i] & 0x0F];
    }
    return out;
}

static const char* interfaceTypeName(InterfaceType interface) {
    switch (interface) {
        case InterfaceType::SERIAL_PORT: return "serial_port";
        case InterfaceType::ESP_NOW: return "esp_now";
        case InterfaceType::WIFI_UDP: return "wifi_udp";
        case InterfaceType::BLUETOOTH: return "bluetooth";
        case InterfaceType::LORA: return "lora";
        case InterfaceType::HAM_MODEM: return "ham_modem";
        case InterfaceType::IPFS: return "ipfs";
        case InterfaceType::LOCAL: return "local";
        default: return "unknown";
    }
}

static void appendRouteDiagnosticCandidate(JsonArray candidates, const RouteDiagnosticCandidate& candidate) {
    JsonObject candidateDoc = candidates.createNestedObject();
    candidateDoc["interface"] = interfaceTypeName(candidate.interface);
    candidateDoc["selected"] = candidate.selected;
    candidateDoc["usable"] = candidate.usable;
    candidateDoc["hops"] = candidate.hops;
    candidateDoc["interface_priority"] = candidate.interface_priority;
    candidateDoc["age_ms"] = candidate.age_ms;
    candidateDoc["last_heard_uptime_ms"] = candidate.last_heard_uptime_ms;
    candidateDoc["next_hop_mac"] = bytesToHexString(candidate.next_hop_mac.data(), candidate.next_hop_mac.size());
    candidateDoc["next_hop_ip"] = candidate.next_hop_ip ? candidate.next_hop_ip.toString() : String("");
    candidateDoc["next_hop_port"] = candidate.next_hop_port;
}

static void appendRouteDiagnostics(JsonArray destinations, const std::vector<RouteDiagnosticGroup>& routeDiagnostics) {
    for (const auto& group : routeDiagnostics) {
        JsonObject destinationDoc = destinations.createNestedObject();
        destinationDoc["destination"] = bytesToHexString(group.destination_addr.data(), group.destination_addr.size());
        destinationDoc["candidate_count"] = static_cast<int>(group.candidates.size());

        const RouteDiagnosticCandidate* selectedCandidate = nullptr;
        for (const auto& candidate : group.candidates) {
            if (candidate.selected) {
                selectedCandidate = &candidate;
                break;
            }
        }

        destinationDoc["selected_interface"] = selectedCandidate ? interfaceTypeName(selectedCandidate->interface) : "";
        if (selectedCandidate) {
            destinationDoc["selected_hops"] = selectedCandidate->hops;
            destinationDoc["selected_priority"] = selectedCandidate->interface_priority;
        }

        JsonArray candidates = destinationDoc.createNestedArray("candidates");
        for (const auto& candidate : group.candidates) {
            appendRouteDiagnosticCandidate(candidates, candidate);
        }
    }
}

static bool checkAuth(const String &authHeader, bool allowBootstrap = false) {
#if WEBSERVER_AUTH_ENABLED
#if JSON_CONFIG_ENABLED
    if (hasSavedConfig() && !validateRuntimeConfigFile(PRODUCTION_BUILD, true)) {
        LOG_WARN("WebServer: saved configuration failed validation - denying request");
        return false;
    }
#endif
    String expected;
#if JSON_CONFIG_ENABLED
    expected = getSavedApiToken();
#endif
    expected.trim();

    if (isPlaceholderApiToken(expected)) {
        if (allowBootstrap && isBootstrapMode()) {
            LOG_WARN("WebServer: No config present - allowing first-time bootstrap access");
            return true;
        }
        LOG_WARN("WebServer: API token missing or placeholder - denying request");
        return false;
    }

    String token = authHeader;
    token.trim();
    if (!token.startsWith("Bearer ")) return false;
    token = token.substring(7);
    token.trim();

    // Constant-time comparison to prevent timing attacks
    if (token.length() != expected.length()) return false;
    volatile uint8_t result = 0;
    for (size_t i = 0; i < token.length(); i++) {
        result |= (uint8_t)(token[i] ^ expected[i]);
    }
    return result == 0;
#else
    (void)authHeader; return true;
#endif
}

static String readLine(WiFiClient &c, unsigned long timeout=1000, size_t maxLength=1024) {
    String s;
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (!c.connected()) break;
        while (c.available()) {
            char ch = c.read();
            if (s.length() >= maxLength) return String();
            s += ch;
            if (s.endsWith("\r\n")) return s;
        }
        delay(1);
    }
    return s;
}

static bool parseContentLength(String value, int &parsed) {
    value.trim();
    if (value.length() == 0) return false;
    uint32_t result = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value.charAt(i);
        if (c < '0' || c > '9') return false;
        result = result * 10u + static_cast<uint32_t>(c - '0');
        if (result > 0x7FFFFFFFu) return false;
    }
    parsed = static_cast<int>(result);
    return true;
}

void processHttpClient(WiFiClient &client) {
    // Read request line
    String req = readLine(client);
    if (req.length() == 0) return;
    // Example: "GET /api/v1/status HTTP/1.1\r\n"
    int firstSpace = req.indexOf(' ');
    int secondSpace = req.indexOf(' ', firstSpace + 1);
    if (firstSpace <= 0 || secondSpace <= firstSpace + 1) {
        sendResponse(client, 400, "text/plain", "Malformed request line");
        return;
    }
    String method = req.substring(0, firstSpace);
    String path = req.substring(firstSpace + 1, secondSpace);
    String protocol = req.substring(secondSpace + 1);
    protocol.trim();
    if ((method != "GET" && method != "POST") || path.length() == 0 || path.charAt(0) != '/' ||
        (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")) {
        sendResponse(client, method != "GET" && method != "POST" ? 405 : 400,
                     "text/plain", "Unsupported or malformed request");
        return;
    }

    // Read headers (case-insensitive comparison)
    int contentLength = 0;
    bool hasContentLength = false;
    bool hasAuthorization = false;
    bool hasSignature = false;
    bool hasFirmwareVersion = false;
    String authHeader;
    String signatureHex;
    String firmwareVersion;
    size_t totalHeaderBytes = 0;
    while (true) {
        String line = readLine(client);
        totalHeaderBytes += line.length();
        if (line.length() == 0 || totalHeaderBytes > 8192) {
            sendResponse(client, 400, "text/plain", "Invalid or oversized headers");
            return;
        }
        if (line.length() <= 2) break; // \r\n
        if (line.charAt(0) == ' ' || line.charAt(0) == '\t' || line.indexOf(':') <= 0) {
            sendResponse(client, 400, "text/plain", "Malformed header");
            return;
        }
        line.trim();
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            int parsedLength = 0;
            if (hasContentLength || !parseContentLength(line.substring(15), parsedLength)) {
                sendResponse(client, 400, "text/plain", "Invalid Content-Length");
                return;
            }
            contentLength = parsedLength;
            hasContentLength = true;
        } else if (lower.startsWith("transfer-encoding:")) {
            sendResponse(client, 400, "text/plain", "Transfer-Encoding is not supported");
            return;
        } else if (lower.startsWith("authorization:")) {
            if (hasAuthorization) { sendResponse(client, 400, "text/plain", "Duplicate Authorization header"); return; }
            authHeader = line.substring(14);
            authHeader.trim();
            hasAuthorization = true;
        } else if (lower.startsWith("x-signature-ed25519:")) {
            if (hasSignature) { sendResponse(client, 400, "text/plain", "Duplicate signature header"); return; }
            signatureHex = line.substring(20);
            signatureHex.trim();
            hasSignature = true;
        } else if (lower.startsWith("x-firmware-version:")) {
            if (hasFirmwareVersion) { sendResponse(client, 400, "text/plain", "Duplicate firmware version header"); return; }
            firmwareVersion = line.substring(19);
            firmwareVersion.trim();
            hasFirmwareVersion = true;
        }
    }

    // Enforce body size limit to prevent memory exhaustion
    const int MAX_BODY_SIZE = 4096;
    bool isOtaUpload = (method == "POST" && path == "/api/v1/ota");
    if (method == "POST" && !hasContentLength) {
        sendResponse(client, 411, "text/plain", "Content-Length required");
        return;
    }
    if (method == "GET" && contentLength != 0) {
        sendResponse(client, 400, "text/plain", "GET requests cannot contain a body");
        return;
    }
    if (!isOtaUpload && contentLength > MAX_BODY_SIZE) {
        sendResponse(client, 413, "text/plain", "Body too large");
        return;
    }

    // For OTA uploads we stream to SPIFFS directly; otherwise read body into memory
    String body;
    if (!isOtaUpload && contentLength > 0) {
        unsigned long start = millis();
        while ((int)body.length() < contentLength && millis() - start < 2000) {
            while (client.available() && (int)body.length() < contentLength) {
                body += (char)client.read();
            }
            delay(1);
        }
        if (static_cast<int>(body.length()) != contentLength) {
            sendResponse(client, 408, "text/plain", "Incomplete request body");
            return;
        }
    }

    // Route handling
    if (method == "GET" && path == "/api/v1/status") {
        if (!checkAuth(authHeader, ALLOW_NETWORK_BOOTSTRAP)) { sendUnauthorized(client); return; }
        reloadRuntimeConfigCache();
        DynamicJsonDocument doc(3072);
        String restartReason;
        bool restartRequired = getRestartRequiredReason(restartReason);
        RoutingTable &routingTable = reticulumNode.getRoutingTable();
        InterfaceManager &interfaceManager = reticulumNode.getInterfaceManager();

        bool wifiConnected = WiFi.status() == WL_CONNECTED;
        doc["node_name"] = getConfiguredNodeName();
        doc["device_id"] = getDefaultDeviceName();
        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["rns_app_name"] = getConfiguredAppName();
        doc["uptime_s"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["active_links"] = (int)reticulumNode.getLinkManager().getActiveLinkCount();
        doc["route_count"] = (int)routingTable.getRouteCount();
        doc["route_candidate_count"] = (int)routingTable.getRouteCandidateCount();
        doc["config_present"] = hasRuntimeConfigFile();
        String configValidationReason;
        const bool configValid = validateRuntimeConfigFile(PRODUCTION_BUILD, true, &configValidationReason);
        doc["config_valid"] = configValid;
        if (!configValid) doc["config_error"] = configValidationReason;
        doc["bootstrap_mode"] = isBootstrapMode();
        doc["wifi_connected"] = wifiConnected;
        doc["wifi_ip"] = wifiConnected ? WiFi.localIP().toString() : String("");
        JsonObject routeCounts = doc.createNestedObject("route_candidates_by_interface");
        appendRouteCandidateCounts(routeCounts, routingTable);
        JsonObject routePriorities = doc.createNestedObject("route_priority_by_interface");
        appendRoutePriorityConfig(routePriorities);
        JsonObject interfaces = doc.createNestedObject("interfaces");
        appendInterfaceHealth(interfaces, interfaceManager);
        doc["restart_required"] = restartRequired;
        if (restartRequired) {
            doc["restart_reason"] = restartReason;
        }
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "GET" && path == "/api/v1/routes") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
        reloadRuntimeConfigCache();
        RoutingTable &routingTable = reticulumNode.getRoutingTable();
        InterfaceManager &interfaceManager = reticulumNode.getInterfaceManager();
        std::vector<RouteDiagnosticGroup> routeDiagnostics = routingTable.getRouteDiagnostics(
            [&interfaceManager](InterfaceType ifType) {
                return interfaceManager.getInterfaceHealthSnapshot(ifType).usable;
            }
        );

        DynamicJsonDocument doc(16384);
        doc["route_count"] = static_cast<int>(routingTable.getRouteCount());
        doc["route_candidate_count"] = static_cast<int>(routingTable.getRouteCandidateCount());
        JsonObject routePriorities = doc.createNestedObject("route_priority_by_interface");
        appendRoutePriorityConfig(routePriorities);
        JsonArray destinations = doc.createNestedArray("destinations");
        appendRouteDiagnostics(destinations, routeDiagnostics);
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "GET" && path == "/api/v1/config") {
        if (!checkAuth(authHeader, ALLOW_NETWORK_BOOTSTRAP)) { sendUnauthorized(client); return; }
        DynamicJsonDocument doc(4096);
        if (SPIFFS.exists(CONFIG_PATH)) {
            File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
            if (!f) { sendResponse(client, 500, "text/plain", "Failed to open config file"); return; }
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (err) { sendResponse(client, 500, "text/plain", "Config parse error"); return; }
            const char* nodeName = doc["node_name"] | "";
            if (strlen(nodeName) == 0) {
                doc["node_name"] = getConfiguredNodeName();
            }
            const char* appName = doc["rns_app_name"] | "";
            if (strlen(appName) == 0) {
                doc["rns_app_name"] = getConfiguredAppName();
            }
            JsonObject wifi = doc["wifi"];
            if (!wifi.isNull()) {
                const char* savedPassword = wifi["password"] | "";
                wifi["password_configured"] = strlen(savedPassword) > 0;
                wifi["password"] = "";
            }
            JsonObject api = doc["api"];
            if (!api.isNull()) {
                const char* savedToken = api["token"] | "";
                api["token_configured"] = !isPlaceholderApiToken(String(savedToken));
                api["token"] = "";
            }
        } else {
            doc["node_name"] = getConfiguredNodeName();
            doc["rns_app_name"] = getConfiguredAppName();
            JsonObject wifi = doc.createNestedObject("wifi");
            wifi["ssid"] = "";
            wifi["password"] = "";
        }
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

    } else if (method == "POST" && path == "/api/v1/config") {
        if (!checkAuth(authHeader, ALLOW_NETWORK_BOOTSTRAP)) { sendUnauthorized(client); return; }
        String validationReason;
        if (!validateRuntimeConfigJson(body, PRODUCTION_BUILD, true, &validationReason)) {
            sendResponse(client, 400, "text/plain", String("Invalid config: ") + validationReason);
            return;
        }
        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendResponse(client, 400, "text/plain", "Invalid JSON"); return; }
#if JSON_CONFIG_ENABLED
        SPIFFS.remove(CONFIG_NEW_PATH);
        File f = SPIFFS.open(CONFIG_NEW_PATH, FILE_WRITE);
        if (!f) { sendResponse(client, 500, "text/plain", "Failed to open config for write"); return; }
        if (serializeJson(doc, f) == 0) { f.close(); SPIFFS.remove(CONFIG_NEW_PATH); sendResponse(client, 500, "text/plain", "Failed to write config"); return; }
        f.flush();
        f.close();
        File verifyFile = SPIFFS.open(CONFIG_NEW_PATH, FILE_READ);
        DynamicJsonDocument verifyDoc(4096);
        const bool verified = verifyFile && deserializeJson(verifyDoc, verifyFile) == DeserializationError::Ok;
        if (verifyFile) verifyFile.close();
        if (!verified) { SPIFFS.remove(CONFIG_NEW_PATH); sendResponse(client, 500, "text/plain", "Config verification failed"); return; }
        SPIFFS.remove(CONFIG_BACKUP_PATH);
        const bool hadConfig = SPIFFS.exists(CONFIG_PATH);
        if (hadConfig && !SPIFFS.rename(CONFIG_PATH, CONFIG_BACKUP_PATH)) {
            SPIFFS.remove(CONFIG_NEW_PATH);
            sendResponse(client, 500, "text/plain", "Config backup failed");
            return;
        }
        if (!SPIFFS.rename(CONFIG_NEW_PATH, CONFIG_PATH)) {
            if (hadConfig) SPIFFS.rename(CONFIG_BACKUP_PATH, CONFIG_PATH);
            SPIFFS.remove(CONFIG_NEW_PATH);
            sendResponse(client, 500, "text/plain", "Config commit failed");
            return;
        }
        SPIFFS.remove(CONFIG_BACKUP_PATH);
#endif
        // Apply WiFi credentials immediately if provided
        if (doc.containsKey("wifi")) {
            JsonObject wifi = doc["wifi"];
            if (wifi.containsKey("ssid") && wifi.containsKey("password")) {
                String ssid = wifi["ssid"].as<String>();
                String pass = wifi["password"].as<String>();
                if (ssid.length() > 0) { DebugSerial.println("WebServer: applying WiFi credentials from config.json"); WiFi.begin(ssid.c_str(), pass.c_str()); }
            }
        }
        reloadRuntimeConfigCache();
        String restartReason;
        bool restartRequired = getRestartRequiredReason(restartReason);
        String extraHeaders = String("X-Restart-Required: ") + (restartRequired ? "true" : "false") + "\r\n";
        if (restartRequired) {
            extraHeaders += "X-Restart-Reason: ";
            extraHeaders += restartReason;
            extraHeaders += "\r\n";
        }
        DynamicJsonDocument responseDoc(256);
        responseDoc["saved"] = true;
        responseDoc["restart_required"] = restartRequired;
        if (restartRequired) responseDoc["restart_reason"] = restartReason;
        String out; serializeJson(responseDoc, out);
        sendResponse(client, 200, "application/json", out, extraHeaders);

    } else if (method == "POST" && path == "/api/v1/config/save") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
#if JSON_CONFIG_ENABLED
        if (SPIFFS.exists(CONFIG_PATH)) sendResponse(client, 200, "text/plain", "saved"); else sendResponse(client, 500, "text/plain", "no config to save");
#else
        sendResponse(client, 404, "text/plain", "JSON config disabled");
#endif

    } else if (method == "POST" && path == "/api/v1/ota") {
        // Signed OTA upload. Stream upload to SPIFFS then verify signature before flashing.
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
#if OTA_ENABLED
        if (contentLength == 0) { sendResponse(client, 400, "text/plain", "Empty body"); return; }
        if (signatureHex.length() == 0) { sendResponse(client, 400, "text/plain", "Missing X-Signature-Ed25519 header"); return; }
        if (!isNewerFirmwareVersion(firmwareVersion)) { sendResponse(client, 409, "text/plain", "Firmware version must be a newer semantic version"); return; }
        const char *tmpPath = "/ota_upload.bin";
        SPIFFS.remove(tmpPath);
        const size_t filesystemReserve = 65536;
        const size_t filesystemAvailable = SPIFFS.totalBytes() > SPIFFS.usedBytes() + filesystemReserve
            ? SPIFFS.totalBytes() - SPIFFS.usedBytes() - filesystemReserve : 0;
        if ((size_t)contentLength > filesystemAvailable) { sendResponse(client, 400, "text/plain", "OTA image exceeds staging capacity"); return; }

        String pubHex;
#if JSON_CONFIG_ENABLED
        DynamicJsonDocument cdoc(4096);
        if (SPIFFS.exists(CONFIG_PATH)) {
            File cf = SPIFFS.open(CONFIG_PATH, FILE_READ);
            if (cf) {
                if (deserializeJson(cdoc, cf) == DeserializationError::Ok) {
                    if (cdoc.containsKey("api") && cdoc["api"].containsKey("public_key")) pubHex = cdoc["api"]["public_key"].as<String>();
                }
                cf.close();
            }
        }
#endif
        if (pubHex.length() == 0) { sendResponse(client, 400, "text/plain", "No public key configured"); return; }

        uint8_t sig[64]; uint8_t pub[32];
        if (!hexToBin(signatureHex, sig, 64)) { sendResponse(client, 400, "text/plain", "Bad signature format (expected 128 hex chars)"); return; }
        if (!hexToBin(pubHex, pub, 32)) { sendResponse(client, 400, "text/plain", "Bad public_key format (expected 64 hex chars)"); return; }

        // Stream upload to temporary SPIFFS file and hash it incrementally.
        // The OTA image is larger than available C3 RAM, so the signing
        // contract is a domain- and version-bound Ed25519 signature over the
        // SHA-512 digest assembled below.
        File out = SPIFFS.open(tmpPath, FILE_WRITE);
        if (!out) { sendResponse(client, 500, "text/plain", "Failed to open temp file"); return; }

        crypto_sha512_ctx sha512;
        crypto_sha512_init(&sha512);
        static const uint8_t otaDomain[] = {'R','N','S','-','O','T','A','-','V','1',0};
        crypto_sha512_update(&sha512, otaDomain, sizeof(otaDomain));
        crypto_sha512_update(&sha512, reinterpret_cast<const uint8_t*>(firmwareVersion.c_str()), firmwareVersion.length());
        const uint8_t separator = 0;
        crypto_sha512_update(&sha512, &separator, 1);
        size_t remaining = (size_t)contentLength;
        size_t totalReceived = 0;
        const unsigned long transferStart = millis();
        unsigned long lastProgress = transferStart;
        const unsigned long inactivityTimeoutMs = 10000;
        const unsigned long totalTimeoutMs = 300000;
        while (remaining > 0 && client.connected() &&
               (millis() - lastProgress) < inactivityTimeoutMs &&
               (millis() - transferStart) < totalTimeoutMs) {
            while (client.available() && remaining > 0) {
                uint8_t buf[1024];
                size_t toRead = client.read(buf, (int)min<size_t>(sizeof(buf), remaining));
                if (toRead == 0) break;
                size_t written = out.write(buf, toRead);
                if (written != toRead) {
                    out.close();
                    SPIFFS.remove(tmpPath);
                    sendResponse(client, 500, "text/plain", "Failed to write temp OTA file");
                    return;
                }
                crypto_sha512_update(&sha512, buf, toRead);
                remaining -= toRead;
                totalReceived += toRead;
                lastProgress = millis();
            }
            delay(1);
        }
        out.flush(); out.close();
        if (remaining != 0) { sendResponse(client, 408, "text/plain", "Incomplete or timed-out upload"); SPIFFS.remove(tmpPath); return; }
        uint8_t firmwareDigest[64];
        crypto_sha512_final(&sha512, firmwareDigest);

        File szFile = SPIFFS.open(tmpPath, FILE_READ);
        size_t fsize = szFile ? szFile.size() : 0;
        if (szFile) szFile.close();
        if (fsize == 0 || fsize != (size_t)contentLength || fsize != totalReceived) {
            sendResponse(client, 400, "text/plain", "Upload size mismatch");
            SPIFFS.remove(tmpPath);
            return;
        }

        int ok = crypto_ed25519_check(sig, pub, firmwareDigest, sizeof(firmwareDigest));
        if (ok != 0) { SPIFFS.remove(tmpPath); sendResponse(client, 403, "text/plain", "Invalid signature"); return; }

        // Apply OTA from temp file
        File fin = SPIFFS.open(tmpPath, FILE_READ);
        if (!fin) { SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "Failed to open temp file for OTA"); return; }
        if (!Update.begin((size_t)fsize)) { fin.close(); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "OTA begin failed"); return; }
        const size_t chunk = 1024;
        uint8_t wbuf[chunk];
        size_t totalWritten = 0;
        while (fin.available()) {
            size_t r = fin.read(wbuf, chunk);
            if (r == 0) break;
            size_t written = Update.write(wbuf, r);
            if (written != r) { Update.abort(); fin.close(); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "Write failed"); return; }
            totalWritten += written;
        }
        fin.close();
        if (totalWritten != fsize) { Update.abort(); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "OTA write size mismatch"); return; }
        if (!Update.end(false)) { SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "OTA finalize failed"); return; }
        SPIFFS.remove(tmpPath);
        // Persist config before reboot to avoid losing packet counter/address
        reticulumNode.saveConfigNow();
        sendResponse(client, 200, "text/plain", "ok");
        delay(250);
        ESP.restart();
#else
        sendResponse(client, 404, "text/plain", "OTA disabled");
#endif

    } else if (method == "POST" && path == "/api/v1/restart") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
        // Persist state before performing a restart
        reticulumNode.saveConfigNow();
        sendResponse(client, 200, "text/plain", "restarting");
        delay(250); ESP.restart();

    } else if (method == "GET" && path == "/api/v1/metrics") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
#if METRICS_ENABLED
        DynamicJsonDocument doc(3072);
        RoutingTable &routingTable = reticulumNode.getRoutingTable();
        InterfaceManager &interfaceManager = reticulumNode.getInterfaceManager();
        doc["heap_free"] = ESP.getFreeHeap();
        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["uptime_s"] = millis() / 1000;
        doc["active_links"] = (int)reticulumNode.getLinkManager().getActiveLinkCount();
        doc["route_count"] = (int)routingTable.getRouteCount();
        doc["route_candidate_count"] = (int)routingTable.getRouteCandidateCount();
        JsonObject interfaces = doc.createNestedObject("interfaces");
        appendInterfaceHealth(interfaces, interfaceManager);
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);
#else
        sendResponse(client, 404, "text/plain", "metrics disabled");
#endif
    } else {
        sendResponse(client, 404, "text/plain", "Not found");
    }
}

void WebServerManager::begin() {
    if (!SPIFFS.begin(false)) {
        DebugSerial.println("WebServer: SPIFFS mount failed; server not started");
        _serverStarted = false;
        return;
    }
    DebugSerial.println("WebServer: SPIFFS mounted");
    _server.begin();
    _serverStarted = true;
    DebugSerial.print("WebServer: started on port "); DebugSerial.println(WEBSERVER_PORT);
}

bool WebServerManager::isStarted() { return _serverStarted; }

void WebServerManager::loop() {
    if (!_serverStarted) return;
    WiFiClient client = _server.accept();
    if (client) {
        if (client.connected()) {
            processHttpClient(client);
            delay(1);
            client.stop();
        }
    }
}

bool WebServerManager::loadConfigFromFS(const char* path) {
#if JSON_CONFIG_ENABLED
    if (!path || !SPIFFS.begin(false)) return false;
    if (!SPIFFS.exists(path)) return false;
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return false;
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    return !err;
#else
    (void)path; return false;
#endif
}

#else // WEBSERVER_ENABLED

void WebServerManager::begin() { (void)0; }
bool WebServerManager::isStarted() { return false; }
void WebServerManager::loop() { (void)0; }
bool WebServerManager::loadConfigFromFS(const char* path) { (void)path; return false; }

#endif // WEBSERVER_ENABLED
