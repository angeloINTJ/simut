/**
 * @file    SystemDefs_Time.h
 * @brief   Timing constants + safeCopy + wrap-safe millis helpers (EXT-003 split).
 * @details Boot timing, sensor timeouts, UI timing, safeCopy, timeReached/
 *          timeSince/timeRemaining. Sub-header de SystemDefs.h (facade).
 *          EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

/* =========================================================================== */
/*                          BOOT TIMING CONSTANTS                            */
/* =========================================================================== */

/** Tempo que o usuário precisa manter o toque para entrar em AP Mode (ms). */
constexpr uint32_t AP_HOLD_DURATION_MS      = 3000;

/** Janela de espera para detectar início do toque no boot (ms). */
constexpr uint32_t AP_DETECT_WINDOW_MS      = 3500;

/** Delay entre etapas do boot para feedback visual (ms). */
constexpr uint32_t BOOT_STEP_DELAY_MS       = 800;

/** Delay de polling durante loops de espera no boot (ms). */
constexpr uint32_t BOOT_POLL_INTERVAL_MS    = 50;

/**
 * Timeout do hardware watchdog em milissegundos.
 * Dimensionado para cobrir o pior caso de write em Flash + scan WiFi
 * sem disparar falso reset durante operações legítimas de I/O.
 */
constexpr uint32_t WATCHDOG_TIMEOUT_MS      = 15000;

/** Toques perdidos tolerados antes de cancelar AP hold. */
constexpr int      AP_HOLD_MAX_MISSED       = 5;


/* =========================================================================== */
/*                          SENSOR TIMEOUTS (CON-006)                         */
/* =========================================================================== */

/**
 * Tempo de conversão DS18B20 antes de ler o resultado (ms).
 *
 * Datasheet: pior caso 12-bit = 750ms. Resoluções menores completam antes
 * (9-bit=94ms, 10-bit=188ms, 11-bit=375ms). Como `cfg.ds18Resolution` é
 * configurável (9..12), usamos o pior caso como safe fallback — custo é
 * até ~600ms extras no path crítico quando 9-bit está selecionado, mas
 * simplifica o state machine (único timer fixo) e tolera qualquer
 * variância de GC de flash/interrupção.
 */
constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

/**
 * Timeout de leitura single-shot DHT22 (ms).
 *
 * Datasheet: ciclo completo ~20ms. O valor de 150ms dá folga contra
 * atrasos de scheduling do PIO state machine, sem alongar o scan
 * desnecessariamente. Se o sensor não respondeu em 150ms é considerado
 * ausente ou com problema.
 */
constexpr uint32_t DHT22_READ_TIMEOUT_MS      = 150;


/* =========================================================================== */
/*                         BOOT & UI TIMING (DOC-002)                          */
/* =========================================================================== */

/** Intervalo entre incrementos da animação "..." na tela de espera de boot
 *  (AppManager aguardando WiFi/NTP). Um dot a cada 800 ms dá feedback visual
 *  sem ruído de redraw. */
constexpr uint32_t BOOT_WAIT_DOT_INTERVAL_MS = 800;

/** Intervalo de rotação automática de slot no dashboard quando há 2+ alarmes
 *  ativos simultâneos. 3 s dá tempo de o usuário ler cada slot. */
constexpr uint32_t ALARM_ROTATE_INTERVAL_MS  = 3000;

/** Meia-período do flash de alarme no dashboard (ms). Ciclo completo = 2×
 *  este valor (on→off→on). 600 ms resulta em ~0.83 Hz — visível mas não
 *  agressivo. */
constexpr uint32_t ALARM_FLASH_INTERVAL_MS   = 600;

/** Duração do toast "Web: <user>" no header do dashboard após um login web
 *  bem-sucedido (ms). */
constexpr uint32_t WEB_NOTIFY_DURATION_MS    = 5000;



/* =========================================================================== */
/*                       SAFE STRING COPY UTILITY                            */
/* =========================================================================== */

/**
 * @brief  Copia uma string para um buffer de tamanho fixo com null-termination garantida.
 *
 * Substitui o padrão inseguro de strncpy sem terminador.
 * Uso típico: safeCopy(cfg.deviceName, source, sizeof(cfg.deviceName));
 *
 * @param  dst      Buffer de destino.
 * @param  src      String de origem (pode ser nullptr — resulta em string vazia).
 * @param  dstSize  Tamanho total do buffer de destino (incluindo o '\0').
 */
inline void safeCopy(char* dst, const char* src, size_t dstSize) {
    if (dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}


/* =========================================================================== */
/*                      WRAP-SAFE MILLIS() COMPARISON                        */
/* =========================================================================== */

/**
 * @brief  Verifica de forma segura se um deadline (baseado em millis()) foi atingido.
 *
 * SEMPRE usar esta função em vez de `millis() > deadline` ou `millis() < deadline`.
 * O contador millis() é um uint32_t que sofre wraparound a cada ~49,7 dias; a
 * comparação direta inverte o resultado após o wrap, causando timeouts eternos
 * (lockouts que nunca expiram, handlers que travam, etc.).
 *
 * A subtração em aritmética signed trata o wraparound corretamente:
 *   - retorna >= 0 quando now já atingiu/passou deadline
 *   - retorna  < 0 quando ainda não chegou
 *
 * Uso típico:
 *   if (timeReached(_lockoutUntil))   forceDashboard();   // destrava
 *   if (!timeReached(_deadline))      _pending = true;    // ainda esperando
 *
 * @param  deadline  Valor absoluto de millis() a comparar com o "agora".
 * @return true se millis() já atingiu ou passou deadline (wrap-safe).
 */
inline bool timeReached(uint32_t deadline) {
    return (int32_t)(millis() - deadline) >= 0;
}

/**
 * @brief  Verifica se um intervalo decorreu desde um timestamp de início.
 *
 * Wrap-safe equivalente a `millis() - start >= duration` — usa subtração em
 * `int32_t` para tratar corretamente o wraparound de millis() (~49,7 dias).
 *
 * Uso típico:
 *   if (timeSince(_lastPoll, 1000))       doPoll();      // 1s desde poll
 *   if (!timeSince(_lastTouch, 30000))    return;        // < 30s desde touch
 *
 * Diferença em relação a `timeReached()`: este helper é para comparar
 * intervalos decorridos desde um evento; `timeReached()` é para deadlines
 * absolutos (ex: `_lockoutUntil`).
 *
 * @param  start     millis() do evento inicial.
 * @param  duration  Intervalo (ms) após o qual retorna true.
 * @return true se `millis() - start` já atingiu ou passou `duration`.
 */
inline bool timeSince(uint32_t start, uint32_t duration) {
    return (int32_t)(millis() - start) >= (int32_t)duration;
}

/**
 * @brief  Tempo restante até deadline, em milissegundos. Wrap-safe.
 *
 * Retorna 0 se o deadline já passou. Substitui o padrão inseguro
 * `deadline - millis()`, que sofre underflow (retorna valor enorme) após
 * o wrap de millis() e produz "segundos restantes" absurdos no UI.
 *
 * @param  deadline  Valor absoluto de millis() a comparar com o "agora".
 * @return millissegundos até deadline, ou 0 se já atingido.
 */
inline uint32_t timeRemaining(uint32_t deadline) {
    int32_t diff = (int32_t)(deadline - millis());
    return (diff > 0) ? (uint32_t)diff : 0;
}

