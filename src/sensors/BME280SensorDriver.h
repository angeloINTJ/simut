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
#include <Wire.h>
#include <vector>
#include <cstring>
#include "BME280Driver.h"
#include "SensorDriver.h"
#include "../LogManager.h"

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

 /* ── Scan ── BME280 is I2C, not detectable by sweeping GPIOs, so scanPin is
  * left at the base default (declines every pin) and the probe runs once here,
  * after the sweep. PIO bit-bang on the default I2C pins (4=SDA, 5=SCL) tries
  * the primary address; non-default wiring is still configured by hand after
  * the scan. This is the old BME_SCAN_CHECK block, verbatim. */
 void scanFinalize(std::vector<ScanResult>& out) override {
  BMx280PIO_RP2040 probe(4, 5, BME280_ADDR_PRIMARY);
  if (probe.begin()) {
   ScanResult res;
   res.pin = 255; /* I2C — no single GPIO */
   res.type = TYPE_BME280;
   memset(res.rom, 0, 8);
   out.push_back(res);
  }

  /* The scan just drove pins 4/5 (and every swept pin) as GPIO/1-Wire/PIO,
   * leaving a hardware-I2C bus off the peripheral. Unlike a reboot, a runtime
   * reload keeps the per-boot _i2cNInit guard and never re-muxes, so a running
   * BME/BMP on Wire answers errors until a power cycle (measured: one scan ->
   * BMP280 "Error" ~10 s later, permanent).
   *
   * A bare pin re-mux (gpio_set_function GPIO_FUNC_I2C + pull-up) is NOT enough:
   * measured on the rig, the sensor still errored ~12 s after the scan, so the
   * PERIPHERAL needs re-initialising, not just the pins. end( ) clears _running
   * (begin( )/setSDA/setSCL no-op on a running bus), then begin( ) re-inits i2cN
   * and restores GPIO_FUNC_I2C. That drags i2c_deinit/i2c_init into the image —
   * release grows ~72 B past its flash budget, raised in tools/flash_budget.json
   * in this change. recoverBus( ) is deliberately NOT used (it bit-bangs to SIO,
   * the reload trap). */
  for (uint8_t bi = 0; bi < _bmeBusCount; bi++) {
   uint8_t sda = _bmeBuses[bi].s, scl = _bmeBuses[bi].d;
   int periph = i2cPeripheralForPins(sda, scl);
   if (periph == 0) {
    Wire.end( );  Wire.setSDA(sda);  Wire.setSCL(scl);  Wire.begin( );
   } else if (periph == 1) {
    Wire1.end( ); Wire1.setSDA(sda); Wire1.setSCL(scl); Wire1.begin( );
   }
   /* periph < 0 is the PIO bit-bang fallback — a separate, rarer path; its
    * own driver owns its PIO and is not re-established here. */
  }
 }

 /* ── Per-slot init ── the manager's initRuntimeSensors Phase 1 for the 280s,
  * moved here whole: clean up last init, then per active BME/BMP slot pick the
  * I2C bus + address, bring the peripheral up once, create the per-bus driver,
  * and adopt the chip-ID type. */
 void initBegin( ) override {
  /* Tear down last init's drivers (reload, config change). */
  for (auto* drv : _list) { delete drv; }
  _list.clear( );
  /* Two lifetimes (the note the old inline statics/locals carried). The
   * ADDRESS BOOKKEEPING is per-call — reset every init, or a reload finds
   * 0x76 "already taken" by the boot pass and strands the sensor. The I2C
   * PERIPHERAL flags (_i2c0Init/_i2c1Init) are per-boot and are NOT reset:
   * recoverBus( ) bit-bangs the pins off the peripheral and TwoWire::begin( )
   * on a running bus does not re-mux them, so a reload must not re-run it. */
  for (uint8_t i = 0; i < 8; i++) { _bmeBuses[i] = BmeAddrTrack{ }; }
  _bmeBusCount = 0;
 }

 bool initSlot(RuntimeSensor& rs) override {
  /* Both 280s use this driver — the chip ID tells them apart at runtime. */
  if (rs.type != TYPE_BME280 && rs.type != TYPE_BMP280) return false;

  auto fmt = SensorFormat::forType(rs.type);
  uint8_t sda = PIN_UNUSED, scl = PIN_UNUSED;
  for (uint8_t pj = 0; pj < fmt.pinCount; pj++) {
   if (fmt.pins[pj].role == ROLE_I2C_SDA) sda = rs.config.pins[pj];
   if (fmt.pins[pj].role == ROLE_I2C_SCL) scl = rs.config.pins[pj];
  }
  if (sda == PIN_UNUSED || scl == PIN_UNUSED) return false;

  /* Track taken addresses per (sda,scl) bus — up to 2 sensors (0x76 and 0x77). */
  BmeAddrTrack* bus = nullptr;
  for (uint8_t bi = 0; bi < _bmeBusCount; bi++) {
   if (_bmeBuses[bi].s == sda && _bmeBuses[bi].d == scl) { bus = &_bmeBuses[bi]; break; }
  }
  if (!bus && _bmeBusCount < 8) {
   bus = &_bmeBuses[_bmeBusCount++];
   bus->s = sda; bus->d = scl; bus->a76 = false; bus->a77 = false;
  }
  uint8_t addr = 0;
  if (bus) {
   if (!bus->a76)      { addr = BME280_ADDR_PRIMARY; bus->a76 = true; }
   else if (!bus->a77) { addr = 0x77;                bus->a77 = true; }
  }
  if (addr == 0) return false;

  int periph = i2cPeripheralForPins(sda, scl);
  int8_t drvIdx = -1;
  if (periph == 0) {
   if (!_i2c0Init) {
    /* Before the peripheral takes the pins: a sensor left mid-byte by the
     * last reset is still holding SDA. */
    BME280Driver::recoverBus(sda, scl);
    Wire.setSDA(sda); Wire.setSCL(scl); Wire.begin( );
    _i2c0Init = true;
   }
   drvIdx = getOrCreate(Wire, addr);
  } else if (periph == 1) {
   if (!_i2c1Init) {
    BME280Driver::recoverBus(sda, scl);
    Wire1.setSDA(sda); Wire1.setSCL(scl); Wire1.begin( );
    _i2c1Init = true;
   }
   drvIdx = getOrCreate(Wire1, addr);
  } else {
   /* Pins not I2C-capable — PIO bit-bang. Wave 2: ~1.6 ms IRQs-off per I2C
    * transaction on Core 0. LOUD so a silent regression never hides. HW pairs:
    * I2C0 = 0/1, 4/5, 8/9, 12/13, 16/17, 20/21; I2C1 = 2/3, 6/7, 10/11, 14/15,
    * 18/19, 26/27. */
   LOG_CODE(LOG_WARN, "SENSOR", SYS_OK, sda,
            TRL("BME in bit-bang (pins have no hardware I2C) — see docs/CONCURRENCY.md"));
   drvIdx = getOrCreate(sda, scl, addr);
  }

  bool retyped = false;
  if (drvIdx >= 0) {
   rs.bmeDriverIdx = drvIdx;
   rs.i2cAddr = addr;
   /* Adopt what the chip says it is (0x60 = BME280, 0x58 = BMP280): the two
    * parts are indistinguishable from the outside, only the humidity channel
    * is at stake, and reading the ID costs nothing now the driver has begun.
    * RAM-only here; the caller persists (loadAndCalibrateSensors). */
   SensorType detected = _list[drvIdx]->isBME( ) ? TYPE_BME280 : TYPE_BMP280;
   if (rs.type != detected) {
    LOG_CODE(LOG_WARN, "SENSOR", SEC_CONFIG_CHANGED, rs.config.pins[0],
             String(TRL("I2C sensor retyped from chip ID: ")) +
             sensorTypeName(rs.type) + " -> " + sensorTypeName(detected));
    rs.type = detected;
    rs.config.sensorType = (uint8_t)detected;
    retyped = true;
   }
  }
  return retyped;
 }

private:
 /* PIO fallback — used when pins don't map to hardware I2C. Caller (initBegin)
  * owns cleanup via _list. */
 int8_t getOrCreate(uint8_t sda, uint8_t scl, uint8_t addr) {
  auto* drv = new (std::nothrow) BME280Driver( );
  if (!drv) return -1;
  if (Serial) { Serial.print("[DBG] BME PIO init addr=0x"); Serial.println((int)addr, HEX); }
  if (drv->begin(sda, scl, addr)) {
   _list.push_back(drv);
   LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, 0, String("BME280 PIO driver OK 0x") + String(addr, HEX));
   return (int8_t)(_list.size( ) - 1);
  }
  if (Serial) Serial.println("[DBG] BME PIO begin failed");
  delete drv;
  return -1;
 }

 /* Hardware I2C (Wire/Wire1) — zero PIO/DMA. Caller owns cleanup via _list. */
 int8_t getOrCreate(TwoWire &wire, uint8_t addr) {
  auto* drv = new (std::nothrow) BME280Driver( );
  if (!drv) return -1;
  if (Serial) { Serial.print("[DBG] BME HW I2C init addr=0x"); Serial.println((int)addr, HEX); }
  if (drv->begin(wire, addr)) {
   _list.push_back(drv);
   LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, 0, String("BME280 HW I2C driver OK 0x") + String(addr, HEX));
   return (int8_t)(_list.size( ) - 1);
  }
  if (Serial) Serial.println("[DBG] BME HW I2C begin failed");
  delete drv;
  return -1;
 }

 std::vector<BME280Driver*>& _list;
 /* Per-boot: the I2C peripheral is muxed once and survives reloads (see initBegin). */
 bool _i2c0Init = false;
 bool _i2c1Init = false;
 /* Per-call: which of 0x76/0x77 is taken on each (sda,scl) bus; reset each init. */
 struct BmeAddrTrack { uint8_t s, d; bool a76, a77; };
 BmeAddrTrack _bmeBuses[8] = { };
 uint8_t _bmeBusCount = 0;
};

#endif /* SIMUT_SENSOR_BME280 */
