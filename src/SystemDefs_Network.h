/**
 * @file SystemDefs_Network.h
 * @brief Network resilience, rate-limiter, login state, BT/CLI/AP/cursor (EXT-003 split).
 * @details Constantes do WebManager (rate-limit, login, sessões, web handler
 * deadline), AP mode timeout, CLI line max, Bluetooth auth buffer,
 * telemetry cursor coalesce. Sub-header de SystemDefs.h (facade).
 * EXT-003 / F17 .
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* =========================================================================== */
/* NETWORK RESILIENCE CONSTANTS */
/* =========================================================================== */

/**
 * Timeout de socket para operações TCP/TLS (ms).
 * Garante que nenhuma chamada bloqueante de rede ultrapasse o watchdog.
 * Deve ser significativamente menor que WATCHDOG_TIMEOUT_MS.
 */
constexpr uint32_t NET_SOCKET_TIMEOUT_MS = 4000;

/**
 * RSSI mínimo aceitável para operações de rede pesadas (dBm).
 * Abaixo desse limiar, telemetria e uploads são adiados para evitar
 * timeouts que congelam o main loop. Dashboard e sensores continuam.
 */
constexpr int32_t RSSI_MIN_THRESHOLD = -78;

/**
 * Intervalo mínimo entre chamadas a MDNS.update( ) (ms).
 * mDNS não precisa de polling a cada loop — throttle evita overhead
 * desnecessário em rede degradada.
 */
constexpr uint32_t MDNS_UPDATE_INTERVAL_MS = 2000;

/**
 * Máximo de ciclos de reconexão WiFi consecutivos antes de entrar
 * em dormência longa (backoff de 10 minutos). Resetado após sucesso.
 */
constexpr uint8_t WIFI_MAX_CONNECT_CYCLES = 5;

/** Backoff de dormência longa após esgotar tentativas WiFi (ms). */
constexpr uint32_t WIFI_DORMANT_DELAY_MS = 600000;

/**
 * Teto para alimentação do watchdog em guards de operações longas (ms).
 *
 * Aplica-se aos *repeating timers* `SendGuard` (WebManager) e
 * `TelemetryGuard` (TelemetryManager). Enquanto uma operação bloqueante
 * está em curso (POST TLS, envio de payload grande), o guard alimenta o
 * watchdog a cada 2 s — até este teto. Se ultrapassado, para de alimentar
 * (watchdog age como *safety net* contra deadlocks reais) E sinaliza
 * *aborto limpo* via flag compartilhada, para que o handler retorne
 * com erro em vez de ser morto pelo watchdog.
 *
 * Dimensionamento: valor deve cobrir o pior caso de operação legítima
 * (TLS handshake + envio de 230 KB em link 2G) — 60 s com folga.
 * Deve ser muito maior que NET_SOCKET_TIMEOUT_MS para evitar falso
 * positivo e muito menor que uptime-ms-wrap (~49 d) por definição.
 */
constexpr uint32_t WDT_FEED_MAX_WINDOW_MS = 120000;

/**
 * Teto do backoff exponencial de retry do NTP (ms).
 *
 * Sequência aplicada em NET_CONNECTED_WAIT_NTP: 20 s → 60 s → 5 min → 15 min.
 * Após 3 falhas consecutivas, faz-se fallback automático para pool.ntp.org.
 * Reset a zero (volta para 20 s) após primeira sincronização bem-sucedida.
 */
constexpr uint32_t NTP_MAX_RETRY_DELAY_MS = 900000;

/**
 * Número de falhas consecutivas no NTP antes de acionar fallback para
 * pool.ntp.org. Se o servidor configurado já for pool.ntp.org, o fallback
 * é silenciosamente ignorado.
 */
constexpr uint8_t NTP_FAILS_BEFORE_FALLBACK = 3;

/* ── Rate-limiter (WebManager) ── */

/** Número de slots no rate-limiter por IP. */
constexpr uint8_t RATE_LIMIT_SLOTS = 16;

/** TTL de uma entrada no rate-limiter (ms). Slot expirado é tratado como livre. */
constexpr uint32_t RATE_LIMIT_TTL_MS = 900000;

/* ── Login state ── */

/** Número de slots para rastreamento de estado de login (IP → failCount). */
constexpr uint8_t LOGIN_STATE_SLOTS = 8;

/** PASSWORD_HMAC_ROUNDS — Número de iterações HMAC-SHA256 para hashing
 * de senhas. OWASP 2023 recomenda ≥600k; NIST recomenda ≥10k. O RP2040
 * (Cortex-M0+ @133MHz) com 5000 rounds consome ~400ms por operação —
 * aceitável para login (infrequente). A cada 50 rounds alimenta o WDT. */
constexpr uint16_t PASSWORD_HMAC_ROUNDS = 5000;

/* ── Bluetooth auth ── */

/** Tamanho máximo do buffer de entrada de senha via Bluetooth. */
constexpr uint8_t BT_AUTH_BUFFER_MAX = 64;

/** SEC-005/F12.5: Tamanho máximo de uma linha da CLI (USB + BT pós-auth).
 * Acima disso o buffer é descartado para evitar DoS de heap por stream
 * sem terminador de linha. Linhas CLI reais (ex: `tel dump json verbose`)
 * ficam bem abaixo desse limite. */
constexpr size_t CLI_LINE_MAX = 256;

/* ── Web handlers ── */

/** Deadline para handlers longos (history, logs, screenshot) em ms.
 * PERF: tools/test_perf.sh apontou export 3d media 10.5s (passava do limite
 * de 10s e gerava CRC fail). Margem +50% absorve concorrencia/burst. */
constexpr uint32_t WEB_LONG_HANDLER_DEADLINE_MS = 15000;

/* ── AP mode ── */

/** Timeout do AP mode sem clientes antes de reboot para STA (ms). */
constexpr uint32_t AP_MODE_TIMEOUT_MS = 900000;

/* ── Telemetry cursor ── */

/** Tempo mínimo entre writes do cursor de telemetria no flash (ms).
 * Múltiplos setLastSentTimestamp dentro dessa janela consolidam em 1 write. */
constexpr uint32_t CURSOR_COALESCE_MS = 5000;
