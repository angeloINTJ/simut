/**
 * @file DHT22SensorDriver.h
 * @brief SensorDriver for the DHT22 family — sequential, one sensor at a time.
 * @details Unlike the DS18B20 batch, a DHT22 read is driven one unit at a
 * time: the state machine picks the next due DHT22, requests it, and waits for
 * DATA_READY / a checksum / a timeout before it will start another. That
 * sequencing is what serviceReads( ) carries — moved verbatim out of
 * SensorManager::processPeriodicReads( ) on 2026-09-26, with `_dht` becoming
 * the held `_hw` and handleSensorResult becoming host.reportResult.
 *
 * The hardware wrapper (DHT22Driver) is still owned by the manager for now;
 * this driver holds a reference to it.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DHT22

#include <Arduino.h>
#include <cstring>
#include "DHT22Driver.h"
#include "SensorDriver.h"

class DHT22SensorDriver : public SensorDriver {
public:
 explicit DHT22SensorDriver(DHT22Driver& hw) : _hw(hw) { }

 SensorType type( ) const override { return TYPE_DHT22; }

 bool begin( ) override { return _hw.begin( ); }

 void serviceReads(SensorHost& host, uint32_t now) override {
  auto& _runtimeSensors = host.runtimeSensors( );

  /* ── DHT22: sequential one-at-a-time ── */
  if (_hw.state == DHT22Driver::DHT_IDLE) {

  for (size_t i = 0; i < _runtimeSensors.size( ); i++) {
  auto &s = _runtimeSensors[i];
  if (s.type == TYPE_DHT22 && (now - s.lastReadTime >= host.readGap(s))) {
  _hw.reset( );
  _hw.requestReading(s.config.pins[0]);

  _hw.timer = millis( );
  _hw.currentSensorIdx = i;
  _hw.state = DHT22Driver::DHT_WAITING;
  break;
  }
  }
  }
  else if (_hw.state == DHT22Driver::DHT_WAITING) {

  if (_hw.currentSensorIdx >= 0 && _hw.currentSensorIdx < (int)_runtimeSensors.size( )) {
  auto &s = _runtimeSensors[_hw.currentSensorIdx];

  _hw.update( );
  DHT22PIO::State st = _hw.getState( );

  if (st == DHT22PIO::DATA_READY) {
  float t, h;
  if (_hw.getResults(t, h)) {
  host.reportResult(s, true, t, h, "");
  } else {
  host.reportResult(s, false, 0, 0, "Checksum Error");
  }
  _hw.reset( );
  s.lastReadTime = millis( );
  _hw.state = DHT22Driver::DHT_IDLE;
  }
  else if (st == DHT22PIO::ERROR_TIMEOUT || st == DHT22PIO::ERROR_CHECKSUM) {
  const char* errMsg = (st == DHT22PIO::ERROR_TIMEOUT) ? "Sensor Timeout" : "Checksum Error";
  host.reportResult(s, false, 0, 0, errMsg);
  _hw.reset( );
  s.lastReadTime = millis( );
  _hw.state = DHT22Driver::DHT_IDLE;
  }

  else if (timeSince(_hw.timer, DHT22_READ_TIMEOUT_MS)) {
  host.reportResult(s, false, 0, 0, "Sensor Timeout");
  _hw.reset( );
  s.lastReadTime = millis( );
  _hw.state = DHT22Driver::DHT_IDLE;
  }
  } else {

  _hw.state = DHT22Driver::DHT_IDLE;
  }
  }
 }

 /* Keep the DHT22 PIO advancing while the manager waits on it — the old
  * `_dht.update( )` called at the top of the scan branch every tick. */
 void scanPump( ) override { _hw.update( ); }

 /* ── Scan ── one DHT22 presence probe on a pin. The old DHT_REQUEST /
  * DHT_WAIT states: request on the first tick, then watch for a frame. A
  * frame (good or checksum-bad) means a DHT22 is there; a timeout hands the
  * pin on (nothing found — the next family, then the next pin). */
 ScanVerdict scanPin(uint8_t pin, bool firstCall, std::vector<ScanResult>& out) override {
  if (firstCall) {
  _hw.requestReading(pin);
  _scanTimer = millis( );
  return SCAN_KEEP;
  }
  DHT22PIO::State s = _hw.getState( );
  if (s == DHT22PIO::DATA_READY || s == DHT22PIO::ERROR_CHECKSUM) {
  ScanResult res;
  res.pin = pin;
  res.type = TYPE_DHT22;
  memset(res.rom, 0, 8);
  out.push_back(res);
  return SCAN_FOUND;
  }
  if (s == DHT22PIO::ERROR_TIMEOUT || timeSince(_scanTimer, DHT22_READ_TIMEOUT_MS)) {
  return SCAN_NEXT_FAMILY;
  }
  return SCAN_KEEP;
 }

private:
 DHT22Driver& _hw;
 uint32_t _scanTimer = 0;   /**< millis( ) stamp of the DHT22 scan request */
};

#endif /* SIMUT_SENSOR_DHT22 */
