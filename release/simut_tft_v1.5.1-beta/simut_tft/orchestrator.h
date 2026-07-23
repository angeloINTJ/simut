/**
 * @file    src/ota/orchestrator.h
 * @brief   Orchestrador do apply OTA (Fase 7).
 *
 * @details Sequência:
 *           1. Lê metadata; se magic ausente ou state != COMMITTED, sai.
 *           2. Marca state=APPLYING + attempts++; persiste em flash.
 *           3. Tear down: WiFi.end(), LittleFS.end(), enterFlashSafeMode
 *              (Core 1 lockout).
 *           4. save_and_disable_interrupts.
 *           5. Salta pra applier (SRAM). Não retorna em sucesso.
 *           6. Em retorno (erro pré-destrutivo), restore IRQs + Core 1 +
 *              report.
 *
 *          Anti-loop: se attempts >= OTA_MAX_APPLY_ATTEMPTS, recusa
 *          apply e deixa user ir pra BOOTSEL recovery.
 *
 *          Em 7a (no-op): o applier sempre reboota; orchestrator nunca
 *          retorna por esse caminho — mas a infra de tear down + IRQ
 *          off + jump é exercitada como em 7b.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>

class StorageManager;

namespace ota {

enum class OrchestratorResult : uint8_t {
    NOT_PENDING       = 0,  /**< Sem update válido em metadata. */
    MAX_ATTEMPTS      = 1,  /**< Excedeu OTA_MAX_APPLY_ATTEMPTS. */
    METADATA_FAIL     = 2,  /**< Não conseguiu persistir state=APPLYING. */
    APPLIER_RETURNED  = 3,  /**< Erro pré-destrutivo dentro do applier. */
    /* Sucesso = não retorna (watchdog_reboot dentro do applier). */
};

/**
 * @brief Dispara o apply do update pendente.
 *
 * Pré-condição: web request acabou de responder e flush. Esta função
 * NÃO RESPONDE A NADA — caller é responsável por send antes.
 *
 * @param storage  Gerencia Core 1 lockout via enterFlashSafeMode.
 * @return Resultado em caso de retorno (raro). Em sucesso, watchdog reset.
 */
OrchestratorResult ota_apply_pending_update(StorageManager* storage);

} /* namespace ota */
