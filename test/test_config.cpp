#include <Arduino.h>
#include <unity.h>

#include "Config.h"

namespace {

String validConfig(const char* ssid = "field-net") {
    String json = "{\"node_name\":\"field-node\",\"rns_app_name\":\"esp32.node\",";
    json += "\"wifi\":{\"ssid\":\"";
    json += ssid;
    json += "\",\"password\":\"safe-password\"},";
    json += "\"routing\":{\"interface_priority\":{\"wifi_udp\":50,\"esp_now\":40}},";
    json += "\"api\":{\"auth_enabled\":true,";
    json += "\"token\":\"0123456789abcdefghijklmnopqrstuvwxyzABCD\",";
    json += "\"public_key\":\"0101010101010101010101010101010101010101010101010101010101010101\"}}";
    return json;
}

} // namespace

void test_runtime_config_validation(void) {
    String reason;
    TEST_ASSERT_TRUE(validateRuntimeConfigJson(validConfig(), true, true, &reason));
    TEST_ASSERT_EQUAL_STRING("", reason.c_str());

    TEST_ASSERT_TRUE(validateRuntimeConfigJson(validConfig(""), false, true, &reason));
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(validConfig(""), true, true, &reason));
    TEST_ASSERT_EQUAL_STRING("WiFi SSID is required", reason.c_str());

    String invalid = validConfig();
    invalid.replace("field-net", "field\\nnet");
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(invalid, true, true, &reason));

    invalid = validConfig();
    invalid.replace("\"wifi_udp\":50", "\"wifi_udp\":5001");
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(invalid, true, true, &reason));

    invalid = validConfig();
    invalid.replace("0101010101010101010101010101010101010101010101010101010101010101",
                    "0000000000000000000000000000000000000000000000000000000000000000");
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(invalid, true, true, &reason));

    const String offlineConfig = "{\"node_name\":\"usb-node\",\"rns_app_name\":\"esp32.node\","
                                 "\"routing\":{\"interface_priority\":{\"serial_port\":50}}}";
    TEST_ASSERT_TRUE(validateRuntimeConfigJson(offlineConfig, false, false, &reason));
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(offlineConfig, false, true, &reason));

    const String malformedOfflineConfig = "{\"node_name\":42,\"routing\":{}}";
    TEST_ASSERT_FALSE(validateRuntimeConfigJson(malformedOfflineConfig, false, false, &reason));
}
