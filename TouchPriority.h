/**
 * @file    TouchPriority.h
 * @brief   REF-004 — singleton de touch-priority state (user interagindo?).
 * @details Provider global único para a noção de "user está interagindo com
 * o display". Antes, 3 managers (LogManager, StorageManager, WebManager)
 * tinham cada um seu `setTouchPriorityChecker()` + membro `bool(*)()`
 * duplicado, registrados via 3 lambdas idênticas em AppManager::setup.
 * Agora um único provider é setado no boot; consumers leem via
 * `TouchPriority::isActive()`.
 *
 * Contract:
 *   - `setProvider(fn)` deve ser chamado uma vez no boot, antes de Core 1
 *     ou quaisquer consumers que dependem de `isActive()`.
 *   - `isActive()` com provider=nullptr retorna false (fail-safe: sistema
 *     age como se não houvesse user interagindo). Mantém o comportamento
 *     do check legado `_fn && _fn()`.
 *
 * Thread-safety: o provider é setado uma vez no boot pelo Core 0. Após
 * isso é só leitura de ponteiro — naturalmente atômico no ARM 32-bit.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

class TouchPriority {
public:
    typedef bool (*Fn)();

    /** Registra o provider. Chamar uma vez no boot. */
    static void setProvider(Fn fn) { _fn = fn; }

    /** Consulta se user está interagindo. false se provider não registrado. */
    static bool isActive() { return _fn && _fn(); }

private:
    /* C++17 inline static — storage único sem .cpp separado. */
    inline static Fn _fn = nullptr;
};
