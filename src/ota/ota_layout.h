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

/* Constantes de tamanho — em bytes. */
#define OTA_FLASH_TOTAL          (2u * 1024u * 1024u)          /* 2 MB Pico W */
#define OTA_EEPROM_RESERVED      (4u * 1024u)                  /* 4 KB EEPROM emulada — reivindicada */
#define OTA_FILESYSTEM_SIZE      (1u * 1024u * 1024u)          /* 1 MB LittleFS — bate com platformio.ini */

/* Offsets relativos ao início do flash (0x10000000 quando via XIP). */
#define OTA_APP_OFFSET           (0u)
#define OTA_APP_MAX_SIZE         (OTA_FLASH_TOTAL - OTA_EEPROM_RESERVED - OTA_FILESYSTEM_SIZE)
                                                                /* 0x000000 - 0x0FEFFF (1020 KB) */

#define OTA_STAGING_OFFSET       (OTA_APP_MAX_SIZE)             /* 0x0FF000 */
#define OTA_STAGING_MAX_SIZE     (OTA_FILESYSTEM_SIZE)          /* 0x100000 (1024 KB) */

#define OTA_METADATA_OFFSET      (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE)  /* 0x1FF000 */
#define OTA_METADATA_SIZE        (OTA_EEPROM_RESERVED)          /* 4 KB */

/* Fase 9 (v3.43.16+): snapshot da config crítica no ÚLTIMO setor da
 * staging area. O firmware típico ocupa ~1.004 KiB → último setor de
 * 4 KiB intocado pelo apply. Persiste através do apply destrutivo;
 * só é apagado pelo staging_erase_all do PRÓXIMO ciclo OTA, momento em
 * que já não importa (snapshot da OTA atual já foi consumido no boot).
 *
 * Sketch máximo seguro mantendo snapshot intacto: 1.020 KiB - 4 KiB =
 * 1.016 KiB. Atualmente em ~1.004 KiB → margem 12 KiB. */
#define OTA_SNAPSHOT_OFFSET      (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE - OTA_FLASH_SECTOR_SIZE)
                                                                /* 0x1FE000 */
#define OTA_SNAPSHOT_SIZE        (OTA_FLASH_SECTOR_SIZE)        /* 4 KiB */

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
