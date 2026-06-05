/**
 * @file DS18B20Driver.h
 * @brief DS18B20 1-Wire sensor driver using PIO state machines.
 * @details Wraps OneWirePIO_RP2040 and DS18B20PIO_RP2040 libraries.
 * Compiled only when SIMUT_SENSOR_DS18B20=1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DS18B20

#include <Arduino.h>
#include "OneWirePIO.h"
#include "DS18B20PIO.h"

#define PIN_ONEWIRE_DEFAULT 0

struct DS18B20Driver {
    OneWirePIO  bus;
    DS18B20PIO  sensor;

    enum State {
        DS_IDLE,
        DS_WAITING
    };
    State    state = DS_IDLE;
    uint32_t timer = 0;

    DS18B20Driver( )
        : bus(pio0),
          sensor(bus)
    {}

    void begin( ) {
        bus.begin(PIN_ONEWIRE_DEFAULT);
    }

    bool readROM(uint8_t gpio, uint8_t* romOut) {
        return sensor.readROM(gpio, romOut);
    }

    bool isSensorPresent( ) {
        return bus.isSensorPresent( );
    }

    void setPin(uint8_t gpio) {
        bus.setPin(gpio);
    }

    void sendReset( ) {
        bus.sendReset( );
    }

    bool setResolution(DS18B20PIO::Resolution res) {
        return sensor.setResolution(PIN_ONEWIRE_DEFAULT, res);
    }

    void requestTemperatures(uint8_t gpio) {
        sensor.requestTemperatures(gpio);
    }

    bool getTemperatureValidated(uint8_t gpio, float& tempC) {
        return sensor.getTemperatureValidated(gpio, tempC, DS18B20PIO::CELSIUS);
    }

    bool checkRomMatch(const uint8_t* romRead, const uint8_t* romConfig) {
        for (int i = 0; i < 8; i++) {
            if (romRead[i] != romConfig[i]) return false;
        }
        return true;
    }
};

#else
#define PIN_ONEWIRE_DEFAULT 255
#endif /* SIMUT_SENSOR_DS18B20 */
