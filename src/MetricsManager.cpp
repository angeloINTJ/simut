/**
 * @file MetricsManager.cpp
 * @brief Implementation of MetricsManager — heap sampling and RSSI tracking.
 *
 * @project SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "MetricsManager.h"

void MetricsManager::sampleHeap( ) {
 uint32_t free = rp2040.getFreeHeap( );
 _m.heapFreeNow = free;
 if (free < _m.heapMinSeen) _m.heapMinSeen = free;
}

/*
 * Binary-search probe: doubles size until failure, then binary-searches between
 * last success and first failure. ~16 malloc+free for ±512 B precision.
 * Each malloc is immediately freed → zero permanent heap impact.
 *
 * Capped at 90% of getFreeHeap( ) to never try to allocate "everything" (avoids
 * UAF in some component that assumes some margin).
 */
void MetricsManager::sampleLargestBlock( ) {
 uint32_t totalFree = rp2040.getFreeHeap( );
 if (totalFree < 256) {
 _m.heapLargestBlock = 0;
 return;
 }

 const size_t CAP = (totalFree * 9) / 10; /* 90% of free */
 const size_t MARGIN = 512; /* ±512 B precision */

 size_t lo = 256;
 size_t hi = CAP;
 while (lo + MARGIN < hi) {
 size_t mid = lo + (hi - lo) / 2;
 void* p = malloc(mid);
 if (p) { free(p); lo = mid; }
 else { hi = mid; }
 }
 _m.heapLargestBlock = (uint32_t)lo;
 if (_m.heapLargestBlock < _m.heapLargestMin) _m.heapLargestMin = _m.heapLargestBlock;
}

void MetricsManager::observeRssi(int32_t rssi) {
 _m.rssiNow = rssi;
 if (rssi < _m.rssiMin) _m.rssiMin = rssi;
 if (rssi > _m.rssiMax) _m.rssiMax = rssi;
}
