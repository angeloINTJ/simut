/**
 * @file    src/ota/config_snapshot.cpp
 * @brief   Captura e restore do `/config/system.bin` em torno do apply OTA.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "config_snapshot.h"
#include "metadata.h"
#include "ota_layout.h"
#include "../../StorageManager.h"
#include "../../SystemDefs_Records.h" /* crc32_init/update/final */

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

/* ---------------------------------------------------------------------------
 * Helpers privados
 * ------------------------------------------------------------------------- */

static const uint8_t* snapshot_region_xip() {
    return (const uint8_t*)(XIP_BASE + OTA_METADATA_OFFSET + OTA_FLASH_PAGE_SIZE);
}

static bool read_header_from_xip(ConfigSnapshotHeader& hdr) {
    memcpy(&hdr, snapshot_region_xip(), sizeof(hdr));
    return hdr.magic == CONFIG_SNAPSHOT_MAGIC &&
           hdr.schema_version == CONFIG_SNAPSHOT_VERSION &&
           hdr.payload_size > 0 &&
           hdr.payload_size <= CONFIG_SNAPSHOT_PAYLOAD_MAX;
}

/* ---------------------------------------------------------------------------
 * API pública
 * ------------------------------------------------------------------------- */

bool ota_snapshot_capture() {
    File f = LittleFS.open(FILE_CONFIG, "r");
    if (!f) {
        return false; /* sem config — nada a salvar (factory boot pendente) */
    }
    const size_t fsize = f.size();
    if (fsize == 0 || fsize > CONFIG_SNAPSHOT_PAYLOAD_MAX) {
        f.close();
        return false;
    }

    /* Monta diretamente em s_applier_buf:
     *   [0..15]                   ConfigSnapshotHeader
     *   [16..16+payload_size]     system.bin raw
     *   [last 4 B]                CRC32 sobre [magic..last payload byte]
     * O scratch tem 4 KiB; cabe a região 1..15 inteira (3840 B). */
    uint8_t* region = s_applier_buf;
    ConfigSnapshotHeader& hdr = *reinterpret_cast<ConfigSnapshotHeader*>(region);
    hdr.magic          = CONFIG_SNAPSHOT_MAGIC;
    hdr.schema_version = CONFIG_SNAPSHOT_VERSION;
    hdr.reserved0      = 0;
    hdr.payload_size   = (uint32_t)fsize;
    hdr.reserved1      = 0;

    uint8_t* payload = region + sizeof(ConfigSnapshotHeader);
    size_t bytes_read = f.read(payload, fsize);
    f.close();
    if (bytes_read != fsize) return false;

    /* CRC32 sobre header + payload. */
    uint32_t crc = crc32_init();
    crc = crc32_update(crc, region, sizeof(ConfigSnapshotHeader) + fsize);
    crc = crc32_final(crc);
    uint8_t* crc_pos = region + sizeof(ConfigSnapshotHeader) + fsize;
    memcpy(crc_pos, &crc, sizeof(crc));

    const uint16_t total_len = (uint16_t)(sizeof(ConfigSnapshotHeader) + fsize + sizeof(crc));
    return ota_snapshot_write(region, total_len);
}

bool ota_snapshot_present() {
    ConfigSnapshotHeader hdr;
    if (!read_header_from_xip(hdr)) return false;

    /* Valida CRC32 — sem isso, restore com payload corrompido pode resultar
     * em config quebrada (CRC mismatch dentro do próprio system.bin pode
     * subir, mas qualquer corrupção silenciosa é pior que factory). */
    const uint8_t* base = snapshot_region_xip();
    uint32_t crc = crc32_init();
    crc = crc32_update(crc, base, sizeof(hdr) + hdr.payload_size);
    crc = crc32_final(crc);

    uint32_t stored = 0;
    memcpy(&stored, base + sizeof(hdr) + hdr.payload_size, sizeof(stored));
    return crc == stored;
}

bool ota_snapshot_restore_to_lfs() {
    ConfigSnapshotHeader hdr;
    if (!read_header_from_xip(hdr)) return false;
    if (!ota_snapshot_present()) return false; /* CRC mismatch — abandona */

    const uint8_t* payload = snapshot_region_xip() + sizeof(ConfigSnapshotHeader);

    /* Caminho idempotente: se restore parcial cair (power loss entre open
     * e close), próximo boot tenta de novo (snapshot ainda no flash). */
    File f = LittleFS.open(FILE_CONFIG, "w");
    if (!f) return false;
    size_t written = f.write(payload, hdr.payload_size);
    f.close();
    return written == hdr.payload_size;
}

} /* namespace ota */
