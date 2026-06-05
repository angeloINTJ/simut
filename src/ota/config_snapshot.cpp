/**
 * @file src/ota/config_snapshot.cpp
 * @brief Capture and restore of /config/system.bin around OTA apply.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "config_snapshot.h"
#include "metadata.h"
#include "ota_layout.h"
#include "StorageManager.h"
#include "SystemDefs_Records.h" /* crc32_init/update/final */

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

/* ---------------------------------------------------------------------------
 * Private helpers
 * ------------------------------------------------------------------------- */

static const uint8_t* snapshot_region_xip( ) {
 return (const uint8_t*)(XIP_BASE + OTA_SNAPSHOT_OFFSET);
}

static bool read_header_from_xip(ConfigSnapshotHeader& hdr) {
 memcpy(&hdr, snapshot_region_xip( ), sizeof(hdr));
 return hdr.magic == CONFIG_SNAPSHOT_MAGIC &&
 hdr.schema_version == CONFIG_SNAPSHOT_VERSION &&
 hdr.payload_size > 0 &&
 hdr.payload_size <= CONFIG_SNAPSHOT_PAYLOAD_MAX;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

uint16_t ota_snapshot_serialize( ) {
 /* Read-only: reads LFS, assembles [hdr | payload | crc] in s_applier_buf.
 * Does NOT call flash_range_*. Can be called with Core 1 active. */
 File f = LittleFS.open(FILE_CONFIG, "r");
 if (!f) {
 return 0; /* no config — nothing to save (factory boot pending) */
 }
 const size_t fsize = f.size( );
 if (fsize == 0 || fsize > CONFIG_SNAPSHOT_PAYLOAD_MAX) {
 f.close( );
 return 0;
 }

 /* Layout in s_applier_buf:
 * [0..15] ConfigSnapshotHeader
 * [16..16+payload_size] system.bin raw
 * [tail 4 B] CRC32 over [magic..last payload byte]
 * Total <= CONFIG_SNAPSHOT_REGION_SIZE (3840 B). */
 uint8_t* region = s_applier_buf;
 ConfigSnapshotHeader& hdr = *reinterpret_cast<ConfigSnapshotHeader*>(region);
 hdr.magic = CONFIG_SNAPSHOT_MAGIC;
 hdr.schema_version = CONFIG_SNAPSHOT_VERSION;
 hdr.reserved0 = 0;
 hdr.payload_size = (uint32_t)fsize;
 hdr.reserved1 = 0;

 uint8_t* payload = region + sizeof(ConfigSnapshotHeader);
 size_t bytes_read = f.read(payload, fsize);
 f.close( );
 if (bytes_read != fsize) return 0;

 uint32_t crc = crc32_init( );
 crc = crc32_update(crc, region, sizeof(ConfigSnapshotHeader) + fsize);
 crc = crc32_final(crc);
 uint8_t* crc_pos = region + sizeof(ConfigSnapshotHeader) + fsize;
 memcpy(crc_pos, &crc, sizeof(crc));

 return (uint16_t)(sizeof(ConfigSnapshotHeader) + fsize + sizeof(crc));
}

bool ota_snapshot_commit(uint16_t total_len) {
 if (total_len == 0 || total_len > CONFIG_SNAPSHOT_REGION_SIZE) return false;
 return ota_snapshot_write(s_applier_buf, total_len);
}

bool ota_snapshot_present( ) {
 ConfigSnapshotHeader hdr;
 if (!read_header_from_xip(hdr)) return false;

 /* Validate CRC32 — without this, restore with corrupted payload may result
 * in broken config (CRC mismatch inside system.bin itself may
 * surface, but any silent corruption is worse than factory). */
 const uint8_t* base = snapshot_region_xip( );
 uint32_t crc = crc32_init( );
 crc = crc32_update(crc, base, sizeof(hdr) + hdr.payload_size);
 crc = crc32_final(crc);

 uint32_t stored = 0;
 memcpy(&stored, base + sizeof(hdr) + hdr.payload_size, sizeof(stored));
 return crc == stored;
}

bool ota_snapshot_restore_to_lfs( ) {
 ConfigSnapshotHeader hdr;
 if (!read_header_from_xip(hdr)) return false;
 if (!ota_snapshot_present( )) return false; /* CRC mismatch — abandon */

 const uint8_t* payload = snapshot_region_xip( ) + sizeof(ConfigSnapshotHeader);

 /* Idempotent path: if partial restore fails (power loss between open
 * and close), next boot tries again (snapshot still in flash). */
 File f = LittleFS.open(FILE_CONFIG, "w");
 if (!f) return false;
 size_t written = f.write(payload, hdr.payload_size);
 f.close( );
 return written == hdr.payload_size;
}

} /* namespace ota */
