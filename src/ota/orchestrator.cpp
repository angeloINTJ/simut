/**
 * @file src/ota/orchestrator.cpp
 * @brief Implementação do orchestrador de apply ( ).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "orchestrator.h"
#include "metadata.h"
#include "applier.h"
#include "StorageManager.h"

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
 if (storage) storage->enterFlashSafeMode( );

 /* (4) Marca APPLYING + attempts++ (antes de qualquer tear down — se
 * cair entre aqui e watchdog_reboot, próximo boot conta a tentativa
 * gasta e eventualmente para via MAX_ATTEMPTS). */
 meta.state = STATE_APPLYING;
 meta.attempts += 1;
 if (!ota_metadata_write(meta)) {
 if (storage) storage->exitFlashSafeMode( );
 return OrchestratorResult::METADATA_FAIL;
 }

 /* (5) Tear down — ordem importa.
 *
 * Fix #2 cyw43_arch_deinit( ) REVERTIDO em v3.43.19 — em v3.43.18 o
 * deinit causou trava do boot inicial após USB flash limpo (USB CDC
 * enumera mas CLI mudo). Hipótese: deinit deixa o chip CYW43 em
 * estado que requer power cycle pra recuperar, mesmo no boot novo.
 * Investigação continua. */
 WiFi.end( );

 /* LittleFS desmontada — staging é acessível via XIP cru. */
 LittleFS.end( );

 /* Fix #4 REVERTIDO em alpha3 ( ): drive
 * WL_REG_ON LOW antes do reboot REGRIDIU brick rate de 24% para 57%
 * (validação 7 iters). Hipótese: power-cycle do CYW43 mid-tear-down
 * deixa USB CDC / bus em estado mais frágil. Mantida apenas em modo
 * experimental — possivelmente precisa ordem diferente (ex: matar
 * CYW43 ANTES do WiFi.end). Investigar isolado. */

 /* marca scratch[5] com magic POST_OTA_APPLY_MAGIC pra sinalizar
 * que próximo boot deve power-cycle CYW43. AppManager::setup detecta cedo
 * e dispara WL_REG_ON LOW 500ms → high-Z. Resolve F-OTA-BOOTLOOP residual
 * onde CYW43 chip fica em estado intermediário pós-watchdog_reboot. */
 *(volatile uint32_t*)(0x40058000u + 0x20u) = 0xC72BAB07u;

 /* (5) IRQs globais OFF + jump pra applier SRAM. */
 uint32_t saved_irq = save_and_disable_interrupts( );

 /* applier_run NÃO RETORNA em sucesso (reboota). Em 7a sempre reboota.
 * Em 7b real, retorna false só em erro pré-destrutivo. */
 bool ok = ota_applier_run(&meta);
 (void)ok;

 /* (6) Apenas alcançado se applier retornar (erro). Restaura IRQs e
 * Core 1, devolve controle ao caller. App slot ainda íntegra. */
 restore_interrupts(saved_irq);
 if (storage) storage->exitFlashSafeMode( );

 return OrchestratorResult::APPLIER_RETURNED;
}

} /* namespace ota */
