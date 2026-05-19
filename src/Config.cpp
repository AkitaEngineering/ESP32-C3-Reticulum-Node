#include "Config.h"

#include <esp_mac.h>

#if JSON_CONFIG_ENABLED
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

namespace {

#if JSON_CONFIG_ENABLED
constexpr const char* CONFIG_PATH = "/config.json";

struct RuntimeConfigCache {
	bool initialized = false;
	char nodeName[48] = {0};
	char appName[64] = {0};
	RoutePriorityConfig routePriorities;
};

RuntimeConfigCache runtimeConfigCache;

bool ensureConfigFsMounted() {
	static bool attemptedMount = false;
	static bool mounted = false;
	if (!attemptedMount) {
		mounted = SPIFFS.begin(false);
		attemptedMount = true;
	}
	return mounted;
}

bool loadRuntimeConfigDocument(DynamicJsonDocument& doc) {
	if (!ensureConfigFsMounted() || !SPIFFS.exists(CONFIG_PATH)) {
		return false;
	}

	File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
	if (!f) {
		return false;
	}

	DeserializationError err = deserializeJson(doc, f);
	f.close();
	return !err;
}

void populateRuntimeConfigCache() {
	snprintf(runtimeConfigCache.nodeName, sizeof(runtimeConfigCache.nodeName), "%s", getDefaultDeviceName());
	snprintf(runtimeConfigCache.appName, sizeof(runtimeConfigCache.appName), "%s", RNS_APP_NAME);
	runtimeConfigCache.routePriorities = RoutePriorityConfig{};

	DynamicJsonDocument doc(2048);
	if (loadRuntimeConfigDocument(doc)) {
		const char* nodeName = doc["node_name"] | "";
		if (nodeName && nodeName[0] != '\0') {
			snprintf(runtimeConfigCache.nodeName, sizeof(runtimeConfigCache.nodeName), "%s", nodeName);
		}

		const char* appName = doc["rns_app_name"] | "";
		if (appName && appName[0] != '\0') {
			snprintf(runtimeConfigCache.appName, sizeof(runtimeConfigCache.appName), "%s", appName);
		}

		JsonObject routing = doc["routing"];
		if (!routing.isNull()) {
			JsonObject interfacePriority = routing["interface_priority"];
			if (!interfacePriority.isNull()) {
				runtimeConfigCache.routePriorities.wifi_udp = interfacePriority["wifi_udp"] | ROUTE_PRIORITY_WIFI_UDP;
				runtimeConfigCache.routePriorities.esp_now = interfacePriority["esp_now"] | ROUTE_PRIORITY_ESP_NOW;
				runtimeConfigCache.routePriorities.lora = interfacePriority["lora"] | ROUTE_PRIORITY_LORA;
				runtimeConfigCache.routePriorities.ham_modem = interfacePriority["ham_modem"] | ROUTE_PRIORITY_HAM_MODEM;
				runtimeConfigCache.routePriorities.serial_port = interfacePriority["serial_port"] | ROUTE_PRIORITY_SERIAL_PORT;
				runtimeConfigCache.routePriorities.bluetooth = interfacePriority["bluetooth"] | ROUTE_PRIORITY_BLUETOOTH;
				runtimeConfigCache.routePriorities.ipfs = interfacePriority["ipfs"] | ROUTE_PRIORITY_IPFS;
			}
		}
	}

	runtimeConfigCache.initialized = true;
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
#if JSON_CONFIG_ENABLED
		if (!runtimeConfigCache.initialized) {
			populateRuntimeConfigCache();
		}
		return runtimeConfigCache.nodeName;
#else
		return getDefaultDeviceName();
#endif
}

const char* getConfiguredAppName() {
#if JSON_CONFIG_ENABLED
		if (!runtimeConfigCache.initialized) {
			populateRuntimeConfigCache();
		}
		return runtimeConfigCache.appName;
#else
		return RNS_APP_NAME;
#endif
}

const RoutePriorityConfig& getConfiguredRoutePriorities() {
#if JSON_CONFIG_ENABLED
	if (!runtimeConfigCache.initialized) {
		populateRuntimeConfigCache();
	}
	return runtimeConfigCache.routePriorities;
#else
	static const RoutePriorityConfig defaultPriorities;
	return defaultPriorities;
#endif
}

void reloadRuntimeConfigCache() {
#if JSON_CONFIG_ENABLED
	populateRuntimeConfigCache();
#endif
}

bool hasRuntimeConfigFile() {
#if JSON_CONFIG_ENABLED
	return ensureConfigFsMounted() && SPIFFS.exists(CONFIG_PATH);
#else
	return false;
#endif
}
