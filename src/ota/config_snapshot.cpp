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
#include "../StorageManager.h"
#include "../SystemDefs_Records.h" /* crc32_init/update/final */

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

/* The snapshot is the only thing carrying config across a destructive apply, so
 * a SystemConfig that outgrows the region does not fail loudly — the device
 * simply comes up on defaults after an update, having lost WiFi, users and
 * every sensor slot. Nothing else in the build ties the two together.
 *
 * v20 cabia num setor (3921 B contra 4076 úteis, margem 155 B). A v21 anexou
 * AlarmTelConfig (segunda linha de telemetria) e passou o orçamento; a região
 * ganhou um SEGUNDO setor (8172 B úteis), e o buffer de scratch em applier.cpp
 * cresceu junto. A folga era ~3,4 KiB na v21; 1.430 B na v25; na v26
 * (SystemConfig 6802 B mais 4 de CRC contra 8172 úteis) é de 1.366 B.
 *
 * Fails at compile time instead. If this fires, either the config has to shrink
 * or the snapshot needs a THIRD sector — do not just raise the constant. */
static_assert(sizeof(SystemConfig) + sizeof(uint32_t) <= ota::CONFIG_SNAPSHOT_PAYLOAD_MAX,
              "SystemConfig + CRC no longer fits the OTA config snapshot: an "
              "update would silently factory-reset every device");

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

namespace ota {

/* ---------------------------------------------------------------------------
 * Helpers privados
 * ------------------------------------------------------------------------- */

#if OTA_RP2040_MAP   /* the RP2350 has no snapshot region (ota_layout.h) */
static const uint8_t* snapshot_region_xip() {
    return (const uint8_t*)(XIP_BASE + OTA_SNAPSHOT_OFFSET);
}

/* Payload size of a valid snapshot laid out at @p base (magic, schema, size in
 * range, CRC32 over header + payload), or 0. The same check serves the region
 * itself (XIP) and the copy in s_applier_buf. */
static uint32_t valid_payload_size(const uint8_t* base) {
    ConfigSnapshotHeader hdr;
    memcpy(&hdr, base, sizeof(hdr));
    if (hdr.magic != CONFIG_SNAPSHOT_MAGIC ||
        hdr.schema_version != CONFIG_SNAPSHOT_VERSION ||
        hdr.payload_size == 0 ||
        hdr.payload_size > CONFIG_SNAPSHOT_PAYLOAD_MAX) {
        return 0;
    }
    /* Valida CRC32 — sem isso, restore com payload corrompido pode resultar
     * em config quebrada (CRC mismatch dentro do próprio system.bin pode
     * subir, mas qualquer corrupção silenciosa é pior que factory). */
    uint32_t crc = crc32_init();
    crc = crc32_update(crc, base, sizeof(hdr) + hdr.payload_size);
    crc = crc32_final(crc);

    uint32_t stored = 0;
    memcpy(&stored, base + sizeof(hdr) + hdr.payload_size, sizeof(stored));
    return (crc == stored) ? hdr.payload_size : 0;
}
#endif

/* ---------------------------------------------------------------------------
 * API pública
 * ------------------------------------------------------------------------- */

uint16_t ota_snapshot_serialize() {
    /* Read-only: lê LFS, monta `[hdr | payload | crc]` em s_applier_buf.
     * NÃO chama flash_range_*. Pode ser chamada com Core 1 ativo. */
    File f = LittleFS.open(FILE_CONFIG, "r");
    if (!f) {
        return 0; /* sem config — nada a salvar (factory boot pendente) */
    }
    const size_t fsize = f.size();
    if (fsize == 0 || fsize > CONFIG_SNAPSHOT_PAYLOAD_MAX) {
        f.close();
        return 0;
    }

    /* Layout em s_applier_buf:
     *   [0..15]                   ConfigSnapshotHeader
     *   [16..16+payload_size]     system.bin raw
     *   [tail 4 B]                CRC32 sobre [magic..last payload byte]
     * Total <= CONFIG_SNAPSHOT_REGION_SIZE (8192 B). */
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
    if (bytes_read != fsize) return 0;

    uint32_t crc = crc32_init();
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

bool ota_snapshot_present() {
#if !OTA_RP2040_MAP
    return false;  /* no snapshot region on the RP2350 (ota_layout.h) */
#else
    return valid_payload_size(snapshot_region_xip()) != 0;
#endif
}

uint32_t ota_snapshot_stash() {
#if !OTA_RP2040_MAP
    return 0;      /* no snapshot region on the RP2350 (ota_layout.h) */
#else
    /* The restore reads this copy, never the region. The region is the last
     * two blocks of the LittleFS partition, and it is only ever needed on a
     * freshly formatted filesystem — where the allocator starts at a block
     * derived from the superblock's CRC, which carries the format's timestamp,
     * so the directories and READMEs StorageManager::begin( ) creates can land
     * on it. They used to be created before the restore read it. Seen on the
     * bench 2026-09-30: after a format and a boot, a directory's metadata pair
     * sat in blocks 254..255 and the snapshot was gone before anything read
     * it. */
    memcpy(s_applier_buf, snapshot_region_xip(), CONFIG_SNAPSHOT_REGION_SIZE);
    return valid_payload_size(s_applier_buf);
#endif
}

bool ota_snapshot_restore_stash(uint32_t payload_size) {
    if (payload_size == 0 || payload_size > CONFIG_SNAPSHOT_PAYLOAD_MAX) return false;
    const uint8_t* payload = s_applier_buf + sizeof(ConfigSnapshotHeader);

    /* Caminho idempotente: se o restore cair (power loss entre open e close),
     * o próximo boot tenta de novo — o open("w") do framework já deixa um
     * system.bin vazio comitado, que o begin( ) conta como ausente, e o
     * snapshot só é apagado depois que o boot passa do StorageManager::begin( )
     * (AppManager_Boot.cpp). */
    File f = LittleFS.open(FILE_CONFIG, "w");
    if (!f) return false;
    size_t written = f.write(payload, payload_size);
    f.close();
    return written == payload_size;
}

} /* namespace ota */
