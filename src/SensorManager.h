/**
 * @file SensorManager.h
 * @brief Sensor orchestration layer — runtime sensor list, periodic reads, scan.
 * @details Manages runtime sensor instances with static ring buffers for
 * moving average calculation (trimmed mean), asynchronous reading
 * state machines, hardware scan across GPIO pins, ROM verification,
 * calibration offset application, and hardware mismatch detection.
 *
 * Hardware-specific drivers live in src/sensors/ (DS18B20Driver, DHT22Driver).
 * Sensor types are conditionally compiled via SIMUT_SENSOR_* flags
 * (see sensors/SensorConfig.h). Disable unused types to reclaim flash.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include "SystemDefs.h"
#include "sensors/DS18B20Driver.h"
#include "sensors/DHT22Driver.h"
#include "LogManager.h"
#include "sensors/SensorHelpers.h"


struct RingBuffer {
 float data[MOVING_AVG_WINDOW];
 uint8_t head = 0;
 uint8_t count = 0;

 void push(float v) {
 data[head] = v;
 head = (head + 1) % MOVING_AVG_WINDOW;
 if (count < MOVING_AVG_WINDOW) count++;
 }

 void clear( ) { head = 0; count = 0; }

 bool empty( ) const { return count == 0; }
 bool full( ) const { return count >= MOVING_AVG_WINDOW; }
 uint8_t size( ) const { return count; }


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
 float calibrationOffsetHum; /* humidity offset (ambient/DHT22 only). */

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
 SensorManager( );
 void begin( );
 void update( );


 void initRuntimeSensors(const SystemConfig &cfg);


 void syncAlarmLimits(const SystemConfig &cfg);


 bool hasNewReadings( );
 const std::vector<RuntimeSensor>& getRuntimeSensors( ) const;


 void startScan( );
 bool isScanning( );
 bool getScanResults(std::vector<ScanResult> &results);

#if SIMUT_SENSOR_DS18B20
 bool setDs18Resolution(DS18B20PIO::Resolution res);
 void requestDs18Reading( );
 bool readDs18(float &temp);
#endif

#if SIMUT_SENSOR_DHT22
 void requestDhtReading( );
 bool readDhtBlocking(float &t, float &h);
#endif

 bool pollAsyncResult(String &msg);

#if SIMUT_SENSOR_DS18B20
 bool identifyPhysicalSensor(uint8_t gpio, uint8_t* romOut);
#endif

 void applyCalibration(uint8_t gpio, String newHwId, float offset, String newName);
 /* Apply temp AND humidity offset on ambient (PIN_DHT_DEFAULT). hwId/name
 * are handled separately via applyCalibration (they share only the 't' line
 * from calib.csv — option B). */
 void applyAmbientCalibration(float offsetT, float offsetH);


 void setHardwareMismatch(uint8_t gpio, bool isMismatch);

private:
#if SIMUT_SENSOR_DS18B20
 DS18B20Driver _ds18;
#endif
#if SIMUT_SENSOR_DHT22
 DHT22Driver _dht;
#endif

 std::vector<RuntimeSensor> _runtimeSensors;
 volatile bool _newDataAvailable = false;


 enum ScanState {
 IDLE,
 SETUP_PIN,
#if SIMUT_SENSOR_DS18B20
 ONEWIRE_RESET,
 ONEWIRE_WAIT,
#endif
#if SIMUT_SENSOR_DHT22
 DHT_REQUEST,
 DHT_WAIT,
#endif
 NEXT_PIN,
 COMPLETE
 };
 ScanState _scanState = IDLE;
 uint8_t _currentScanPin = 0;
 uint32_t _scanTimer = 0;
 std::vector<ScanResult> _scanResults;


 void processPeriodicReads( );
 void addSample(RuntimeSensor &sensor, float v1, float v2);

 void handleSensorResult(RuntimeSensor &s, bool success, float v1, float v2, const char* errorMsg);
};
