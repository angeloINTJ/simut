/**
 * @file air/AirConfig.h
 * @brief SIMUT Air persistent configuration (headless hibernating build).
 *
 * Deliberately stored OUTSIDE SystemConfig in /config/air.bin so that
 * CONFIG_VERSION stays frozen (no migration, no risk of losing the main
 * config). The file carries its own magic + version + CRC32; a missing or
 * corrupt file falls back to the simut_config.h defaults.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include "simut_config.h"

#define AIR_CONFIG_MAGIC 0x41495231u  /* "AIR1" */
#define AIR_CONFIG_VERSION 2
#define AIR_CONFIG_PATH "/config/air.bin"
#define AIR_CONFIG_TMP  "/config/air.tmp"

/* Dormant-wake marker in watchdog scratch[0]. scratch[0..2] are documented as
 * "reserved by Pico SDK" but hardware_sleep (their only user) is NOT linked by
 * this framework, so scratch[0] is free. It survives a dormant wake (always-on
 * domain) but is zeroed on a true power cycle — exactly the M0/M1 discriminator
 * we need. Mirrors the POST_OTA_APPLY_MAGIC pattern in AppManager_Boot.cpp. */
#define AIR_DORMANT_MAGIC 0xA1B2C3D4u

/* scratch[1] — two facts the sleep has to carry to the next boot, tagged so a
 * garbage register is not mistaken for a measurement:
 *
 *   bits 31..24  AIR_SLEPT_MAGIC
 *   bits 23..17  wakes since the last one that raised the radio (0..127)
 *   bits 16..0   seconds the last sleep really lasted (0..131071 = 36 h)
 *
 * The seconds come from the RTC read after the WFI returns and before the
 * reset; they are what turned the cycle period into a measured number instead
 * of one inferred from USB enumeration, and they now also seed the provisional
 * clock on the wakes that never reach NTP.
 *
 * The wake count is the telemetry schedule. It lives here rather than in flash
 * because it changes on EVERY wake, and a device reading once a minute would
 * otherwise pay a flash write per minute for a counter. Losing it costs one
 * delayed telemetry, and only on a power cycle or a physical reset — a watchdog
 * reset keeps it, which is the case that actually matters.
 *
 * 17 bits for the seconds is not a squeeze: the alarm is armed from a datetime
 * whose hour field wraps at 24, so no sleep this firmware can request comes
 * close to 36 hours. */
#define AIR_SLEPT_MAGIC 0x5E000000u
#define AIR_SLEPT_MASK  0xFF000000u
#define AIR_SLEPT_SEC_MASK 0x0001FFFFu
#define AIR_WAKES_SHIFT 17
#define AIR_WAKES_MAX   127u

inline uint32_t airScratch1Pack(uint32_t sleptSec, uint8_t wakes) {
  if (sleptSec > AIR_SLEPT_SEC_MASK) sleptSec = AIR_SLEPT_SEC_MASK;
  if (wakes > AIR_WAKES_MAX) wakes = AIR_WAKES_MAX;
  return AIR_SLEPT_MAGIC | ((uint32_t)wakes << AIR_WAKES_SHIFT) | sleptSec;
}

inline bool airScratch1Valid(uint32_t v) {
  return (v & AIR_SLEPT_MASK) == AIR_SLEPT_MAGIC;
}

inline uint32_t airScratch1Slept(uint32_t v) { return v & AIR_SLEPT_SEC_MASK; }

inline uint8_t airScratch1Wakes(uint32_t v) {
  return (uint8_t)((v >> AIR_WAKES_SHIFT) & AIR_WAKES_MAX);
}

struct __attribute__((packed)) AirConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t idleTimeoutSec;    /* M0 inactivity -> auto-hibernate (seconds) */
  uint16_t stabTimeoutMs;     /* sensor stabilization cap */
  /* Was wifiScanTimeoutMs, a presence-scan cap that nothing ever read (plan
   * F17 confirmed it dead). Taken over rather than appended, so the file keeps
   * its size and its CRC: a v2 file loads with everything else intact —
   * idleTimeoutSec, the armed flag, the crash-loop count — instead of falling
   * back to defaults over one new byte.
   *
   * Nothing ever wrote it, so every file that can exist holds exactly the old
   * default of 4000 ms: little-endian, its low byte lands here as 160, which is
   * not a GPIO, so the range check in airSanitise turns it into the default.
   * That is the whole migration, and test_charger_pin_from_legacy_field pins
   * it — the version is deliberately NOT bumped, so nothing else would. */
  uint8_t  chargerPin;        /* GPIO that reads high while charging; PIN_UNUSED = off */
  uint8_t  chargerRsv;        /* was the high byte of wifiScanTimeoutMs */
  uint16_t connectTimeoutMs;  /* connect + NTP cap */
  uint16_t flushTimeoutMs;    /* telemetry flush cap */
  uint8_t  sensorPowerPin;    /* GPIO power-gating for sensors; PIN_UNUSED = off */
  uint8_t  flags;             /* AIR_FLAG_* below (bit 0) + dirty-resume count (bits 4..7) */
  uint32_t crc32;
};

/* ── flags ────────────────────────────────────────────────────────────────────
 * Bit 0 — the operator ARMED the hibernation cycle, and it stays armed across
 * resets. The watchdog scratch marker answers a different question ("did I just
 * wake from sleep?") and is deliberately cleared on every boot, so on its own it
 * cannot tell a device that was never hibernating from one whose cycle a reset
 * interrupted. Without this bit the second case fell back to M0 awake, radio on,
 * and only the full idle timeout could bring it back — which on the bench never
 * happened, because the fault repeated first (plan F25).
 *
 * Bits 4..7 — how many consecutive boots resumed the cycle after an UNCLEAN
 * reset. It is the crash-loop guard: the point of clearing the marker was to
 * keep a device that dies inside the cycle reachable, and resuming
 * unconditionally would throw that away. A few dirty resumes get a short grace;
 * past AIR_MAX_DIRTY_BOOTS the device holds M0 for the full idle timeout so an
 * operator has a window to get in. Zeroed by the first healthy sleep.
 *
 * A v2 file (or a missing one) reads flags = 0, which means "not armed" — the
 * safe default, and the reason this needed no version bump. */
#define AIR_FLAG_CYCLE_ARMED 0x01u
#define AIR_DIRTY_SHIFT 4
#define AIR_DIRTY_MASK  0xF0u

inline bool airCycleArmed(const AirConfig& c) {
  return (c.flags & AIR_FLAG_CYCLE_ARMED) != 0;
}

inline uint8_t airDirtyBoots(const AirConfig& c) {
  return (uint8_t)((c.flags & AIR_DIRTY_MASK) >> AIR_DIRTY_SHIFT);
}

inline void airSetDirtyBoots(AirConfig& c, uint8_t n) {
  if (n > 15) n = 15;
  c.flags = (uint8_t)((c.flags & ~AIR_DIRTY_MASK) | (uint8_t)(n << AIR_DIRTY_SHIFT));
}

inline AirConfig airDefaultConfig( ) {
  AirConfig c;
  memset(&c, 0, sizeof(c));
  c.magic = AIR_CONFIG_MAGIC;
  c.version = AIR_CONFIG_VERSION;
#if SIMUT_AIR
  c.idleTimeoutSec  = AIR_IDLE_TIMEOUT_SEC;
  c.stabTimeoutMs   = AIR_STAB_TIMEOUT_MS;
  c.chargerPin      = AIR_CHARGER_PIN;
  c.chargerRsv      = 0;
  c.connectTimeoutMs  = AIR_CONNECT_TIMEOUT_MS;
  c.flushTimeoutMs    = AIR_FLUSH_TIMEOUT_MS;
  c.sensorPowerPin    = AIR_SENSOR_POWER_PIN;
#endif
  return c;
}

/* CRC32 over every byte except the trailing crc32 field. Same polynomial as
 * StorageManager (IEEE 802.3), kept local so AirConfig.h has no heavy deps. */
inline uint32_t airCrc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

inline uint32_t airComputeCrc(const AirConfig& c) {
  return airCrc32(reinterpret_cast<const uint8_t*>(&c), offsetof(AirConfig, crc32));
}

/* A GPIO number this chip has AND the board leaves free, or the "off"
 * sentinel.
 *
 * 23, 24, 25 and 29 are not free on a Pico W: they are the CYW43 side band —
 * WL_ON / power-save, the shared SPI data line, the chip select that doubles
 * as the LED, and the ADC3 / VSYS sense. Driving any of them as a charger or
 * sensor-power pin takes the radio down, and on a device that spends its life
 * asleep the symptom is a unit that stops reporting with no console attached
 * to say why. The bound alone (<= 29) accepted all four (V-06). */
inline bool airPinValid(uint8_t pin) {
  if (pin == PIN_UNUSED) return true;
  if (pin > 29) return false;
  switch (pin) {
    case 23: case 24: case 25: case 29: return false;  /* CYW43 on the Pico W */
    default: return true;
  }
}

/* An idle timeout the uint16 field can actually hold (plan F09).
 *
 * The upper bound is the field, not the wording: `air idle` used to accept up
 * to 86400 and cast, so 86400 became 20864 and 65536 became ZERO. Zero is the
 * one that hurts — it makes the M0 loop hibernate on its next pass, and the
 * only way back in is catching a wake window on the console. The floor keeps
 * the operator's window usable at all. */
inline bool airIdleSecValid(long sec) {
  return sec >= 10 && sec <= 65535;
}

/* Fix up what a file cannot be trusted to carry: a field whose meaning changed
 * under it (chargerPin, see the struct) reads as whatever the old field held. */
inline void airSanitise(AirConfig& c) {
  if (!airPinValid(c.chargerPin)) {
#if SIMUT_AIR
    c.chargerPin = AIR_CHARGER_PIN;
#else
    c.chargerPin = PIN_UNUSED;
#endif
    c.chargerRsv = 0;
  }
}

inline bool airConfigValid(const AirConfig& c) {
  if (c.magic != AIR_CONFIG_MAGIC) return false;
  if (c.version != AIR_CONFIG_VERSION) return false;
  return c.crc32 == airComputeCrc(c);
}
