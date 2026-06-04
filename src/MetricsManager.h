/**
 * @file MetricsManager.h
 * @brief Operational metrics singleton — counters and observational stats.
 * @details Centralizes system operational health counters for
 * visualization via CLI (`show metrics`) and future web exposure.
 * Zeroed on boot; totals aggregated since last reboot.
 *
 * @project SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs.h"

/**
 * Operational counters and observations. All uint32_t except RSSIs
 * (int32_t for negative dBm unit) to match WiFi.RSSI( ).
 */
struct SystemMetrics {
 /* Network */
 uint32_t wifiReconnects = 0;
 uint32_t mqttReconnects = 0;
 int32_t rssiNow = 0; /**< 0 = never sampled */
 int32_t rssiMin = 127; /**< initializes outside range to detect "never sampled" */
 int32_t rssiMax = -127;

 /* Telemetry */
 uint32_t telSent = 0;
 uint32_t telFailed = 0;
 uint32_t telRetries = 0;
 uint32_t telTotalBytes = 0;
 uint32_t telLastLatencyMs = 0;

 /* Sensors (aggregate, not per slot) */
 uint32_t sensorReadsOk = 0;
 uint32_t sensorReadsErr = 0;

 /* Storage */
 uint32_t configSaves = 0;

 /* System — updated by sampleHeap( ) */
 uint32_t heapFreeNow = 0;
 uint32_t heapMinSeen = 0xFFFFFFFF; /**< Inverse HWM: smallest free observed */
 uint32_t heapLargestBlock = 0; /**< Largest contiguous allocatable block (fragmentation indicator) */
 uint32_t heapLargestMin = 0xFFFFFFFF; /**< Smallest observed value of heapLargestBlock */
};

class MetricsManager {
public:
 static MetricsManager& instance( ) {
 static MetricsManager _instance;
 return _instance;
 }

 SystemMetrics& data( ) { return _m; }

 /** Sample current free heap and update min seen. Call periodically. */
 void sampleHeap( );

 /** Measure largest contiguous allocatable block via binary-search probe (malloc/free).
 * Cost: ~16 malloc+free. Call infrequently (e.g., alongside sampleHeap,
 * or on-demand via /api/status). Do not call under memory pressure. */
 void sampleLargestBlock( );

 /** Observe an RSSI value and update now/min/max. */
 void observeRssi(int32_t rssi);

private:
 MetricsManager( ) = default;
 SystemMetrics _m;
};
