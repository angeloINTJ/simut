/**
 * @file DHT22Driver.h
 * @brief DHT22 single-wire sensor driver using PIO state machines.
 * @details Wraps DHTBus and DHT22PIO_RP2040 libraries.
 * Compiled only when SIMUT_SENSOR_DHT22=1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DHT22

#include <Arduino.h>
#include "DHTBus.h"
#include "DHT22PIO.h"

#define PIN_DHT_DEFAULT 10

struct DHT22Driver {
    DHTBus    bus;
    DHT22PIO  sensor;

    enum State {
        DHT_IDLE,
        DHT_WAITING
    };
    State    state = DHT_IDLE;
    int      currentSensorIdx = -1;
    uint32_t timer = 0;

    DHT22Driver( )
        : bus(pio1),
          sensor(bus)
    {}

    void begin( ) {
        bus.begin(PIN_DHT_DEFAULT);
        gpio_set_pulls(PIN_DHT_DEFAULT, true, false);
    }

    void requestReading(uint8_t gpio) {
        sensor.requestReading(gpio);
    }

    void update( ) {
        sensor.update( );
    }

    DHT22PIO::State getState( ) {
        return sensor.getState( );
    }

    bool getResults(float& t, float& h) {
        return sensor.getResults(t, h);
    }

    void reset( ) {
        sensor.reset( );
    }
};

#else
#define PIN_DHT_DEFAULT 255
#endif /* SIMUT_SENSOR_DHT22 */
