/**
 * @file    src/ota/applier.h
 * @brief   Aplicador OTA — roda inteiramente da SRAM (Fase 7).
 *
 * @details `ota_applier_run` é a rotina destrutiva que apaga o slot da app
 *          e reescreve com o conteúdo descomprimido do staging. ESTÁ MARCADA
 *          COM `__not_in_flash_func` para residir em SRAM, e portanto pode
 *          executar mesmo enquanto a flash está sendo erased/programmed.
 *
 *          PRECONDIÇÕES (caller orchestrator garante):
 *           - WiFi/CYW43 desligado (`WiFi.end()`).
 *           - LittleFS desmontada (`LittleFS.end()`).
 *           - Core 1 pausado via `multicore_lockout_start_blocking()`.
 *           - IRQs globais desabilitadas (`save_and_disable_interrupts`).
 *           - Metadata.state == APPLYING e persistida em flash.
 *
 *          7a (atual — não destrutivo): a função apenas faz uma pausa breve
 *          e dispara `watchdog_reboot`. Demonstra que a infraestrutura de
 *          jump-to-SRAM + lockout + IRQ-off + reboot funciona, sem riscar a
 *          app slot. Validar 7a antes de evoluir pra 7b real.
 *
 *          7b (a implementar — destrutivo): apaga slot da app sector-by-
 *          sector, descomprime staging via uzlib (que precisará estar em
 *          SRAM), grava no slot, valida CRC do output relendo via XIP,
 *          watchdog_reboot. NÃO retorna em sucesso.
 *
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include "metadata.h"

namespace ota {

/**
 * @brief Aplica o update em modo destrutivo. NÃO RETORNA em sucesso.
 *
 * @param meta  Metadata em RAM (caller leu antes de IRQ disable).
 *              Em 7a (no-op), parâmetro é ignorado.
 *
 * @return false só se erro detectado ANTES de qualquer ação destrutiva.
 *         Em 7a, NUNCA retorna (sempre reboota).
 */
bool ota_applier_run(const UpdateMetadata* meta);

} /* namespace ota */
