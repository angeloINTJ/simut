/**
 * @file BME280SensorDriver.h
 * @brief SensorDriver for the BME280/BMP280 family — one driver per I2C bus.
 * @details The 280s are the multi-instance case: one BME280Driver exists per
 * (sda, scl, addr) triplet, each with an independent forced-mode state
 * machine, so one bus can be mid-conversion while another is idle. serviceReads
 * ( ) walks that vector and runs each instance's cycle — moved verbatim out of
 * SensorManager::processPeriodicReads( ) on 2026-09-26, with pushChannelSample
 * for the pressure axis becoming host.pushSample.
 *
 * The vector itself is still owned by the manager (initRuntimeSensors builds
 * and retypes it); this driver holds a reference to it. A later step folds the
 * ownership and the per-slot bus/address/chip-ID init in here too — that init
 * is the most family-specific part left in the manager.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_BME280

#include <Arduino.h>
#include <vector>
#include "BME280Driver.h"
#include "SensorDriver.h"

class BME280SensorDriver : public SensorDriver {
public:
 explicit BME280SensorDriver(std::vector<BME280Driver*>& list) : _list(list) { }

 SensorType type( ) const override { return TYPE_BME280; }

 /* Per-instance begin( ) is deferred to initRuntimeSensors( ) — it needs the
  * I2C bus up and the pin pairs known. Nothing to claim at boot. */
 bool begin( ) override { return true; }

 void serviceReads(SensorHost& host, uint32_t now) override {
  auto& _runtimeSensors = host.runtimeSensors( );

  /* ── BME280: multi-driver forced-mode via PIO ──
   * Each driver operates independently — one can be in WAITING
   * while another is IDLE. No shared bus contention. */
  for (size_t di = 0; di < _list.size(); di++) {
   auto *drv = _list[di];

   if (drv->state == BME280Driver::BME_IDLE) {
    for (size_t i = 0; i < _runtimeSensors.size( ); i++) {
     auto &s = _runtimeSensors[i];
     if ((s.type == TYPE_BME280 || s.type == TYPE_BMP280) && s.bmeDriverIdx == (int8_t)di
         && (now - s.lastReadTime >= host.readGap(s))) {
      drv->reset( );
      drv->requestReading( );
      drv->timer = millis( );
      drv->currentSensorIdx = i;
      drv->state = BME280Driver::BME_WAITING;
      break;
     }
    }
   }
   else if (drv->state == BME280Driver::BME_WAITING) {
    if (drv->currentSensorIdx >= 0 && drv->currentSensorIdx < (int)_runtimeSensors.size( )) {
     auto &s = _runtimeSensors[drv->currentSensorIdx];

     if (timeSince(drv->timer, BME280_MEAS_TIME_MS)) {
      float t, h, p;
      if (drv->getResults(t, h, p)) {
       /* BME280: v1=temp, v2=humidity (pressure available via API) */
      if (!drv->isBME( )) h = NAN;  /* BMP280: cached flag, not a live I2C read */
       host.reportResult(s, true, t, h, "");
       /* Pressure rides the same per-channel path as everything else.
        * It used to have its own inline copy of the mean AND its own offset
        * add — the era when it was pushed raw left a stored CH_PRESS offset
        * changing nothing anywhere; the calibration was write-only. */
       host.pushSample(s, CH_PRESS, p);
      } else {
       host.reportResult(s, false, 0, 0, "I2C Read Error");
      }
      drv->reset( );
      s.lastReadTime = millis( );
     }
    } else {
     drv->state = BME280Driver::BME_IDLE;
    }
   }
  }
 }

private:
 std::vector<BME280Driver*>& _list;
};

#endif /* SIMUT_SENSOR_BME280 */
