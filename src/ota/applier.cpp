/**
 * @file    src/ota/applier.cpp
 * @brief   Aplicador SRAM-resident (Fase 7a no-op nesta versão).
 *
 * @details ATENÇÃO: `__not_in_flash_func` na definição é o que coloca o
 *          símbolo na seção .data (RAM) — sem isso, a função executa via
 *          XIP e crasha quando a flash está sendo erased. Toda função
 *          chamada DEPOIS de `flash_range_erase` no slot da app DEVE
 *          também estar em SRAM (em 7a não há erase, então flash-resident
 *          callees como `watchdog_reboot` são OK).
 *
 *          7a check-list:
 *           [x] Função em SRAM via __not_in_flash_func.
 *           [x] Não toca a app slot (sem flash_range_erase).
 *           [x] watchdog_reboot dispara reset limpo (BootROM segue).
 *           [x] Caller (orchestrator) cobre teardown/lockout/IRQ.
 *
 *          7b TODO:
 *           [ ] uzlib funções marcadas com __not_in_flash_func ou copiadas
 *               para SRAM. Atualmente uzlib reside na app slot e seria
 *               apagada antes de poder rodar.
 *           [ ] Loop erase-decompress-program em granularidade de 4 KiB.
 *           [ ] Validação CRC pós-write relendo via XIP.
 *           [ ] watchdog_reboot só após CRC OK.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "applier.h"

#include <Arduino.h>
#include <hardware/watchdog.h>

namespace ota {

bool __not_in_flash_func(ota_applier_run)(const UpdateMetadata* meta) {
    (void)meta;  /* 7a: metadata não usada (no-op). */

    /* 7a: spin breve em SRAM pra marcar "estamos na rotina". 100k iterations
     * de NOP @ 133 MHz ≈ 750 µs. Suficiente pra distinguir de chamada-falha
     * (caso aplicador nem fosse linkado, vínhamos direto pra reboot). */
    for (volatile uint32_t i = 0; i < 100000u; i++) {
        __asm volatile("nop");
    }

    /* watchdog_reboot vive em flash mas XIP está online (nenhum
     * flash_range_erase executado). Chamada segura nesta sub-fase. */
    watchdog_reboot(0u, 0u, 0u);

    /* Unreachable. */
    while (1) { __asm volatile("nop"); }
    return false;
}

} /* namespace ota */
