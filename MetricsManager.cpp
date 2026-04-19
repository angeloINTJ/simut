/**
 * @file    MetricsManager.cpp
 * @brief   Implementation of MetricsManager — heap sampling and RSSI tracking.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#include "MetricsManager.h"

void MetricsManager::sampleHeap() {
    uint32_t free = rp2040.getFreeHeap();
    _m.heapFreeNow = free;
    if (free < _m.heapMinSeen) _m.heapMinSeen = free;
}

void MetricsManager::observeRssi(int32_t rssi) {
    _m.rssiNow = rssi;
    if (rssi < _m.rssiMin) _m.rssiMin = rssi;
    if (rssi > _m.rssiMax) _m.rssiMax = rssi;
}
