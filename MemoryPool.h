/**
 * @file    MemoryPool.h
 * @brief   Shared BSS pool — mutex-exclusive zone usado por graph caches OU
 *          telemetry payload, mas nunca os dois ao mesmo tempo.
 * @details Resolve fragmentação de heap: alocações grandes (~45 KB) que
 *          falhariam em heap fragmentada têm endereço fixo aqui em BSS,
 *          sempre disponível. Justifica o BSS reservado pelo fato de que
 *          os dois usuários são mutuamente exclusivos no tempo:
 *
 *          • Graph caches: alocadas quando user toca display (telemetria
 *            já pausada por isUserInteracting). Liberadas 5s após retorno
 *            à dashboard.
 *          • Telemetry payload: construído durante POST quando user está
 *            inativo. Liberado imediatamente após HTTP response.
 *
 *          Tamanho dimensionado para o maior caso (graph cache):
 *          (MAX_SENSORS + 2 + 5) × sizeof(GraphCacheEntry) ≈ 45 KB.
 *          Telemetria usa só ~5-15 KB, sobra espaço.
 *
 *          Atomicidade via __atomic_compare_exchange_n garante CAS
 *          cross-core (Core 0 sensores/web/CLI, Core 1 display).
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once
#include <Arduino.h>

class MemoryPool {
public:
    enum Owner : uint8_t {
        OWNER_NONE      = 0,
        OWNER_GRAPH     = 1,
        OWNER_TELEMETRY = 2,
    };

    /** Tamanho do pool em bytes. Calculado para acomodar o maior usuário
     *  (graph caches: 17 × ~2640 B = 44 880 B). Arredondado para 256 B. */
    static constexpr size_t SIZE = 46080;

    /** Tenta adquirir o pool. Retorna ponteiro para buffer em sucesso;
     *  nullptr se outro owner já o possui.
     *  Idempotente: se já é dono, retorna o buffer sem trocar. */
    static uint8_t* tryClaim(Owner who);

    /** Libera o pool. No-op se caller não é o owner atual. */
    static void release(Owner who);

    /** Retorna o owner corrente (OWNER_NONE se livre). */
    static Owner currentOwner();

    /** Para diagnóstico/log. */
    static const char* ownerName(Owner o);

private:
    static uint8_t _buffer[SIZE] __attribute__((aligned(8)));
    static volatile uint8_t _owner;
};
