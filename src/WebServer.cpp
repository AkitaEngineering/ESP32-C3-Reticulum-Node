#include "WebServer.h"
#include "Config.h"
#include "Utils.h"

#if WEBSERVER_ENABLED

#include "ReticulumNode.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <monocypher.h>

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

static void sendResponse(WiFiClient &client, int code, const char *contentType, const String &body) {
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

static bool checkAuth(const String &authHeader) {
#if WEBSERVER_AUTH_ENABLED
    String expected;
#if JSON_CONFIG_ENABLED
    expected = getSavedApiToken();
#endif
    expected.trim();
    // If no token configured, allow bootstrap (first-time config)
    if (expected.length() == 0) return true;

    String token = authHeader;
    token.trim();
    if (token.startsWith("Bearer ")) token = token.substring(7);
    token.trim();
    return token == expected;
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

    // Read headers
    int contentLength = 0;
    String authHeader;
    String signatureHex;
    while (true) {
        String line = readLine(client);
        if (line.length() <= 2) break; // \r\n
        line.trim();
        if (line.startsWith("Content-Length:")) {
            contentLength = line.substring(15).toInt();
        } else if (line.startsWith("Authorization:")) {
            authHeader = line.substring(14);
            authHeader.trim();
        } else if (line.startsWith("X-Signature-Ed25519:")) {
            signatureHex = line.substring(20);
            signatureHex.trim();
        }
    }

    // For OTA uploads we stream to SPIFFS directly; otherwise read body into memory
    bool isOtaUpload = (method == "POST" && path == "/api/v1/ota");
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
        DynamicJsonDocument doc(512);
        doc["uptime_s"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["active_links"] = (int)reticulumNode.getLinkManager().getActiveLinkCount();
        doc["route_count"] = (int)reticulumNode.getRoutingTable().getRouteCount();
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
        } else {
            doc["node_name"] = "esp32-rns-node";
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
        String out; serializeJson(doc, out);
        sendResponse(client, 200, "application/json", out);

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
        size_t fsize = SPIFFS.open(tmpPath, FILE_READ).size();
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

        int ok = crypto_ed25519_verify(sig, buf, len, pub);
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
        DynamicJsonDocument doc(512);
        doc["heap_free"] = ESP.getFreeHeap();
        doc["uptime_s"] = millis() / 1000;
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
