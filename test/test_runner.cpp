#include <Arduino.h>
#include <unity.h>

// Forward declarations of test functions
void test_ed25519_sign_verify(void);
void test_kiss_encode_escape(void);
void test_kiss_ignores_non_data_commands(void);
void test_serialize_deserialize_roundtrip(void);
void test_eeprom_persistence(void);
void test_packet_counter_wrap_skips_reserved_values(void);
void test_rns_destination_hash_plain(void);
void test_rns_sha256_empty(void);
void test_rns_name_hash(void);
void test_aprs_uncompressed_position_roundtrip(void);
void test_aprs_compressed_position_format(void);

void setup() {
    // On ESP32-C3 with ARDUINO_USB_MODE=1, USB CDC must be started
    // explicitly before any serial output (including Unity test results).
    Serial.begin(115200);
    delay(2000);  // let USB CDC enumerate on the host
    UNITY_BEGIN();
    // Run unit tests
    RUN_TEST(test_ed25519_sign_verify);
    RUN_TEST(test_eeprom_persistence);
    RUN_TEST(test_packet_counter_wrap_skips_reserved_values);
    RUN_TEST(test_kiss_encode_escape);
    RUN_TEST(test_kiss_ignores_non_data_commands);
    RUN_TEST(test_serialize_deserialize_roundtrip);
    RUN_TEST(test_rns_sha256_empty);
    RUN_TEST(test_rns_name_hash);
    RUN_TEST(test_rns_destination_hash_plain);
    RUN_TEST(test_aprs_uncompressed_position_roundtrip);
    RUN_TEST(test_aprs_compressed_position_format);
    UNITY_END();
}

void loop() {}
