/**
 * @file    test/native_stubs/Arduino.h
 * @brief   Minimal Arduino.h stub for native (host) unit testing.
 * @details Inclui-se ANTES do <Arduino.h> real (via -I test/native_stubs em
 *          [env:native] do platformio.ini). Provê apenas o subset usado pelos
 *          headers SystemDefs_*.h cobertos pelos validators sob teste:
 *            · class String (parseIntStrict)
 *            · millis() (timeReached, timeSince, timeRemaining)
 *            · tipos integer Arduino (uint8_t etc — vem do <stdint.h>)
 *            · strncpy/strlen — vem do <string.h>
 *
 * REMOVER ESTE STUB SE migrar para ArduinoFake como dep (mais idiomático,
 * mas adiciona ~500KB de superfície de teste).
 *
 * @project SIMUT — EXT-009 (F-BUILD)
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <string>

/* ----- millis() stub: setável pelo teste para validar timeReached etc ----- */
namespace simut_native {
    extern uint32_t fake_millis_value;
}
inline uint32_t millis() { return simut_native::fake_millis_value; }
inline void set_native_millis(uint32_t v) { simut_native::fake_millis_value = v; }

/* ----- Arduino String mínimo (apenas o que parseIntStrict usa) ----- */
class String {
public:
    String() : data_() {}
    String(const char* s) : data_(s ? s : "") {}
    String(const std::string& s) : data_(s) {}

    size_t length() const { return data_.length(); }
    char operator[](size_t i) const { return i < data_.size() ? data_[i] : '\0'; }
    const char* c_str() const { return data_.c_str(); }

    /* Arduino String::toInt() retorna 0 em string mal-formada — mesma semântica. */
    long toInt() const {
        if (data_.empty()) return 0;
        try {
            return std::stol(data_);
        } catch (...) {
            return 0;
        }
    }

    /* v3.36.3 (M7): Arduino String::toFloat() — também retorna 0.0 silenciosamente
     * em string inválida; parseFloatStrict() usa pra obter o valor depois de validar
     * o formato. Mesma semântica do firmware. */
    float toFloat() const {
        if (data_.empty()) return 0.0f;
        try {
            return std::stof(data_);
        } catch (...) {
            return 0.0f;
        }
    }

private:
    std::string data_;
};
