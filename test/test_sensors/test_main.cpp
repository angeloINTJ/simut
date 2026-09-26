/**
 * @file    test/test_sensors/test_main.cpp
 * @brief   Host-side tests for the sensor type table (SensorHelpers.h).
 * @details Runs via `pio test -e native_sensors` (no hardware). The sensor
 *   pipeline had NO native coverage; the feature-model seam (P2) turns the
 *   per-type metadata that four switch(SensorType) helpers used to carry under
 *   #if SIMUT_SENSOR_* into one data table (SENSOR_TYPE_TABLE), which is pure
 *   data and therefore host-testable. This suite pins that table's contract so
 *   the rest of the seam — moving each driver to its own .cpp, the scan and
 *   read dispatch onto the table — has a net under it.
 *
 *   Covered: sensorTypeName / sensorTypeEnabled / sensorDefaultIntervalMs,
 *   SensorFormat::forType (channel mask + pins), sensorValueCount /
 *   sensorHasChannel / sensorNthChannel / sensorLimitCount, and
 *   sensorHasSerialNumber. The expected values are what the switch statements
 *   returned before the table, so a regression in the table fails here.
 *
 *   <hardware/gpio.h> is included first for the host stub: SensorHelpers.h
 *   defines an inline gpioInitForRole( ) that names the Pico SDK gpio_* calls,
 *   which the compiler must resolve when it parses the header even though no
 *   test calls that function.
 *
 * @project SIMUT — feature model, P2 (sensor seam, increment 1)
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include <unity.h>
#include <cstring>
#include <hardware/gpio.h>
#include "sensors/SensorHelpers.h"

void setUp(void) {}
void tearDown(void) {}

/* Every profile ships all three families today, so enabled is true for each
 * driver type; a build compiling one out would flip its row's `enabled`. */
static void test_type_enabled(void) {
    TEST_ASSERT_TRUE(sensorTypeEnabled(TYPE_DS18B20));
    TEST_ASSERT_TRUE(sensorTypeEnabled(TYPE_DHT22));
    TEST_ASSERT_TRUE(sensorTypeEnabled(TYPE_BME280));
    TEST_ASSERT_TRUE(sensorTypeEnabled(TYPE_BMP280));
    TEST_ASSERT_FALSE(sensorTypeEnabled(TYPE_NONE));
    TEST_ASSERT_FALSE(sensorTypeEnabled(TYPE_UNKNOWN_ACTIVITY));
}

static void test_type_name(void) {
    TEST_ASSERT_EQUAL_STRING("DS18B20", sensorTypeName(TYPE_DS18B20));
    TEST_ASSERT_EQUAL_STRING("DHT22",   sensorTypeName(TYPE_DHT22));
    TEST_ASSERT_EQUAL_STRING("BME280",  sensorTypeName(TYPE_BME280));
    TEST_ASSERT_EQUAL_STRING("BMP280",  sensorTypeName(TYPE_BMP280));
    /* TYPE_NONE and anything without a row answer "Unknown". */
    TEST_ASSERT_EQUAL_STRING("Unknown", sensorTypeName(TYPE_NONE));
    TEST_ASSERT_EQUAL_STRING("Unknown", sensorTypeName(TYPE_UNKNOWN_ACTIVITY));
}

static void test_default_interval(void) {
    TEST_ASSERT_EQUAL_UINT32(1000, sensorDefaultIntervalMs(TYPE_DS18B20));
    TEST_ASSERT_EQUAL_UINT32(2000, sensorDefaultIntervalMs(TYPE_DHT22));
    TEST_ASSERT_EQUAL_UINT32(5000, sensorDefaultIntervalMs(TYPE_BME280));
    TEST_ASSERT_EQUAL_UINT32(5000, sensorDefaultIntervalMs(TYPE_BMP280));
    /* Unknown falls back to the slow default. */
    TEST_ASSERT_EQUAL_UINT32(5000, sensorDefaultIntervalMs(TYPE_NONE));
}

static void test_channel_masks(void) {
    TEST_ASSERT_EQUAL_UINT8((1u << CH_TEMP),
                            SensorFormat::forType(TYPE_DS18B20).channelMask);
    TEST_ASSERT_EQUAL_UINT8((1u << CH_TEMP) | (1u << CH_HUM),
                            SensorFormat::forType(TYPE_DHT22).channelMask);
    TEST_ASSERT_EQUAL_UINT8((1u << CH_TEMP) | (1u << CH_HUM) | (1u << CH_PRESS),
                            SensorFormat::forType(TYPE_BME280).channelMask);
    /* The BMP280 hole at CH_HUM is the whole point of the mask. */
    TEST_ASSERT_EQUAL_UINT8((1u << CH_TEMP) | (1u << CH_PRESS),
                            SensorFormat::forType(TYPE_BMP280).channelMask);
    TEST_ASSERT_FALSE(SensorFormat::forType(TYPE_BMP280).hasChannel(CH_HUM));
    TEST_ASSERT_TRUE(SensorFormat::forType(TYPE_DHT22).hasChannel(CH_HUM));
    /* Unknown falls back to a single temperature channel. */
    TEST_ASSERT_EQUAL_UINT8((1u << CH_TEMP),
                            SensorFormat::forType(TYPE_NONE).channelMask);
}

static void test_value_and_limit_counts(void) {
    TEST_ASSERT_EQUAL_UINT8(1, sensorValueCount(TYPE_DS18B20));
    TEST_ASSERT_EQUAL_UINT8(2, sensorValueCount(TYPE_DHT22));
    TEST_ASSERT_EQUAL_UINT8(3, sensorValueCount(TYPE_BME280));
    TEST_ASSERT_EQUAL_UINT8(2, sensorValueCount(TYPE_BMP280));
    /* Two editable limits (min + max) per reported channel. */
    TEST_ASSERT_EQUAL_UINT8(4, sensorLimitCount(TYPE_DHT22));
    TEST_ASSERT_EQUAL_UINT8(6, sensorLimitCount(TYPE_BME280));
    TEST_ASSERT_EQUAL_UINT8(4, sensorLimitCount(TYPE_BMP280));
}

static void test_nth_channel(void) {
    /* A BMP280 asked for row 0 and row 1 gets temperature and pressure — never
     * a humidity row it cannot fill. */
    TEST_ASSERT_EQUAL_UINT8(CH_TEMP,  sensorNthChannel(TYPE_BMP280, 0));
    TEST_ASSERT_EQUAL_UINT8(CH_PRESS, sensorNthChannel(TYPE_BMP280, 1));
    TEST_ASSERT_EQUAL_UINT8(CH_COUNT, sensorNthChannel(TYPE_BMP280, 2));
    TEST_ASSERT_EQUAL_UINT8(CH_TEMP,  sensorNthChannel(TYPE_DHT22, 0));
    TEST_ASSERT_EQUAL_UINT8(CH_HUM,   sensorNthChannel(TYPE_DHT22, 1));
}

static void test_pin_map(void) {
    SensorFormat ds = SensorFormat::forType(TYPE_DS18B20);
    TEST_ASSERT_EQUAL_UINT8(1, ds.pinCount);
    TEST_ASSERT_EQUAL_INT(ROLE_DATA, ds.pins[0].role);
    TEST_ASSERT_EQUAL_STRING("1-Wire", ds.pins[0].label);
    TEST_ASSERT_EQUAL_UINT8(FLAG_PULLUP, ds.pins[0].flags);

    SensorFormat bme = SensorFormat::forType(TYPE_BME280);
    TEST_ASSERT_EQUAL_UINT8(2, bme.pinCount);
    TEST_ASSERT_EQUAL_INT(ROLE_I2C_SDA, bme.pins[0].role);
    TEST_ASSERT_EQUAL_INT(ROLE_I2C_SCL, bme.pins[1].role);
    TEST_ASSERT_EQUAL_STRING("SDA", bme.pins[0].label);
    TEST_ASSERT_EQUAL_STRING("SCL", bme.pins[1].label);

    /* Unknown fallback keeps flags 0 (not FLAG_PULLUP) — the pre-table default. */
    SensorFormat none = SensorFormat::forType(TYPE_NONE);
    TEST_ASSERT_EQUAL_UINT8(1, none.pinCount);
    TEST_ASSERT_EQUAL_UINT8(0, none.pins[0].flags);
}

static void test_serial_number(void) {
    /* Only the DS18B20 carries a factory ROM the firmware can read back and
     * verify; adopt/verify flows must not be offered for the others. */
    TEST_ASSERT_TRUE(sensorHasSerialNumber(TYPE_DS18B20));
    TEST_ASSERT_FALSE(sensorHasSerialNumber(TYPE_DHT22));
    TEST_ASSERT_FALSE(sensorHasSerialNumber(TYPE_BME280));
    TEST_ASSERT_FALSE(sensorHasSerialNumber(TYPE_BMP280));
}

static void test_table_shape(void) {
    /* Four driver rows, one per real type; each row's lookup finds itself and
     * TYPE_NONE finds none. */
    const size_t n = sizeof(SENSOR_TYPE_TABLE) / sizeof(SENSOR_TYPE_TABLE[0]);
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_NOT_NULL(sensorTypeDef(TYPE_DS18B20));
    TEST_ASSERT_NOT_NULL(sensorTypeDef(TYPE_BMP280));
    TEST_ASSERT_NULL(sensorTypeDef(TYPE_NONE));
    TEST_ASSERT_NULL(sensorTypeDef(TYPE_UNKNOWN_ACTIVITY));
    for (const SensorTypeDef& d : SENSOR_TYPE_TABLE) {
        TEST_ASSERT_EQUAL_PTR(&d, sensorTypeDef(d.type));
    }
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_type_enabled);
    RUN_TEST(test_type_name);
    RUN_TEST(test_default_interval);
    RUN_TEST(test_channel_masks);
    RUN_TEST(test_value_and_limit_counts);
    RUN_TEST(test_nth_channel);
    RUN_TEST(test_pin_map);
    RUN_TEST(test_serial_number);
    RUN_TEST(test_table_shape);
    return UNITY_END();
}
