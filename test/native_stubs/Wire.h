/**
 * @file    test/native_stubs/Wire.h
 * @brief   Wire.h for the host: the two I2C controllers the sensor drivers name, and nothing they could drive.
 * @details BME280SensorDriver.h keeps a reference to Wire or Wire1 and re-muxes it after a scan. On the host there is no bus: the calls
 *   are inert, and the objects are defined by the unit that compiles the driver (test/test_sensors/bmx_scan_twin.cpp).
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include <stdint.h>

class TwoWire {
public:
    void begin( ) {}
    void end( ) {}
    void setSDA(uint8_t) {}
    void setSCL(uint8_t) {}
};
extern TwoWire Wire;
extern TwoWire Wire1;
