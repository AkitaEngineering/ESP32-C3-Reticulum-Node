#include <Arduino.h>
#include <unity.h>

// Forward declarations of test functions
void test_ed25519_sign_verify(void);
void test_kiss_encode_escape(void);
void test_serialize_deserialize_roundtrip(void);

void setup() {
    delay(2000);
    UNITY_BEGIN();
    // Run only the ed25519 unit test for now
    RUN_TEST(test_ed25519_sign_verify);
    UNITY_END();
}

void loop() {}
