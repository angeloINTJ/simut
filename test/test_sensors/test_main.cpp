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

/* ── Hardware I2C pin selection (i2cPeripheralForPins / i2cPeripheralFor) ──
 * Every GPIO has ONE role per controller: GPn is SDA of I2C0 when n%4 == 0,
 * SCL of I2C0 when n%4 == 1, SDA of I2C1 when n%4 == 2 and SCL of I2C1 when
 * n%4 == 3. Answering a peripheral for a pair the framework then refuses is
 * not an error path: TwoWire::setSDA( ) on an illegal pin is a panic( ), on
 * every boot, before the console exists. */

static void test_i2c_hw_pairs_by_role(void) {
    const uint8_t i2c0[][2] = { {0,1}, {4,5}, {8,9}, {12,13}, {16,17}, {20,21} };
    const uint8_t i2c1[][2] = { {2,3}, {6,7}, {10,11}, {14,15}, {18,19}, {26,27} };
    for (auto& p : i2c0) TEST_ASSERT_EQUAL_INT(0, i2cPeripheralForPins(p[0], p[1]));
    for (auto& p : i2c1) TEST_ASSERT_EQUAL_INT(1, i2cPeripheralForPins(p[0], p[1]));
    /* Any SDA of a controller pairs with any of its SCLs: the function select
     * is per pin, and the framework accepts each one on its own. */
    TEST_ASSERT_EQUAL_INT(0, i2cPeripheralForPins(4, 9));
    TEST_ASSERT_EQUAL_INT(0, i2cPeripheralForPins(0, 13));
    TEST_ASSERT_EQUAL_INT(1, i2cPeripheralForPins(2, 15));
}

/* The boot-lock: SDA and SCL wired (and configured) the other way round. Both
 * pins belong to the controller, so a check by pin SET said 0, and
 * Wire.setSDA(5) panicked. Swapped roles have no hardware path; the driver
 * takes the bit-bang path, which drives any two GPIOs. */
static void test_i2c_swapped_roles_have_no_hw_path(void) {
    const uint8_t swapped[][2] = { {1,0}, {5,4}, {9,8}, {13,12},
                                   {3,2}, {7,6}, {11,10}, {15,14} };
    for (auto& p : swapped) TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(p[0], p[1]));
}

static void test_i2c_no_hw_path_otherwise(void) {
    /* One pin of each controller. */
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(4, 3));
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(2, 5));
    /* The same pin twice: the CLI allowed it inside one slot, and setSCL(5)
     * on a bus whose SDA is 5 is the same panic. */
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(5, 5));
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(4, 4));
    /* Unassigned or out of range: `1u << 255` is undefined behaviour, not -1. */
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(PIN_UNUSED, 5));
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(4, PIN_UNUSED));
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralForPins(32, 1));
}

/* One controller drives one SDA/SCL pair. Two 280s on different pairs of the
 * same controller used to share it: the second slot's driver ran on the bus
 * the first had muxed — same address, the other sensor's wires — and read the
 * first sensor as its own. The second pair now takes the bit-bang path. */
static void test_i2c_bound_controller_keeps_its_pair(void) {
    I2cBinding bound[2];
    TEST_ASSERT_EQUAL_INT(0, i2cPeripheralFor(4, 5, bound));   /* free: may bind */
    bound[0].sda = 4; bound[0].scl = 5;                         /* what the driver does */
    TEST_ASSERT_EQUAL_INT(0,  i2cPeripheralFor(4, 5, bound));  /* its own pair */
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralFor(8, 9, bound));  /* I2C0, other pins */
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralFor(4, 9, bound));  /* shares SDA only */
    TEST_ASSERT_EQUAL_INT(1,  i2cPeripheralFor(2, 3, bound));  /* I2C1 still free */
    bound[1].sda = 2; bound[1].scl = 3;
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralFor(6, 7, bound));
    TEST_ASSERT_EQUAL_INT(-1, i2cPeripheralFor(5, 4, bound));  /* no hw path at all */
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_i2c_hw_pairs_by_role);
    RUN_TEST(test_i2c_swapped_roles_have_no_hw_path);
    RUN_TEST(test_i2c_no_hw_path_otherwise);
    RUN_TEST(test_i2c_bound_controller_keeps_its_pair);
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
