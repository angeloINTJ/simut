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

### 2.4 Auditoria técnica v3.19.0 (2026-04-20)

Achados da auditoria externa documentada em `SIMUT_Audit_Report.md`. IDs herdam o prefixo do relatório original (`SEC-`, `BUG-`, `CON-`, `MEM-`, `PER-`, `REF-`, `DOC-`) para rastreabilidade cruzada.

#### Segurança (SEC)

| ID | Sev | Arquivo:linha | Problema | Impacto |
|----|----|---|---|---|
| **SEC-001** | 🔴 | `WebManager.cpp:1795` | `upload.filename` usado sem sanitização; path traversal via `../config/system.bin`. | Sobrescrita de config/users por usuário com `PERM_FILE_UPLOAD`. |
| **SEC-002** | 🟠 | `WebManager.cpp:1819` | `targetDir.replace("..","")` não-recursivo bypassa com `"...."` ou `%2e%2e`. | Escape do diretório de upload. |
| **SEC-003** | 🟠 | `StorageManager.cpp:165` | `admin/admin` e `viewer/viewer` hardcoded (hash SHA-256 conhecido). | Janela de setup + pós factory reset vulnerável. |
| **SEC-004** | 🟠 | `StorageManager.cpp:206` | PIN `"1234"` no display sem flag `mustChangePin`. | Acesso físico não exige troca. |
| **SEC-005** | 🟠 | `CommandManager.cpp:84,105` | `_usbBuffer += c` e `_btBuffer += c` sem bound-check. | DoS de heap via stream USB sem `\n`. |
| **SEC-006** | 🟡 | `WebManager.cpp:794` | LRU evict de `_loginStates[8]` zera `failCount`/`lockoutUntil` da vítima. | Bypass de rate-limit via IPs rotativos (gap remanescente pós-D1). |
| **SEC-007** | 🟢 | `StorageManager.cpp:995` | `hashPassword` trunca para 30 chars hex = 120 bits (NIST recomenda ≥128). | Compliance; 120 bits ainda seguro na prática. |
| **SEC-008** | 🟢 | `StorageManager.cpp:989` | 2500 rounds HMAC-SHA256 (OWASP 2023: ≥600k; NIST: ≥10k). | Força bruta offline se flash vazar. |
| **SEC-009** | 🟢 | `StorageManager.cpp:983` | Salt = username lowercase (determinístico). | Rainbow table por pepper viável. |

#### Bugs (BUG)

| ID | Sev | Arquivo:linha | Problema | Impacto |
|----|----|---|---|---|
| **BUG-001** | 🟢 | múltiplos (26 ocorrências) | `millis() - X > Y` vs `timeReached(X+Y)` — técnicamente wrap-safe por unsigned, mas inconsistente com helpers do projeto. | **Reclassificado como legibilidade**; ver nota abaixo. |
| **BUG-002** | 🟡 | `DisplayManager.h:453-476` | Pares `(data,flag)` cross-core com `volatile` sem `__dmb()`. | Core 1 pode ler flag antes dos dados (race µs). |
| **BUG-003** | 🟡 | `StorageManager.cpp:526-570` | `FLASH_OP` macro em `saveConfiguration` mas `writeHistoryEntryFlash` duplica o padrão manualmente sem chunking. | Core 1 bloqueado além do necessário no write de histórico. |
| **BUG-004** | 🟢 | `DisplayManager.cpp:1333` | `mutex_try_enter` em `_webBusy` → flicker do overlay. | UX apenas. |
| **BUG-005** | 🟢 | `LogManager.cpp:440,601` | `_preBootSnapshotTaken` oportunista em `setModule` vs leitura em `performCrashAutopsy` — ordem frágil. | Autópsia lê módulo errado se setup for refatorado. |

> **Nota BUG-001**: relatório classificou como 🟠 mas tecnicamente `millis() - X > Y` é wrap-safe por aritmética modular em `uint32_t` (idiom Arduino). A migração para `timeReached()` fica como **item de legibilidade/consistência** (F14), não bloqueante.

#### Inconsistências e Documentação (CON / DOC)

| ID | Sev | Local | Problema |
|----|----|---|---|
| **CON-001** | 🟢 | `LogManager.cpp:450 vs 554` | Comentários contraditórios sobre `scratch[4]/scratch[5]`. |
| **CON-002** | 🟢 | `SystemDefs.h:298` | `enum LanguageCode` só EN/PT/ES mas `DICTIONARY` tem 8. |
| **CON-003** | ⚪ | vários headers | Docstrings "3 idiomas / EN/PT/ES". |
| **CON-004** | 🟢 | `StorageManager.cpp:385` | `_lastSavedCrc` como `static` local em vez de membro. |
| **CON-005** | 🟢 | `SystemDefs.h:936`, `WebManager.h:89` | Mistura `String` + `char[]` em `CliDemand`/`LoginState`. |
| **CON-006** | ⚪ | `SensorManager.h:136` | `DS_CONVERSION_TIME = 750` local em vez de `SystemDefs.h`. |
| **DOC-002** | 🟢 | vários | Magic numbers (`100ms` DHT, `800ms` dots, `3000ms` alarm rotate) sem nome. |
| **DOC-003** | ⚪ | ausente | Falta `SECURITY.md` com threat model + rotação + resposta a incidente. |

#### Memória e Performance (MEM / PER)

| ID | Sev | Local | Problema |
|----|----|---|---|
| **MEM-001** | 🟡 | hot paths | `String` em `getHistoryFileName`, `getIpAddress`, mensagens de log. |
| **MEM-002** | 🟡 | `CliDemand` | 2× `String` na fila de 2 slots → 4 allocs por enqueue. |
| **MEM-003** | ⚪ | `WebUI.h` | 333 KB raw possivelmente redundante com `WebUI_GZ.h`. |
| **PER-001** | 🟢 | `AppManager::loop` | 14× `watchdog_update()` + `TRACE_BEAT(0)` → extrair `feedWdt()`. |
| **PER-002** | 🟢 | `WebManager.cpp:1842` | `RenderGuard` por chunk causa 50-100 pauses em upload 100 KB. |
| **PER-003** | ⚪ | `StorageManager.cpp:604` | `isValidHistoryFileName` por iteração sem fast-path. |

#### Refatoração (REF)

| ID | Sev | Local | Escopo |
|----|----|---|---|
| **REF-001** | 🟡 | `DisplayManager.cpp` (7.872 L) | Split em `_Dashboard.cpp`, `_Graph.cpp`, `_Settings.cpp`, `_Auth.cpp`, `_i18n.cpp`, etc. |
| **REF-002** | 🟡 | `AppManager.cpp` (3.334 L) | Split em `_Boot.cpp`, `_Commands.cpp`, `_Graph.cpp`, `_Events.cpp`, `_Sensors.cpp`, etc. |
| **REF-003** | 🟢 | `WebManager.cpp` (2.515 L) | Split em `_Auth.cpp`, `_Files.cpp`, `_Api.cpp`, `_Pages.cpp`, `_Commit.cpp`. |
| **REF-004** | 🟢 | 5 managers | 5× `setTouchPriorityChecker` → singleton `TouchPriority::setProvider`. |
| **REF-007** | 🟢 | `WebManager.cpp:831` | `handleApiLogin` ~130 linhas — extrair `findLoginStateForIp`, `checkLockout`, etc. |

> Deduplicações internas: `REF-005` ↔ `PER-001`, `REF-006` ↔ `BUG-003`, `DOC-001` ↔ `CON-003`.

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

### 🔴 FASE 12 — SEC Críticas/Altas (bloqueia exposição pública)

**Escopo:** SEC-001..005 · **Arquivos:** `SystemDefs.h`, `WebManager.cpp`, `StorageManager.cpp/h`, `CommandManager.cpp/h`, `AppManager.cpp`, `DisplayManager.cpp`.

| Tarefa | Resolve |
|---|---|
| 12.1 Helper `isSafeUploadFilename(const char*)` em `SystemDefs.h`; aplicar em `handleUploadData` antes de montar `finalPath`; rejeita `..`, controle, `\`, `:`, `<`, `>`, `|`, `?`, `*`, len>64. HTTP 400 + `LOG_CODE(SEC_UNAUTHORIZED)`. | SEC-001 |
| 12.2 Substituir `targetDir.replace("..","")` por rejeição via `indexOf("..")>=0` com HTTP 400 + log. | SEC-002 |
| 12.3 `generateInitialAdminPassword(char*, size_t)` em `StorageManager` — 8 chars `[A-Z2-9]` via `rp2040.hwrand32()`; exibe no display TFT por 5 min ou até 1º login; flag `_factoryDefaults` bloqueia ops sensíveis enquanto ativa. Viewer recebe `"viewer"` + `mustChangePassword` (não tem display). | SEC-003 |
| 12.4 Overlay `SetupFlagsData` em `SystemConfig.reserved[26..27]` com `FLAG_MUST_CHANGE_PIN`; aviso persistente no display; menu de config bloqueia saída até troca do PIN. Factory reset reseta flag. | SEC-004 |
| 12.5 `CLI_LINE_MAX=256` em `SystemDefs.h`; helper `appendCharWithLimit(String&, char, const char*)` em `CommandManager.cpp`; aplicar em `_usbBuffer` e `_btBuffer`. Log warning uma vez por rajada (flag anti-spam). | SEC-005 |

**Validação (gate humano):**
- Upload `curl -F "file=@x.bin;filename=../config/system.bin"` retorna 400.
- Boot pós factory reset mostra senha random no display; login funciona; após troca, senha zerada em RAM.
- PIN `1234` exibe aviso + bloqueia saída do menu.
- `yes | head -c 10000 > /dev/ttyUSB0` não trava dispositivo; heap estável.

**Saída esperada:** `v3.20.0`.

---

### 🟠 FASE 13 — Bugs latentes

**Escopo:** BUG-002..005 · **Arquivos:** `DisplayManager.h/cpp`, `StorageManager.cpp/h`, `LogManager.h/cpp`.

| Tarefa | Resolve |
|---|---|
| 13.1 `__dmb()` nos pares `(data,flag)` de preview de som / volume / packet arrow (producer em Core 0, consumer em Core 1). Helpers `requestPreviewSound`/`consumePreviewSound`. | BUG-002 |
| 13.2 Template method `StorageManager::flashOp<F>(F&&)` substituindo macro local `FLASH_OP`; `writeHistoryEntryFlash` refatorado para usar o helper com chunks granulares. | BUG-003 |
| 13.3 `_lastWebBusy` sticky no consumer de `_webBusy` em `DisplayManager::loopCore1`. | BUG-004 |
| 13.3b UX fix no menu de Sons: `acceptSlideTouch` gate-único no topo seta `_lastTouchRegion=0..3`, causando mismatch em `acceptHoldTouch(20/21)` → inc/dec de Volume/AlarmVol não funcionava. Move os gates para dentro dos branches específicos. | corolário descoberto em teste HW de F13.3 |
| 13.4 `LogManager::captureBootSnapshot()` público explícito; chamado na 1ª linha de `begin()`; remover captura oportunista em `setModule`. | BUG-005 |
| 13.4b Guard `_autopsyPerformed` em `performCrashAutopsy()` — roda 1x por sessão; evita falsa `HW WATCHDOG` em chamadas subsequentes de `begin()` (`clear log`, web). | BUG-005 (corolário descoberto em teste HW) |

**Saída esperada:** `v3.21.0`.

---

### 🟢 FASE 14 — Inconsistências + docs + consistência de tempo

**Escopo:** CON-001..006, DOC-002, DOC-003, REF-004, BUG-001 (migração mecânica opcional).

| Tarefa | Resolve |
|---|---|
| 14.1 Consolidar comentário autoritativo sobre `scratch[0..7]` em `LogManager.cpp:450`; remover duplicata em `:554`. | CON-001 |
| 14.2 Completar `enum LanguageCode` (LANG_EN..LANG_ZH + `LANG_COUNT`); `static_assert` contra `LANG_NAMES`. | CON-002 |
| 14.3 Grep `"3 idiomas\|3 languages\|EN/PT/ES"` e atualizar comentários. | CON-003 |
| 14.4 `_lastSavedCrc` → membro privado de `StorageManager`. | CON-004 |
| 14.5 `CliDemand.strVal1/2` e `LoginState.nonce` → `char[]` fixo; atualizar parser/handlers com `safeCopy`. | CON-005 |
| 14.6 `DS18B20_CONVERSION_TIME_MS` e `DHT22_READ_TIMEOUT_MS` em `SystemDefs.h`. | CON-006 |
| 14.7 Nomear magic numbers (`BOOT_DOTS_INTERVAL_MS=800`, `ALARM_ROTATE_INTERVAL_MS=3000`, etc.). | DOC-002 |
| 14.8 Criar `SECURITY.md` na raiz (threat model, rotação, incidente, factory reset, auditoria). | DOC-003 |
| 14.9 Classe `TouchPriority` com `setProvider`/`isActive` singleton; remover `setTouchPriorityChecker` de 5 managers. | REF-004 |
| 14.10 *(Opcional)* Migração mecânica de 26 `millis() - X > Y` para `timeReached()`. Substituição automática via script + revisão. | BUG-001 |

**Saída esperada:** `v3.22.0`.

---

### 🟢 FASE 15 — Hash migration (risco alto — migra auth)

**Escopo:** SEC-006..009. **Requer migração transparente**: schema bump + compatibilidade com hashes antigos.

| Tarefa | Resolve |
|---|---|
| 15.1 LRU evict preserva `failCount`/`lockoutUntil` se lockout ativo não-expirado; só sobrescreve `ip`. | SEC-006 |
| 15.2 `hashPassword` emite 32 hex chars (128 bits); `handleApiLogin` detecta `stored.length()==30`, valida com truncate e re-hash silencioso. | SEC-007 |
| 15.3 `PASSWORD_HMAC_ROUNDS=5000` (ou benchmark para definir), documentar o trade-off. | SEC-008 |
| 15.4 `UserAccount.salt[8]` random via `hwrand32`; bump `CONFIG_VERSION` com rotina de migração; reset admin regera salt. | SEC-009 |

**Validação:** logar com senha antiga → hash migrado para 32 chars; 2 SIMUTs com mesma senha geram hashes diferentes; reset admin via CLI regera salt. Login <1s no Pico W.

**Saída esperada:** `v3.23.0` (bump CONFIG_VERSION).

---

### 🟢 FASE 16 — Performance + String em hot paths

**Escopo:** PER-001..003, MEM-001, MEM-002.

| Tarefa | Resolve |
|---|---|
| 16.1 Helper `feedWdt()` em `SystemDefs.h` (`watchdog_update()+TRACE_BEAT(0)`); substituir as 14+ ocorrências em Core 0. | PER-001 |
| 16.2 Buffer `_uploadBatchBuf[8192]` em `WebManager`; `RenderGuard` apenas no flush de 8 KB; flush final em `UPLOAD_FILE_END`. | PER-002 |
| 16.3 Fast-path em `isValidHistoryFileName` via `length()==12 && endsWith(EXT)`. | PER-003 |
| 16.4 `getHistoryFileNameC()` com buffer de membro `_historyFnBuf[40]`; `NetworkManager::getIpAddress(char*, size_t)`; `getMacAddress(char*, size_t)`; ajustar call-sites. | MEM-001 |
| 16.5 `CliDemand.strVal1/2` → `char[64]` (coberto parcialmente por 14.5 se ainda pendente). | MEM-002 |

**Validação:** upload 100 KB causa ≤15 pauses Core 1; `largestBlock` após 24h ≥70% do total; heap estável após 100 comandos CLI.

**Saída esperada:** `v3.24.0`.

---

### 🟢 FASE 17 — Refatorações grandes (file split)

**Escopo:** REF-001..003, REF-007, MEM-003. **Risco alto — refatoração organizacional pura**.

| Tarefa | Resolve |
|---|---|
| 17.1 Split `DisplayManager.cpp` em 9 arquivos (`_Dashboard/_Graph/_Settings/_Auth/_Calibration/_Alarm/_i18n/_Calendar/.cpp`); core ≤1500 linhas. | REF-001 |
| 17.2 Split `AppManager.cpp` em 8 arquivos (`_Boot/_Commands/_Graph/_Events/_Sensors/_History/_Alarm/.cpp`); core ≤800. | REF-002 |
| 17.3 Split `WebManager.cpp` em 8 arquivos (`_Auth/_Files/_Api/_Pages/_History/_Commit/_Util/.cpp`); nenhum >800. | REF-003 |
| 17.4 Decompor `handleApiLogin` em `findLoginStateForIp`, `checkLockout`, `validateNonce`, `verifyPasswordFor`, `allocSessionSlot`, `completeLogin`. | REF-007 |
| 17.5 Auditar usos de `WebUI::` vs `WebUI_GZ::`; remover raw se viável (~333 KB); ou manter só para páginas sem gzip. | MEM-003 |

**Validação:** build incremental <50% do full-build; navegação completa do site testada; binário `.uf2` reduzido em ≥200 KB (17.5).

**Saída esperada:** `v4.0.0` (major bump por mudança estrutural).

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
| **F11 — Touch Priority (U17/U18/U19)** | ✅ Concluída | `stability-fixes-tier1` | `v3.14.0` | 2026-04-19 |
| **F12 — SEC Críticas/Altas (audit v3.19.0)** | ✅ Concluída | `stability-fixes-tier1` | `v3.20.0` | 2026-04-20 |
| **F13 — Bugs latentes (BUG-002..005)** | ✅ Concluída | `stability-fixes-tier1` | `v3.21.0` | 2026-04-21 |
|   · F13.1 BUG-005 | ✅ Concluída (HW validada) | `ea799f5` | — | 2026-04-21 |
|   · F13.2 BUG-004 | ✅ Concluída (HW validada) | `04b5515` | — | 2026-04-21 |
|   · F13.3 BUG-002 | ✅ Concluída (HW validada) | `1dee6ca` | — | 2026-04-21 |
|   · F13.4 BUG-003 | ✅ Concluída (HW validada; teste 3 rolagem diária 23:59→00:00 validado em v3.22.4 via manual time) | `b7161d2` | — | 2026-04-21 |
|   · F13.4b revert template→macro (flash economy) | ✅ HW validada | em v3.22.0 | — | 2026-04-21 |
| **F-I18N-TRIM.1 (feature fora da auditoria)** | ✅ Concluída (HW validada) | em v3.22.0 | `v3.22.0` | 2026-04-21 |
| **F-NET-TIME (feature fora da auditoria)** | ✅ Concluída | `stability-fixes-tier1` | `v3.23.0` | 2026-04-21 |
|   · F-NET-TIME.1 + 1b — overlay NetworkTimeData + CLI `conf system factory` | ✅ HW | `6e239f1` | — | 2026-04-21 |
|   · F-NET-TIME.2 — consumer NetworkManager (flags DNS/NTP) | ✅ HW | `8a85796` | — | 2026-04-21 |
|   · F-NET-TIME.3a — back-end web (GET/POST + /api/set_time + setManualTime) | ✅ HW | `436248f` | — | 2026-04-21 |
|   · F-NET-TIME.3b — front-end /network (DNS separado) + /config (data/hora) + i18n PT | ✅ HW | `ed9b21e` | — | 2026-04-21 |
|   · F-NET-TIME.4 — CLI (`conf ntp`, `conf time`, `conf net dns`) + tokenizer 5→6 slots | ✅ HW | `cc39e06` | — | 2026-04-21 |
|   · F-NET-TIME.5a — cursor-no-futuro auto-reset + hint t_int=0 na UI | ✅ HW | `95ea5e4` | — | 2026-04-21 |
|   · F-NET-TIME.5b — fechamento (regressão acumulada ao longo do path) | ✅ | em v3.23.0 | — | 2026-04-21 |
| **F-IP-FIX (fora da auditoria)** | ✅ HW validada | `0860d21` | — | 2026-04-21 |
|   Ordem de args em `WiFi.config` corrigida para padrão arduino-pico (ip, dns, gw, mask). Bug pré-existente desde v3.4.8 que impedia modo IP static de associar. | — | — | — | — |
| **F14 (auditoria)** | ✅ Concluída | `stability-fixes-tier1` | v3.24.0 | 2026-04-21 |
|   · WEB-001 — escape JSON em `/api/ls` (filename+dirname) + auto-test shell | ✅ HW validada via `tools/test_web001.sh` | `1826a85` | — | 2026-04-21 |
|   · REF-004 — `TouchPriority` singleton (remove 3 setters + 3 membros + 3 lambdas duplicadas) | ✅ HW validada (503 pós-touch em commit_all) | `b8b9314` | — | 2026-04-21 |
|   · CON-002 — `LanguageCode` enum + `LANG_COUNT` + `static_assert` | ✅ HW validada | `62bfa0c` | — | 2026-04-21 |
|   · CON-004 — `_lastSavedCrc` → membro privado de `StorageManager` | ✅ HW validada | `8537feb` | — | 2026-04-21 |
|   · CON-006 — `DS18B20_CONVERSION_TIME_MS` + `DHT22_READ_TIMEOUT_MS` em `SystemDefs.h` | ✅ HW validada | `9690a90` | — | 2026-04-21 |
|   · CON-005a — `LoginState.nonce` → `char[65]` | ✅ HW validada | `40795d2` | — | 2026-04-21 |
|   · CON-005b — `CliDemand.strVal1/2` → `char[64]` | ✅ HW validada | `2e98a3e` | — | 2026-04-21 |
|   · DOC-003 — `SECURITY.md` raiz (threat model + defesas + operações) | ✅ Doc puro | `26ac277` | — | 2026-04-21 |
|   · CON-001 — bloco autoritativo `SCRATCH REGISTER MAP` em `LogManager.cpp` + dedup comentários | ✅ Doc/comment | em v3.23.10 | — | 2026-04-21 |
|   · CON-003 — `DisplayManager.{h,cpp}` headers "8 languages" → "2 languages (EN + PT)" | ✅ Doc/comment | em v3.23.10 | — | 2026-04-21 |
|   · DOC-002 — magic numbers `800/3000/600/5000` nomeados em `SystemDefs.h` + unificação DHT22 (100→150ms) | ✅ HW validada | em v3.23.11 | — | 2026-04-21 |
|   · BUG-001 — `timeSince()` helper + 45 sites migrados (unsigned / signed-cast / negated) | ✅ HW validada | em v3.23.12 | — | 2026-04-21 |

**Débitos técnicos descobertos em CON-005b (pré-existentes, fora de escopo):**
- **F-LOCKOUT-STUCK** — primeiro `write memory` com mudança real após longo idle pode disparar `[DSP] Lockout stuck >10s, restarting Core 1` (2×). Saves subsequentes OK. Não afeta integridade (config grava corretamente). Provável fragmentação de heap pós-telemetria ou GC do LittleFS. Investigar em ciclo futuro.
- **Tokenizer não strip aspas** — `conf system ssid "X"` salva com aspas literais. Também `isValidName` rejeita hífen em nome. Polish de UX para futuro.
| **F14 — Inconsistências + docs + CON/DOC** | ✅ Concluída | `stability-fixes-tier1` | v3.24.0 | 2026-04-21 |
| **F15 — Hash migration (SEC-006..009)** | 🟡 Em andamento | `stability-fixes-tier1` | (v3.25.0) | — |
|   · F15.1 — SEC-006: LRU evict pula slots com lockout ativo | ✅ HW validada via `tools/test_f15_1_sec006.sh` | em v3.24.3 | — | 2026-04-21 |
|   · F15.2.a — schema bump v14→v15: UserAccount +salt+hashVersion + fix parser JSON `\"` em /api/commit_all (payload builder não salvava) | ✅ HW validada (parser fix confirmado via payload builder salvo com escapes; telemetria 200) | `816eb4d` | — | 2026-04-21 |

**Débitos técnicos observados durante teste HW de F15.2.a (v3.24.4, 2026-04-21):**
- **F-LOCKOUT-STUCK exacerbado**: 3× "Lockout stuck >10s" consecutivos durante `/api/commit_all` pós-migração. Confirma o débito técnico de F14 mas em severidade maior no cenário "first save após migração v14→v15". Investigar em ciclo futuro (provável F16 ou dedicado).
- **Factory defaults auto-triggered**: em algum reboot após migração inicial, `loadConfiguration` falhou em ambos `FILE_CONFIG` e `FILE_BACKUP`, caindo em `loadDefaults`. Causa provável: save anterior interrompido por WDT no meio do atomic rename (`FILE_CONFIG` → `FILE_BACKUP`). Não reproduzível em steady state após device estabilizar.
| **F16 — Performance + String hot paths** | ⚪ Pendente | — | (v3.25.0) | — |
| **F17 — File split (refatoração grande)** | ⚪ Pendente | — | (v4.0.0) | — |

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
| U3 | 🔴 | F5 | ✅ | **Concluído (2026-04-19):** 5.3 — `formatLineCustomBuf` char-buffer single-pass elimina ~20 `String::replace()` por registro; `buildPayload` Custom branch emite linhas via `concat(buf, len)`, sem intermediário. Preview JS (`_previewCustomLine` + `_previewGlobal`) reescrito como espelho byte-fiel; `/api/config` expõe `serial` + `sensors[].{hwid,active}`. CLI `tel dump` (one-shot via `LogManager::writeConsole`, USB+BT chunked). 5.2 — auditoria de 33 handlers web: todos já em streaming+stack (`char buf[]`+snprintf+safeSend); `handleApiLs` migrado de `String json; json.reserve(2048)` para `safeSend`/`char buf[256]`; `handleApiHistoryData`/`handleApiLogs` já estavam em streaming. 5.4 — maxlength validation in place. 5.5 — `heapLargestBlock` via binary-search malloc probe (MetricsManager::sampleLargestBlock, ~16 malloc/free por call); exposto em `/api/status` (`heap_lb`) e `show metrics` (com HWM de mínimo). |
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
| U17 | 🟠 | F10 | ✅ | **Post-audit (2026-04-19):** auditoria de notificações da web UI após Fase 3 de Touch Priority (503 response). Bugs: (a) `fetchSafe` lançava `throw` para todo status ≥500, impedindo o branch `r.status === 503` em 7 cópias espalhadas pelas páginas — fix: `resp.status !== 503` no throw; (b) HIST page tinha chamada a `showToast` em `clearLogs` sem CSS/div/função definidos → `ReferenceError` silencioso; (c) DASH e LICENSE tinham `<div id="net-toast">` sem função `showToast`; (d) silent catches em `saveConfig`/`addUser`/`delUsr`/`rstUsr`/`clearLogs`/`fmMkdir`/`fmDelete` mascaravam erros de rede. Fix: infra de toast em todas as 9 páginas internas (login mantém erro inline pelo timer de lockout); catches agora mostram `net_conn_err`; ações de file/user/log mostram toast de sucesso. i18n PT adicionada para 7 chaves novas. |
| U18 | 🟠 | F10 | ✅ | **Touch Priority Fase 4 (2026-04-19):** comandos CLI (USB+BT) executados durante `isUserInteracting()` competem com touch pelo WDT/heap/flash (ex: `show history`, `conf save`). Fix: ring buffer de 2 `CliDemand` em `AppManager`; `processInput` continua rodando normal (cheap — só lê UART+acumula String), mas quando linha completa chega e touch está ativo, comando é enfileirado em vez de executado. Drain 1-por-loop no topo de `update()` após touch liberar. Overflow (3º+) descarta com mensagem `"CLI busy"` única por rajada. Heap: ~+200 B. Zero regressão em estado normal. |
| U19 | 🟡 | F10 | ✅ | **Touch Priority Fase 5 (2026-04-19):** buffers deferidos durante touch (logs, hist record, cursor) ficavam em RAM até a próxima chamada natural — janela de exposição até ~60s se user não interagisse com nada depois. Fix: edge detection em `AppManager::loop()` na transição `isUserInteracting()` true→false dispara `onTouchReleased()` que orquestra flush em série: (1) `LogManager::flushPendingIfAny()` — público novo; (2) `StorageManager::flushPendingHist()` — novo, bypassa touch checker usando `writeHistoryEntryFlash` direto; (3) `StorageManager::flushCursorIfDirty()` — já existente. WdtWindow de 30s cobre os 3 writes. Fecha a janela "dado em RAM não em flash" de minutos para <100ms após touch liberar. |
| U23 | 🔴 | F10 | ✅ | **Instrumentação + timeout no lockout (2026-04-19):** autópsia consistente mostrava `C0=[WEB_SERVER]` mas não distinguia onde exatamente travava. Fix parte 1 (instrumentação): novos módulos `MOD_SAVE_CONFIG`, `MOD_LOG_FLASH`, `MOD_HIST_FLASH`, `MOD_CORE1_LOCK` + `TraceScope` RAII aplicado nos paths críticos. Autópsia seguinte revelou `C0=[CORE1_LOCK]` — Core 0 preso no `multicore_lockout_start_blocking()` esperando Core 1 ackear. Fix parte 2 (robustez): `pauseRendering(true)` troca `start_blocking` (timeout infinito) por loop com `start_timeout_us(500ms)` + `watchdog_update`; a cada 2s limpa state via `end_blocking` idempotente; se 10s sem sucesso, restart Core 1 via `multicore_reset_core1()` + `multicore_launch_core1()`. Elimina HW WATCHDOG em travamentos de lockout — pior caso vira "save lento" em vez de reboot. |
| SEC-001 | 🔴 | F12 | ✅ | **F12.1 (2026-04-20):** helper `isSafeUploadFilename` em `SystemDefs.h` (blocklist `.. \ " : < > \| ? * % control`, len>64, vazio); `_uploadRejected` flag em `WebManager.h` ganha START→WRITE/END no-op + HTTP 400 em `handleUploadComplete`. Script `tools/test_f12_1_sec001.sh` + `hw_test_lib.sh` (challenge-response com SHA256 Latin-1 compat com frontend JS). Validado em HW v3.19.0+patch: 28/28 casos (incl. traversal literal, percent-encoding `%2e%2e%2f`, chars `" < > \ \| ? * :`, controle 0x01, len>64, vazio, regressões, stress heap estável). Log: `SEC_UNAUTHORIZED Upload rejeitado: filename invalido '<nome>'`. |
| SEC-002 | 🟠 | F12 | ✅ | **F12.2 (2026-04-20):** `handleUploadData` rejeita `uploadDir` contendo `..` OU `%` em vez do antigo `replace("..","")` não-recursivo (bypass com `"...."` ou `%2e%2e`). `_uploadRejected=true` → HTTP 400 + `SEC_UNAUTHORIZED`. Script `tools/test_f12_2_sec002.sh` cobre `/history/..`, `....`, `%2e%2e/config`, paths compostos, + regressões para `/`, `/history`, `/history/`. |
| SEC-003 | 🟠 | F12 | ✅ | **F12.3 (2026-04-20, Variante B):** `loadDefaults` gera senha admin aleatória 8 chars `[A-Z2-9]` (exclui `O/0/I/1`) via `rp2040.hwrand32()`, hash salvo com mesma lógica `hashPassword(u, SHA256(plain))` que frontend. Plaintext em RAM (`_initialAdminPassword[9]`) — nunca persistido em flash; zerado quando `mustChangePassword` flip false OU `loadConfiguration` carrega config válida do flash. Anúncio via Serial USB com banner `SEC-003: FACTORY DEFAULTS ATIVADO` + LOG_CODE audit trail. CLI `conf system admin reset` também migrado para gerar random (era `simut` hardcoded). Viewer mantido como decisão do audit (mustChangePassword forçado, perms mínimas). Script `tools/test_f12_3_sec003.sh` valida: admin/admin rejeitado, login real funciona, viewer cai em /force_chpass. Teste destrutivo factory reset é manual. |
| SEC-004 | 🟠 | F12 | ✅ | **F12.4 (2026-04-20):** `SetupFlagsData` overlay em `reserved[26..27]` (magic=0xBE, `FLAG_MUST_CHANGE_PIN`). `StorageManager::mustChangePin/clear/set`. `loadDefaults` seta flag; `AppManager::EVT_AUTH_SUCCESS` redireciona para `showSettingsPassword()` em vez de main se flag ativa; `EVT_SAVE_PASSWORD` limpa flag só se novo PIN != "1234". Configs legadas sem magic retornam `mustChangePin=false` (compat com upgrade). Teste manual HW-only (touch UI). |
| SEC-005 | 🟠 | F12 | ✅ | **F12.5 (2026-04-20):** `CLI_LINE_MAX=256` em `SystemDefs.h`; helper `CommandManager::appendCharWithLimit` com anti-spam (1 warning por rajada de overflow, flag resetada ao receber `\n`). Aplicado em `_usbBuffer` (USB) e `_btBuffer` (BT pós-auth). Sem guard, `yes \| cat > /dev/ttyACM0` reallocaria `String` até OOM. Log `CLI_UNKNOWN_CMD Linha > 256 descartada em USB\|BT`. Script `tools/test_f12_5_sec005.sh` envia 1KB + 10KB sem `\n`, valida device responsivo + heap estável + comando válido pós-overflow + log em `/api/logs`. |
| SEC-006 | 🟡 | F15 | ✅ | LRU evict pula slots com lockout ativo + 429 se todos trancados (v3.24.3). |
| SEC-007 | 🟢 | F15 | ⚪ | Hash 120→128 bits com migração transparente. |
| SEC-008 | 🟢 | F15 | ⚪ | `PASSWORD_HMAC_ROUNDS` 2500→5000. |
| SEC-009 | 🟢 | F15 | ⚪ | Salt random por usuário (schema bump). |
| BUG-001 | 🟢 | F14 | ✅ | `timeSince(start, duration)` helper em SystemDefs.h; 45 sites migrados em 8 arquivos (v3.23.12). |
| BUG-002 | 🟡 | F13 | ✅ | Wrappers `requestPreviewSound/requestVolumePreview/requestAlarmVolumePreview` + `__dmb()` nos 3 pares cross-core Core 1 → Core 0. Barrier no producer (`setTelemetrySendStatus`) e readers (`render`/`drawTopBar`) do pack `_pktArrowState` Core 0 → Core 1. `_touchSoundPending`/`_errorSoundPending` fora do escopo (single-flag sem dado emparelhado). + UX fix (F13.3b): touch gates separados para volume no menu Sons. Validado HW. |
| BUG-003 | 🟡 | F13 | ✅ | Template `StorageManager::flashOp<F>()` (private no header) substitui macro local `FLASH_OP` de `saveConfiguration`. `writeHistoryEntryFlash` refatorado em chunks granulares: enforceStorageLimit (se rolagem), open+write+close, fallback enforce+open+write+close — cada um em seu próprio lockout. File handle nunca sobrevive entre chunks. HW validado (testes 1/2/4/5; teste 3 rolagem diária pendente até feature manual time). |
| BUG-004 | 🟢 | F13 | ✅ | Membro `_lastWebBusy` sticky (Core 1 only) em `DisplayManager`. Consumers em `loopCore1` e `handleTouch` atualizam o sticky quando `mutex_try_enter` sucede; usam o sticky como fallback quando falha. Validado HW. |
| BUG-005 | 🟢 | F13 | 🟡 | `captureBootSnapshot()` público + chamada explícita em `begin()`; `setModule` não captura mais oportunisticamente; assertion defensiva + guard `_autopsyPerformed` em `performCrashAutopsy` (fix de falsa autópsia em `clear log`). HW pendente. |
| CON-001 | 🟢 | F14 | ✅ | Bloco `SCRATCH REGISTER MAP` em `LogManager.cpp` + dedup (v3.23.10). |
| CON-002 | 🟢 | F14 | ✅ | `enum LanguageCode` EN+PT + `LANG_COUNT` sentinela (v3.23.4). |
| CON-003 | ⚪ | F14 | ✅ | `DisplayManager.{h,cpp}` "8 languages" → "2 (EN + PT)" (v3.23.10). |
| CON-004 | 🟢 | F14 | ⚪ | `_lastSavedCrc` → membro de classe. |
| CON-005 | 🟢 | F14 | ⚪ | `String` → `char[]` em `CliDemand`/`LoginState`. |
| CON-006 | ⚪ | F14 | ⚪ | `DS_CONVERSION_TIME` → `SystemDefs.h`. |
| MEM-001 | 🟡 | F16 | ⚪ | `String` em hot paths → buffer estático. |
| MEM-002 | 🟡 | F14/F16 | ⚪ | Coberto por CON-005. |
| MEM-003 | ⚪ | F17 | ⚪ | Avaliar remoção de `WebUI.h` raw. |
| PER-001 | 🟢 | F16 | ⚪ | Helper `feedWdt()` consolidando `watchdog_update+TRACE_BEAT(0)`. |
| PER-002 | 🟢 | F16 | ⚪ | Upload batching 8 KB para reduzir pauses Core 1. |
| PER-003 | ⚪ | F16 | ⚪ | Fast-path em `isValidHistoryFileName`. |
| REF-001 | 🟡 | F17 | ⚪ | Split `DisplayManager.cpp` em 9 arquivos. |
| REF-002 | 🟡 | F17 | ⚪ | Split `AppManager.cpp` em 8 arquivos. |
| REF-003 | 🟢 | F17 | ⚪ | Split `WebManager.cpp` em 8 arquivos. |
| REF-004 | 🟢 | F14 | ⚪ | Singleton `TouchPriority`. |
| REF-007 | 🟢 | F17 | ⚪ | Decompor `handleApiLogin` em 6 helpers. |
| DOC-002 | 🟢 | F14 | ✅ | `BOOT_WAIT_DOT/ALARM_ROTATE/ALARM_FLASH/WEB_NOTIFY` nomeados + DHT22 unificado (v3.23.11). |
| DOC-003 | ⚪ | F14 | ⚪ | Criar `SECURITY.md` na raiz. |
| WEB-001 | 🟢 | F14 | ⚪ | **Post-audit F12.1 (2026-04-20):** `handleApiLs` emite JSON sem escapar bytes de controle (0x00-0x1F/0x7F) nos `name` — 1 arquivo com byte ruim quebra todo o listing (observado com `/x␁y.txt` criado por teste pré-patch). F12.1 impede upload via HTTP, mas não cobre entrada por outros canais. Fix: `jsonEscape()` ou skip de entries com chars inválidos no `handleApiLs`. |
| U24 | 🔴 | F11 | ✅ | **Commit-all + reboot pattern (2026-04-19):** rajadas de saves consecutivos eram a fonte original de todos os bugs de concorrência (U16/U21/U23). Mudança arquitetural do modelo UX: interface web acumula mudanças no `sessionStorage` client-side; botão único "Salvar e Reiniciar" no topbar (só aparece se há pendentes). Ao clicar: confirmação com aviso de risco → POST `/api/commit_all` com JSON → server aplica tudo em 1 save → reboot limpo. **Phase A.1** (v3.15.0): migrado `/config`. **Phase A.2** (v3.16.0): migrado `/alarms`; `handleApiSaveAlarms` e `handleSaveSystem` grande removidos. **Phase B** (v3.17.0): migrado `/users` como queue de ações (`add`/`del`/`reset`) com overlay visual + botão ↶ de desfazer; `handleApiUserAdd/Del/Reset` removidos. **Phase C** (v3.18.0): migrado `/network` (ssid, pass, dhcp, ip, mask, gw, dns, ntp_server, web_port); detecção de mudança de porta → redirect automático pro novo host:porta após reboot; `handleSaveNetwork` removido. **Phase D** (v3.19.0): `Pending` + `commitAll` + CSS + botão injeção centralizados em `/lang.js` (removidas ~8KB de código duplicado); botão "Salvar e Reiniciar" agora aparece em TODAS as páginas (dash/hist/file/license/cfg/alarms/users/net); versão do firmware exibida ao lado de "SIMUT" (endpoint `/api/perms` extendido com `version`); botão de toggle tema claro/escuro (`#theme-toggle`) com preferência em `localStorage`; paleta de tema claro refinada (slate + cyan-700 AA-contrast) com overrides cobrindo topbar/drawer/cards/inputs/tabelas/chart/badges/calendar/sounds. Todas as 4 páginas de configuração agora compartilham o mesmo padrão; economia total de ~18KB de flash. |
