/**
 * @file    MetricsManager.cpp
 * @brief   Implementation of MetricsManager — heap sampling and RSSI tracking.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "MetricsManager.h"

void MetricsManager::sampleHeap() {
    uint32_t free = rp2040.getFreeHeap();
    _m.heapFreeNow = free;
    if (free < _m.heapMinSeen) _m.heapMinSeen = free;
}

/*
 * Probe binary-search: duplica tamanho até falhar, depois binary-search entre
 * último sucesso e primeiro fail. ~16 malloc+free para precisão de ±512 B.
 * Cada malloc é imediatamente freed → impacto zero na heap permanente.
 *
 * Cap em 90% do getFreeHeap() pra nunca tentar alocar "tudo" (evita UAF em
 * algum componente que assume alguma margem).
 */
void MetricsManager::sampleLargestBlock() {
    uint32_t totalFree = rp2040.getFreeHeap();
    if (totalFree < 256) {
        _m.heapLargestBlock = 0;
        return;
    }

    const size_t CAP    = (totalFree * 9) / 10;          /* 90 % do free */
    const size_t MARGIN = 512;                           /* precisão ±512 B */

    size_t lo = 256;
    size_t hi = CAP;
    while (lo + MARGIN < hi) {
        size_t mid = lo + (hi - lo) / 2;
        void* p = malloc(mid);
        if (p) { free(p); lo = mid; }
        else   { hi = mid; }
    }
    _m.heapLargestBlock = (uint32_t)lo;
    if (_m.heapLargestBlock < _m.heapLargestMin) _m.heapLargestMin = _m.heapLargestBlock;
}

void MetricsManager::observeRssi(int32_t rssi) {
    _m.rssiNow = rssi;
    if (rssi < _m.rssiMin) _m.rssiMin = rssi;
    if (rssi > _m.rssiMax) _m.rssiMax = rssi;
}
