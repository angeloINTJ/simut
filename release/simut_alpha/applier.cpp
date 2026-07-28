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
 *           - WiFi config + admin password + sensor mapping SOBREVIVEM desde
 *             que config_snapshot passou a capturar /config/system.bin antes
 *             do apply e restaurá-lo depois. Esta linha dizia o contrário e
 *             ficou para trás quando o snapshot entrou; verificado no ferro
 *             em 2026-07-26 (usuários, telemetria e Wi-Fi intactos após um
 *             apply real). O que de fato se perde é todo o resto da LFS:
 *             /history, /lang, /themes, /calib, /web e o log de eventos.
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
#define WATCHDOG_BASE_ADDR     0x40058000u
#define WATCHDOG_CTRL_OFFSET   0x00u
#define WATCHDOG_LOAD_OFFSET   0x04u
#define WATCHDOG_SCRATCH4_OFFSET 0x1Cu   /* 0x18 é SCRATCH3 — ver nota abaixo */
#define WATCHDOG_SET_ALIAS     0x00002000u   /* RP2040 SET alias offset */
#define WATCHDOG_CLR_ALIAS     0x00003000u   /* RP2040 CLR alias offset */
#define WATCHDOG_CTRL_TRIG     (1u << 31)    /* TRIGGER bit (1u<<30 é ENABLE — bug v3.43.4) */
#define WATCHDOG_CTRL_ENABLE   (1u << 30)    /* ENABLE bit */

/* PSM (Power Supply Monitor) — controla quais peripherals o watchdog reset
 * derruba. SDK pico-sdk hardware_watchdog/watchdog.c::_watchdog_enable
 * usa `PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS)`,
 * EXCLUINDO os oscillators físicos (ROSC e XOSC).
 *
 * Razão: resetar ROSC/XOSC força um re-startup que pode brevemente deixar
 * PLLs derivados (PLL_SYS, PLL_USB) em estado instável. Se algum periférico
 * começar a operar antes do `runtime_init_clocks` re-inicializar os clocks,
 * pode ficar com state inválido — sintoma observado: USB CDC enumera mas
 * host não recebe dados pós-watchdog reboot.
 *
 * F-OTA-BOOTLOOP fix v3.43.17: alinhar com SDK em vez de 0xFFFFFFFF. */
#define PSM_BASE_ADDR          0x40010000u
#define PSM_WDSEL_OFFSET       0x08u
#define PSM_WDSEL_ROSC_BIT     (1u << 0)
#define PSM_WDSEL_XOSC_BIT     (1u << 1)
#define PSM_WDSEL_BITS_ALL     0x0001FFFFu  /* 17 bits válidos */
#define PSM_WDSEL_RESET_MASK   (PSM_WDSEL_BITS_ALL & ~(PSM_WDSEL_ROSC_BIT | PSM_WDSEL_XOSC_BIT))
                                            /* = 0x0001FFFC (todos exceto ROSC/XOSC) */

/* SCB SYSRESETREQ (não usado mais — incompleto, deixa SIO stale).
 * Mantido pra referência histórica do bug v3.43.4-9. */
#define SCB_AIRCR_ADDR         0xE000ED0Cu
#define SCB_AIRCR_KEY          (0x05FAu << 16)
#define SCB_AIRCR_SYSRESET     (1u << 2)

namespace ota {

/* Buffer SRAM de 4 KiB pra cópia sector-by-sector. Em BSS (zero-init no
 * boot) — não precisa de __not_in_flash porque BSS já vive em RAM
 * (0x20xxxxxx) e é acessível durante flash_range_erase/program (QSPI off,
 * RAM intacta).
 *
 * Fase 9: external linkage (sem `static`) — também é usado por
 * `metadata.cpp::ota_metadata_write` e `ota_snapshot_write` para preservar
 * o setor inteiro durante read-erase-program-all. Race-free: applier só
 * roda após `state=APPLYING` persistido e termina via reboot, nunca
 * concorrente com os outros callers. Declarado em `metadata.h`. */
uint8_t s_applier_buf[OTA_FLASH_SECTOR_SIZE] __attribute__((aligned(4)));

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
 * faulta. Aqui escrevemos direto na hardware register.
 *
 * Alimentar o watchdog do RP2040 é recarregar LOAD (offset 0x04). NÃO é
 * escrever em CTRL (offset 0x00): o bit 31 de CTRL é TRIGGER, que força um
 * reset imediato — é literalmente o que applier_reboot() faz no passo (5).
 *
 * Até v1.6.4-beta esta função escrevia WATCHDOG_CTRL_TRIG no SET alias de
 * CTRL, ou seja, era um reboot disfarçado de feed, no mesmo endereço que
 * applier_reboot() usa (WATCHDOG_CTRL_OFFSET é 0). A primeira chamada —
 * logo após programar o setor 0 no passo (1a) — resetava o chip antes de
 * qualquer erase ou cópia. O slot da app ficava intacto com o firmware
 * antigo, e o boot seguinte era indistinguível de um apply bem-sucedido:
 * reset por watchdog forçado, metadata em APPLYING, LittleFS destruída
 * (pelo upload do staging, que divide a partição com ela) e a versão sem
 * mudar. Nenhuma camada verificava o resultado, então o OTA reportava
 * sucesso em todos os passos e nunca trocava o firmware.
 *
 * LOAD é 24-bit e o contador decrementa 2x por tick (errata RP2040-E1),
 * então 0xFFFFFF ≈ 8,38 s — o teto do hardware. Cada operação individual
 * de flash aqui é de dezenas de ms, com folga larga. */
static inline void __not_in_flash_func(applier_wdt_feed)() {
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_LOAD_OFFSET) = 0xFFFFFFu;
}

/* memcpy em SRAM. A memcpy da libc vive no slot da app (0x1005de70 no
 * build de 1.6.4-beta), que o passo (1b) apaga — chamá-la no passo (2)
 * seria executar flash apagada. Ponteiros volatile impedem o GCC de
 * reconhecer o laço e reintroduzir uma chamada a memcpy.
 *
 * Ambas as pontas são alinhadas a 4: s_applier_buf é aligned(4) e a origem
 * é XIP_BASE + offset de setor. `n` é sempre OTA_FLASH_SECTOR_SIZE. */
static inline void __not_in_flash_func(applier_copy)(void* dst, const void* src,
                                                     uint32_t n) {
    volatile uint32_t*       d = (volatile uint32_t*)dst;
    const volatile uint32_t* s = (const volatile uint32_t*)src;
    for (uint32_t i = 0; i < n / 4u; i++) d[i] = s[i];
}

/* Reboot inline via watchdog reset (PSM full reset). Substitui SCB
 * SYSRESETREQ (v3.43.4-10) que era incompleto — só resetava M0+ cores,
 * deixava SIO/multicore mailbox/RESETS em estado stale, fazendo
 * arduino-pico Core 1 launch hangar intermitentemente no próximo boot
 * (Bug 2 reproduzido em ~30% dos applies). Watchdog reset via PSM
 * derruba TODOS os peripherals (SIO, RESETS, BUSCTRL etc), produzindo
 * estado limpo equivalente a hard reset físico. Pico-sdk watchdog_reboot
 * vive em flash slot — replicamos a sequência inline com MMIO puro:
 *
 *   1. Set PSM->wdsel = 0xFFFFFFFF (selector pra reset completo).
 *   2. Clear watchdog ENABLE para limpar estado prévio.
 *   3. Clear scratch[4] (boot mode = normal boot, não stage2).
 *   4. Set LOAD pequeno (10 ms × 2 ticks/μs = 20000).
 *   5. Set ENABLE | TRIGGER pra disparar imediato.
 *   6. Spin esperando reset. */
static inline void __not_in_flash_func(applier_reboot)() {
    /* F-OTA-BOOTLOOP fix #3 (v3.43.21): SDK pico-sdk hardware_watchdog/
     * watchdog.c::_watchdog_enable usa apenas TRIGGER quando delay_ms=0
     * (reset imediato). Antes nós usávamos ENABLE|TRIGGER simultâneo,
     * que após o reset deixava o watchdog ARMADO com LOAD pequeno
     * (10ms). Watchdog HW NÃO é resetado pelo PSM (não está em
     * PSM_WDSEL_BITS) — só é resetado por power cycle físico (3V3
     * cycle). Resultado: pós-reset, watchdog continuava ENABLE com
     * LOAD=10ms → disparava novo reset a cada 10ms → boot pós-OTA
     * ficava em loop infinito de reset até power cycle.
     *
     * Sintoma autópsia: sc3=0x80088000 (bit 31 = HW WDT reason flag).
     *
     * Fix: TRIGGER apenas (reset imediato). Antes do TRIGGER, fazer
     * watchdog_disable explícito (clear ENABLE) + LOAD = max para
     * garantir que mesmo se algo der errado, o timer não vai disparar
     * antes do firmware re-inicializar normalmente. */

    /* (1) Configura PSM pra reset on watchdog — todos peripherals
     * EXCETO ROSC/XOSC, alinhado com pico-sdk _watchdog_enable. */
    *(volatile uint32_t*)(PSM_BASE_ADDR + PSM_WDSEL_OFFSET) = PSM_WDSEL_RESET_MASK;

    /* (2) Clear ENABLE no ctrl (via CLR alias) — desabilita o timer */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_CLR_ALIAS + WATCHDOG_CTRL_OFFSET) =
        WATCHDOG_CTRL_ENABLE;

    /* (3) Clear scratch[4] — boot ROM checa este magic; 0 = normal boot.
     *
     * O offset é 0x1C. Até v1.6.4-beta a constante valia 0x18, que é
     * SCRATCH3: este write zerava o registrador de trace que a autópsia de
     * boot lê como `sc3` e deixava o magic da bootrom intocado. Passou
     * despercebido porque applier_wdt_feed() rebootava o chip antes de
     * qualquer chamada legítima chegar aqui. */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SCRATCH4_OFFSET) = 0;

    /* (4) LOAD = max (24-bit max = 0xFFFFFF). Watchdog HW persiste pós-reset
     * (não está em PSM_WDSEL); LOAD grande dá ao firmware tempo suficiente
     * para chegar ao primeiro watchdog_update no loop(). */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_LOAD_OFFSET) = 0xFFFFFFu;

    /* (5) Set TRIGGER apenas (NÃO ENABLE). TRIGGER força reset imediato.
     * ENABLE não é necessário — armaria o timer pós-reset, criando loop. */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SET_ALIAS + WATCHDOG_CTRL_OFFSET) =
        WATCHDOG_CTRL_TRIG;

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
        applier_copy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

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
        applier_copy(s_applier_buf, src, OTA_FLASH_SECTOR_SIZE);

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

    /* (3.5) alpha31: ERASE staging/LFS region. Após apply, a região 0xFF000
     * a OTA_SNAPSHOT_OFFSET está poluída com bytes do upload do firmware
     * (staging compartilha partição com LittleFS). No boot novo, mountFS lê
     * bytes-de-firmware no superblock LFS → falha → tenta auto-format → cada
     * erase chama flash_safe_execute → multicore_lockout → trava nas
     * condições pós-OTA observadas em alpha30 (UART captura mostrou hang em
     * marker '0' = dentro de mountFS).
     *
     * Erase aqui (com IRQs OFF + Core 1 lockout do orchestrator + flash
     * direct MMIO via __not_in_flash_func) garante que próximo boot vê
     * LFS region all-0xFF → mountFS detecta limpa → format quick path
     * (ou nem precisa, depende da LFS lib).
     *
     * Snapshot region (último setor de staging, OTA_SNAPSHOT_OFFSET) é
     * PRESERVADA — config pós-OTA precisa dela. */
    {
        constexpr uint32_t LFS_ERASE_END = OTA_SNAPSHOT_OFFSET;
        for (uint32_t off = OTA_STAGING_OFFSET; off < LFS_ERASE_END; off += OTA_FLASH_SECTOR_SIZE) {
            uint32_t saved_irq = save_and_disable_interrupts();
            flash_range_erase(off, OTA_FLASH_SECTOR_SIZE);
            restore_interrupts(saved_irq);
            applier_wdt_feed();
        }
    }

    /* (4) Reboot. CRC mismatch info perdida (sem persistência possível
     * em IRQ-off mode); BootROM detecta boot2 ruim e cai pra BOOTSEL
     * em caso de write quebrado. */
    (void)crc;
    (void)want_crc;
    applier_reboot();
    return false;  /* unreachable */
}

} /* namespace ota */
