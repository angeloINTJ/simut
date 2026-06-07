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

/* ===========================================================================
 * SENSOR CHANNELS — universal measurement axes
 *
 * Each sensor type exposes N channels (temperature, humidity, pressure, etc.).
 * Drivers declare their channels via SensorFormat::forType().
 * Consumer code queries capabilities instead of hardcoding "humidity".
 * =========================================================================== */

#define MAX_SENSOR_CHANNELS 4

enum SensorChannel : uint8_t {
 CH_TEMP = 0,  /**< Temperature (always channel 0 for all types) */
 CH_HUM  = 1,  /**< Relative humidity (DHT22, BME280) */
 CH_PRESS = 2, /**< Atmospheric pressure (BME280) */
 CH_LUX  = 3,  /**< Luminosity / light */
 CH_COUNT = 4  /**< Sentinel */
};

/** @return human-readable channel name */
inline const char* sensorChannelName(uint8_t ch) {
 switch (ch) {
 case CH_TEMP:  return "Temperature";
 case CH_HUM:   return "Humidity";
 case CH_PRESS: return "Pressure";
 case CH_LUX:   return "Luminosity";
 default:       return "Channel";
 }
}

/** Forward declaration — implementation after SensorFormat definition below. */
inline bool sensorHasChannel(SensorType t, uint8_t channel);

/** @deprecated Use sensorHasChannel(t, CH_HUM) instead. */
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

/* ===========================================================================
 * Sensor display format — per-value formatting metadata.
 *
 * Each sensor driver defines its own format via SensorFormat::forType().
 * Display code queries this instead of hardcoding units/decimal places/icons.
 * =========================================================================== */

/** Describes how to display a single sensor value (temperature, humidity, etc.). */
struct SensorValueFormat {
 const char* unit;     /**< "°C", "%", "hPa", "lux", "pH", "ppm", "" */
 uint8_t     decimals; /**< 0, 1, or 2 decimal places */
 const char* icon;     /**< Icon identifier for procedural drawing */
};

/** Display format for a sensor type — one SensorValueFormat per measurement. */
struct SensorFormat {
 uint8_t          valueCount;       /**< 1, 2, or 3 values */
 SensorValueFormat values[3];       /**< One per value (unused entries are zeroed) */

 /** Factory: returns the format metadata for a given sensor type. */
 static SensorFormat forType(SensorType t) {
 SensorFormat f = {};
 switch (t) {
#if SIMUT_SENSOR_DS18B20
 case TYPE_DS18B20:
 f.valueCount = 1;
 f.values[0] = {"°C", 1, "thermometer"};
 break;
#endif
#if SIMUT_SENSOR_DHT22
 case TYPE_DHT22:
 f.valueCount = 2;
 f.values[0] = {"°C", 1, "thermometer"};
 f.values[1] = {"%",  0, "drop"};
 break;
#endif
#if SIMUT_SENSOR_BME280
 case TYPE_BME280:
 f.valueCount = 3;
 f.values[0] = {"°C",  1, "thermometer"};
 f.values[1] = {"%",   0, "drop"};
 f.values[2] = {"hPa", 1, "gauge"};
 break;
#endif
 default:
 f.valueCount = 1;
 f.values[0] = {"", 1, ""};
 break;
 }
 return f;
 }
};

/* Implementation — after SensorFormat definition (resolves circular dependency). */
inline bool sensorHasChannel(SensorType t, uint8_t channel) {
 auto f = SensorFormat::forType(t);
 return channel < f.valueCount;
}
