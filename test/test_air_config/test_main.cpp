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
    TEST_ASSERT_TRUE(airPinValid(22));
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

/* V-06. 23/24/25/29 are the CYW43 side band on a Pico W — WL_ON, the shared
 * SPI data line, the chip select that also drives the LED, and ADC3/VSYS.
 * The old rule was `pin <= 29`, which accepted every one of them, so `air
 * charger 25` was a valid command that took the radio down on a device with
 * nobody watching the console.
 *
 * Written over the whole 0..255 domain rather than the four values: this is a
 * denylist, and a denylist tested only at its own entries cannot tell you
 * whether it also started refusing something legitimate. */
static void test_pin_denylist_cyw43(void) {
    TEST_ASSERT_FALSE(airPinValid(23));
    TEST_ASSERT_FALSE(airPinValid(24));
    TEST_ASSERT_FALSE(airPinValid(25));
    TEST_ASSERT_FALSE(airPinValid(29));
    for (unsigned p = 0; p <= 255; p++) {
        bool expected = (p == PIN_UNUSED)
                        || (p <= 29 && p != 23 && p != 24 && p != 25 && p != 29);
        TEST_ASSERT_EQUAL_MESSAGE(expected, airPinValid((uint8_t)p),
                                  "airPinValid disagrees with the denylist");
    }
}

/* A denied pin must not survive a load. air.bin is a file: it can be restored
 * from a backup written before this rule existed, or hand-forged with a valid
 * CRC. airSanitise is the only thing standing between that file and the GPIO,
 * and it already calls airPinValid — this test is what keeps that wiring from
 * being quietly removed. */
static void test_sanitise_resets_denied_pin(void) {
    AirConfig c = airDefaultConfig( );
    c.chargerPin = 25;                 /* CYW43 chip select / onboard LED */
    airSanitise(c);
    TEST_ASSERT_NOT_EQUAL(25, c.chargerPin);
    TEST_ASSERT_TRUE(airPinValid(c.chargerPin));

    c.chargerPin = 23;
    airSanitise(c);
    TEST_ASSERT_TRUE(airPinValid(c.chargerPin));
}

/* Plan F09. The value that matters is 65536: it used to be accepted and cast
 * to zero, and an idle timeout of zero hibernates the device on the next loop
 * pass — recoverable only by catching a wake window on the console. */
static void test_idle_sec_bounds(void) {
    TEST_ASSERT_FALSE(airIdleSecValid(0));
    TEST_ASSERT_FALSE(airIdleSecValid(9));
    TEST_ASSERT_TRUE(airIdleSecValid(10));
    TEST_ASSERT_TRUE(airIdleSecValid(300));
    TEST_ASSERT_TRUE(airIdleSecValid(65535));
    TEST_ASSERT_FALSE(airIdleSecValid(65536));   /* casts to 0 */
    TEST_ASSERT_FALSE(airIdleSecValid(86400));   /* casts to 20864 */
    TEST_ASSERT_FALSE(airIdleSecValid(-1));
    /* Every accepted value survives the cast the config field applies. */
    for (long s = 10; s <= 65535; s += 4095) {
        if (airIdleSecValid(s)) {
            TEST_ASSERT_EQUAL_UINT16(s, (uint16_t)s);
        }
    }
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
    RUN_TEST(test_pin_denylist_cyw43);
    RUN_TEST(test_sanitise_resets_denied_pin);
    RUN_TEST(test_idle_sec_bounds);
    return UNITY_END( );
}
