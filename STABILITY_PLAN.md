# Plano de Estabilidade — SIMUT

> **Escopo:** auditoria e eliminação de vulnerabilidades que podem provocar travamentos, reboots ou degradação sob rede instável, alta demanda ou longo tempo de atividade.
> **Versão base da auditoria:** `v3.4.9` (branch `stability-fixes-tier1` @ `1224853`).
> **Backup íntegro:** `/home/angelo/Documentos/SIMUT_backup_20260415_232255*` (dir + bundle git + tarball).
> **Autor da auditoria:** análise técnica assistida por Claude Code (Opus 4.6, 1M context), 2026-04-15.

---

## 1. Contexto Arquitetural

**Alvo:** Raspberry Pi Pico W (RP2040, dual-core Cortex-M0+) sob Arduino-Pico. ~28,5K linhas em 15 módulos.

**Separação por core:**
- **Core 0**: boot, rede (WiFi/NTP/mDNS), servidor HTTP, telemetria HTTP/MQTT, persistência LittleFS, CLI, alarmes, *preload* de gráficos, *watchdog* de hardware (8,3 s).
- **Core 1**: render TFT ILI9341 + touch XPT2046, máquina de estados da UI.
- **IPC**: fila de `UiEvent`, *mutex* para leitura do FS, `multicore_lockout` para escrita em flash, `__atomic_*` para flags cross-core, *scratch registers* do watchdog para autópsia de crash.

**Pontos fortes de engenharia defensiva já presentes:**
1. `timeReached()` / `timeRemaining()` wrap-safe (`SystemDefs.h:149/163`).
2. `TelemetryGuard` e `SendGuard` — *repeating timers* alimentam o watchdog durante POST TLS bloqueante.
3. `CompactLogRecord` (12 B) — formato binário com rotação (800 registros).
4. Config com CRC32 + *dual-bank* + salvamento atômico (`tmp` → `rename`) + migração.
5. Backoff exponencial com jitter em WiFi e telemetria, modo dormente de 10 min após 5 falhas.
6. HMAC-SHA256 com *pepper* (serial único do chip) + 2500 rounds.
7. `safeBatchLimit()` calcula *batch* dinamicamente pela heap livre.
8. `WiFiClientSecure` pré-alocado no boot (evita fragmentação por 16 KB).
9. Autópsia de crash via *scratch registers* do watchdog (`LogManager.cpp:390`).
10. Watchdog alimentado incondicionalmente no `loop()` (`SIMUT.ino:45/49`).

---

## 2. Vulnerabilidades Identificadas

Legenda: 🔴 **Crítica** · 🟠 **Alta** · 🟡 **Média** · 🟢 **Baixa** · ✅ **Resolvida**

### 2.1 Redes Instáveis

| ID | Sev | Arquivo:linha | Problema | Impacto |
|----|----|---|---|---|
| **N1** | 🔴 | `WebManager.cpp:1235` | `delay(1000)` antes de `rp2040.reboot()` sem feed do watchdog. | Reboot prematuro. |
| **N2** | 🔴 | `NetworkManager.cpp:179` | NTP retry fixo de 20 s eterno, sem fallback para `pool.ntp.org`. | CPU/flash desperdiçados indefinidamente. |
| **N3** | 🔴 | `TelemetryManager.cpp:40` | `_telGuardCallback` para de alimentar watchdog após 30 s. POST TLS em rede ruim excede. | Reboot durante operação legítima. |
| **N4** | 🔴 | `WebManager.cpp:162` | `_sendGuardTimerCallback` para de alimentar após 35 s. Screenshot 230 KB em cliente lento excede. | Reboot no meio da resposta. |
| **N5** | 🟠 | `NetworkManager` | AP mode sem timeout — fica preso indefinidamente. | Dispositivo não opera até intervenção manual. |
| **N6** | 🟠 | `TelemetryManager.cpp:480` | `mqttEnsureConnected()` tem throttle 5 s interno — ok, mas connect bloqueia até 4 s. | Latência de UI/web em MQTT offline. |
| **N7** | 🟠 | `TelemetryManager.cpp:605` | MQTT QoS=0 default, avança cursor sem confirmação. | Perda silenciosa de dados em rede instável. |
| **N8** | 🟠 | `NetworkManager.cpp:185` | `WiFi.status()` mantém `WL_CONNECTED` em túneis de roaming mesmo com socket caído. | Handlers zumbi. |
| **N9** | 🟡 | `TelemetryManager.cpp:85` | `cert.pem` lido via `readString()` no boot sem validação de tamanho. | OOM se cert corrompido >8 KB. |
| **N10** | 🟡 | `BluetoothManager.cpp:57` | `_authBuffer` cresce sem limite durante input. | DoS/OOM via BT. |

### 2.2 Alta Demanda

| ID | Sev | Arquivo:linha | Problema | Impacto |
|----|----|---|---|---|
| **D1** | 🔴 | `WebManager.cpp:117` | Rate-limiter com 3 slots LRU — bypass com 4+ IPs. | Brute-force ilimitado. |
| **D2** | 🔴 | `WebManager.cpp:1020` | `failCount` não incrementa em nonce expirado. | Lockout nunca dispara. |
| **D3** | 🔴 | `WebManager` (geral) | `WebServer` síncrono — 1 request pesado trava `loop()` inteiro. | Sistema travado para todos enquanto 1 baixa histórico. |
| **D4** | 🔴 | `TelemetryManager.cpp:942` | `refreshPendingCount` varre TODOS os arquivos/registros a cada 10 s. | Core 0 saturado após 30+ dias. |
| **D5** | 🔴 | `LogManager.cpp:201` | Open/write/close por entrada — amplificação de escrita. | Flash wear + I/O excessivo. |
| **D6** | 🟠 | `WebManager` (39 ocorrências) | `String += "..."` em loop fragmenta heap. | OOM após horas de uptime com múltiplos clientes. |
| **D7** | 🟠 | `WebManager.cpp` | `_handlerDeadline = 30 s` — bloqueia outros clientes. | Timeout percebido pelo cliente. |
| **D8** | 🟠 | `TelemetryManager.cpp:712` | `_isSending` não atômico — race entre `forceSync` CLI e `update`. | Race condition rara. |
| **D9** | 🟠 | `LogManager.cpp:89` | `_pendingLogs[8]` silencia logs após 8º. | Perda de eventos de segurança. |
| **D10** | 🟠 | `StorageManager.cpp:381` | `setLastSentTimestamp` escreve 4 B por batch (~1440×/dia). | Flash wear em 2-3 anos. |
| **D11** | 🟡 | `LogManager.cpp:131` | Serial.printf dentro do mutex do log bloqueia Core 1. | Stutter de UI. |
| **D12** | 🟡 | `WebManager` screenshot | `pauseRendering` sem cancel — requests concorrentes se enfileiram. | Display congelado. |
| **D13** | 🟡 | `WebManager` login | Username de 10 MB → SHA256 trava core. | DoS via payload. |
| **D14** | 🟢 | `WebManager` upload | Sem validação de `Content-Length` total. | FS cheio por upload overflow. |

### 2.3 Longo Tempo de Atividade

| ID | Sev | Arquivo:linha | Problema | Impacto |
|----|----|---|---|---|
| **U1** | 🔴 | `TelemetryManager.cpp:942` | `refreshPendingCount` O(N_arq × N_reg) — degradação linear no tempo. | Ver D4. |
| **U2** | 🔴 | `AppManager.cpp:1558` | `APP_HEAP_REPORT` a cada 60 s polui log e causa rotação prematura. | Logs importantes sobrescritos. |
| **U3** | 🔴 | Geral | Uso extensivo de `String` fragmenta heap em escala de semanas. | OOM inevitável. |
| **U4** | 🔴 | `SystemDefs.h:753` | `uptimeHr = millis()/3600000UL` volta a 0 após wrap (49 d). | Confunde análise de logs. |
| **U5** | 🟠 | `StorageManager.cpp:653` | `hashPassword` feed watchdog a cada 100 rounds (~100 ms). | Watchdog possível sob carga. |
| **U6** | 🟠 | `LogManager.cpp:356` | Check de heartbeat pula 1 ciclo no wrap de `millis()`. | Janela de detecção aumentada. |
| **U7** | 🟠 | `StorageManager.cpp:318` | `_cachedOldestFile` não invalidado em novos arquivos mais antigos (restore manual). | Delete do arquivo errado. |
| **U8** | 🟠 | `TelemetryManager.cpp:287` | Sem NTP, `collectBatch` varre TODOS os arquivos. | Latência enorme por batch. |
| **U9** | 🟡 | `NetworkManager.cpp:119` | Uso inconsistente de `timeReached()`. | Risco de manutenção. |
| **U10** | 🟡 | `StorageManager.cpp:487` | `correctProvisionalTimestamps` sem retomada após budget. | Histórico inconsistente. |
| **U11** | 🟡 | `TelemetryManager.cpp:677` | Logs suprimidos após 10 falhas — operador sem visibilidade. | Diagnóstico difícil. |
| **U12** | 🟢 | Geral | Inputs de config sem limite de tamanho. | Fragmentação em escrita. |
| **U13** | 🟢 | `TelemetryManager.cpp:701` | TLS client (~16 KB) sempre alocado mesmo com `telInterval=0`. | Heap ocupada. |

---

## 3. Plano de Execução em 7 Fases

Cada fase é *stand-alone*, testável isoladamente, e pode ser revertida. Branches sugeridas: `stability-fixes-tier2`, `tier3`, etc.

---

### 🔴 FASE 1 — Watchdog & Timeouts de Rede

**Escopo:** N2, N3, N4 · **Arquivos:** `SystemDefs.h`, `WebManager.cpp/h`, `TelemetryManager.cpp`, `NetworkManager.cpp/h`.

| Tarefa | Resolve |
|---|---|
| 1.1 Estender cap do `SendGuard` / `TelemetryGuard` para `WDT_FEED_MAX_WINDOW_MS` (60 s) + flag de expiração que dispara aborto limpo via `isClientGone()`. | N3, N4 |
| 1.2 NTP com backoff exponencial (20 s → 60 s → 5 min → 15 min) + fallback automático para `pool.ntp.org` após 3 falhas. | N2 |
| 1.3 Adicionar `WDT_FEED_MAX_WINDOW_MS` e `NTP_MAX_RETRY_DELAY_MS` em `SystemDefs.h`. | Fundação |
| 1.4 *[Revisado]* N6 já mitigado — throttle 5 s interno em `mqttEnsureConnected`. Documentar. | N6 |

**Validação:**
- NTP indisponível 10 min → backoff crescendo, 0 reboots.
- WiFi a -80 dBm + POST TLS 50 KB → abort limpo.
- Uptime 24 h com MQTT broker offline → heap estável.

**Saída:** 0 reboots não programados em 24 h de teste de rede instável.

---

### 🔴 FASE 2 — Autenticação Robusta & Rate Limiting

**Escopo:** D1, D2, D13, N10 · **Arquivos:** `WebManager.cpp`, `BluetoothManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 2.1 Trocar `RateEntry[3]` por array de 16 slots com LRU explícita + TTL 900 s. | D1 |
| 2.2 Incrementar `failCount` em nonce expirado; limitar 5 nonces/min/IP. | D2 |
| 2.3 Validar `username` com `isValidName(name, 31)` antes de SHA256; limitar `password <= 128`. | D13 |
| 2.4 `_authBuffer` BT limitado a 64 chars. | N10 |
| 2.5 Endpoint `GET /api/sec_status` (PERM_USER_MGR) listando lockouts ativos. | Observabilidade |

**Saída:** pentest básico (Hydra) não obtém >5 tentativas/min após lockout.

---

### 🔴 FASE 3 — Handlers Síncronos & Concorrência

**Escopo:** D3 (parcial), D4/U1, D7, D8, D12 · **Arquivos:** `WebManager.cpp`, `TelemetryManager.cpp`, `AppManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 3.1 `TelemetryManager::_pendingEstimate` incremental (writeHistoryEntry +1, setLastSentTimestamp -delta). Full scan só no boot. | D4, U1 |
| 3.2 Reduzir `_handlerDeadline` para 10 s + *early abort* quando `_server.handleClient` não é chamado há >2 s. | D3 parcial, D7 |
| 3.3 Guard atômico `__atomic_compare_exchange_n` em `forceSync()`/`update()`. | D8 |
| 3.4 Flag `volatile bool _cancelScreenshot` + `409 Conflict` se um já está rodando. | D12 |
| 3.5 Throttle em `refreshPendingCount()` — só com flag dirty. | Escalabilidade |

**Saída:** latência P99 de `/api/status` < 500 ms sob carga; 0 reboots em 48 h.

---

### 🟠 FASE 4 — Logging & Flash Wear

**Escopo:** D5, D9, D10, U2 · **Arquivos:** `LogManager.cpp/h`, `StorageManager.cpp/h`, `AppManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 4.1 `File _logFileHandle` persistente em `"a"`; flush a cada N ou 2 s. | D5 |
| 4.2 `LOG_PENDING_MAX = 32` + contador `_pendingOverflow` logado. | D9 |
| 4.3 `setLastSentTimestamp` double-buffer A/B com CRC, coalesce em 5 s. | D10 |
| 4.4 `APP_HEAP_REPORT` só quando `heapFree < 32 KB` OU 1×/h. | U2 |
| 4.5 Endpoint `GET /api/flash_stats` com contadores. | Observabilidade |

**Saída:** rate de escrita em flash reduzido ≥5×; zero logs perdidos em teste.

---

### 🟠 FASE 5 — Heap & String Fragmentation

**Escopo:** U3, D6, U12 · **Arquivos:** todos os `handle*` em `WebManager.cpp`, `TelemetryManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 5.1 Helper `safeSendJsonKV(key, value)` com buffer stack 256 B. | Fundação |
| 5.2 Refatorar ondas: `handleApiConfig` → `handleApiNetwork` → `handleApiUsers` → `handleApiAlarms` → `handleApiStatus` → `handleApiPerms` → `handleApiLs` → `handleApiHistoryDays`. | D6 |
| 5.3 `formatLineCustom` — substituir `String::replace()` por `snprintf` em buffer. | U3 |
| 5.4 `maxlength` no HTML + validação `<= sizeof(cfg.campo)-1` no servidor. | U12 |
| 5.5 Métrica `heapLargestBlock` em `/api/status` + telemetria. | Observabilidade |

**Saída:** ≥30 dias sob carga leve sem OOM; `heapLargestBlock` decai <10 %.

---

### 🟠 FASE 6 — Operação de Longo Prazo & Edge Cases

**Escopo:** N5, N7, U7, U8, U10, U11 · **Arquivos:** `NetworkManager.cpp/h`, `TelemetryManager.cpp`, `StorageManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 6.1 `AP_MODE_TIMEOUT_MS = 900000` → reboot em STA se nenhum cliente conectar. | N5 |
| 6.2 MQTT QoS=1 com ACK tracking antes de avançar cursor. | N7 |
| 6.3 Invalidar `_cachedOldestFile` ao criar novo arquivo em cruzamento de meia-noite. | U7 |
| 6.4 `collectBatch` sem NTP → usar `getLastRecordedTimestamp() - 30d`. | U8 |
| 6.5 `correctProvisionalTimestamps` com watermark persistente para retomada. | U10 |
| 6.6 1 log `LOG_ERROR` a cada hora após supressão. | U11 |

---

### 🟢 FASE 7 — Hardening & Polish

**Escopo:** N1, N8, N9, U4, U5, U6, U9, U13, D11, D14.

| Tarefa | Resolve |
|---|---|
| 7.1 `handleSaveNetwork` com spin + `watchdog_update` no lugar do `delay(1000)`. | N1 |
| 7.2 TCP keepalive via LwIP. | N8 |
| 7.3 Validar `cert.pem` <= 16 KB antes de `readString()`. | N9 |
| 7.4 `uptime` real via `now - _bootEpoch` persistido. | U4 |
| 7.5 `hashPassword` — feed a cada 50 rounds OU reduzir para 2000. | U5 |
| 7.6 `checkCrossCoreHealth` — ciclo adicional no wrap. | U6 |
| 7.7 `timeReached()` padronizado em todos os `millis() -` restantes. | U9 |
| 7.8 Alocar `_httpSecurePtr` só se `telInterval > 0`. | U13 |
| 7.9 `Serial.printf` fora do mutex em `LogManager::logCode`. | D11 |
| 7.10 Validar upload vs FS free; `413` se exceder. | D14 |

---

## 4. Disciplina de Execução

1. **Uma fase por PR**, descrição listando IDs resolvidos.
2. **Testes de regressão antes de cada merge**: boot frio (AP/STA/sem NTP), burn-in ≥2 h, teste específico da fase.
3. **Tag semver ao final**: `v3.5.0` (F1), `v3.5.1` (F2), …, `v3.6.0` (final).
4. **Não misturar fases**: bug em F5 que parece de F2 → issue, conserto após F5.
5. **Canary**: 1 dispositivo em produção interna rodando nightly.
6. **Rollback plan**: toda PR menciona tag anterior segura.

### Marcos de Entrega

| Marco | Versão | Critério |
|---|---|---|
| M1 | v3.5.0 | Fase 1 — zero reboots falsos |
| M2 | v3.5.1 | Fase 2 — pentest aprovado |
| M3 | v3.5.2 | Fase 3 — concorrência estável |
| M4 | v3.5.4 | Fases 4+5 — heap estável 30 dias |
| Final | v3.6.0 | Fases 6+7 — *production-ready* |

---

## 5. Status de Execução

Atualize esta tabela conforme cada fase for concluída.

| Fase | Status | PR | Tag | Data conclusão |
|---|---|---|---|---|
| **F1 — Watchdog & Timeouts** | ✅ Concluída (pendente teste em hardware) | `stability-fixes-tier1` | — | 2026-04-15 |
| F2 — Autenticação & Rate Limit | ⚪ Pendente | — | — | — |
| F3 — Handlers & Concorrência | ⚪ Pendente | — | — | — |
| F4 — Logging & Flash Wear | ⚪ Pendente | — | — | — |
| F5 — Heap & String | ⚪ Pendente | — | — | — |
| F6 — Long-term & Edge Cases | ⚪ Pendente | — | — | — |
| F7 — Hardening & Polish | ⚪ Pendente | — | — | — |

### Legenda de Status

- ⚪ Pendente
- 🟡 Em andamento
- ✅ Concluída
- 🚫 Bloqueada

---

## 6. Tabela Mestre de Findings (Rastreamento)

| ID | Sev | Fase | Status | Observação |
|---|---|---|---|---|
| N1 | 🔴 | F7 | ⚪ | |
| N2 | 🔴 | F1 | ✅ | Backoff exponencial + fallback pool.ntp.org (`NetworkManager.cpp`). |
| N3 | 🔴 | F1 | ✅ | `TelemetryGuard` estendido para `WDT_FEED_MAX_WINDOW_MS` (60 s). |
| N4 | 🔴 | F1 | ✅ | `SendGuard` + flag `_sendGuardExpired` → aborto limpo em `isClientGone()`. |
| N5 | 🟠 | F6 | ⚪ | |
| N6 | 🟠 | F1 | ✅ | Já mitigado em prod (throttle 5 s interno). |
| N7 | 🟠 | F6 | ⚪ | |
| N8 | 🟠 | F7 | ⚪ | |
| N9 | 🟡 | F7 | ⚪ | |
| N10 | 🟡 | F2 | ⚪ | |
| D1 | 🔴 | F2 | ⚪ | |
| D2 | 🔴 | F2 | ⚪ | |
| D3 | 🔴 | F3 | ⚪ | Mitigação parcial; solução completa exige async. |
| D4 | 🔴 | F3 | ⚪ | |
| D5 | 🔴 | F4 | ⚪ | |
| D6 | 🟠 | F5 | ⚪ | |
| D7 | 🟠 | F3 | ⚪ | |
| D8 | 🟠 | F3 | ⚪ | |
| D9 | 🟠 | F4 | ⚪ | |
| D10 | 🟠 | F4 | ⚪ | |
| D11 | 🟡 | F7 | ⚪ | |
| D12 | 🟡 | F3 | ⚪ | |
| D13 | 🟡 | F2 | ⚪ | |
| D14 | 🟢 | F7 | ⚪ | |
| U1 | 🔴 | F3 | ⚪ | |
| U2 | 🔴 | F4 | ⚪ | |
| U3 | 🔴 | F5 | ⚪ | |
| U4 | 🔴 | F7 | ⚪ | |
| U5 | 🟠 | F7 | ⚪ | |
| U6 | 🟠 | F7 | ⚪ | |
| U7 | 🟠 | F6 | ⚪ | |
| U8 | 🟠 | F6 | ⚪ | |
| U9 | 🟡 | F7 | ⚪ | |
| U10 | 🟡 | F6 | ⚪ | |
| U11 | 🟡 | F6 | ⚪ | |
| U12 | 🟢 | F5 | ⚪ | |
| U13 | 🟢 | F7 | ⚪ | |
