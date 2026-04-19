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
| **U14** | 🔴 | `LogManager.cpp:412` | `elapsed = now - lastBeat` unsigned subtract dispara soft panic falso em cross-core race (lastBeat levemente adiantado → underflow ≈ UINT32_MAX > 8000). Mesmo bug na checagem `millis() - beat` de Core 1 dead em `AppManager.cpp:446`. Autópsia confundia `rp2040.reboot()` com HW watchdog. | Reboots fantasma não reprodutíveis desde F7 (2026-04-18). |
| **U15** | 🔴 | `LogManager.cpp:252` | `writeCompactToFlash` / `flushPendingLogs` sem feeds de watchdog entre LittleFS open/write/close. Sob LittleFS >70%, GC interno bloqueia por segundos. `LOG_CODE` chamado dentro de rotas internas (ex.: `BluetoothManager::update` no login) excede WDT de 8.3s. | HW WATCHDOG em login BT. |

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
| **F1 — Watchdog & Timeouts** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.0` | 2026-04-18 |
| **F2 — Autenticação & Rate Limit** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.1` | 2026-04-18 |
| **F3 — Handlers & Concorrência** | 🟡 Em andamento | `stability-fixes-tier1` | — | — |
| **F4 — Logging & Flash Wear** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.4` | 2026-04-18 |
| **F5 — Heap & String** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.5` | 2026-04-18 |
| **F6 — Long-term & Edge Cases** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.6` | 2026-04-18 |
| **F7 — Hardening & Polish** | ✅ Concluída | `stability-fixes-tier1` | `v3.6.0` | 2026-04-18 |
| **F8 — Watchdog panic false-positive** | ✅ Concluída | `stability-fixes-tier1` | `v3.9.2` | 2026-04-18 |
| **F9 — WDT feeds no path de flash + audit BT** | ✅ Concluída | `stability-fixes-tier1` | `v3.11.1` | 2026-04-19 |
| **F10 — Estabilidade em rajadas de save (U16)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-04-19 |

### Legenda de Status

- ⚪ Pendente
- 🟡 Em andamento
- ✅ Concluída
- 🚫 Bloqueada

---

## 6. Tabela Mestre de Findings (Rastreamento)

| ID | Sev | Fase | Status | Observação |
|---|---|---|---|---|
| N1 | 🔴 | F7 | ✅ | `delay(1000)` → spin + `watchdog_update()` antes de reboot. |
| N2 | 🔴 | F1 | ✅ | Backoff exponencial + fallback pool.ntp.org (`NetworkManager.cpp`). |
| N3 | 🔴 | F1 | ✅ | `TelemetryGuard` estendido para `WDT_FEED_MAX_WINDOW_MS` (60 s). |
| N4 | 🔴 | F1 | ✅ | `SendGuard` + flag `_sendGuardExpired` → aborto limpo em `isClientGone()`. |
| N5 | 🟠 | F6 | ✅ | AP mode timeout 15 min → reboot para STA se SSID configurado. |
| N6 | 🟠 | F1 | ✅ | Já mitigado em prod (throttle 5 s interno). |
| N7 | 🟠 | F6 | ⚠️ | PubSubClient não suporta QoS>0 por publish; cursor parcial mitiga. |
| N8 | 🟠 | F7 | ⚠️ | TCP keepalive via LwIP — risco de regressão; adiado. |
| N9 | 🟡 | F7 | ✅ | `cert.pem` validado `<= 16 KB` antes de `readString()`. |
| N10 | 🟡 | F2 | ✅ | `_authBuffer` limitado a `BT_AUTH_BUFFER_MAX` (64 chars). |
| D1 | 🔴 | F2 | ✅ | Rate-limiter 16 slots com TTL 15 min (`WebManager.cpp`). |
| D2 | 🔴 | F2 | ✅ | `failCount++` em nonce expirado + lockout exponencial. |
| D3 | 🔴 | F3 | ✅ | Deadline 30s→10s; mitigação parcial (async exige rewrite). |
| D4 | 🔴 | F3 | ✅ | `refreshPendingCount` com dirty flag + `notifyNewRecord` incremental. |
| D5 | 🔴 | F4 | ✅ | File handle persistente — 1 open no boot, close só na rotação. |
| D6 | 🟠 | F5 | ✅ | `handleApiConfig` e `handleApiThemes` refatorados para snprintf stack. |
| D7 | 🟠 | F3 | ✅ | `WEB_LONG_HANDLER_DEADLINE_MS = 10000` (era 30s). |
| D8 | 🟠 | F3 | ✅ | `_isSending` com `__atomic_compare_exchange_n` (CAS). |
| D9 | 🟠 | F4 | ✅ | `LOG_PENDING_MAX` 8→32 + overflow counter logado. |
| D10 | 🟠 | F4 | ✅ | `setLastSentTimestamp` com coalesce 5s (~5x menos writes). |
| D11 | 🟡 | F7 | ✅ | `Serial.println` movido para fora do mutex em `logCode`/`log`. |
| D12 | 🟡 | F3 | ✅ | `_cancelScreenshot` flag + 409 Conflict em request concorrente. |
| D13 | 🟡 | F2 | ✅ | Validação `isValidName(u,31)` + `password<=128` antes de `hashPassword`. |
| D14 | 🟢 | F7 | ✅ | Upload valida `Content-Length` vs espaço livre; 413 se excede. |
| U1 | 🔴 | F3 | ✅ | Ver D4 — dirty flag elimina scan contínuo. |
| U2 | 🔴 | F4 | ✅ | `APP_HEAP_REPORT` só quando heap < 32 KB ou 1x/hora. |
| U3 | 🔴 | F5 | ✅ | **Concluído (2026-04-19):** 5.3 — `formatLineCustomBuf` char-buffer single-pass elimina ~20 `String::replace()` por registro; `buildPayload` Custom branch emite linhas via `concat(buf, len)`, sem intermediário. Preview JS (`_previewCustomLine` + `_previewGlobal`) reescrito como espelho byte-fiel; `/api/config` expõe `serial` + `sensors[].{hwid,active}`. CLI `tel dump` (one-shot via `LogManager::writeConsole`, USB+BT chunked). 5.2 — auditoria de 33 handlers web: todos já em streaming+stack (`char buf[]`+snprintf+safeSend); `handleApiLs` migrado de `String json; json.reserve(2048)` para `safeSend`/`char buf[256]`; `handleApiHistoryData`/`handleApiLogs` já estavam em streaming. 5.4 — maxlength validation in place. **5.5 — métrica `heapLargestBlock` ⚪ pendente (polish, não estabilidade).** |
| U4 | 🔴 | F7 | ⚠️ | `uptimeHr` em CompactLogRecord — mudar exige migração de formato. |
| U5 | 🟠 | F7 | ✅ | `hashPassword` feed a cada 50 rounds (era 100). |
| U6 | 🟠 | F7 | ⚠️ | Guard `now >= lastBeat` removido; wrap-safe vs millis()-wrap mas **regressão em cross-core race** — ver U14. |
| U7 | 🟠 | F6 | ✅ | `invalidateOldestFileCache()` em upload + criação de arquivo. |
| U8 | 🟠 | F6 | ✅ | `collectBatch` fallback: `lastRecordedTs - 30d` quando cursor=0. |
| U9 | 🟡 | F7 | ✅ | Documentado como wrap-safe (unsigned subtraction). |
| U10 | 🟡 | F6 | ✅ | Watermark persistente em `correctProvisionalTimestamps`. |
| U11 | 🟡 | F6 | ✅ | Heartbeat 1x/hora após supressão de logs de telemetria. |
| U12 | 🟢 | F5 | ✅ | `maxlength` em t_glob (255), t_line (511), t_sep (7). |
| U13 | 🟢 | F7 | ✅ | TLS client só alocado se `telInterval > 0`. |
| U14 | 🔴 | F8 | ✅ | **Post-audit**: `(int32_t)(now - lastBeat)` em `checkCrossCoreHealth` e `AppManager:446`. `markCleanReboot()` via `scratch[5] = 0xC1EA8007` antes de `rp2040.reboot()` (CLI, WebManager save de rede, NetworkManager AP timeout). Autópsia distingue SOFT PANIC / HW WATCHDOG / reboot limpo. Scratch[7] passa a guardar `elapsed` real (era `now - moduleStartTime`, mascarava como 0ms). |
| U15 | 🔴 | F9 | ✅ | **Post-audit**: feeds de `watchdog_update()` entre cada LittleFS open/write/close/rename em `writeCompactToFlash` e `flushPendingLogs`. Descoberto após add de `LOG_CODE` no `BluetoothManager::update()` (audit BT, #12) disparar HW WATCHDOG sob LittleFS 82%. |
| U16 | 🔴 | F10 | ✅ | **Post-audit (2026-04-19):** rajadas de save web travavam HW WDT mesmo com feeds de U15. Fixes em camadas: (a) `watchdog_enable(30000)` em 5 paths críticos (saveConfiguration, writeHistoryEntry, flushCursorIfDirty, writeCompactToFlash, flushPendingLogs) antes do `enterFlashSafeMode`/`requestFsLock` — cobre lockout wait + flash ops; gated por `LogManager::isWdtActive()` pra não armar WDT no setup. (b) CRC skip em `saveConfiguration` — rajadas de clicks sem mudança pulam gravação. (c) audit `LOG_CODE` só em save real via `lastSaveWasNoOp()`. (d) rate-limit server-side 1s (HTTP 429) em 6 handlers. (e) soft panic threshold 8s→15s (cobre multicore_lockout cumulativo). (f) autópsia migrada de scratch[4] (reservado SDK watchdog_reboot) para scratch[3]; TRACE_MOD(0, MOD_BOOT) movido pra depois de LogManager::begin pra não sobrescrever. (g) dirty tracker client-side em /config, /network, /alarms — Save button disabled até mudança real. Validado em HW com ≥14 saves consecutivos sob graph preload. |
