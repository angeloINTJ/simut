/**
 * @file SensorConfig.h
 * @brief Compile-time sensor feature flags — delegates to simut_config.h.
 *
 * All user-configurable sensor and Bluetooth options are now centralized in
 * `src/simut_config.h`. This header is kept for backward compatibility:
 * existing code that includes SensorConfig.h continues to work.
 *
 * To customize:
 *   1. Edit src/simut_config.h
 *   2. Or override via platformio.ini build_flags (e.g. -DSIMUT_SENSOR_BME280=1)
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Angelo Moises Alves
 * @license MIT License
 */

#pragma once

#include "../simut_config.h"
