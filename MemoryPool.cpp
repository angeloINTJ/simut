/**
 * @file    MemoryPool.cpp
 * @brief   Implementação do shared BSS pool com CAS atômico.
 * @project SIMUT
 * @license MIT License
 */
#include "MemoryPool.h"

uint8_t MemoryPool::_buffer[MemoryPool::SIZE] __attribute__((aligned(8)));
volatile uint8_t MemoryPool::_owner = MemoryPool::OWNER_NONE;

uint8_t* MemoryPool::tryClaim(Owner who) {
    /* CAS: NONE → who. Sucesso = pool agora é nosso. */
    uint8_t expected = OWNER_NONE;
    uint8_t desired  = (uint8_t)who;
    if (__atomic_compare_exchange_n(&_owner, &expected, desired, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        return _buffer;
    }
    /* CAS falhou — mas pode ser que JÁ somos o owner (claim idempotente). */
    if (__atomic_load_n(&_owner, __ATOMIC_ACQUIRE) == desired) {
        return _buffer;
    }
    /* Outro owner ativo — alocação negada. */
    return nullptr;
}

void MemoryPool::release(Owner who) {
    /* CAS: who → NONE. Falha se não somos o owner (no-op). */
    uint8_t expected = (uint8_t)who;
    __atomic_compare_exchange_n(&_owner, &expected, (uint8_t)OWNER_NONE, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

MemoryPool::Owner MemoryPool::currentOwner() {
    return (Owner)__atomic_load_n(&_owner, __ATOMIC_ACQUIRE);
}

const char* MemoryPool::ownerName(Owner o) {
    switch (o) {
        case OWNER_NONE:      return "free";
        case OWNER_GRAPH:     return "graph";
        case OWNER_TELEMETRY: return "telemetry";
    }
    return "?";
}
