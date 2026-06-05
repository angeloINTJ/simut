/**
 * @file SensorConfig.h
 * @brief Compile-time sensor feature flags — enable/disable sensor types.
 * @details Each flag controls whether the corresponding sensor driver and
 * all its support code are compiled into the firmware. Disable unused
 * sensor types to reclaim flash space.
 *
 * Set via platformio.ini build_flags:
 *   -DSIMUT_SENSOR_DS18B20=0   (disable DS18B20, saves ~X KB)
 *   -DSIMUT_SENSOR_DHT22=0     (disable DHT22, saves ~Y KB)
 *   -DSIMUT_SENSOR_BME280=1    (enable BME280, costs ~Z KB)
 *
 * Defaults (all defined as 1 if not overridden) keep current behavior
 * unchanged. BME280 defaults to 0 (future).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#ifndef SIMUT_SENSOR_DS18B20
#define SIMUT_SENSOR_DS18B20 1
#endif

#ifndef SIMUT_SENSOR_DHT22
#define SIMUT_SENSOR_DHT22 1
#endif

#ifndef SIMUT_SENSOR_BME280
#define SIMUT_SENSOR_BME280 0
#endif
