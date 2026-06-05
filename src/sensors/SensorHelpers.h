/**
 * @file SensorHelpers.h
 * @brief Compile-time sensor type helpers — inline, zero-cost.
 * @details Centralizes all SensorType-dependent logic so that consumer code
 * never needs #if SIMUT_SENSOR_* guards. The compiler constant-folds the
 * flag checks and dead-code-eliminates unreachable branches.
 *
 * Include this header to get sensorHasHumidity(), sensorTypeName(),
 * sensorValueCount(), sensorTypeEnabled(), and sensorDefaultIntervalMs().
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include "SensorConfig.h"
#include "SystemDefs_Records.h" /* SensorType enum */

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
