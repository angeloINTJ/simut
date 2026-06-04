/**
 * @file TouchPriority.h
 * @brief Singleton touch-priority state (is user interacting?).
 * @details Single global provider for the notion of "user is interacting with
 * the display". 3 managers (LogManager, StorageManager, WebManager)
 * each had their own `setTouchPriorityChecker()` + duplicated `bool(*)()`
 * member, registered via 3 identical lambdas in AppManager::setup.
 * Now a single provider is set at boot; consumers read via
 * `TouchPriority::isActive()`.
 *
 * Contract:
 * - `setProvider(fn)` must be called once at boot, before Core 1
 * or any consumers that depend on `isActive()`.
 * - `isActive()` with provider=nullptr returns false (fail-safe: system
 * behaves as if no user is interacting). Maintains the behavior
 * of the legacy check `_fn && _fn()`.
 *
 * Thread-safety: the provider is set once at boot by Core 0. After
 * that it is pointer-only reads — naturally atomic on ARM 32-bit.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

class TouchPriority {
public:
 typedef bool (*Fn)( );

 /** Registers the provider. Call once at boot. */
 static void setProvider(Fn fn) { _fn = fn; }

 /** Queries whether user is interacting. false if provider not registered. */
 static bool isActive( ) { return _fn && _fn( ); }

private:
 /* C++17 inline static — single storage without separate .cpp. */
 inline static Fn _fn = nullptr;
};
