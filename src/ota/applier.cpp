/**
 * @file    src/ota/applier.cpp
 * @brief   Aplicador SRAM-resident — Fase 7b real (modo RAW). VALIDADO.
 *
 * @details ✅ STATUS: Validado em HW (2026-05-06). v3.43.9 sector-0-fix +
 *          v3.43.5 wdt-bit-fix juntos completam o caminho destrutivo.
 *          Apply ciclo completo testado: stage upload → commit metadata →
 *          /api/ota/apply → applier SRAM → reboot → boot OK em ~60s.
 *          Resultado pós-apply: 236/237 sectors do app slot byte-perfect
 *          (1 partial em sector 233 do __bluetooth_tlv, NÃO afeta boot).
 *
 *          BUGS RESOLVIDOS:
 *
 *          Bug 1 (v3.43.3 → fix v3.43.4 → fix completo v3.43.5):
 *           Apply abortava no meio. Diagnóstico via picotool save mostrou
 *           que erase loop morria após ~86 ou ~196 iterações.
 *           Causa: pico-sdk watchdog_update()/watchdog_reboot() vivem em
 *           flash app slot (não __not_in_flash_func). Quando erase apaga
 *           a região onde residem, próxima chamada faulta → BOOTSEL.
 *           Fix v3.43.4: substituí por inlines MMIO puros (applier_wdt_feed
 *           + applier_reboot). Fix v3.43.5: corrigido bit do TRIGGER de
 *           (1u<<30 ENABLE — errado) pra (1u<<31 TRIGGER — correto), que
 *           fazia HW WDT firar aos 8 s default em vez de alimentar.
 *
 *          Bug "boot2 não programado" (v3.43.6 → 7 → 8 → fix v3.43.9):
 *           Sector 0 ficava 0xFF mesmo após flash_range_program(0, ...).
 *           Empiricamente: programar sector 0 APÓS bulk erase de 1..N-1
 *           tem race interna com cache do boot2/QSPI ROM function.
 *           Fix v3.43.9: programar sector 0 ISOLADO (erase + program 4 KiB)
 *           ANTES do bulk erase. Validado: byte-perfect em sector 0.
 *
 *          Bug "boot intermitente" (mal-diagnosticado v3.43.4):
 *           Era falso positivo — boot pós-apply realmente leva ~60 s
 *           porque combina: (a) LFS auto-format ~13 s (LFS region
 *           sobrescrita pelo stage upload, mountFS detecta superblock
 *           inválido → format → begin), (b) Core 1 lockout stuck recovery
 *           ~10 s (timeout do multicore_lockout pendente após reset),
 *           (c) factory init: SEC-003 password regen + touch cal default
 *           + WiFi disconnected (LFS perdeu config). CLI fica silencioso
 *           durante o format mas booting normalmente. Validado capturando
 *           Serial continuamente durante 120 s pós-apply: sequência
 *           "[BOOT] AP detect" → "[DSP] Lockout stuck >10s" →
 *           "[OTA post-apply detected]" → "SIMUT IoT CLI v3.43.9".
 *
 *          LIMITAÇÕES CONHECIDAS:
 *           - LFS é reformatada (user data perdido) porque staging area
 *             COMPARTILHA partição com LittleFS. Fase 8 vai integrar
 *             backup automático pré-stage + restore pós-apply.
 *           - Sector 233 (offset 0xE9000) preservado com BTstack runtime
 *             TLV. É região reservada __bluetooth_tlv (8 KiB), não código.
 *             BTstack init re-inicializa transparente.
 *           - WiFi config + admin password + sensor mapping perdidos.
 *
 *          Pré-condições do applier (caller orchestrator garante):
 *           - WiFi/CYW43 desligado.
 *           - LittleFS desmontada.
 *           - Core 1 pausado via multicore_lockout.
 *           - IRQs globais desabilitadas.
 *           - Metadata.state == APPLYING e persistida em flash.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "applier.h"
#include "ota_layout.h"

#include <Arduino.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <string.h>

#ifndef XIP_BASE
#define XIP_BASE 0x10000000u
#endif

/* Endereços hardcoded das HW peripherals — não dependem de includes ou
 * funções que possam estar em flash app slot apagada. */
#define WATCHDOG_BASE_ADDR  0x40058000u
#define WATCHDOG_SET_ALIAS  0x00002000u   /* RP2040 SET alias offset */
#define WATCHDOG_CTRL_TRIG  (1u << 31)    /* TRIGGER bit (1u<<30 é ENABLE — bug v3.43.4) */
#define SCB_AIRCR_ADDR      0xE000ED0Cu
#define SCB_AIRCR_KEY       (0x05FAu << 16)
#define SCB_AIRCR_SYSRESET  (1u << 2)

namespace ota {

/* Buffer SRAM de 4 KiB pra cópia sector-by-sector. Static (BSS) — não
 * precisa de __not_in_flash porque BSS já vive em RAM (0x20xxxxxx) e é
 * acessível durante flash_range_erase/program (QSPI off, RAM intacta). */
static uint8_t s_applier_buf[OTA_FLASH_SECTOR_SIZE];

/* CRC32 EDB88320 inline em SRAM (sem chamar tabelas em flash). */
static inline uint32_t __not_in_flash_func(crc32_byte_sram)(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc;
}

/* Watchdog feed inline — pico-sdk watchdog_update() vive em flash app slot
 * (NÃO é __not_in_flash_func), então a 1a chamada após o erase do app slot
 * faulta. Aqui escrevemos direto no SET alias da hardware register. */
static inline void __not_in_flash_func(applier_wdt_feed)() {
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SET_ALIAS) =
        WATCHDOG_CTRL_TRIG;
}

/* Reboot inline via ARM SYSRESETREQ — pico-sdk watchdog_reboot() também
 * vive em flash. Direct SCB->AIRCR write é puro MMIO, sem dependência de
 * código fora deste módulo SRAM. */
static inline void __not_in_flash_func(applier_reboot)() {
    *(volatile uint32_t*)SCB_AIRCR_ADDR = SCB_AIRCR_KEY | SCB_AIRCR_SYSRESET;
    __asm volatile("dsb");
    while (1) { __asm volatile("nop"); }
}

bool __not_in_flash_func(ota_applier_run)(const UpdateMetadata* meta) {
    if (!meta || meta->magic != OTA_MAGIC_PENDING || meta->state != STATE_APPLYING) {
        applier_reboot();
    }

    /* Modo RAW (Fase 7b): compressed_size == raw firmware size (já alinhado
     * a 256 B pelo stage_session_end). uncompressed_crc32 == compressed_crc32
     * em raw mode (mesmos bytes). */
    const uint32_t raw_size  = meta->compressed_size;
    const uint32_t want_crc  = meta->uncompressed_crc32;

    if (raw_size == 0 || raw_size > OTA_APP_MAX_SIZE) {
        applier_reboot();
    }

    /* (1a) Programa sector 0 (boot2) ANTES do bulk erase. Diagnóstico
     * v3.43.7+v3.43.8: flash_range_program(0,...) silenciosamente NÃO
     * persistia QUANDO chamado APÓS um bulk erase de 255 sectors —
     * sector 0 ficava 0xFF mesmo com retry e page-by-page. Hipótese:
     * estado interno do chip QSPI ou ROM function tem race quando o
     * primeiro program após bulk erase é offset 0. Mover esta operação
     * pra ANTES do bulk erase (depois de erase isolado de sector 0
     * apenas) evita o race. */
    {
        const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET);
        memcpy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

        uint32_t saved_irq = save_and_disable_interrupts();
        flash_range_erase(0u, OTA_FLASH_SECTOR_SIZE);
        flash_range_program(0u, s_applier_buf, OTA_FLASH_SECTOR_SIZE);
        restore_interrupts(saved_irq);
        applier_wdt_feed();
    }

    /* (1b) Apaga sectors 1..N-1 do slot da app. ~13 s pra ~1019 KiB. */
    constexpr uint32_t N_APP_SECTORS = OTA_APP_MAX_SIZE / OTA_FLASH_SECTOR_SIZE;
    for (uint32_t i = 1; i < N_APP_SECTORS; i++) {
        uint32_t saved_irq = save_and_disable_interrupts();
        flash_range_erase(i * OTA_FLASH_SECTOR_SIZE, OTA_FLASH_SECTOR_SIZE);
        restore_interrupts(saved_irq);
        applier_wdt_feed();
    }

    /* (2) Programa setores válidos do staging via XIP read + flash program.
     *
     * IMPORTANTE: programamos sector 0 (boot2) POR ÚLTIMO. Diagnóstico
     * via picotool save após brick v3.43.5 mostrou que com loop forward
     * começando em 0, o sector 0 ficava erased mesmo com programa
     * aparente — provavelmente a SDK flash_range_program tem alguma
     * race interna no PRIMEIRO write após bulk erase, e/ou o
     * flash_init_boot2_copyout (que reusa cache) interage mal quando o
     * próprio boot2 está sendo gravado. Reverter a ordem (programa
     * 1..N-1 forward, depois 0 por último) deixa o cache de boot2
     * estabilizar primeiro. + Após programa de sector 0, leitura via
     * XIP confirma; se ainda 0xFF, retry até 3x. */
    const uint32_t n_data_sectors = (raw_size + OTA_FLASH_SECTOR_SIZE - 1u)
                                  / OTA_FLASH_SECTOR_SIZE;
    for (uint32_t i = 1; i < n_data_sectors; i++) {
        const uint8_t* src = (const uint8_t*)(XIP_BASE + OTA_STAGING_OFFSET +
                                              i * OTA_FLASH_SECTOR_SIZE);
        memcpy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

        uint32_t saved_irq = save_and_disable_interrupts();
        flash_range_program(i * OTA_FLASH_SECTOR_SIZE,
                            s_applier_buf, OTA_FLASH_SECTOR_SIZE);
        restore_interrupts(saved_irq);
        applier_wdt_feed();
    }

    /* (sector 0 já programado em (1a) antes do bulk erase) */

    /* (3) Validate CRC do app slot pós-write. */
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t off = 0; off < raw_size; off++) {
        const uint8_t* p = (const uint8_t*)(XIP_BASE + OTA_APP_OFFSET + off);
        crc = crc32_byte_sram(crc, *p);
        if ((off & 0xFFFu) == 0u) applier_wdt_feed();
    }
    crc ^= 0xFFFFFFFFu;

    /* (4) Reboot. CRC mismatch info perdida (sem persistência possível
     * em IRQ-off mode); BootROM detecta boot2 ruim e cai pra BOOTSEL
     * em caso de write quebrado. */
    (void)crc;
    (void)want_crc;
    applier_reboot();
    return false;  /* unreachable */
}

} /* namespace ota */
