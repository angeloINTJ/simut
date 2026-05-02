/**
 * @file    Favicon.h
 * @brief   Favicon embedado em PROGMEM, servido por GET /favicon.ico.
 * @details Array gerado por tools/build_favicon_header.py a partir de
 *          data/favicon.ico. Embedar em flash (em vez de servir do FS)
 *          evita perder o favicon em uploadfs e tira dependência do FS.
 *          Para trocar o favicon: substituir data/favicon.ico, rodar
 *          o script, recompilar.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <Arduino.h>

namespace Favicon {
    extern const uint8_t DATA[] PROGMEM;
    extern const size_t LEN;
}
