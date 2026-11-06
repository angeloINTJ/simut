/**
 * @file    test/native_stubs/BMx280PIO_RP2040.h
 * @brief   A stand-in for the BMx280PIO_RP2040 library, for the host: the probe that BME280SensorDriver::scanFinalize( ) builds.
 * @details The real class (github.com/angeloINTJ/BMx280PIO_RP2040, a lib_dep of the firmware images) drives PIO or the I2C
 *   controller. Here a probe writes down the pins and the address it was built with, in `bmxProbe`, and answers what the test
 *   put there: whether begin( ) finds a chip, and whether it is a BME280 (the chip ID 0x60) or a BMP280 (0x58), which the real
 *   class keeps in isBME280( ) once begin( ) has read it. The calls the driver's other paths make are here and inert, so that all
 *   of BME280Driver.h and BME280SensorDriver.h compile on the host.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include <stdint.h>
#include <Wire.h>

#define BME280_ADDR_PRIMARY  0x76
#define BME280_MODE_FORCED   1

struct BmxProbeScript {
    bool    found = false;   /* what begin( ) answers */
    bool    bme   = false;   /* isBME280( ) after a begin( ) that found a chip: true = BME280, false = BMP280 */
    int     built = 0;       /* probes built on a pin pair since reset( ) */
    uint8_t sda = 0xEE, scl = 0xEE, addr = 0xEE;   /* the arguments of the last pin-pair probe: the pair it drove */
    void reset( ) { *this = BmxProbeScript( ); }
};
inline BmxProbeScript bmxProbe;

class BMx280PIO_RP2040 {
public:
    BMx280PIO_RP2040(TwoWire&, uint8_t = BME280_ADDR_PRIMARY) {}
    BMx280PIO_RP2040(uint8_t sda, uint8_t scl, uint8_t addr = BME280_ADDR_PRIMARY) {
        bmxProbe.built++;
        bmxProbe.sda = sda; bmxProbe.scl = scl; bmxProbe.addr = addr;
    }
    bool begin( ) { _init = bmxProbe.found; return _init; }
    bool isBME280( ) const { return _init && bmxProbe.bme; }
    uint8_t getChipID( ) { return _init ? (bmxProbe.bme ? 0x60 : 0x58) : 0; }
    void forceGPIO(bool) {}
    void setMode(uint8_t) {}
    bool readAll(float*, float*, float*) { return false; }
private:
    bool _init = false;
};
