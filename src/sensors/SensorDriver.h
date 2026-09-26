/**
 * @file SensorDriver.h
 * @brief The per-family sensor driver interface and the host it talks back to.
 * @details Each sensor family (DS18B20, DHT22, BME280, and whatever comes
 * next) is a SensorDriver: it owns its family's asynchronous read state
 * machine and knows how to service the RuntimeSensors of its type. The
 * SensorManager holds a registry of the compiled-in drivers and iterates it,
 * so adding a compatible sensor is a new SensorDriver subclass rather than
 * another `#if SIMUT_SENSOR_*` branch woven through the manager.
 *
 * A driver never reaches into the manager directly — that would be a cycle,
 * since the manager owns the drivers. Instead it calls back through SensorHost,
 * a narrow interface the manager implements: the sensor list, the read-gap
 * policy, and the three ways a read cycle changes a sensor's state (a result,
 * a raw sample, a bare data-changed notification). Keeping it narrow is the
 * point — the error hysteresis, the metrics, the trimmed-mean filter and the
 * cross-core flag all stay in one place (the manager), and every driver shares
 * them instead of reimplementing them.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <vector>
#include "RuntimeSensor.h"


/** What a SensorDriver needs from the SensorManager to run a read cycle.
 *  The manager implements this; a driver holds only a SensorHost&, never a
 *  SensorManager*, so src/sensors/ has no dependency back on the manager. */
class SensorHost {
public:
 virtual ~SensorHost( ) { }

 /** The live runtime sensor list — a driver services the entries of its type. */
 virtual std::vector<RuntimeSensor>& runtimeSensors( ) = 0;

 /** The gap a sensor must sit idle before the next conversion: 0 while fast
  *  sampling, else s.readInterval. One policy so the driver paths cannot drift. */
 virtual uint32_t readGap(const RuntimeSensor& s) const = 0;

 /** Feed one read outcome through the error hysteresis (3 to flag, 5 to
  *  recover), the metrics counters, the log, and — on success — the raw
  *  sample push for CH_TEMP (and CH_HUM when the type carries humidity). */
 virtual void reportResult(RuntimeSensor& s, bool success,
                           float v1, float v2, const char* errorMsg) = 0;

 /** Push one RAW sample into one channel and refresh both means (raw and the
  *  calibrated avg). Used for channels reportResult does not push, e.g. the
  *  BME280 pressure axis. */
 virtual void pushSample(RuntimeSensor& s, uint8_t ch, float rawV) = 0;

 /** Mark new data available for the other core, without pushing a sample —
  *  the DS18B20 mismatch quarantine uses this after it NaNs a channel. */
 virtual void notifyNewData( ) = 0;
};


/** One instance per compiled-in sensor family. Inc 1 (2026-09-26) gives it the
 *  read state machine only; the scan and per-slot init still live in the
 *  manager and move here in later steps. */
class SensorDriver {
public:
 virtual ~SensorDriver( ) { }

 /** The family this driver services (its primary SensorType). */
 virtual SensorType type( ) const = 0;

 /** Boot-time family init — claim PIO, etc. false means the family is
  *  unavailable for the rest of this boot (the caller logs it loudly). */
 virtual bool begin( ) = 0;

 /** Advance this family's asynchronous read state machine once. Called every
  *  SensorManager::update( ) with the loop's single millis( ) snapshot, so the
  *  three families share one timestamp exactly as the old inline blocks did. */
 virtual void serviceReads(SensorHost& host, uint32_t now) = 0;
};
