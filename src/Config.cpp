#include "Config.h"

#include <esp_mac.h>
#include <cctype>

#if JSON_CONFIG_ENABLED
#include <ArduinoJson.h>
#include <SPIFFS.h>
#endif

namespace {

#if JSON_CONFIG_ENABLED
constexpr const char* CONFIG_PATH = "/config.json";
constexpr const char* CONFIG_NEW_PATH = "/config.new";
constexpr const char* CONFIG_BACKUP_PATH = "/config.backup";

struct RuntimeConfigCache {
	bool initialized = false;
	char nodeName[48] = {0};
	char appName[64] = {0};
	char wifiSsid[33] = {0};
	char wifiPassword[65] = {0};
	RoutePriorityConfig routePriorities;
};

RuntimeConfigCache runtimeConfigCache;

bool ensureConfigFsMounted() {
	static bool mounted = false;
	if (!mounted) mounted = SPIFFS.begin(false);
	return mounted;
}

bool isValidConfigFile(const char* path) {
	if (!path || !SPIFFS.exists(path)) return false;
	File file = SPIFFS.open(path, FILE_READ);
	if (!file) return false;
	DynamicJsonDocument doc(4096);
	const bool valid = deserializeJson(doc, file) == DeserializationError::Ok && doc.is<JsonObject>();
	file.close();
	return valid;
}

bool recoverRuntimeConfig() {
	if (!ensureConfigFsMounted()) return false;

	if (isValidConfigFile(CONFIG_PATH)) {
		// The committed file always wins; these files are leftovers from an
		// interrupted transaction or its cleanup phase.
		SPIFFS.remove(CONFIG_NEW_PATH);
		SPIFFS.remove(CONFIG_BACKUP_PATH);
		return true;
	}

	const bool hadInvalidConfig = SPIFFS.exists(CONFIG_PATH);
	const char* recoveryPath = nullptr;
	if (hadInvalidConfig && isValidConfigFile(CONFIG_BACKUP_PATH)) {
		recoveryPath = CONFIG_BACKUP_PATH;
	} else if (isValidConfigFile(CONFIG_NEW_PATH)) {
		recoveryPath = CONFIG_NEW_PATH;
	} else if (isValidConfigFile(CONFIG_BACKUP_PATH)) {
		recoveryPath = CONFIG_BACKUP_PATH;
	}

	if (!recoveryPath) return false;
	if (hadInvalidConfig && !SPIFFS.remove(CONFIG_PATH)) return false;
	if (!SPIFFS.rename(recoveryPath, CONFIG_PATH)) return false;
	SPIFFS.remove(CONFIG_NEW_PATH);
	SPIFFS.remove(CONFIG_BACKUP_PATH);
	return true;
}

bool loadRuntimeConfigDocument(DynamicJsonDocument& doc) {
	if (!recoverRuntimeConfig()) {
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

bool validateRuntimeConfigDocument(const DynamicJsonDocument& doc, bool requireWifi,
								   bool requireManagement,
								   String* errorReason);

void populateRuntimeConfigCache() {
	snprintf(runtimeConfigCache.nodeName, sizeof(runtimeConfigCache.nodeName), "%s", getDefaultDeviceName());
	snprintf(runtimeConfigCache.appName, sizeof(runtimeConfigCache.appName), "%s", RNS_APP_NAME);
	snprintf(runtimeConfigCache.wifiSsid, sizeof(runtimeConfigCache.wifiSsid), "%s", WIFI_SSID);
	snprintf(runtimeConfigCache.wifiPassword, sizeof(runtimeConfigCache.wifiPassword), "%s", WIFI_PASSWORD);
	runtimeConfigCache.routePriorities = RoutePriorityConfig{};

	DynamicJsonDocument doc(4096);
	if (loadRuntimeConfigDocument(doc) &&
		validateRuntimeConfigDocument(doc, PRODUCTION_BUILD, WEBSERVER_ENABLED, nullptr)) {
		const char* nodeName = doc["node_name"] | "";
		if (nodeName && nodeName[0] != '\0') {
			snprintf(runtimeConfigCache.nodeName, sizeof(runtimeConfigCache.nodeName), "%s", nodeName);
		}

		const char* appName = doc["rns_app_name"] | "";
			if (appName && appName[0] != '\0') {
				snprintf(runtimeConfigCache.appName, sizeof(runtimeConfigCache.appName), "%s", appName);
			}

			JsonObject wifi = doc["wifi"];
			if (!wifi.isNull()) {
				const char* ssid = wifi["ssid"] | "";
				const char* password = wifi["password"] | "";
				snprintf(runtimeConfigCache.wifiSsid, sizeof(runtimeConfigCache.wifiSsid), "%s", ssid);
				snprintf(runtimeConfigCache.wifiPassword, sizeof(runtimeConfigCache.wifiPassword), "%s", password);
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

#if JSON_CONFIG_ENABLED
namespace {

bool validationError(String* errorReason, const char* message) {
	if (errorReason) *errorReason = message;
	return false;
}

bool isSafeConfigText(const char* value, size_t minLength, size_t maxLength) {
	if (!value) return false;
	const size_t length = strlen(value);
	if (length < minLength || length > maxLength) return false;
	for (size_t i = 0; i < length; ++i) {
		const uint8_t c = static_cast<uint8_t>(value[i]);
		if (c < 0x20 || c == 0x7F) return false;
	}
	return true;
}

bool isValidWifiPassword(const char* password) {
	if (!password) return false;
	const size_t length = strlen(password);
	if (length == 0) return true;
	if (length == 64) {
		for (size_t i = 0; i < length; ++i) {
			if (!isxdigit(static_cast<unsigned char>(password[i]))) return false;
		}
		return true;
	}
	return isSafeConfigText(password, 8, 63);
}

bool isValidNonzeroHexKey(const char* value) {
	if (!value || strlen(value) != 64) return false;
	uint8_t combined = 0;
	for (size_t i = 0; i < 64; i += 2) {
		auto nibble = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		const int high = nibble(value[i]);
		const int low = nibble(value[i + 1]);
		if (high < 0 || low < 0) return false;
		combined |= static_cast<uint8_t>((high << 4) | low);
	}
	return combined != 0;
}

bool validateRuntimeConfigDocument(const DynamicJsonDocument& doc, bool requireWifi,
								   bool requireManagement,
								   String* errorReason) {
	if (!doc.is<JsonObjectConst>()) return validationError(errorReason, "root must be an object");
	JsonObjectConst root = doc.as<JsonObjectConst>();
	JsonVariantConst nodeNameVariant = root["node_name"];
	JsonVariantConst appNameVariant = root["rns_app_name"];
	if ((!nodeNameVariant.isNull() && !nodeNameVariant.is<const char*>()) ||
		(!appNameVariant.isNull() && !appNameVariant.is<const char*>())) {
		return validationError(errorReason, "node and application names must be strings");
	}
	const char* nodeName = nodeNameVariant | "";
	const char* appName = appNameVariant | "";
	if ((requireManagement && !isSafeConfigText(nodeName, 1, 47)) ||
		(strlen(nodeName) > 0 && !isSafeConfigText(nodeName, 1, 47))) {
		return validationError(errorReason, "invalid node_name");
	}
	if ((requireManagement && !isSafeConfigText(appName, 1, 63)) ||
		(strlen(appName) > 0 && !isSafeConfigText(appName, 1, 63))) {
		return validationError(errorReason, "invalid rns_app_name");
	}

	JsonVariantConst wifiVariant = root["wifi"];
	if (wifiVariant.isNull()) {
		if (requireWifi || requireManagement) return validationError(errorReason, "wifi must be an object");
	} else {
		if (!wifiVariant.is<JsonObjectConst>()) return validationError(errorReason, "wifi must be an object");
		JsonObjectConst wifi = wifiVariant.as<JsonObjectConst>();
		if ((!wifi["ssid"].isNull() && !wifi["ssid"].is<const char*>()) ||
			(!wifi["password"].isNull() && !wifi["password"].is<const char*>())) {
			return validationError(errorReason, "WiFi credentials must be strings");
		}
		const char* ssid = wifi["ssid"] | "";
		const char* password = wifi["password"] | "";
		if (strlen(ssid) > 0 && !isSafeConfigText(ssid, 1, 32)) {
			return validationError(errorReason, "invalid WiFi SSID");
		}
		if (requireWifi && strlen(ssid) == 0) return validationError(errorReason, "WiFi SSID is required");
		if (!isValidWifiPassword(password)) return validationError(errorReason, "invalid WiFi password");
	}

	JsonVariantConst apiVariant = root["api"];
	if (apiVariant.isNull()) {
		if (requireManagement) return validationError(errorReason, "api must be an object");
	} else {
		if (!apiVariant.is<JsonObjectConst>()) return validationError(errorReason, "api must be an object");
		JsonObjectConst api = apiVariant.as<JsonObjectConst>();
		if (requireManagement) {
			if (!api["auth_enabled"].is<bool>() || !api["auth_enabled"].as<bool>()) {
				return validationError(errorReason, "API authentication must be enabled");
			}
			const char* token = api["token"] | "";
			if (!isSafeConfigText(token, 32, 128) || strcmp(token, "CHANGE_ME_generate_a_strong_token") == 0) {
				return validationError(errorReason, "invalid API token");
			}
			if (!isValidNonzeroHexKey(api["public_key"] | "")) {
				return validationError(errorReason, "invalid OTA public key");
			}
		}
	}

	JsonVariantConst routing = root["routing"];
	if (!routing.isNull()) {
		if (!routing.is<JsonObjectConst>()) return validationError(errorReason, "routing must be an object");
		JsonVariantConst prioritiesVariant = routing["interface_priority"];
		if (!prioritiesVariant.isNull()) {
			if (!prioritiesVariant.is<JsonObjectConst>()) {
				return validationError(errorReason, "interface_priority must be an object");
			}
			static const char* names[] = {
				"wifi_udp", "esp_now", "lora", "ham_modem", "serial_port", "bluetooth", "ipfs"
			};
			JsonObjectConst priorities = prioritiesVariant.as<JsonObjectConst>();
			for (const char* name : names) {
				JsonVariantConst value = priorities[name];
				if (!value.isNull() && (!value.is<int>() || value.as<int>() < -1000 || value.as<int>() > 1000)) {
					return validationError(errorReason, "invalid route priority");
				}
			}
		}
	}

	if (errorReason) *errorReason = "";
	return true;
}

} // namespace
#endif

const char* getConfiguredWiFiSsid() {
#if JSON_CONFIG_ENABLED
	if (!runtimeConfigCache.initialized) {
		populateRuntimeConfigCache();
	}
	return runtimeConfigCache.wifiSsid;
#else
	return WIFI_SSID;
#endif
}

const char* getConfiguredWiFiPassword() {
#if JSON_CONFIG_ENABLED
	if (!runtimeConfigCache.initialized) {
		populateRuntimeConfigCache();
	}
	return runtimeConfigCache.wifiPassword;
#else
	return WIFI_PASSWORD;
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
	return recoverRuntimeConfig();
#else
	return false;
#endif
}

bool validateRuntimeConfigJson(const String& json, bool requireWifi, bool requireManagement,
							   String* errorReason) {
#if JSON_CONFIG_ENABLED
	DynamicJsonDocument doc(4096);
	const DeserializationError error = deserializeJson(doc, json);
	if (error) return validationError(errorReason, "invalid JSON");
	return validateRuntimeConfigDocument(doc, requireWifi, requireManagement, errorReason);
#else
	(void)json;
	(void)requireWifi;
	(void)requireManagement;
	if (errorReason) *errorReason = "JSON configuration is disabled";
	return false;
#endif
}

bool validateRuntimeConfigFile(bool requireWifi, bool requireManagement, String* errorReason) {
#if JSON_CONFIG_ENABLED
	DynamicJsonDocument doc(4096);
	if (!loadRuntimeConfigDocument(doc)) return validationError(errorReason, "configuration file is missing or invalid");
	return validateRuntimeConfigDocument(doc, requireWifi, requireManagement, errorReason);
#else
	(void)requireWifi;
	(void)requireManagement;
	if (errorReason) *errorReason = "JSON configuration is disabled";
	return false;
#endif
}
