/**
 * @file    test/test_sensors/bmx_scan_twin.cpp
 * @brief   The real BME280SensorDriver::scanFinalize( ), run on the host against a fake BMx280 probe.
 * @details The probe scan of the sensor page printed "GP255" for a BME280 or BMP280 because the firmware pushed a ScanResult with
 *          pin = 255 and nothing else (2026-11-06). The fix is three assignments in scanFinalize( ): the pair the probe drove
 *          (sda, scl) and the part that answered (the chip ID, isBME280( )). The cases of test_main.cpp build ScanResults by
 *          hand to pin the JSON, so they cannot see those assignments: deleting them kept every case green and the page printed
 *          GP255 again. This unit compiles the REAL driver (sensors/BME280SensorDriver.h, with BME280Driver.h under it) on the host,
 *          with BMx280PIO_RP2040 and Wire replaced by the stand-ins of test/native_stubs, and runs the real scanFinalize( ): the
 *          probe is built on the pair the result reports; a BMP280 and a BME280 come out as what they are; nothing is reported when
 *          nothing answers; and the whole answer, through the firmware's own formatter, is what the page reads.
 *          It is a unit of its own for the reason alpha_driver_twin.cpp is: the driver needs the Arduino calls the shared stub does
 *          not carry, answered here, and the cases that call it are in test_main.cpp.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <hardware/gpio.h>
#define SIMUT_DISPLAY_TFT 0   /* BME280Driver.h brings SensorDrawing.h, whose TFT half needs Adafruit_GFX; this unit has no panel */
#include "simut_config.h"     /* SIMUT_SENSOR_BME280 defaults to 1 here, before the driver's #if reads it */

/* What the driver calls and the shared Arduino.h stub does not carry. Nothing here runs in these cases (scanFinalize( ) does not
 * reach the bus code, which the compiler still needs to see); the definitions keep the link whole. */
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif
#ifndef HEX
#define HEX 16
#endif
static inline void pinMode(uint8_t, uint8_t) {}
static inline int  digitalRead(uint8_t) { return HIGH; }
static inline void digitalWrite(uint8_t, uint8_t) {}
static inline void delayMicroseconds(unsigned) {}

#include "sensors/BME280SensorDriver.h"
#include "sensors/ScanResultJson.h"

TwoWire Wire;
TwoWire Wire1;
namespace simut_native { uint32_t fake_millis_value = 0; }
FakeSerial Serial;

/* LogManager.cpp is not part of this suite. The driver's other paths (initSlot( ), getOrCreate( )) write log lines, and the linker
 * needs the three members those lines reach; scanFinalize( ) writes none, so they are a sink here. */
LogManager::LogManager( ) {}
const char* LogManager::tr(const char* en) const { return en; }
void LogManager::logCode(LogLevel, const char*, LogCode, int, String) {}
void LogPolicy::reset( ) {}   /* LogManager's constructor builds a LogPolicy; LogPolicy.cpp is not compiled in this suite either */

/* Runs the real scanFinalize( ) with a probe that answers (or not) as the test says, and returns what it pushed. */
static std::vector<ScanResult> scan(bool found, bool bme) {
    bmxProbe.reset( );
    bmxProbe.found = found;
    bmxProbe.bme = bme;
    std::vector<BME280Driver*> drivers;
    BME280SensorDriver driver(drivers);
    std::vector<ScanResult> out;
    driver.scanFinalize(out);
    return out;
}

/* The defect, at its source: the result carries the pair the probe was built on, so the page can print it. The pair is the
 * default I2C0 pins, GP4 and GP5, which chapter 6 of the manual names, and it is the one the probe drove, whichever the driver
 * holds: the two must be the same numbers. */
void test_bmx_scan_result_carries_the_pair_the_probe_drove(void) {
    std::vector<ScanResult> r = scan(true, false);
    TEST_ASSERT_EQUAL_UINT32(1, r.size( ));
    TEST_ASSERT_EQUAL_INT(1, bmxProbe.built);
    TEST_ASSERT_EQUAL_UINT8(bmxProbe.sda, r[0].sda);
    TEST_ASSERT_EQUAL_UINT8(bmxProbe.scl, r[0].scl);
    TEST_ASSERT_EQUAL_UINT8(4, r[0].sda);
    TEST_ASSERT_EQUAL_UINT8(5, r[0].scl);
    TEST_ASSERT_EQUAL_UINT8(BME280_ADDR_PRIMARY, bmxProbe.addr);
    TEST_ASSERT_EQUAL_UINT8(PIN_UNUSED, r[0].pin);   /* "no single GPIO", as before */
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(0, r[0].rom[i]);
}

/* The chip ID decides the type: a BMP280 (no humidity) is not reported as a BME280, which it always was. */
void test_bmx_scan_names_the_chip_that_answered(void) {
    std::vector<ScanResult> bmp = scan(true, false);
    TEST_ASSERT_EQUAL_UINT32(1, bmp.size( ));
    TEST_ASSERT_EQUAL_INT(TYPE_BMP280, bmp[0].type);
    std::vector<ScanResult> bme = scan(true, true);
    TEST_ASSERT_EQUAL_UINT32(1, bme.size( ));
    TEST_ASSERT_EQUAL_INT(TYPE_BME280, bme[0].type);
}

/* A probe that finds no chip reports nothing: no entry with a pair and a type for a bus with nothing on it. */
void test_bmx_scan_reports_nothing_when_nothing_answers(void) {
    std::vector<ScanResult> r = scan(false, true);
    TEST_ASSERT_EQUAL_UINT32(0, r.size( ));
    TEST_ASSERT_EQUAL_INT(1, bmxProbe.built);   /* it did look */
}

/* The whole answer the page reads, from the real scan through the firmware's own formatter. */
void test_bmx_scan_json_is_what_the_page_reads(void) {
    std::vector<ScanResult> r = scan(true, false);
    std::string doc;
    scanResultsJson(r, doc);
    TEST_ASSERT_EQUAL_STRING("{\"scanning\":false,\"found\":[{\"pin\":255,\"type\":5,\"rom\":\"\",\"name\":\"BMP280\",\"sda\":4,\"scl\":5}]}",
                             doc.c_str( ));
    doc.clear( );
    scanResultsJson(scan(false, false), doc);
    TEST_ASSERT_EQUAL_STRING("{\"scanning\":false,\"found\":[]}", doc.c_str( ));
}
