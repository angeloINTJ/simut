/**
 * @file    src/ota/ota_layout.h
 * @brief   Layout de memória flash para o sistema OTA SIMUT (Fase 4+).
 *
 * @details Layout REAL do Pico W (2 MB QSPI flash) confirmado por inspeção
 *          do builder do PlatformIO (`platforms/raspberrypi/builder/main.py`):
 *
 *          | Endereço (XIP)              | Tamanho | Função                    |
 *          |-----------------------------|---------|---------------------------|
 *          | 0x10000000 - 0x100FEFFF     | 1020 KB | Sketch slot (app + boot2) |
 *          | 0x100FF000 - 0x101FEFFF     | 1024 KB | LittleFS (modo normal) /  |
 *          |                             |         | staging .bin.gz (update)  |
 *          | 0x101FF000 - 0x101FFFFF     |    4 KB | OTA metadata              |
 *          |                             |         | (era EEPROM emulada do    |
 *          |                             |         |  core; SIMUT não usa)     |
 *
 *          DIVERGÊNCIA do plano §3 (corrigida aqui):
 *          - Plano: sketch=1024KB, staging=1020KB, metadata=4KB
 *          - Real:  sketch=1020KB, staging=1024KB, metadata=4KB (era EEPROM)
 *
 *          Os 4 KiB finais foram REIVINDICADOS da EEPROM emulada do
 *          arduino-pico core. Verificado: SIMUT não inclui <EEPROM.h>
 *          em nenhum .cpp/.h/.ino — zero risco de colisão.
 *
 *          ATENÇÃO: nunca chamar EEPROM API no SIMUT (`EEPROM.begin/read/
 *          write/commit`). Se alguma lib futura precisar, o OTA metadata
 *          terá que migrar pra outro lugar.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>

/* No install over the air on the RP2350 until its A/B slots exist
 * (docs/analysis/OTA_AB_RP2350.md, step 2). By the code, a stage there took a
 * signed RP2040 release — its boot2 CRC checks, and its env is "release" like
 * the RP2350 image's — and the applier would copy it over a program the
 * RP2350's ROM cannot boot: the board waits in BOOTSEL. On the bench board
 * (2026-10-03, main badb4a8) three such uploads began, and each dropped before
 * its end (9.8 to 28.7 s). The SDK defines PICO_RP2350=1 only on that chip
 * (lib/rp2350/platform_def.txt), so the RP2040 images keep their code. */
#if defined(PICO_RP2350) && PICO_RP2350
#define OTA_INSTALL_AVAILABLE 0
#else
#define OTA_INSTALL_AVAILABLE 1
#endif
/* What the stage and the apply answer instead (501); the Files page shows it. */
#define OTA_UNAVAILABLE_TEXT "Over-the-air update is not available on the RP2350 yet. Install over USB."

/* Everything after the RP2350's map below is the Pico W's 2 MB map: the
 * staging area, the config snapshot and the metadata. On the RP2350 those
 * offsets fall inside the slots (step 3), and with the program mapped from
 * slot A an XIP read there faults, because the window the boot ROM maps is the
 * slot's 1,532 KB. OTA_RP2040_MAP is 0 there: every function that reads or
 * writes them (metadata.cpp, config_snapshot.cpp, staging.cpp) answers
 * "absent" or refuses, so the boot finds no update in flight and no snapshot,
 * as on a board that never had one. */
#if defined(PICO_RP2350) && PICO_RP2350
#define OTA_RP2040_MAP 0
#else
#define OTA_RP2040_MAP 1
#endif

#if defined(PICO_RP2350) && PICO_RP2350
/* The RP2350's map (step 3): what tools/rp2350/partition_table.json writes
 * for the boot ROM, in offsets from the start of the flash.
 * tools/test_rp2350_layout.py holds these numbers to that table, to
 * tools/rp2350/memmap_slot.ld and to the PlatformIO profile. One image runs in
 * either slot, because the ROM maps the slot it boots at 0x10000000. LittleFS,
 * outside the slot, is read through the untranslated window
 * (tools/arduino_pico_overrides/patches/littlefs_rp2350_untranslated.patch). */
#define OTA_RP2350_PT_OFFSET      0x000000u
#define OTA_RP2350_PT_SIZE        0x002000u
#define OTA_RP2350_SLOT_A_OFFSET  0x002000u
#define OTA_RP2350_SLOT_B_OFFSET  0x181000u
#define OTA_RP2350_SLOT_SIZE      0x17F000u   /* 1,532 KB: the largest image */
#define OTA_RP2350_FS_OFFSET      0x300000u
#define OTA_RP2350_FS_SIZE        0x0FF000u
#define OTA_RP2350_EEPROM_OFFSET  0x3FF000u   /* arduino-pico's 4 KB, unused by SIMUT */
#ifdef __cplusplus
static_assert(OTA_RP2350_PT_OFFSET + OTA_RP2350_PT_SIZE == OTA_RP2350_SLOT_A_OFFSET,
              "slot A must follow the partition table");
static_assert(OTA_RP2350_SLOT_A_OFFSET + OTA_RP2350_SLOT_SIZE == OTA_RP2350_SLOT_B_OFFSET,
              "slot B must follow slot A");
static_assert(OTA_RP2350_SLOT_B_OFFSET + OTA_RP2350_SLOT_SIZE == OTA_RP2350_FS_OFFSET,
              "LittleFS must follow slot B");
static_assert(OTA_RP2350_FS_OFFSET + OTA_RP2350_FS_SIZE == OTA_RP2350_EEPROM_OFFSET,
              "the EEPROM sector must follow LittleFS");
static_assert(OTA_RP2350_EEPROM_OFFSET + 4096u == 4u * 1024u * 1024u,
              "the map must cover the Pico 2 W's 4 MB");
static_assert((OTA_RP2350_SLOT_SIZE % 4096u) == 0u && (OTA_RP2350_FS_OFFSET % 4096u) == 0u,
              "slots and LittleFS must be sector-aligned");
#endif
#endif

/* Constantes de tamanho — em bytes. */
#define OTA_FLASH_TOTAL          (2u * 1024u * 1024u)          /* 2 MB Pico W */
#define OTA_EEPROM_RESERVED      (4u * 1024u)                  /* 4 KB EEPROM emulada — reivindicada */
#define OTA_FILESYSTEM_SIZE      (1u * 1024u * 1024u)          /* 1 MB LittleFS — bate com platformio.ini */

/* Offsets relativos ao início do flash (0x10000000 quando via XIP). */
#define OTA_APP_OFFSET           (0u)
#define OTA_APP_MAX_SIZE         (OTA_FLASH_TOTAL - OTA_EEPROM_RESERVED - OTA_FILESYSTEM_SIZE)
                                                                /* 0x000000 - 0x0FEFFF (1020 KB) */

/* The largest image the apply can copy WITHOUT touching the config snapshot
 * (sectors 254..255 of the staging area, OTA_SNAPSHOT_OFFSET below). The
 * physical slot is 1020 KiB, but an image between 1016 and 1020 KiB has its
 * tail in the same sectors the snapshot is written to at stage time: the
 * validator saw a good CRC over bytes the snapshot then overwrote, and the
 * device booted a corrupted image with every layer reporting success. This
 * is the ceiling ota_validate_staging( ) and the applier enforce. */
#define OTA_APP_SAFE_MAX_SIZE    (OTA_APP_MAX_SIZE - OTA_FLASH_SECTOR_SIZE)  /* 1016 KiB */

#define OTA_STAGING_OFFSET       (OTA_APP_MAX_SIZE)             /* 0x0FF000 */
#define OTA_STAGING_MAX_SIZE     (OTA_FILESYSTEM_SIZE)          /* 0x100000 (1024 KB) */

#define OTA_METADATA_OFFSET      (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE)  /* 0x1FF000 */
#define OTA_METADATA_SIZE        (OTA_EEPROM_RESERVED)          /* 4 KB */

/* Fase 9 (v3.43.16+): snapshot da config crítica no FIM da staging area.
 * v21 (segunda linha de telemetria de alarmes) cresceu SystemConfig além
 * dos 4076 B úteis de um setor, então o snapshot ganhou um SEGUNDO setor
 * (8 KiB no total, setores 254..255 da staging).
 *
 * O firmware típico ocupa ~1.004 KiB; o apply copia setores completos
 * ceil(raw_size / 4 KiB), e o primeiro setor do snapshot começa em
 * exatamente 1.016 KiB — o mesmo "sketch máximo seguro" de antes.
 * Persiste através do apply destrutivo; só é apagado pelo
 * staging_erase_all do PRÓXIMO ciclo OTA, momento em que já não importa
 * (snapshot da OTA atual já foi consumido no boot).
 *
 * Sketch máximo seguro mantendo snapshot intacto: 1.020 KiB - 4 KiB =
 * 1.016 KiB (INALTERADO — o apply nunca lê o setor 254 para
 * raw_size <= 1.016 KiB). Atualmente em ~1.004 KiB → margem 12 KiB. */
#define OTA_SNAPSHOT_OFFSET      (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE - 2u * OTA_FLASH_SECTOR_SIZE)
                                                                /* 0x1FD000 */
#define OTA_SNAPSHOT_SIZE        (2u * OTA_FLASH_SECTOR_SIZE)   /* 8 KiB */

/* Sanity check em compile-time. */
#ifdef __cplusplus
static_assert(OTA_APP_OFFSET == 0u, "App offset must be 0");
static_assert(OTA_APP_OFFSET + OTA_APP_MAX_SIZE == OTA_STAGING_OFFSET, "App and staging are not adjacent");
static_assert(OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE == OTA_METADATA_OFFSET, "Staging and metadata are not adjacent");
static_assert(OTA_METADATA_OFFSET + OTA_METADATA_SIZE == OTA_FLASH_TOTAL, "Layout does not cover full flash");
static_assert((OTA_STAGING_OFFSET % 4096u) == 0u, "Staging must be 4KB-aligned for flash erase");
static_assert((OTA_METADATA_OFFSET % 4096u) == 0u, "Metadata must be 4KB-aligned for flash erase");
#endif

/* Constantes do hardware Pico SDK (replicadas pra evitar include de hardware/flash.h
 * em arquivos non-Pico). Mantenha em sync. */
#define OTA_FLASH_PAGE_SIZE      256u    /* Mínimo write granularity (write em múltiplos). */
#define OTA_FLASH_SECTOR_SIZE    4096u   /* Mínimo erase granularity. */
