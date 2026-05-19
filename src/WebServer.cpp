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
static const char* CONFIG_PATH = "/config.json";

static void sendResponse(WiFiClient &client, int code, const char *contentType, const String &body, const String &extraHeaders = String()) {
    const char *statusText = "OK";
    switch (code) {
        case 200: statusText = "OK"; break;
        case 201: statusText = "Created"; break;
        case 400: statusText = "Bad Request"; break;
        case 401: statusText = "Unauthorized"; break;
        case 403: statusText = "Forbidden"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        default: statusText = "OK"; break;
    }

    client.print("HTTP/1.1 ");
    client.print(code);
    client.print(" "); client.print(statusText); client.print("\r\n");
    client.print("Content-Type: "); client.print(contentType); client.print("\r\n");
    client.print("Content-Length: "); client.print(body.length()); client.print("\r\n");
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
    client.print("Connection: close\r\n\r\n");
    client.print(body);
}

#if JSON_CONFIG_ENABLED
static String getSavedApiToken() {
    if (!SPIFFS.exists(CONFIG_PATH)) return String();
    File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
    if (!f) return String();
    DynamicJsonDocument doc(2048);
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
    return getSavedApiToken().length() == 0;
#else
    return false;
#endif
}

static bool getRestartRequiredReason(String &reason) {
    if (strcmp(reticulumNode.getRuntimeAppName(), getConfiguredAppName()) != 0) {
        reason = "rns_app_name_changed";
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

static bool checkAuth(const String &authHeader) {
#if WEBSERVER_AUTH_ENABLED
    String expected;
#if JSON_CONFIG_ENABLED
    expected = getSavedApiToken();
#endif
    expected.trim();
    // If no token configured, allow bootstrap (first-time config)
    if (expected.length() == 0) {
        LOG_WARN("WebServer: No API token configured - bootstrap mode (unauthenticated access)");
        return true;
    }

    String token = authHeader;
    token.trim();
    if (token.startsWith("Bearer ")) token = token.substring(7);
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

static String readLine(WiFiClient &c, unsigned long timeout=1000) {
    String s;
    unsigned long start = millis();
    while (millis() - start < timeout) {
        if (!c.connected()) break;
        while (c.available()) {
            char ch = c.read();
            s += ch;
            if (s.endsWith("\r\n")) return s;
        }
        delay(1);
    }
    return s;
}

void processHttpClient(WiFiClient &client) {
    // Read request line
    String req = readLine(client);
    if (req.length() == 0) return;
    // Example: "GET /api/v1/status HTTP/1.1\r\n"
    int firstSpace = req.indexOf(' ');
    int secondSpace = req.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) return;
    String method = req.substring(0, firstSpace);
    String path = req.substring(firstSpace + 1, secondSpace);

    // Read headers (case-insensitive comparison)
    int contentLength = 0;
    String authHeader;
    String signatureHex;
    while (true) {
        String line = readLine(client);
        if (line.length() <= 2) break; // \r\n
        line.trim();
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            contentLength = line.substring(15).toInt();
        } else if (lower.startsWith("authorization:")) {
            authHeader = line.substring(14);
            authHeader.trim();
        } else if (lower.startsWith("x-signature-ed25519:")) {
            signatureHex = line.substring(20);
            signatureHex.trim();
        }
    }

    // Enforce body size limit to prevent memory exhaustion
    const int MAX_BODY_SIZE = 4096;
    bool isOtaUpload = (method == "POST" && path == "/api/v1/ota");
    if (!isOtaUpload && contentLength > MAX_BODY_SIZE) {
        sendResponse(client, 400, "text/plain", "Body too large");
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
    }

    // Route handling
    if (method == "GET" && path == "/api/v1/status") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
        reloadRuntimeConfigCache();
        DynamicJsonDocument doc(3072);
        String restartReason;
        bool restartRequired = getRestartRequiredReason(restartReason);
        RoutingTable &routingTable = reticulumNode.getRoutingTable();
        InterfaceManager &interfaceManager = reticulumNode.getInterfaceManager();

        bool wifiConnected = WiFi.status() == WL_CONNECTED;
        doc["node_name"] = getConfiguredNodeName();
        doc["device_id"] = getDefaultDeviceName();
        doc["rns_app_name"] = getConfiguredAppName();
        doc["uptime_s"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["active_links"] = (int)reticulumNode.getLinkManager().getActiveLinkCount();
        doc["route_count"] = (int)routingTable.getRouteCount();
        doc["route_candidate_count"] = (int)routingTable.getRouteCandidateCount();
        doc["config_present"] = hasRuntimeConfigFile();
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

    } else if (method == "GET" && path == "/api/v1/config") {
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
        DynamicJsonDocument doc(1024);
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
        if (!checkAuth(authHeader)) { sendUnauthorized(client); return; }
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendResponse(client, 400, "text/plain", "Invalid JSON"); return; }
#if JSON_CONFIG_ENABLED
        File f = SPIFFS.open(CONFIG_PATH, FILE_WRITE);
        if (!f) { sendResponse(client, 500, "text/plain", "Failed to open config for write"); return; }
        if (serializeJson(doc, f) == 0) { f.close(); sendResponse(client, 500, "text/plain", "Failed to write config"); return; }
        f.close();
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
        String out; serializeJson(doc, out);
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

        String pubHex;
#if JSON_CONFIG_ENABLED
        DynamicJsonDocument cdoc(2048);
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

        auto hexToBin = [](const String &hex, uint8_t *out, size_t expectedLen)->bool {
            if ((size_t)hex.length() != expectedLen*2) return false;
            for (size_t i=0;i<expectedLen;i++) {
                char h = hex.charAt(2*i);
                char l = hex.charAt(2*i+1);
                auto val = [](char c)->int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
                int hi = val(h); int lo = val(l);
                if (hi < 0 || lo < 0) return false;
                out[i] = (uint8_t)((hi << 4) | lo);
            }
            return true;
        };

        uint8_t sig[64]; uint8_t pub[32];
        if (!hexToBin(signatureHex, sig, 64)) { sendResponse(client, 400, "text/plain", "Bad signature format (expected 128 hex chars)"); return; }
        if (!hexToBin(pubHex, pub, 32)) { sendResponse(client, 400, "text/plain", "Bad public_key format (expected 64 hex chars)"); return; }

        // Stream upload to temporary SPIFFS file
        const char *tmpPath = "/ota_upload.bin";
        File out = SPIFFS.open(tmpPath, FILE_WRITE);
        if (!out) { sendResponse(client, 500, "text/plain", "Failed to open temp file"); return; }

        size_t remaining = (size_t)contentLength;
        unsigned long start = millis();
        const unsigned long streamTimeoutMs = 30000; // 30s total
        while (remaining > 0 && (millis() - start) < streamTimeoutMs) {
            while (client.available() && remaining > 0) {
                uint8_t buf[1024];
                size_t toRead = client.read(buf, (int)min<size_t>(sizeof(buf), remaining));
                if (toRead == 0) break;
                out.write(buf, toRead);
                remaining -= toRead;
            }
            delay(1);
        }
        out.flush(); out.close();
        if (remaining != 0) { sendResponse(client, 400, "text/plain", "Incomplete upload"); SPIFFS.remove(tmpPath); return; }

        // Size safety: avoid loading huge files into RAM for verification
        File szFile = SPIFFS.open(tmpPath, FILE_READ);
        size_t fsize = szFile ? szFile.size() : 0;
        if (szFile) szFile.close();
        const size_t MAX_OTA_VERIFY_SIZE = 2 * 1024 * 1024; // 2 MB
        if (fsize == 0 || fsize != (size_t)contentLength) { sendResponse(client, 400, "text/plain", "Upload size mismatch"); SPIFFS.remove(tmpPath); return; }
        if (fsize > MAX_OTA_VERIFY_SIZE) { sendResponse(client, 400, "text/plain", "OTA image too large to verify"); SPIFFS.remove(tmpPath); return; }

        // Read file into RAM for signature verification (bounded by MAX_OTA_VERIFY_SIZE)
        File vf = SPIFFS.open(tmpPath, FILE_READ);
        if (!vf) { sendResponse(client, 500, "text/plain", "Failed to open temp file for verify"); SPIFFS.remove(tmpPath); return; }
        size_t len = vf.size();
        uint8_t *buf = (uint8_t*)malloc(len);
        if (!buf) { vf.close(); sendResponse(client, 500, "text/plain", "Out of memory"); SPIFFS.remove(tmpPath); return; }
        vf.read(buf, len);
        vf.close();

        int ok = crypto_ed25519_check(sig, pub, buf, len);
        if (ok != 0) { free(buf); SPIFFS.remove(tmpPath); sendResponse(client, 403, "text/plain", "Invalid signature"); return; }

        // Apply OTA from temp file
        File fin = SPIFFS.open(tmpPath, FILE_READ);
        if (!fin) { free(buf); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "Failed to open temp file for OTA"); return; }
        if (!Update.begin((size_t)fsize)) { fin.close(); free(buf); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "OTA begin failed"); return; }
        const size_t chunk = 1024;
        uint8_t wbuf[chunk];
        size_t totalWritten = 0;
        while (fin.available()) {
            size_t r = fin.read(wbuf, chunk);
            if (r == 0) break;
            size_t written = Update.write(wbuf, r);
            if (written != r) { fin.close(); free(buf); SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "Write failed"); return; }
            totalWritten += written;
        }
        fin.close(); free(buf);
        if (!Update.end(true)) { SPIFFS.remove(tmpPath); sendResponse(client, 500, "text/plain", "OTA finalize failed"); return; }
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
    if (!SPIFFS.begin(true)) DebugSerial.println("WebServer: SPIFFS mount failed"); else DebugSerial.println("WebServer: SPIFFS mounted");
    _server.begin();
    DebugSerial.print("WebServer: started on port "); DebugSerial.println(WEBSERVER_PORT);
}

void WebServerManager::loop() {
    WiFiClient client = _server.available();
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

bool WebServerManager::saveConfigToFS(const char* path) {
#if JSON_CONFIG_ENABLED
    (void)path; return true; // Config saving is handled in processHttpClient
#else
    (void)path; return false;
#endif
}

#else // WEBSERVER_ENABLED

void WebServerManager::begin() { (void)0; }
void WebServerManager::loop() { (void)0; }
bool WebServerManager::loadConfigFromFS(const char* path) { (void)path; return false; }
bool WebServerManager::saveConfigToFS(const char* path) { (void)path; return false; }

#endif // WEBSERVER_ENABLED
