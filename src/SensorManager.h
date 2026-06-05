/**
 * @file SensorManager.h
 * @brief Sensor driver layer with PIO-based DS18B20 and DHT22 support.
 * @details Manages runtime sensor instances with static ring buffers for
 * moving average calculation (trimmed mean), asynchronous reading
 * state machines, hardware scan across GPIO pins, ROM verification,
 * calibration offset application, and hardware mismatch detection.
 * All PIO operations use custom libraries: OneWirePIO_RP2040 and
 * DHT22PIO_RP2040.
 *
 * Sensor types are conditionally compiled via SIMUT_SENSOR_* flags
 * (see SensorConfig.h). Disable unused types to reclaim flash.
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
#if SIMUT_SENSOR_DS18B20
#include "OneWirePIO.h"
#include "DS18B20PIO.h"
#endif
#if SIMUT_SENSOR_DHT22
#include "DHTBus.h"
#include "DHT22PIO.h"
#endif
#include "LogManager.h"

#if SIMUT_SENSOR_DS18B20
#define PIN_ONEWIRE_DEFAULT 0
#else
#define PIN_ONEWIRE_DEFAULT 255  /* unused when DS18B20 disabled */
#endif
#if SIMUT_SENSOR_DHT22
#define PIN_DHT_DEFAULT 10
#else
#define PIN_DHT_DEFAULT 255  /* unused when DHT22 disabled */
#endif


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

/* ===========================================================================
 * Compile-time sensor type helpers — inline, zero-cost.
 *
 * These centralize all SensorType-dependent logic so that consumer code
 * never needs #if SIMUT_SENSOR_* guards. The compiler constant-folds the
 * flag checks and dead-code-eliminates unreachable branches.
 * =========================================================================== */

/** @return true if this sensor type produces a humidity/secondary value. */
inline bool sensorHasHumidity(SensorType t) {
#if SIMUT_SENSOR_DHT22
 if (t == TYPE_DHT22) return true;
#endif
#if SIMUT_SENSOR_BME280
 if (t == TYPE_BME280) return true;
#endif
 (void)t;
 return false;
}

/** @return human-readable sensor type name (e.g. "DS18B20", "DHT22"). */
inline const char* sensorTypeName(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return "DS18B20";
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return "DHT22";
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return "BME280";
#endif
 default:           return "Unknown";
 }
}

/** @return number of measurement values this sensor produces (1, 2, or 3). */
inline uint8_t sensorValueCount(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return 1;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return 2;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return 3;
#endif
 default:           return 1;
 }
}

/** @return true if this sensor type is compiled-in and available. */
inline bool sensorTypeEnabled(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return true;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return true;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return true;
#endif
 default:           return false;
 }
}

/** @return the default read interval in ms for this sensor type. */
inline uint32_t sensorDefaultIntervalMs(SensorType t) {
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20: return 1000;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:   return 2000;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:  return 5000;
#endif
 default:           return 5000;
 }
}

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
 OneWirePIO _oneWireBus;
 DS18B20PIO _ds18Sensor;
#endif
#if SIMUT_SENSOR_DHT22
 DHTBus _dhtBus;
 DHT22PIO _dhtSensor;
#endif

 std::vector<RuntimeSensor> _runtimeSensors;
 volatile bool _newDataAvailable = false;

#if SIMUT_SENSOR_DS18B20
 enum Ds18State {
 DS_IDLE,
 DS_WAITING
 };
 Ds18State _dsState = DS_IDLE;
 uint32_t _dsTimer = 0;
 /* DS18B20_CONVERSION_TIME_MS defined in SystemDefs.h. */
#endif

#if SIMUT_SENSOR_DHT22
 enum DhtState {
 DHT_IDLE,
 DHT_WAITING
 };
 DhtState _dhtState = DHT_IDLE;
 int _dhtCurrentSensorIdx = -1;
 uint32_t _dhtTimer = 0;
#endif


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
#if SIMUT_SENSOR_DS18B20
 bool checkRomMatch(const uint8_t* romRead, const uint8_t* romConfig);
#endif
};
