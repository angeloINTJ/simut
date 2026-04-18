/**
 * @file    SensorManager.h
 * @brief   Sensor driver layer with PIO-based DS18B20 and DHT22 support.
 * @details Manages runtime sensor instances with static ring buffers for
 * moving average calculation (trimmed mean), asynchronous reading
 * state machines, hardware scan across GPIO pins, ROM verification,
 * calibration offset application, and hardware mismatch detection.
 * All PIO operations use custom libraries: OneWirePIO_RP2040 and
 * DHT22PIO_RP2040.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include "SystemDefs.h"
#include "OneWirePIO.h"
#include "DS18B20PIO.h"
#include "DHTBus.h"
#include "DHT22PIO.h"
#include "LogManager.h"

#define PIN_ONEWIRE_DEFAULT 0
#define PIN_DHT_DEFAULT     10


struct RingBuffer {
    float data[MOVING_AVG_WINDOW];
    uint8_t head  = 0;
    uint8_t count = 0;

    void push(float v) {
        data[head] = v;
        head = (head + 1) % MOVING_AVG_WINDOW;
        if (count < MOVING_AVG_WINDOW) count++;
    }

    void clear() { head = 0; count = 0; }

    bool empty() const { return count == 0; }
    bool full()  const { return count >= MOVING_AVG_WINDOW; }
    uint8_t size() const { return count; }


    void copyTo(float* dst) const {
        if (count == 0) return;
        uint8_t start = (head >= count) ? (head - count) : (MOVING_AVG_WINDOW - (count - head));
        for (uint8_t i = 0; i < count; i++) {
            dst[i] = data[(start + i) % MOVING_AVG_WINDOW];
        }
    }
};


struct RuntimeSensor {
    SensorRecord config;
    SensorType type;

    RingBuffer buffer1;
    RingBuffer buffer2;
    float avgValue1;
    float avgValue2;
    bool bufferFull;
    float calibrationOffset;

    uint32_t lastReadTime;
    uint32_t readInterval;

    uint32_t totalReadings;
    uint8_t consecutiveErrors;
    uint8_t consecutiveSuccess;
    bool inErrorState;
    bool hardwareMismatch;
};

class SensorManager {
public:
    SensorManager();
    void begin();
    void update();


    void initRuntimeSensors(const SystemConfig &cfg);


    void syncAlarmLimits(const SystemConfig &cfg);


    bool hasNewReadings();
    const std::vector<RuntimeSensor>& getRuntimeSensors() const;


    void startScan();
    bool isScanning();
    bool getScanResults(std::vector<ScanResult> &results);


    bool setDs18Resolution(DS18B20PIO::Resolution res);
    void requestDs18Reading();
    bool readDs18(float &temp);

    void requestDhtReading();
    bool readDhtBlocking(float &t, float &h);

    bool pollAsyncResult(String &msg);


    bool identifyPhysicalSensor(uint8_t gpio, uint8_t* romOut);

    void applyCalibration(uint8_t gpio, String newHwId, float offset, String newName);


    void setHardwareMismatch(uint8_t gpio, bool isMismatch);

private:

    OneWirePIO _oneWireBus;
    DS18B20PIO _ds18Sensor;
    DHTBus     _dhtBus;
    DHT22PIO   _dhtSensor;

    std::vector<RuntimeSensor> _runtimeSensors;
    volatile bool _newDataAvailable = false;


    enum Ds18State {
        DS_IDLE,
        DS_WAITING
    };
    Ds18State _dsState = DS_IDLE;
    uint32_t _dsTimer = 0;
    const uint32_t DS_CONVERSION_TIME = 750;


    enum DhtState {
        DHT_IDLE,
        DHT_WAITING
    };
    DhtState _dhtState = DHT_IDLE;
    int _dhtCurrentSensorIdx = -1;
    uint32_t _dhtTimer = 0;


    enum ScanState {
        IDLE,
        SETUP_PIN,
        ONEWIRE_RESET,
        ONEWIRE_WAIT,
        DHT_REQUEST,
        DHT_WAIT,
        NEXT_PIN,
        COMPLETE
    };
    ScanState _scanState = IDLE;
    uint8_t _currentScanPin = 0;
    uint32_t _scanTimer = 0;
    std::vector<ScanResult> _scanResults;


    void processPeriodicReads();
    void addSample(RuntimeSensor &sensor, float v1, float v2);

    void handleSensorResult(RuntimeSensor &s, bool success, float v1, float v2, const char* errorMsg);
    bool checkRomMatch(const uint8_t* romRead, const uint8_t* romConfig);
};
