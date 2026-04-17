#include "Config.h"

#include <esp_mac.h>

#if JSON_CONFIG_ENABLED
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

namespace {

#if JSON_CONFIG_ENABLED
constexpr const char* CONFIG_PATH = "/config.json";

bool loadConfigString(const char* key, char* out, size_t outSize) {
	static bool attemptedMount = false;
	static bool mounted = false;
	if (!attemptedMount) {
		mounted = SPIFFS.begin(false);
		attemptedMount = true;
	}
	if (!mounted || !SPIFFS.exists(CONFIG_PATH)) {
		return false;
	}

	File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
	if (!f) {
		return false;
	}

	DynamicJsonDocument doc(1024);
	DeserializationError err = deserializeJson(doc, f);
	f.close();
	if (err) {
		return false;
	}

	const char* value = doc[key] | "";
	if (!value || value[0] == '\0') {
		return false;
	}

	snprintf(out, outSize, "%s", value);
	return true;
}
#endif

} // namespace

// Debug serial shim instance
DebugSerialShim DebugSerial;

// --- WiFi Credentials ---
const char *WIFI_SSID = ""; // <<< CHANGE ME
const char *WIFI_PASSWORD = ""; // <<< CHANGE ME

// --- Node Configuration ---
const char* getDefaultDeviceName() {
	static char deviceName[24] = {0};
	static bool initialized = false;
	if (!initialized) {
		uint8_t mac[6] = {0};
		if (esp_efuse_mac_get_default(mac) == ESP_OK) {
			snprintf(deviceName, sizeof(deviceName), "rns-%02X%02X%02X", mac[3], mac[4], mac[5]);
		} else {
			snprintf(deviceName, sizeof(deviceName), "rns-unknown");
		}
		initialized = true;
	}
	return deviceName;
}

const char* getConfiguredNodeName() {
	static char nodeName[48] = {0};
	static bool initialized = false;
	if (!initialized) {
#if JSON_CONFIG_ENABLED
		if (!loadConfigString("node_name", nodeName, sizeof(nodeName))) {
			snprintf(nodeName, sizeof(nodeName), "%s", getDefaultDeviceName());
		}
#else
		snprintf(nodeName, sizeof(nodeName), "%s", getDefaultDeviceName());
#endif
		initialized = true;
	}
	return nodeName;
}

const char* getConfiguredAppName() {
	static char appName[64] = {0};
	static bool initialized = false;
	if (!initialized) {
#if JSON_CONFIG_ENABLED
		if (!loadConfigString("rns_app_name", appName, sizeof(appName))) {
			snprintf(appName, sizeof(appName), "%s", RNS_APP_NAME);
		}
#else
		snprintf(appName, sizeof(appName), "%s", RNS_APP_NAME);
#endif
		initialized = true;
	}
	return appName;
}
