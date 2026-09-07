/**
 * @file    test/test_air_config/test_main.cpp
 * @brief   Host-side tests of the SIMUT Air persistent config (air/AirConfig.h).
 * @details Runs via: pio test -e native_air (no HW).
 *
 * @project SIMUT Air
 * @license MIT License
 */

#include <unity.h>
#include <string.h>
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

/* The struct's size did not change and neither did its version, so a v2 file
 * written before chargerPin existed still loads and still passes the CRC. What
 * lands in the new byte is the low half of the old wifiScanTimeoutMs, which
 * nothing ever wrote: it is always the 4000 ms default, i.e. 160 here.
 *
 * That number is the whole migration. If it were ever a valid GPIO the device
 * would silently sense the charger on a pin nobody wired, so this test pins it
 * from the value an old file actually holds — not from a constant. */
static void test_charger_pin_from_legacy_field(void) {
    AirConfig c = airDefaultConfig( );
    const uint16_t legacy = 4000;           /* the only value any v2 file holds */
    memcpy(&c.chargerPin, &legacy, sizeof(legacy));
    TEST_ASSERT_EQUAL_UINT8(160, c.chargerPin);
    TEST_ASSERT_FALSE(airPinValid(c.chargerPin));
    airSanitise(c);
#if SIMUT_AIR
    TEST_ASSERT_EQUAL_UINT8(AIR_CHARGER_PIN, c.chargerPin);
#else
    TEST_ASSERT_EQUAL_UINT8(PIN_UNUSED, c.chargerPin);
#endif
    TEST_ASSERT_EQUAL_UINT8(0, c.chargerRsv);
}

/* A pin the operator set stays set: sanitising is for what a file cannot be
 * trusted to carry, not a second chance to override a deliberate choice. */
static void test_charger_pin_bounds(void) {
    TEST_ASSERT_TRUE(airPinValid(0));
    TEST_ASSERT_TRUE(airPinValid(29));
    TEST_ASSERT_TRUE(airPinValid(PIN_UNUSED));
    TEST_ASSERT_FALSE(airPinValid(30));
    TEST_ASSERT_FALSE(airPinValid(254));

    AirConfig c = airDefaultConfig( );
    c.chargerPin = 22;
    airSanitise(c);
    TEST_ASSERT_EQUAL_UINT8(22, c.chargerPin);

    c.chargerPin = PIN_UNUSED;              /* charger sense switched off */
    airSanitise(c);
    TEST_ASSERT_EQUAL_UINT8(PIN_UNUSED, c.chargerPin);
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
    RUN_TEST(test_charger_pin_from_legacy_field);
    RUN_TEST(test_charger_pin_bounds);
    return UNITY_END( );
}
