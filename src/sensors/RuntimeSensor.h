/**
 * @file RuntimeSensor.h
 * @brief The runtime sensor instance and its moving-average ring buffer.
 * @details Split out of SensorManager.h on 2026-09-26 so the per-family sensor
 * drivers under src/sensors/ can service a RuntimeSensor without pulling in
 * the whole manager. Pure code motion — the struct definitions are
 * verbatim what SensorManager.h carried, and the release image is byte-
 * identical across the move (sha256 unchanged).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include "../SystemDefs.h"     /* MAX_SENSOR_CHANNELS, MOVING_AVG_WINDOW, SensorRecord, SensorType, ScanResult */
#include "SensorConfig.h"      /* SIMUT_SENSOR_* feature flags */
#include "SensorHelpers.h"     /* CH_TEMP/CH_HUM/CH_PRESS, SensorFormat — self-sufficient when a
                                * driver header pulls this in without SensorManager.h ahead of it */
#include "CalibCurve.h"        /* CalibCurve */


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

 /* Channel arrays — one buffer + averages + calibration per measurement axis.
  * [CH_TEMP]=0, [CH_HUM]=1, [CH_PRESS]=2, [CH_LUX]=3.
  * Inactive channels maintain NAN averages and empty buffers.
  *
  * The ring holds RAW samples and the curve is applied to the filtered mean,
  * not the other way around: for the old constant offset both orders are the
  * same arithmetic, but a piecewise curve pushed through the trimmed mean
  * would let the correction move the outlier cut, and editing a curve at
  * runtime would leave 10 samples of the old correction in the window. */
 RingBuffer buffers[MAX_SENSOR_CHANNELS];
 float rawValue[MAX_SENSOR_CHANNELS]; /**< filtered mean of the raw samples */
 float avgValue[MAX_SENSOR_CHANNELS]; /**< rawValue through the curve — what every consumer reads */
 CalibCurve calib[MAX_SENSOR_CHANNELS];

 uint32_t lastReadTime;
 uint32_t readInterval;

 uint32_t totalReadings;
 uint8_t consecutiveErrors;
 uint8_t consecutiveSuccess;
 bool inErrorState;
 bool hardwareMismatch;
 uint8_t mismatchRechecks;   /**< Wave 2: skip-cycles since last ROM re-verify (auto-recovery) */

#if SIMUT_SENSOR_BME280
 int8_t  bmeDriverIdx = -1;  /**< Index into SensorManager::_bmeDrivers, -1 = not BME280 */
 uint8_t i2cAddr = 0;        /**< I2C address (0x76 or 0x77), 0 = unassigned */
#endif

 /** @return true if at least CH_TEMP buffer is full. */
 bool bufferFull() const { return buffers[CH_TEMP].full(); }
};
