/**
 * @file    MetricsManager.h
 * @brief   Operational metrics singleton — counters and observational stats.
 * @details Centraliza contadores de saúde operacional do sistema para
 * visualização via CLI (`show metrics`) e futura exposição web.
 * Zerados no boot; agregados totais desde a última reboot.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs.h"

/**
 * Contadores e observações operacionais. Todos uint32_t salvo os RSSIs
 * (int32_t por unidade dBm negativa) para casar com WiFi.RSSI().
 */
struct SystemMetrics {
    /* Network */
    uint32_t wifiReconnects   = 0;
    uint32_t mqttReconnects   = 0;
    int32_t  rssiNow          = 0;     /**< 0 = nunca amostrado */
    int32_t  rssiMin          = 127;   /**< inicializa fora do range para detectar "nunca amostrado" */
    int32_t  rssiMax          = -127;

    /* Telemetry */
    uint32_t telSent          = 0;
    uint32_t telFailed        = 0;
    uint32_t telRetries       = 0;
    uint32_t telTotalBytes    = 0;
    uint32_t telLastLatencyMs = 0;

    /* Sensors (agregado, não por slot) */
    uint32_t sensorReadsOk    = 0;
    uint32_t sensorReadsErr   = 0;

    /* Storage */
    uint32_t configSaves      = 0;

    /* System — atualizados por sampleHeap() */
    uint32_t heapFreeNow      = 0;
    uint32_t heapMinSeen      = 0xFFFFFFFF;  /**< HWM inverso: menor livre observado */
    uint32_t heapLargestBlock = 0;            /**< Maior bloco contíguo alocável (indicador de fragmentação) */
    uint32_t heapLargestMin   = 0xFFFFFFFF;   /**< Menor valor observado de heapLargestBlock */
};

class MetricsManager {
public:
    static MetricsManager& instance() {
        static MetricsManager _instance;
        return _instance;
    }

    SystemMetrics& data() { return _m; }

    /** Amostra heap livre atual e atualiza min seen. Chamar periodicamente. */
    void sampleHeap();

    /** Mede o maior bloco contíguo alocável via binary-search probe (malloc/free).
     *  Custo: ~16 malloc+free. Chamar com baixa frequência (ex.: junto a sampleHeap,
     *  ou on-demand via /api/status). Não chamar sob pressão de memória. */
    void sampleLargestBlock();

    /** Observa um valor de RSSI e atualiza now/min/max. */
    void observeRssi(int32_t rssi);

private:
    MetricsManager() = default;
    SystemMetrics _m;
};
