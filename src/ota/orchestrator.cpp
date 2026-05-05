/**
 * @file    src/ota/orchestrator.cpp
 * @brief   Implementação do orchestrador de apply (Fase 7).
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "orchestrator.h"
#include "metadata.h"
#include "applier.h"
#include "../../StorageManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <hardware/sync.h>

namespace ota {

OrchestratorResult ota_apply_pending_update(StorageManager* storage) {
    /* (1) Lê metadata. */
    UpdateMetadata meta;
    if (!ota_metadata_read(meta)) {
        return OrchestratorResult::NOT_PENDING;
    }
    if (meta.state != STATE_COMMITTED) {
        return OrchestratorResult::NOT_PENDING;
    }

    /* (2) Anti-loop. */
    if (meta.attempts >= OTA_MAX_APPLY_ATTEMPTS) {
        return OrchestratorResult::MAX_ATTEMPTS;
    }

    /* (3) Pausa Core 1 ANTES de qualquer flash_range_*. Padrão do projeto
     * — sem isto, Core 1 (DisplayManager) lendo flash via XIP durante o
     * erase causa hard fault classificado pela autópsia como reset
     * "external" (sem flag de watchdog), e a metadata nunca é persistida. */
    if (storage) storage->enterFlashSafeMode();

    /* (4) Marca APPLYING + attempts++ (antes de qualquer tear down — se
     * cair entre aqui e watchdog_reboot, próximo boot conta a tentativa
     * gasta e eventualmente para via MAX_ATTEMPTS). */
    meta.state = STATE_APPLYING;
    meta.attempts += 1;
    if (!ota_metadata_write(meta)) {
        if (storage) storage->exitFlashSafeMode();
        return OrchestratorResult::METADATA_FAIL;
    }

    /* (5) Tear down — ordem importa.
     *
     * WiFi primeiro: cyw43 driver tem IRQs próprias que precisam ser
     * desligadas antes do save_and_disable_interrupts global. WiFi.end()
     * chama cyw43_arch_deinit internamente. Sem isso, IRQ pendente do
     * CYW43 dispara após disable_interrupts e trava o sistema. */
    WiFi.end();

    /* LittleFS desmontada — staging é acessível via XIP cru. */
    LittleFS.end();

    /* (5) IRQs globais OFF + jump pra applier SRAM. */
    uint32_t saved_irq = save_and_disable_interrupts();

    /* applier_run NÃO RETORNA em sucesso (reboota). Em 7a sempre reboota.
     * Em 7b real, retorna false só em erro pré-destrutivo. */
    bool ok = ota_applier_run(&meta);
    (void)ok;

    /* (6) Apenas alcançado se applier retornar (erro). Restaura IRQs e
     * Core 1, devolve controle ao caller. App slot ainda íntegra. */
    restore_interrupts(saved_irq);
    if (storage) storage->exitFlashSafeMode();

    return OrchestratorResult::APPLIER_RETURNED;
}

} /* namespace ota */
