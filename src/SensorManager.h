/**
 * @file SensorManager.h
 * @brief Sensor orchestration layer — runtime sensor list, periodic reads, scan.
 * @details Manages runtime sensor instances with static ring buffers for
 * moving average calculation (trimmed mean), asynchronous reading
 * state machines, hardware scan across GPIO pins, ROM verification,
 * calibration curve application, and hardware mismatch detection.
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
#include "sensors/CalibCurve.h"
#include "sensors/RuntimeSensor.h"   /* RingBuffer + RuntimeSensor (moved out 2026-09-26) */
#include "sensors/SensorDriver.h"    /* SensorDriver interface + SensorHost */
#include "sensors/DS18B20SensorDriver.h"
#include "sensors/DHT22SensorDriver.h"
#include "sensors/BME280SensorDriver.h"


/* SensorManager IS the SensorHost the drivers call back into: the read-cycle
 * helpers (readGap, reportResult, pushSample, notifyNewData) that used to be
 * private are now the host interface each driver shares. */
class SensorManager : public SensorHost {
public:
 SensorManager( );
 void begin( );
 void update( );

 /** Read back to back, ignoring each sensor's readInterval.
  *
  * readInterval is a RATE LIMIT for a device that samples continuously to keep
  * a display and a web page current — it is not part of the measurement. It
  * also does not overlap the conversion: lastReadTime is stamped when the read
  * COMPLETES, so the true period is readInterval + conversion time. On a
  * DS18B20 that is 1000 + 750 ms, and SIMUT Air's SAMPLE phase waits for a
  * MOVING_AVG_WINDOW of ten of them before it may write its one record —
  * measured 14,81 s of a 25,5 s wake on 2026-09-08.
  *
  * With this on, the same ten samples cost only their conversions. The values
  * and the filter are untouched; what goes is the idling between them. SIMUT
  * Air turns it on for the WARMUP/SAMPLE phases of a wake and off at DECIDE,
  * so M0 keeps the ordinary cadence. */
 void setFastSampling(bool on) { _fastSampling = on; }
 bool isFastSampling( ) const { return _fastSampling; }


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

 /** DS18B20 path: temperature curve + hwId/name adoption from the ROM row. */
 void applyCalibration(uint8_t gpio, String newHwId, const CalibCurve& tempCurve, String newName);
 /** Apply one curve per channel, indexed by channel id.
  *  The float[] ancestor took (offsetT, offsetH, offsetP) positionally, which
  *  meant a new quantity changed the signature and every call site. */
 void applyCalibrationCurves(uint8_t gpio, const CalibCurve curves[MAX_SENSOR_CHANNELS]);


 void setHardwareMismatch(uint8_t gpio, bool isMismatch);

 /** Slots whose sensorType initRuntimeSensors corrected from the chip ID
  *  (BME280 <-> BMP280). Non-zero means the caller should persist the
  *  config, otherwise the correction is redone every boot. Cleared at the
  *  start of each initRuntimeSensors. */
 uint8_t takeRetypedCount( ) { uint8_t n = _retypedSlots; _retypedSlots = 0; return n; }

private:
 uint8_t _retypedSlots = 0;
 /* Hardware wrappers, still owned here; each is wrapped by the SensorDriver
  * below it, which holds the read state machine. The scan and per-slot init
  * still reach the wrappers directly (later steps move those in too). */
#if SIMUT_SENSOR_DS18B20
 DS18B20Driver _ds18;
 DS18B20SensorDriver _dsDriver{_ds18};
#endif
#if SIMUT_SENSOR_DHT22
 DHT22Driver _dht;
 DHT22SensorDriver _dhtDriver{_dht};
#endif
#if SIMUT_SENSOR_BME280
 std::vector<BME280Driver*> _bmeDrivers;  /**< One driver per (sda,scl,addr) triplet */
 BME280SensorDriver _bmeDriver{_bmeDrivers};
 int8_t _getOrCreateBmeDriver(uint8_t sda, uint8_t scl, uint8_t addr);  /**< PIO fallback */
 int8_t _getOrCreateBmeDriver(TwoWire &wire, uint8_t addr);             /**< Hardware I2C (Wire/Wire1) */
#endif

 /** The compiled-in families, in read order (DS18B20, DHT22, BME280). Built
  *  once in begin( ); processPeriodicReads iterates it. Adding a sensor family
  *  is a new SensorDriver here, not another #if in the read loop. */
 std::vector<SensorDriver*> _drivers;

 /* ── SensorHost: the read-cycle callbacks the drivers share ── */
 std::vector<RuntimeSensor>& runtimeSensors( ) override { return _runtimeSensors; }
 uint32_t readGap(const RuntimeSensor& s) const override;   /**< 0 while fast sampling; else s.readInterval */
 void reportResult(RuntimeSensor& s, bool success, float v1, float v2, const char* errorMsg) override;
 void pushSample(RuntimeSensor& s, uint8_t ch, float rawV) override;
 void notifyNewData( ) override {
 __atomic_store_n(&_newDataAvailable, true, __ATOMIC_RELEASE);
 }

 std::vector<RuntimeSensor> _runtimeSensors;
 volatile bool _newDataAvailable = false;
 bool _fastSampling = false;   /**< see setFastSampling( ) */


 /* The scan is now family-agnostic: the per-family sub-states (ONEWIRE_RESET/
  * WAIT, DHT_REQUEST/WAIT, BME_SCAN_CHECK) moved into each SensorDriver's
  * scanPin( )/scanFinalize( ). What is left is the outer sweep — set up a pin,
  * offer it to each driver in turn, advance, and finalize once. */
 enum ScanState {
 IDLE,
 SETUP_PIN,
 PROBE,
 NEXT_PIN,
 FINALIZE,
 COMPLETE
 };
 ScanState _scanState = IDLE;
 uint8_t _currentScanPin = 0;
 int _scanDriverIdx = 0;       /**< which driver PROBE is currently offering the pin to */
 bool _scanFirstCall = true;   /**< true on the first scanPin( ) of a (pin, driver) pair */
 std::vector<ScanResult> _scanResults;


 void processPeriodicReads( );
};
