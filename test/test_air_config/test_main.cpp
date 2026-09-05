/**
 * @file    test/test_air_config/test_main.cpp
 * @brief   Host-side tests of the SIMUT Air persistent config (air/AirConfig.h).
 * @details Runs via: pio test -e native_air (no HW).
 *
 * @project SIMUT Air
 * @license MIT License
 */

#include <unity.h>
#include "air/AirConfig.h"

static void test_default_config(void) {
    AirConfig c = airDefaultConfig( );
    TEST_ASSERT_EQUAL_UINT32(AIR_CONFIG_MAGIC, c.magic);
    TEST_ASSERT_EQUAL_UINT16(AIR_CONFIG_VERSION, c.version);
#if SIMUT_AIR
    TEST_ASSERT_EQUAL_UINT32(5, AIR_WAKE_INTERVAL_MIN);
    TEST_ASSERT_EQUAL_UINT16(AIR_IDLE_TIMEOUT_SEC, c.idleTimeoutSec);
    TEST_ASSERT_EQUAL_UINT16(AIR_STAB_TIMEOUT_MS, c.stabTimeoutMs);
    TEST_ASSERT_EQUAL_UINT8(AIR_SENSOR_POWER_PIN, c.sensorPowerPin);
    TEST_ASSERT(c.stabTimeoutMs > 0);
    TEST_ASSERT(c.idleTimeoutSec > 0);
#endif
}

static void test_crc_roundtrip(void) {
    AirConfig c = airDefaultConfig( );
    c.crc32 = airComputeCrc(c);
    TEST_ASSERT_TRUE(airConfigValid(c));
}

static void test_invalid_magic(void) {
    AirConfig c = airDefaultConfig( );
    c.crc32 = airComputeCrc(c);
    c.magic = 0xDEADBEEF;
    TEST_ASSERT_FALSE(airConfigValid(c));
}

static void test_invalid_version(void) {
    AirConfig c = airDefaultConfig( );
    c.crc32 = airComputeCrc(c);
    c.version = (uint16_t)(AIR_CONFIG_VERSION + 1);
    TEST_ASSERT_FALSE(airConfigValid(c));
}

static void test_invalid_crc(void) {
    AirConfig c = airDefaultConfig( );
    c.crc32 = airComputeCrc(c) ^ 0xFFFF;
    TEST_ASSERT_FALSE(airConfigValid(c));
}

static void test_crc_is_tail(void) {
    TEST_ASSERT_EQUAL_size_t(sizeof(AirConfig) - sizeof(uint32_t),
                             offsetof(AirConfig, crc32));
}

static void test_crc_detects_change(void) {
    AirConfig a = airDefaultConfig( );
    AirConfig b = airDefaultConfig( );
    b.idleTimeoutSec = (uint16_t)(a.idleTimeoutSec + 123);
    a.crc32 = airComputeCrc(a);
    b.crc32 = airComputeCrc(b);
    TEST_ASSERT_NOT_EQUAL(a.crc32, b.crc32);
}

int main(void) {
    UNITY_BEGIN( );
    RUN_TEST(test_default_config);
    RUN_TEST(test_crc_roundtrip);
    RUN_TEST(test_invalid_magic);
    RUN_TEST(test_invalid_version);
    RUN_TEST(test_invalid_crc);
    RUN_TEST(test_crc_is_tail);
    RUN_TEST(test_crc_detects_change);
    return UNITY_END( );
}
