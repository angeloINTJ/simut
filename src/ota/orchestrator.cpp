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
#include <hardware/gpio.h>

/* CYW43 WL_REG_ON pin no Pico W = GPIO 23 (pin power gate do chip externo).
 * Drive LOW pra power-cycles o chip antes do watchdog_reboot — evita
 * acúmulo de estado entre OTAs consecutivos (Achado #4 docs/INVESTIGATION_BOOTLOOP.md).
 * Default vem de pico-sdk pico_w.h se não estiver definido. */
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23u
#endif

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
     * Fix #2 cyw43_arch_deinit() REVERTIDO em v3.43.19 — em v3.43.18 o
     * deinit causou trava do boot inicial após USB flash limpo (USB CDC
     * enumera mas CLI mudo). Hipótese: deinit deixa o chip CYW43 em
     * estado que requer power cycle pra recuperar, mesmo no boot novo.
     * Investigação continua. */
    WiFi.end();

    /* LittleFS desmontada — staging é acessível via XIP cru. */
    LittleFS.end();

    /* Fix #4 (v3.44.0-alpha2): power-cycle CYW43 antes do watchdog reset.
     * WiFi.end() não desliga o chip externo (apenas chama cyw43_wifi_leave).
     * Watchdog reset reseta RP2040 mas NÃO o CYW43 (chip separado via SPI).
     * Após N applies consecutivos, state interno do CYW43 acumula até
     * cyw43_arch_init no próximo boot falhar silencioso → bootloop.
     * Solução: drive WL_REG_ON LOW por 100ms — corta 3V3 do chip via load
     * switch. RP2040 segue funcionando. Boot pós-reboot vai religar o
     * pin (cyw43_arch_init drive HIGH) e o chip inicializa fresh.
     * Hipótese principal pra bricks residuais ~24% (loop20 v3.43.21). */
    gpio_init(CYW43_DEFAULT_PIN_WL_REG_ON);
    gpio_set_dir(CYW43_DEFAULT_PIN_WL_REG_ON, GPIO_OUT);
    gpio_put(CYW43_DEFAULT_PIN_WL_REG_ON, 0);
    busy_wait_ms(100);

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
