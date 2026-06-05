/**
 * @file DisplayManager_PresetsDemo.cpp
 * @brief Sensor preset catalog entry point (minimal — flash-constrained).
 *
 * Triggered via CLI: `display presets`
 * Full catalog in sensors/SensorPresets.h — too large for serial at 98.7% flash.
 *
 * Compiled only when SIMUT_PRESETS_DEMO=1.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#include "DisplayManager.h"
#if SIMUT_PRESETS_DEMO
void DisplayManager::drawPresetsDemoPage(int) {}
void DisplayManager::handlePresetsDemoTouch(int16_t, int16_t) {}
bool DisplayManager::isInPresetsDemo() const { return false; }
void DisplayManager::showPresetsDemo() {
    Serial.println("SENSOR PRESETS: see src/sensors/SensorPresets.h (130+ presets, 30+ categories)");
}
#else
void DisplayManager::drawPresetsDemoPage(int) {}
void DisplayManager::handlePresetsDemoTouch(int16_t, int16_t) {}
bool DisplayManager::isInPresetsDemo() const { return false; }
void DisplayManager::showPresetsDemo() {}
#endif
