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
#include "sensors/BME280Driver.h"
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

 /* Channel arrays — one buffer + average + calibration per measurement axis.
  * [CH_TEMP]=0, [CH_HUM]=1, [CH_PRESS]=2, [CH_LUX]=3.
  * Inactive channels maintain NAN avgValue and empty buffers. */
 RingBuffer buffers[MAX_SENSOR_CHANNELS];
 float avgValue[MAX_SENSOR_CHANNELS];
 float calibrationOffset[MAX_SENSOR_CHANNELS];

 uint32_t lastReadTime;
 uint32_t readInterval;

 uint32_t totalReadings;
 uint8_t consecutiveErrors;
 uint8_t consecutiveSuccess;
 bool inErrorState;
 bool hardwareMismatch;

#if SIMUT_SENSOR_BME280
 int8_t  bmeDriverIdx = -1;  /**< Index into SensorManager::_bmeDrivers, -1 = not BME280 */
 uint8_t i2cAddr = 0;        /**< I2C address (0x76 or 0x77), 0 = unassigned */
#endif

 /** @return true if at least CH_TEMP buffer is full. */
 bool bufferFull() const { return buffers[CH_TEMP].full(); }
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

#if SIMUT_SENSOR_BME280
 void requestBmeReading( );
 bool readBmeBlocking(float &t, float &h, float &p);
#endif

 bool pollAsyncResult(String &msg);

#if SIMUT_SENSOR_DS18B20
 bool identifyPhysicalSensor(uint8_t gpio, uint8_t* romOut);
#endif

 void applyCalibration(uint8_t gpio, String newHwId, float offset, String newName);
 /* Apply temp AND humidity offset on first DHT22 sensor found.
 * hwId/name are handled via applyCalibration separately. */
 void applyAmbientCalibration(float offsetT, float offsetH);


 void setHardwareMismatch(uint8_t gpio, bool isMismatch);

private:
#if SIMUT_SENSOR_DS18B20
 DS18B20Driver _ds18;
#endif
#if SIMUT_SENSOR_DHT22
 DHT22Driver _dht;
#endif
#if SIMUT_SENSOR_BME280
 std::vector<BME280Driver*> _bmeDrivers;  /**< One driver per (sda,scl,addr) triplet */
 int8_t _getOrCreateBmeDriver(uint8_t sda, uint8_t scl, uint8_t addr);
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
#if SIMUT_SENSOR_BME280
 BME_SCAN_CHECK,
 BME_SCAN_WAIT,
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
