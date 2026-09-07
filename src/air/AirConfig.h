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

/* scratch[1]: how many seconds the last sleep really lasted, read from the RTC
 * after the WFI returns and before the reset. Tagged so a garbage register is
 * not mistaken for a measurement; the low 24 bits hold the seconds (194 days,
 * far past any wake interval). Printed by the next boot beside the requested
 * value, which is what turns the cycle period into a measured number instead of
 * one inferred from USB enumeration. */
#define AIR_SLEPT_MAGIC 0x5E000000u
#define AIR_SLEPT_MASK  0xFF000000u

struct __attribute__((packed)) AirConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t idleTimeoutSec;    /* M0 inactivity -> auto-hibernate (seconds) */
  uint16_t stabTimeoutMs;     /* sensor stabilization cap */
  uint16_t wifiScanTimeoutMs; /* presence-scan cap */
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
  c.wifiScanTimeoutMs = AIR_WIFI_SCAN_TIMEOUT_MS;
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

inline bool airConfigValid(const AirConfig& c) {
  if (c.magic != AIR_CONFIG_MAGIC) return false;
  if (c.version != AIR_CONFIG_VERSION) return false;
  return c.crc32 == airComputeCrc(c);
}
