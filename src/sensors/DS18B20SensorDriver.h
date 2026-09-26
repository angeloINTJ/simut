/**
 * @file DS18B20SensorDriver.h
 * @brief SensorDriver for the DS18B20 1-Wire family — parallel batch read.
 * @details The DS18B20 read model is a mass conversion: every DS18B20 on the
 * bus is told to convert at once, and after one 750 ms window they are all
 * read back. That, plus the per-sensor ROM identity check (every 5th read) and
 * the mismatch quarantine with 10-cycle auto-recovery, is exactly what this
 * driver's serviceReads( ) carries — moved verbatim out of
 * SensorManager::processPeriodicReads( ) on 2026-09-26, `_ds18` becoming the
 * held `_hw` and the manager's private helpers becoming SensorHost callbacks.
 *
 * The hardware wrapper (DS18B20Driver) is still owned by the manager for now;
 * this driver holds a reference to it. A later step folds the wrapper in and
 * moves the scan probe here too.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DS18B20

#include <Arduino.h>
#include "DS18B20Driver.h"
#include "SensorDriver.h"
#include "../LogManager.h"

class DS18B20SensorDriver : public SensorDriver {
public:
 explicit DS18B20SensorDriver(DS18B20Driver& hw) : _hw(hw) { }

 SensorType type( ) const override { return TYPE_DS18B20; }

 bool begin( ) override { return _hw.begin( ); }

 void serviceReads(SensorHost& host, uint32_t now) override {
  auto& _runtimeSensors = host.runtimeSensors( );

  /* ── DS18B20: parallel batch read ── */
  if (_hw.state == DS18B20Driver::DS_IDLE) {
  bool needsRead = false;
  for (auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20 && (now - s.lastReadTime >= host.readGap(s))) {
  needsRead = true; break;
  }
  }

  if (needsRead) {
  for (auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20) _hw.requestTemperatures(s.config.pins[0]);
  }
  _hw.timer = now;
  _hw.state = DS18B20Driver::DS_WAITING;
  }
  }
  else if (_hw.state == DS18B20Driver::DS_WAITING) {
  if (now - _hw.timer >= DS18B20_CONVERSION_TIME_MS) {
  for (auto &s : _runtimeSensors) {
  if (s.type == TYPE_DS18B20) {


  if (s.hardwareMismatch) {
  /* Wave 2 (sensor doc issue #3): the quarantine used to be permanent
   * until reboot or manual recalibration. Every 10th skipped cycle,
   * re-read the ROM — if the CONFIGURED chip is back on the pin (user
   * swapped the right sensor back), lift the quarantine and let the
   * normal read path below run this very cycle. A different chip
   * keeps failing the match and stays quarantined (safety preserved). */
  if (++s.mismatchRechecks >= 10) {
  s.mismatchRechecks = 0;
  uint8_t romNow[8];
  if (_hw.readROM(s.config.pins[0], romNow) &&
      _hw.checkRomMatch(romNow, s.config.rom)) {
  s.hardwareMismatch = false;
  s.inErrorState = false;
  s.consecutiveErrors = 0;
  LOG_CODE(LOG_INFO, "SENSOR", SYS_OK, s.config.pins[0], TRL("Hardware match restored"));
  }
  }
  }

  if (s.hardwareMismatch) {
  if (!s.inErrorState) {
  LOG_CODE(LOG_ERROR, "SENSOR", ERR_SENSOR_MISMATCH, s.config.pins[0], TRL("Hardware Mismatch (Access Denied)"));
  }
  s.inErrorState = true;
  s.buffers[0].clear( );
  s.avgValue[0] = NAN;
  s.rawValue[0] = NAN;
  s.consecutiveSuccess = 0;
  s.lastReadTime = now;
  host.notifyNewData( );
  continue;
  }

  s.totalReadings++;
  bool romVerified = true;
  const char* failReason = "";

  /* ROM verification every 5 reads — skip if config ROM is all zeros
   * (unpaired sensor). A zero ROM means "accept any DS18B20 on this pin". */
  bool romIsZero = true;
  for (int k = 0; k < 8; k++) if (s.config.rom[k] != 0) romIsZero = false;

  if (!romIsZero && s.totalReadings % 5 == 0) {
  uint8_t currentRom[8];
  if (_hw.readROM(s.config.pins[0], currentRom)) {
  if (!_hw.checkRomMatch(currentRom, s.config.rom)) {
  romVerified = false;
  failReason = "ROM Mismatch";
  s.hardwareMismatch = true;
  }
  } else {
  romVerified = false; failReason = "ROM Read Failed";
  }
  }

  if (romVerified) {
  float tempC = 0.0f;
  bool success = _hw.getTemperatureValidated(s.config.pins[0], tempC);

  if (!success) host.reportResult(s, false, 0, 0, "CRC/Read Error");
  else if (tempC < -50 || tempC > 150) host.reportResult(s, false, 0, 0, "Out of Range");
  else host.reportResult(s, true, tempC, NAN, "");
  s.lastReadTime = now;
  } else {
  host.reportResult(s, false, 0, 0, failReason);
  s.lastReadTime = now;
  }
  }
  }
  _hw.state = DS18B20Driver::DS_IDLE;
  }
  }
 }

 /* ── Scan ── 1-Wire presence + ROM read on one pin. The old ONEWIRE_RESET /
  * ONEWIRE_WAIT states: the first tick drives the reset, later ticks wait out
  * the 1200 us presence window. Present + ROM read -> found; present but the
  * ROM read fails, or nothing present, hands the pin to the next family (a
  * DHT22 shares the same GPIO probe). */
 ScanVerdict scanPin(uint8_t pin, bool firstCall, std::vector<ScanResult>& out) override {
  if (firstCall) {
  _hw.setPin(pin);
  _hw.sendReset( );
  _scanTimer = micros( );
  return SCAN_KEEP;
  }
  if (micros( ) - _scanTimer < 1200) return SCAN_KEEP;
  if (_hw.isSensorPresent( )) {
  ScanResult res;
  res.pin = pin;
  res.type = TYPE_DS18B20;
  if (_hw.readROM(pin, res.rom)) {
  out.push_back(res);
  return SCAN_FOUND;
  }
  }
  return SCAN_NEXT_FAMILY;
 }

 /* Park the 1-Wire pin back at the default once the sweep is done — the old
  * `_ds18.setPin(PIN_ONEWIRE_DEFAULT)` that closed the scan. */
 void scanFinalize(std::vector<ScanResult>& out) override {
  (void)out;
  _hw.setPin(PIN_ONEWIRE_DEFAULT);
 }

private:
 DS18B20Driver& _hw;
 uint32_t _scanTimer = 0;   /**< micros( ) stamp of the 1-Wire reset during scan */
};

#endif /* SIMUT_SENSOR_DS18B20 */
