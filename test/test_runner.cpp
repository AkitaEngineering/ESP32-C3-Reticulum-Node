#include <Arduino.h>
#include <unity.h>

// Forward declarations of test functions
void test_ed25519_sign_verify(void);
void test_kiss_encode_escape(void);
void test_serialize_deserialize_roundtrip(void);
void test_eeprom_persistence(void);
void test_rns_destination_hash_plain(void);
void test_rns_sha256_empty(void);
void test_rns_name_hash(void);

void setup() {
    delay(2000);
    UNITY_BEGIN();
    // Run unit tests
    RUN_TEST(test_ed25519_sign_verify);
    RUN_TEST(test_eeprom_persistence);
    RUN_TEST(test_kiss_encode_escape);
    RUN_TEST(test_serialize_deserialize_roundtrip);
    RUN_TEST(test_rns_sha256_empty);
    RUN_TEST(test_rns_name_hash);
    RUN_TEST(test_rns_destination_hash_plain);
    UNITY_END();
}

void loop() {}
