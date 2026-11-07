/**
 * @file ScanResultJson.h
 * @brief The JSON of a finished sensor scan, as POST /api/action?op=scan_results sends it.
 * @details Pure: a vector of ScanResult in, text out, so the native suite
 * (test/test_sensors) can pin it. It was printed inline in WebManager_Calib.cpp,
 * out of the host's reach, and a BME280 or BMP280 went out as {"pin":255,...}:
 * the page printed "GP255" for it (seen on the capture of the manual's
 * figures, v2.14.0, 2026-11-06).
 *
 * One entry of "found":
 *   {"pin":2,"type":1,"rom":"28FF641E8016034A","name":"DS18B20"}   a probe on a GPIO
 *   {"pin":255,"type":5,"rom":"","name":"BMP280","sda":4,"scl":5}   an I2C part
 * pin, type and rom are what they always were (pin 255 = no single GPIO, rom
 * only for a DS18B20); name is sensorTypeName( ) of the type; sda and scl are
 * there only when the result holds a bus, and are the pair the probe drove, so
 * the page prints what the firmware says and keeps no copy of it.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "SensorHelpers.h" /* sensorTypeName, SensorType, ScanResult, PIN_UNUSED */

/** Writes one entry of the "found" array into out, snprintf-style: the text is
 *  cut and NUL-terminated when cap is too small. `first` leaves out the comma
 *  that separates the entries. */
inline void scanResultJson(const ScanResult& r, bool first, char* out, size_t cap) {
 if (!out || cap == 0) return;
 char rom[17] = {0};
 if (r.type == TYPE_DS18B20) {
  for (int b = 0; b < 8; b++) snprintf(rom + b * 2, 3, "%02X", r.rom[b]);
 }
 int n = snprintf(out, cap, "%s{\"pin\":%u,\"type\":%d,\"rom\":\"%s\",\"name\":\"%s\"",
                  first ? "" : ",", (unsigned)r.pin, (int)r.type, rom, sensorTypeName(r.type));
 if (n > 0 && (size_t)n < cap && r.sda != PIN_UNUSED && r.scl != PIN_UNUSED) {
  int m = snprintf(out + n, cap - (size_t)n, ",\"sda\":%u,\"scl\":%u", (unsigned)r.sda, (unsigned)r.scl);
  if (m > 0) n += m;
 }
 if (n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "}");
}

/** The whole answer, {"scanning":false,"found":[...]}. Str is Arduino's String
 *  in the firmware and std::string in the host test: anything with `+= const char*`. */
template <typename Str>
inline void scanResultsJson(const std::vector<ScanResult>& results, Str& out) {
 out += "{\"scanning\":false,\"found\":[";
 for (size_t i = 0; i < results.size( ); i++) {
  char item[128];
  scanResultJson(results[i], i == 0, item, sizeof item);
  out += item;
 }
 out += "]}";
}
