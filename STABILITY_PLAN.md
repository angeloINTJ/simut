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
| **U25** | 🔴 | `BluetoothManager.cpp:95,114` | `LOG_CODE` dentro de `BluetoothManager::update()` dispara `writeCompactToFlash` síncrono durante o login BT. O path completo envolve: (1) `_btMgr.update()` → validação HMAC-SHA256 → `LOG_CODE` → (2) `flushPendingLogs()` com lockout do Core 1 + flash ops → (3) segundo lockout em `writeCompactToFlash` para o registro da auth. Cada lockout faz `pauseRendering(true)` que espera até 10s pelo ACK do Core 1 antes de hard-reset. Sob LittleFS fragmentado (>70%), o GC interno no `open("a")` amplifica a latência. O banner de boas-vindas também era impresso DEPOIS do `LOG_CODE`, deixando o usuário sem feedback durante o bloqueio. | Travamento + possível WDT reset durante login BT. |

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

### 2.5 Revisão técnica externa cooperativa (v1, 2026-04-22)

Achados de uma revisão externa documentada em `audits/SIMUT_ANALISE_TECNICA_v1.md` (Claude
externo, sob brief de revisor cooperativo). IDs com prefixo `EXT-` para rastreabilidade.

> **Nota sobre invalidações**: o revisor recebeu zip incompleto. Achados §4.2
> (`WebUI_GZ.h`/`compressor.py` ausentes) e §6.3 (README ausente) **são inválidos** — todos
> os artefatos existem em `/`, `/tools/`, e `/README.md`. Esses dois itens foram descartados
> e não geram entrada `EXT-`.

| ID | Sev | Local | Problema | Origem |
|----|----|---|---|---|
| **EXT-001** | 🟡 | repo root | Sem `platformio.ini`/`arduino-cli.yaml`/`CMakeLists.txt`. Build hoje é Arduino IDE only (cache em `build/rp2040.rp2040.rpipicow`). Versões de libs externas (`OneWirePIO_RP2040`, `DS18B20PIO`, `DHT22PIO`, `BuzzerPIO_RP2040`) não pinned. Sem flags `-Wall -Wextra` configuráveis. | §4.1 + §4.4 |
| **EXT-002** | 🟠 | múltiplos | Comando `CMD_DBG_SENSOR_HISTORY_ALL` (`SystemDefs.h:1085`, `CommandManager.cpp:392`, `AppManager.cpp:1193`) marcado `TEST-ONLY — REMOVE BEFORE PRODUCTION` foi enviado em release. Permite zerar `provisionEpoch` de todos os sensores via CLI USB/BT. | §4.3 |
| **EXT-003** | 🟡 | `SystemDefs.h` (1342 L) | Catch-all com macros, enums, structs, helpers, parser CLI, validators inline. Edição em qualquer seção invalida cache de build do projeto inteiro. | §5.1 |
| **EXT-004** | 🟡 | `WebUI.h` | 72 ocorrências de `@LANG_BEGIN:<es\|de\|fr\|it\|ru\|zh>` para idiomas removidos do firmware em F-I18N-TRIM.1 (`v3.22.0`). Cada edição de string da UI obriga editar 8 blocos repetidos. | §5.3 |
| **EXT-005** | 🟢 | `AppManager.h:16-26` | Header inclui 10 managers (`SensorManager`, `StorageManager`, `CommandManager`, `DisplayManager`, etc.) por valor → edição em qualquer `<Manager>.h` recompila TU inteira. | §5.4 |
| **EXT-006** | 🟢 | `WebManager.cpp:2530` | `LogManager::instance().begin(true, LOG_DEBUG)` chamado em `handleApiClearLogs` (não-boot). Funcional via guard `_autopsyPerformed`, mas acoplamento estranho. Propor `LogManager::resetAfterExternalWipe(bool)`. | §5.6 |
| **EXT-007** | 🟢 | `SystemUtils.cpp:41-48` | Dois docblocks empilhados em `isValidHistoryFileName`. O primeiro cita `.csv` (formato abandonado em v3.11+; histórico hoje é `.bin`). Doxygen pega o primeiro. | §5.7 |
| **EXT-008** | 🟢 | `docs/` ausente | Tags de comentário no código (`F-LOCKOUT-STUCK`, `BUG-002`, `CON-005a/b`, `Patch C`, `Fase 4/5`, `#4/5/7/8/11`, etc.) não têm dicionário in-tree. Revisores externos ficam sem decoder. | §6.2 |
| **EXT-009** | 🟢 | sem `test/` | Helpers puros (`parseIntStrict`, `isValidIpv4`, `isSafeUploadFilename`, `dallasCrc8`, `floatToI16`/`i16ToFloat`, `timeReached`) podem rodar host-side via `pio test -e native` + Unity. Hoje só há scripts HW em `tools/test_*`. **Depende de EXT-001.** | §6.4 |
| **EXT-010** | 🟢 | `AppManager.cpp` (8+ sites) | `enterFlashReadLock()` / `exitFlashReadLock()` não-RAII em pares manuais (linhas 782/786, 2289/2292, 2301/2303, 2309/2320, 2352/2354, 2693/2696, 2748/2750, 2766/...). `ReadGuard` RAII existe em `WebManager.h:153` mas não está exposto publicamente. | §6.6 |
| **EXT-011** | 🟢 | múltiplos | Polish: (a) `AppManager.cpp:711-713` tem `watchdog_update();` duas vezes seguidas; (b) `DisplayManager.cpp:27` envolve `pico/multicore.h` em `extern "C"` — provavelmente redundante no SDK Pico atual; (c) `TelemetryManager::releaseIdleResources()` é no-op intencional, mas o nome esconde a decisão. | §7 |
| **EXT-012** | 🟢 | `README.md:24` | Cita "8 display languages (English, Portuguese, Spanish, French, German, Italian, Russian, Chinese)" — stale após F-I18N-TRIM.1 (`v3.22.0` reduziu para EN+PT). Achado descoberto durante validação da revisão (não está no documento original). | bonus |

> **Achados opinativos descartados**: §4.4 (warnings) coberto por EXT-001; §5.5 (`String` count)
> coberto por F16/MEM-001; §6.1 (idioma de logs) sem ganho mensurável; §6.5 (macro
> `WDT_FEED_BARRIER()`) coberto por F16/PER-001 (`feedWdt()`); §7 itens sobre `MAX_USERS`
> comment, `handleApiScreenshot` pause — cosméticos sem evidência de problema real.

> **Adoções não-finding**: padrão de PR (§10) e protocolo `symbols_inventory_before/after`
> (§11) viram *guideline obrigatório* para fases F17 (split de arquivos grandes). Glossário
> (§3) é a base de conteúdo para EXT-008.

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

**Saída:** `v3.24.18` (CONFIG_VERSION mantido em 15 — schema já criado em F15.2.a).

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
| **F3 — Handlers & Concorrência** | ✅ Concluída | `stability-fixes-tier1` | `v3.5.2` | 2026-04-18 |
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
| **F15 — Hash migration (SEC-006..009)** | ✅ Concluída | `stability-fixes-tier1` | v3.24.18 | 2026-04-25 |
|   · F15.1 — SEC-006: LRU evict pula slots com lockout ativo | ✅ HW validada via `tools/test_f15_1_sec006.sh` | em v3.24.3 | — | 2026-04-21 |
|   · F15.2.a — schema bump v14→v15: UserAccount +salt+hashVersion + fix parser JSON `\"` em /api/commit_all (payload builder não salvava) | ✅ HW validada (parser fix confirmado via payload builder salvo com escapes; telemetria 200) | `816eb4d` | — | 2026-04-21 |
|   · F15.2.b — SEC-007 hash 120→128 bits, SEC-008 PASSWORD_HMAC_ROUNDS=5000, SEC-009 salt random por usuário: `hashPasswordCore()` parameterizado + `hashPasswordLegacy()`/`hashPasswordV1()` wrappers + `generateSalt()` via hwrand32. Migração transparente no `handleApiLogin` (detecta stored.length()==30, valida com legacy, re-hash com salt random). BT validator, CLI handlers, force-chpass e factory defaults todos usam formato v1. | ✅ Implementado | — | — | 2026-04-25 |

**Débitos técnicos observados durante teste HW de F15.2.a (v3.24.4, 2026-04-21):**
- **F-LOCKOUT-STUCK exacerbado**: 3× "Lockout stuck >10s" consecutivos durante `/api/commit_all` pós-migração. Confirma o débito técnico de F14 mas em severidade maior no cenário "first save após migração v14→v15". **Tratado em v3.24.5 via quiet mode cooperativo** (ver abaixo).
- **Factory defaults auto-triggered**: em algum reboot após migração inicial, `loadConfiguration` falhou em ambos `FILE_CONFIG` e `FILE_BACKUP`, caindo em `loadDefaults`. Causa provável: save anterior interrompido por WDT no meio do atomic rename (`FILE_CONFIG` → `FILE_BACKUP`). Não reproduzível em steady state após device estabilizar.

|   · F-LOCKOUT-STUCK fix (v3.24.5) — quiet mode cooperativo em `saveConfiguration`: Core 1 congela em loop RAM-only (IRQs off) via handshake `_quietModeRequested`/`_quietModeActive`, Core 0 faz todas as flash ops sem `multicore_lockout` IRQ-based a cada chunk. Elimina cascata de stucks. | ✅ HW validada (commit_all web) | `9a55ef2` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.6) — quiet mode re-entrant (refcount); `CMD_WRITE_MEMORY` e `sensor accept` agora wrappam save + `loadAndCalibrateSensors` no mesmo quiet mode (elimina stuck residual no log `APP_SENSORS_CALIBRATED`). | 🟡 Parcial (stuck residual no 2º save consecutivo) | `b57c6af` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.7) — timeout do `requestQuietMode` 5s→15s; log warning em timeout. | 🟡 Parcial (timeout ainda disparava em 2º save consecutivo) | `4259483` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.8) — removido `_forceFullRedraw=true` do post-quiet; Core 1 ainda ficava unresponsive >15s no 2º save. | 🔴 Não resolveu | `dab353f` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.9) — **PIVOT**: abordagem cooperativa descartada. `requestQuietMode` agora faz **HARD-RESET** do Core 1 via `multicore_reset_core1`. | 🟡 Parcial (flash branco entre reset e re-init do ILI9341) | `d116445` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.10) — `_tftFirstInit` flag: `_tft->begin()` (que faz HW reset do ILI9341 e causa flash branco) só roda na 1ª launch do Core 1. | 🟡 Parcial (touch quebrou pós-save) | `37c3c9d` | — | 2026-04-21 |
|   · F-LOCKOUT-STUCK fix (v3.24.11) — `_ts->begin()` (attach IRQ do touch na NVIC de Core 1) restaurado em TODA launch; `_tft->begin()` continua only-on-first. Touch volta a responder pós-save. | ✅ HW validada (save sem stucks, touch OK, display sem flash branco) | `295f564` | — | 2026-04-22 |
|   · TEST-ONLY: `conf sensor <N> history all` — comando oculto para recuperar visualização de histórico pós factory reset (zera `provisionEpoch`). **REMOVER ANTES DE PRODUÇÃO** (marcadores `TEST-ONLY` em SystemDefs.h, CommandManager.cpp, AppManager.cpp). | ✅ HW validada | `8806273` | — | 2026-04-22 |
| **F-BT-LOGIN — Defer flash no login Bluetooth (U25)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-04-25 |
|   · U25 — `LogManager::setForceBuffer(true/false)` wrappando `_btMgr.update()` em `CommandManager::processInput`. Todos os `LOG_CODE` durante o update BT vão para buffer RAM `_pendingLogs[]` (32 slots) em vez de disparar flash síncrono. Banner de boas-vindas reordenado antes do `LOG_CODE` para resposta imediata. Sem alocações novas de heap. | ✅ Implementado (HW pendente) | — | — | 2026-04-25 |
| **F16 — Performance + String hot paths** | ✅ Concluída | `stability-fixes-tier1` | v3.24.16 | 2026-04-25 |
|   · PER-001 feedWdt(), PER-002 upload batching 8KB, PER-003 fast-path, MEM-001 buffer IP/mac/hist, EXT-006 resetAfterExternalWipe, EXT-010 ReadGuard público. | ✅ | — | — | — |
| **F17 — File split (refatoração grande)** | ✅ Concluída | `stability-fixes-tier1` | `v3.29.0` | 2026-05-02 |
|   · F17 etapa 1 — REF-003: split `WebManager.cpp` em 8 arquivos (v3.25.0, commit `61c221b`). | ✅ HW validada | — | `v3.25.0` | 2026-04-25 |
|   · F17 etapa 2 — REF-002: split `AppManager.cpp` em 8 arquivos (v3.25.1, commit `5972b6b`). | ✅ HW validada | — | `v3.25.1` | 2026-04-25 |
|   · F17 etapa 3 — REF-007: `handleApiLogin` em 7 helpers (v3.25.3, commit `e4d72c4`). | ✅ HW validada | — | `v3.25.3` | 2026-04-25 |
|   · F17 etapa 4 — EXT-003: split `SystemDefs.h` em 7 sub-headers + facade (v3.25.4, commit `ded4c0a`). | ✅ HW validada | — | `v3.25.4` | 2026-04-25 |
|   · F17 etapa 5 — EXT-005: `AppManager.h` forward decl + `unique_ptr` (v3.28.12, commit `e7a78d7`). Root cause de regressão histórica resolvido (`_tft`/`_ts` sem `= nullptr`). | ✅ HW validada | — | `v3.28.12` | 2026-05-02 |
|   · F17 etapa 6 — MEM-003: `WebUI.h` `#error` guard (v3.25.6, commit `ce2becf`). | ✅ HW validada | — | `v3.25.6` | 2026-04-25 |
|   · F17 etapa 8 — REF-001: split `DisplayManager.cpp` em 9 sub-arquivos (v3.25.13, commit `56b1a60`). | ✅ HW validada | — | `v3.25.13` | 2026-04-25 |
|   · Tag `v3.29.0` marca o fechamento da fase. | — | — | `v3.29.0` | 2026-05-02 |
| **F-CLEANUP — Polish puntual de baixo risco** | ✅ Concluída | `stability-fixes-tier1` | v3.24.15 | 2026-04-25 |
|   · EXT-002 — remover `CMD_DBG_SENSOR_HISTORY_ALL` (TEST-ONLY) **antes de qualquer release público**. | ✅ Enum + parser + handler removidos | — | — | — |
|   · EXT-007 — apagar docblock obsoleto em `SystemUtils.cpp:41-44` (referência a `.csv`). | ✅ + `.csv` → `.bin` no header do arquivo | — | — | — |
|   · EXT-011a — remover `watchdog_update()` duplicado em `AppManager.cpp:711-713`. | ✅ | — | — | — |
|   · EXT-011b — validar e remover `extern "C"` em `DisplayManager.cpp:27` (provável paranoia histórica do SDK Pico). | ✅ SDK Pico já inclui guards `extern "C"` | — | — | — |
|   · EXT-011c — renomear/documentar `TelemetryManager::releaseIdleResources()` para refletir decisão consciente de no-op. | ✅ Docblock atualizado no .h | — | — | — |
| **F-I18N-TRIM.2 (feature fora da auditoria)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-04-25 |
|   · EXT-004 — remover blocos `@LANG_BEGIN:es\|de\|fr\|it\|ru\|zh` de `WebUI.h` (consistência com F-I18N-TRIM.1 do firmware). Reduz manutenção de strings da UI; impacto em flash desprezível (gzip já colapsa). | ✅ 336 linhas removidas (72 blocos) | — | — | — |
| **F-DOC-EXT — Documentação externa cooperativa** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-04-25 |
|   · EXT-008 — `docs/GLOSSARY.md` in-tree (tags BUG/SEC/CON/DOC/F-/Patch/#N), reaproveita §3 do `audits/SIMUT_ANALISE_TECNICA_v1.md`. | ✅ 113 tags documentadas | — | — | — |
|   · EXT-012 — atualizar `README.md` ("8 display languages" → "EN + PT" pós F-I18N-TRIM.1). | ✅ + SIMUT.ino + docs/ na estrutura | — | — | — |
| **F-BUILD** | ✅ Concluída | `stability-fixes-tier1` | `v3.30.0` | 2026-05-02 |
|   · EXT-001 — `platformio.ini` reprodutível com lib pins explícitos + `-Wall -Wextra` + pre-build script `build_webui_gz.py`. Estrutura criada na audit cooperativa v1 (2026-04-22, herdada de v3.26.0). Versões pinadas: Adafruit GFX Library@1.12.6, Adafruit ILI9341@1.6.3, **XPT2046_Touchscreen pinado via GitHub URL + SHA d57f64c (v1.4)** porque PIO registry só tem alpha de 2019, knolleary/PubSubClient@2.8, OneWirePIO_RP2040#3f0251e..., DHT22PIO_RP2040#d500e4d..., BuzzerPIO_RP2040#ab8eab1... (SHA 40-char). Build reproduz byte-a-byte. | ✅ | — | `v3.29.1` (start) → `v3.30.0` (XPT2046 fix) | 2026-05-02 |
|   · EXT-009 — host-side unit tests via `pio test -e native` + Unity. **25 test cases / 25 passed em 0.82s.** Cobertura: `isValidIpv4` (4 tests), `isSafeUploadFilename` (4 tests, incl. traversal/percent-encoding/control chars/size), `isValidName` (2), `isValidCfgString` (1), `isInRange` (1), `parseIntStrict` (2), `timeReached`/`timeSince` (3, incl. millis-wrap-safe), `dallasCrc8` (2, incl. ROM checksum self-validation), `floatToI16`/`i16ToFloat` (6, incl. NAN/clamp/roundtrip). Infra: `test/native_stubs/Arduino.h` (String + millis() stub), `test/test_validators/test_main.cpp` (Unity asserts). Refator de `platformio.ini`: settings HW movidas de `[env]` para `[pico_base]` (estendido por release/debug); `[env:native]` standalone. | ✅ | — | `v3.30.0` | 2026-05-02 |
|   · Tag `v3.30.0` marca o fechamento da fase. F-BUILD foi a última pendência genuína do plano de estabilidade — todas as fases F1..F17 + F-BUILD agora ✅. | — | — | `v3.30.0` | 2026-05-02 |
| **F-TEL-V2READER — regressão crítica de telemetria pós-codec V2** | 🟡 HW pendente | `stability-fixes-tier1` | `v3.30.1` | 2026-05-02 |
|   · Diagnóstico: usuário reportou que telemetria não enviava (`metrics.telSent=0`) e cursor `/config/t_cursor.bin` nunca era criado, mesmo com history saving normal a cada minuto. Investigação via Serial CLI + Web API (`/api/config`, `/api/ls`) confirmou: telInterval=10000 OK, t_srv/port/path OK, history files V2 presentes em `/history/`. `tel sync` retornava silencioso, `tel dump` dizia "Sem dados pendentes". `metrics` mostrava 0 envios, 0 falhas, 0 retries — o `update()` nunca chegava a `http.POST`, `collectBatch` retornava vazio. **Root cause:** a migração do codec de histórico V1 (28B raw) → V2 (header SIM2 16B + records delta variáveis com anchor period 60) em **v3.27.0-α5 (commit `0431461`, 2026-04-06)** atualizou todos os readers (`StorageManager::getLastRecorded`, `WebManager::handleApiHistoryData`, `AppManager_Graph`, `AppManager_HistoryAlarm`) **menos** `TelemetryManager::collectBatch` (linhas 391-446) e `TelemetryManager::refreshPendingCount` (linhas 1268-1293). Esses 2 sites continuavam fazendo `f.read((uint8_t*)&rec, HISTORY_RECORD_SIZE=28)` em arquivo V2 → cast em garbage → epoch fail validation `>=1700000000` → batch sempre vazio. Telemetria silenciosamente quebrada **por ~26 dias** sem nenhum log de erro/warn (silêncio de boa-fé). | 🔴 Confirmado | — | — | — |
|   · Fix: ambas funções refatoradas usando `HistoryFileHeaderV2` + `historyCodecReset` + `historyDecodeRecord` (mesmo padrão dos outros readers V2 do projeto). Skip silencioso de arquivos sem header V2 válido (legacy V1 ou corrompidos). Adiciona `#include "HistoryCodec.h"` em `TelemetryManager.cpp`. Preserva toda a lógica adjacente (cursor-no-futuro reset F-NET-TIME.5, fallback de 30 dias quando cursor=0, minFileName por data, periódicos WDT feeds + yield, atomic guards). HW: telemetria voltou a enviar imediatamente, 222 B/batch HTTP 200. | ✅ HW validada | — | `v3.30.1` | 2026-05-02 |
| **F-TIME-GATE — hardening: history só com time ref** | ✅ HW validada | `stability-fixes-tier1` | `v3.30.2` | 2026-05-02 |
|   · Garantia: history files não são criados sem referência de hora válida (NTP atualizado OU provisional ativo). Gate `now > 1600000000` em `processHistoryLogging` já existia, agora com **warn-once log**: `APP_HIST_NO_TIME_REF=512` quando entra no estado, `APP_HIST_TIME_REF_RECOVERED=513` quando volta. Antes era skip silencioso, podia perder horas de records sem aviso. Membro `_histTimeRefWarned` em AppManager.h reseta automaticamente quando time ref retorna. + **Defesa em profundidade**: `StorageManager::writeHistoryEntryFlash` rejeita early `rec.epoch <= 1600000000UL`. Caso futuro caller esqueça o gate, dados ruins (epoch=0 → file `19700101.bin`) não entram. | ✅ HW validada | — | `v3.30.2` | 2026-05-02 |
|   · `tools/stress_test/` toolkit (entregável paralelo): `lib_simut_api.sh` (helpers login/ls/get/put/delete), `generate_history_v2.py` (gera N dias × M records V2), `backup_fs.sh` + `restore_fs.sh` (full FS dump + restore com `--delete-extras`), `run_stress_test.sh` (orchestrator end-to-end). Validado HW com 30 dias × 1440 records: drain 9.37 MB em 340s @ telInterval=300ms (~27 KB/s sustained), 0 falhas em 424 envios consecutivos, CSV export 189 KB validado, restore 9/9 arquivos. README.md com docs de uso. v3.30.4 simplificou phase 3.5 com novo CLI `tel reset` (invalida cache + apaga flash sem reboot). | ✅ Toolkit validado | — | — | 2026-05-02 |
| **F-DISPLAY-ATOMIC — eliminar efeito top-down em renders de tela** | ✅ Fechada (v3.31.0) | `stability-fixes-tier1` | (v3.30.x → v3.31.0) | 2026-05-02 |
|   · Diagnóstico: 403 chamadas diretas `_tft->draw*` em 8 arquivos do DisplayManager, distribuídas em telas de Settings (120), Calibration (89), Graph (78), DisplayManager.cpp (48), Auth (24), i18n (20), Alarm (16), Dashboard (8 — só blits, JÁ usa canvas). Cada call é uma transação SPI separada com gap CPU entre — user vê redesenho top-down em ~200-500ms. Estratégia A1: canvas off-screen 320×80 (51 KB) reusado em 3 strips (240px total). Heap alloc/free por render libera memória pra telemetria entre renders. Cada strip ~13 ms SPI = ~40 ms total full-screen, percebido como atômico. | — | — | — | — |
|   · v3.30.5 (Fase 0): infra `beginScreenRender/commitScreenStrip(0..2)/endScreenRender` em DisplayManager + pilot em `drawAuthScreen` (3 modos: permanent lockout, timed lockout, normal). Helper `drawAuthChromeViaStrips` encapsula o pattern (title + msgs centrais opcionais + cancel/license buttons). Fallback `_tft->` direto preservado em caso de OOM. Build OK (Flash 98.6%, +0.1%). **HW: TRAVOU** ao tocar Settings — alloc de 51KB falhou por fragmentação (heap_lb=31KB) MAS `new GFXcanvas16` retornou objeto válido com `buffer` interno NULL (uninitialized member, mesmo bug do EXT-005). Próximo `fillScreen` segfaultou → Core 1 hung. | 🔴 FAIL crash | — | `v3.30.5` | 2026-05-02 |
|   · v3.30.6 (Fase 0 fix): STRIP_H reduzido 80→40px (320×40×2=25KB cabe em heap_lb 31KB com 6KB margem). Buffer-null detection após `new GFXcanvas16` em `beginScreenRender` — se `getBuffer()==null`, delete + retorna nullptr → caller cai em fallback `_tft` direto sem crash. 6 strips/screen (vs 3) — mais chunks visíveis mas cada chunk atômico. Build OK. **HW: sem travamento**. | ✅ HW (sem crash) | — | `v3.30.6` | 2026-05-02 |
|   · v3.30.7 (Fase 0 refactor): `beginScreenRender` agora REUSA `_canvasWide` (já alocado pro dashboard) em vez de allocar dinâmico. Durante full-screen render, dashboard não está ativo → canvas livre. Blita 40 das 45 rows do canvas (5 ignoradas). Elimina toda categoria de bug heap-alloc + null-buffer. drawAuthScreen simplificado (~80 linhas removidas). Base limpa pras próximas fases. **HW PASS** — visualmente bom, sem travamentos. | ✅ HW validada | — | `v3.30.7` | 2026-05-02 |
|   · v3.30.8 (Fase 1): `drawAlarmAction` (16 _tft calls). Padrão "draw-everything-with-offset-per-strip" — cada strip desenha TODOS os elementos (header + 3 botões) com Y offset `-stripIdx*40`. Adafruit_GFX clipa pixels fora do canvas automaticamente. 3 dos 4 elementos cruzam fronteiras de strip mas o clipping resolve transparente. ~5× mais CPU draws vs single-pass mas elimina top-down. Padrão validado pra reuso nas Fases 2-5. **HW PASS**. | ✅ HW validada | — | `v3.30.8` | 2026-05-02 |
|   · v3.30.9 (Fase 2): `DisplayManager_Calibration.cpp` (89 _tft calls → 52, 37 migrados). 4 funções refatoradas: `drawCalibrationMessage` (full redraw 20→0), `drawTouchCalibration` (full path migrado, incremental fica direto entre taps), `drawTouchSensitivity` (full path migrado, dynamic crosshair/bar/value fica direto), `drawSettingsDisplayOffset` (full path migrado, numeric updates ficam direto). Helper `drawCrosshairOnCanvas` adicionado. **HW: top-down eliminado, MAS user reportou flicker em elementos estáticos** (problema preexistente exposto pela ausência de top-down — dynamic updates clear+redraw todo frame mesmo sem mudança). | 🔴 flicker preexistente | — | `v3.30.9` | 2026-05-02 |
|   · v3.30.10 (state diffing): adicionou tracking de last-state pra eliminar flicker. **HW: TRAVOU BOOT** — Core 1 "Lockout stuck >10s" repetindo por ~2 min antes de "Sistema pronto". Runtime OK pós-boot e flicker fixado. Causa raiz não identificada (state diffing logic não tem loops nem alocações novas). | 🔴 boot freeze | — | `v3.30.10` | 2026-05-02 |
|   · v3.30.11 (revert state diffing): mantém v3.30.9 strips (top-down eliminado), remove v3.30.10 state diffing. ✅ HW: boot voltou a funcionar; flicker reaparece nos 4 fns. | ✅ HW validada | — | `v3.30.11` | 2026-05-02 |
|   · v3.30.12 (state diffing v2 file-static): tentativa via `namespace { ... }` em vez de class members. Hipótese: struct shift do v3.30.10 era a causa. **HW: BOOT FREEZE IDÊNTICO ao v3.30.10**. Hipótese descartada. | 🔴 boot freeze | — | `v3.30.12` | 2026-05-02 |
|   · D5 (binary diff v11_pure vs v12 ELFs): comparação ELF-level. **Resultado: H1 (linker layout shift) DESCARTADA** — funções críticas do boot path (DisplayManager ctor, pauseRendering, requestQuietMode, releaseQuietMode, loopCore1, core1Entry) e ISR `multicore_lockout_handler` em **endereços bit-exatos idênticos** entre v11_pure e v12. ELFs preservados em `/tmp/v11_pure.elf` e `/tmp/v12.elf`. | ✅ Custo zero (sem flash) | — | — | 2026-05-02 |
|   · D1.A→D.D (bisect HW 4 estágios): adicionando incrementalmente storage+writes (D1.A), reads em drawAuthScreen (D1.B), em drawTouchSensitivity (D1.C), em drawTouchCalibration (D1.D). **TODOS PASSARAM HW** — boot 33-37s, sem freeze. Mistério genuíno: D1.D é funcionalmente equivalente ao v3.30.12 que travou, mas passa. Hipótese mais provável: ausência de `volatile` no v3.30.10/12 original permitia race cross-core (Core 0 escreve em show*(), Core 1 lê em draw sem barrier) → comportamento indefinido + estado contaminado entre flashes. Volatile força memory accesses idempotentes e estabiliza. | ✅ HW (4 stages) | — | `v3.30.12A..D` | 2026-05-02 |
|   · v3.30.13 (release candidate): D1.D + canvas-atomic text rendering em `drawTouchCalibration` (substitui `_tft->fillRect(20, 85, 280, 65, BG)` direto que flashava preto antes do redraw). 2 blits via `_canvasWide`: strip 0 (y=85..130, 45px, title+msg) + strip 1 (y=130..150, 20px, cycle counter). Cada blit é atômico. Elimina último flash visível. **HW PASS — sem flicker em todas as telas (auth dots/keypad, touch sens crosshair/bar/value, touch cal text/crosshair)**. | ✅ HW validada | — | `v3.30.13` | 2026-05-02 |
|   · **v3.31.0 (fase fechada — bump minor)**: F-DISPLAY-ATOMIC concluída. Top-down eliminado em todas telas refatoradas; flicker eliminado via state diffing volatile cross-core-safe. ELFs de bisect preservados em `/tmp/v1{1_pure,2A..D,3}.elf` para referência futura. Tag git `v3.31.0` planejada. | ✅ HW validada | (commit pendente) | `v3.31.0` | 2026-05-02 |
| **F-CSV-EXPORT (feature fora da auditoria)** | 🟡 Em andamento | `stability-fixes-tier1` | (v3.28.0) | — |
|   · F-CSV.1 — helper `crc32_init/update/final` em `SystemUtils.cpp` (CRC32-IEEE bitwise, mesma matemática de `StorageManager::calculateCRC32` mas exposto incremental para streaming). Forward decls em `SystemDefs_Records.h`. | 🟡 Implementado, HW pendente | `42aac18` | — | 2026-05-01 |
|   · F-CSV.2 — `GET /api/export/history.bin?from=&to=` (cap 31d, formato `.simx` kind='H': HEADER 32B packed + SENSOR_TABLE + N×`BinaryHistoryRecord` 28B + CRC32 trailer). Auth+RBAC herdados, `_inHistoryHandler` atomic guard, `HeavyTaskGuard`, deadline `WEB_LONG_HANDLER_DEADLINE_MS`. Day-aligned iteration via `historyDecodeRecord()`. Teste automatizado: `tools/test_f_csv_2.sh` (9 casos: auth, args, cap, struct, CRC32, concorrência, regressão). **Build flash 97.5%**. **HW validada (21/21 asserts; CRC32 0xB63C6FD8 bateu; sensor table 11 entries STM0001..STM0010+AMB).** | ✅ HW validada | — | — | 2026-05-01 |
|   · F-CSV.3 — `GET /api/export/logs.bin?from=&to=&level=err\|inf\|all` (formato `.simx` kind='L' + CRC32 trailer). Filtro server-side por epoch + `rec->getLevel()`. Itera `/system.old.blog` + `/system.blog`. `_inExportLogsHandler` atomic guard separado (history+logs export concorrentes permitidos). Teste `tools/test_f_csv_3.sh` (12 casos: auth, args, level inválido, struct, CRC32 nas 3 variantes, coerência all=err+inf+other, concorrência, regressão). **HW validada (28/28; level=all 1432 records DBG=0 INF=747 WRN=685 ERR=0; CRC 0xE83A927F bateu).** | ✅ HW validada | — | — | 2026-05-01 |
|   · F-CSV.4 — UI `/history` abaixo do `#chartContainer`: card "Export Sensor History" com 2 pickers date+time + dropdown sensor (`Todos` ou slot único, populado de `populateSensorDropdown`) + botão. JS embarcado: mini lib CRC32-IEEE com tabela 256 (compatível com firmware), `_iterMonths()` divide range em meses calendar, fetch sequencial `/api/export/history.bin`, valida magic+version+kind+recordSize+CRC32, decodifica sensor_table + payload, gera CSV (BOM UTF-8, ISO-8601 com tz local, 5 colunas), trigger download `simut_history[_sN]_<YYYY-MM>.csv`. 7 i18n keys + estados de status (`exp_fetching/validating/done/err_*`). Teste E2E `tools/test_f_csv_4.sh` (31 asserts: UI presente, CRC32 byte-a-byte vs Python, BOM, regressão de 6 páginas). **HW validada (31/31; CRC 0x0ABAC7BD bateu).** Build flash 97.6% → 98.0%. | ✅ HW validada | — | — | 2026-05-01 |
|   · F-CSV.5 — UI `/history` abaixo do card de logs: 2 pickers + dropdown nível (Todos / Erros / Infos) + botão. JS `_decodeSimxLogs()` valida magic+version+kind=L+recordSize=12+CRC32, decodifica N×`CompactLogRecord` (12B) e gera CSV com 7 colunas (`timestamp_iso, level, module, code, message, context, uptime_hr`). **Reusa** `_crcTab/crc32/_iterMonths/_isoLocal` de F-CSV.4 e `evtName/TAG_NAMES/LVL_LABELS` já existentes (lookup `code`→texto inline em PROGMEM, sem dependência de `/api/lang`). CSS `.exp-row/.exp-btn/.exp-status` reaproveitado. Download `simut_logs_<level>_<YYYY-MM>.csv`. Teste E2E `tools/test_f_csv_5.sh` (22 asserts: UI, anti-duplicação de funções, E2E level=all + level=err com filtro byte-a-byte). **HW validada (22/22; CRC 0xEFFAC7B7 bateu; 1527 records DBG=0 INF=807 WRN=719 ERR=1; level=err filtrou para 1 record corretamente).** | ✅ HW validada | — | — | 2026-05-01 |
| **F-GRAPH-REVAMP (feature fora da auditoria)** | 🟡 Em andamento | `stability-fixes-tier1` | (v3.28.0) | — |
|   · F-GRAPH.1 — Backend: `rangeDuration[]` expandido para 7 níveis (1h, 6h, 24h, 7d, 1M, 1A, MAX=`0`). Para `r>=4` lista arquivos via `LittleFS.openDir(/history)` + sort cronológico (evita 365× `exists()`). Novo endpoint `GET /api/history_multi?sensors=<csv>&range=<0..6>&end=<ep>` que retorna 1 response com TODAS as séries: `{cutoff,end,now,rangeUsed,sensors:[{id,hwId,name,type,hasH}],data:[{t,v[N],h?}],minT,maxT,tsMinT,tsMaxT}`. `v[]` alinhado por índice. `h` apenas quando ambient incluso. Reusa `_inHistoryHandler` atomic + `HeavyTaskGuard` + decodificador codec v2 existente. | ✅ HW validada | — | — | 2026-05-01 |
|   · F-GRAPH.2 — Frontend `/history`: dropdown multi-select custom (`.msel`) com bullet de cor; `fetchAndDraw` usa `/api/history_multi`; paleta T quente / H azul tracejada. Default boot: 24h + ambient. Eixo X linear em epoch ms (largura representa o período pedido) com ticks por range (1h=10min, 6h=1h, 24h=4h, 7d=1d, 1M=5d, 1A=mês, MAX=auto) e tooltip formatado `DD/MM/YYYY HH:MM:SS`. Rangesel virou `<select>` convertido pelo helper global `_makeCustomSelect`. | ✅ HW validada | — | — | 2026-05-01 |
|   · F-GRAPH.3 — Export chunked com retry adaptativo + ETA + cancel. Inicia em 24h (calibrado pelo `heap_lb` de `/api/status`), split em falha (24h→12h→6h→3h→1h) e recovery após 5 OK. Loop com cursor (não índice). AbortController + flag `_expCancelled`; cancelamento gera CSV `_partial` com o que conseguiu. Overlay full-screen com %, OK/falhas, ETA por média móvel de 5 amostras. `tools/test_chunk_size.sh` + `test_chunk_perf.sh` — calibração empírica. | ✅ HW validada | — | — | 2026-05-01 |
| **F-PERF-AUDIT (sub-fase de F-GRAPH-REVAMP)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-05-01 |
|   · `tools/test_perf.sh` (5 dimensões: páginas HTML, APIs leves, history_multi por range, export throughput, concorrência) — descobriu que APIs leves levam ~600ms (gargalo do main loop, fora do escopo) e `export 3d` beirava o `WEB_LONG_HANDLER_DEADLINE_MS=10s`. Fixes: (a) deadline 10s → 15s (+50% margem); (b) `_server.handleClient()` drena até 4 requests por tick (cap 50ms); (c) removido `handleApiHistoryData` (substituído por `/api/history_multi`) — economizou ~15KB flash. | — | — | — | — |
| **🚨 BUG CRIT — loop infinito em `handleApiExportHistory`** | ✅ Corrigido | `stability-fixes-tier1` | — | 2026-05-01 |
|   · `tools/test_stress.sh` reproduziu: range em arquivo inexistente (ex.: 30 dias atrás num flash com só 21d) → `if (!fileOk) continue;` saltava sem incrementar `dayStart` → loop infinito → WDT 8s → **reboot**. Fix: `dayStart` avançado no TOPO do loop antes de qualquer continue; check de `isClientGone()`/`isHandlerOvertime()` no nível do dia. Validado: 69 steps stress, 0 reboots, heap delta -5B. | — | — | — | — |
| **F-UX-POLISH (sub-fase de F-GRAPH-REVAMP)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-05-01 |
|   · UI consistency: dropdown custom global (`.csel` em LANG_JS) — resolve `<select>` nativo abrindo na cor do SO em Linux/Chrome com tema GTK claro. Toggle padrão (`.toggle/.slider`) movido pra LANG_JS — usado em `/config` (NTP, log, TLS), `/network` (DHCP, DNS auto), `/alarms` (sons + alm-active per sensor). Mudo Global lógico (snd_mute exclusivo). Spinners de input number removidos globalmente. Campos condicionais via CSS `:has()`: limites de alarme aparecem só quando `.alm-active` checked; campos static IP/DNS aparecem só quando DHCP/DNS auto OFF. Toggle "Alarm Active" movido pro `.sensor-header` (à esquerda do nome). Layout `.sound-item` alinhado (mel-group fixo à direita). | — | — | — | — |
| **F-REFACTOR-LANGJS (sub-fase de F-GRAPH-REVAMP)** | ✅ Concluída | `stability-fixes-tier1` | — | 2026-05-01 |
|   · Plan A: infra duplicada (`window.t`, `applyLang`, `setLang`, `showToast`, `fetchSafe`) movida das 8 páginas autenticadas (DASH/HIST/CFG/NET/USR/FILE/ALARMS/LICENSE) para LANG_JS. Net -4KB gz, com LANG_JS +2.2KB. Liberou espaço pros toggles em `/cfg` e `/network`. LOGIN/FORCE_CHPASS mantém infra local (não carregam `/lang.js`). | — | — | — | — |
| **F-TERMINOLOGY-SLOT (sub-fase de F-GRAPH-REVAMP)** | ✅ Concluída (UI; CLI pendente) | `stability-fixes-tier1` | — | 2026-05-01 |
|   · GPIO → SLOT em todos lugares user-facing: cabeçalho da tabela `/dash`, "GPIO N" → "SLOT N" no card de sensor em `/alarms`, dict PT/ES atualizados. Display TFT já usa S0..S9. Memória `feedback_terminology_slot.md` documenta a regra para CLI/help no futuro. | — | — | — | — |
|   · F-CSV.6 — i18n PT/ES das 23 chaves novas (`exp_*`, `hist_3d/1m/1y/max/none_sel/n_sel`) em `data/lang/*.lng`; `SECURITY.md` ganhou seção dos endpoints CSV (auth, atomic guard, cap 31d, integridade CRC32, filtros server-side, hardening anti-loop-infinito). Release `v3.28.0` com tag git. | ✅ Concluída | `425ecf0` (acumulado v3.27.8..32) + commit final v3.28.0 | `v3.28.0` | 2026-05-01 |
| **F-CSV.6 — i18n + SECURITY.md + release v3.28.0** | ✅ Concluída | `stability-fixes-tier1` | `v3.28.0` | 2026-05-01 |
|   · F-CSV.6 — i18n PT/ES/EN dos endpoints CSV + tag `v3.28.0`. (entrelaçada com F-GRAPH/UX abaixo — release única) | ⚪ Pendente | — | — | — |
| **F-BFCACHE — Fix bfcache + dropdown clip + UI polish** | ✅ HW validada | `stability-fixes-tier1` | `v3.28.4` | 2026-05-01 |
|   · v3.28.2: `serveProtectedPage` em `WebManager_Auth.cpp:74` envia `Cache-Control: no-store` (era `public, max-age=3600`). Sintoma: ao voltar de outra página pra `/history`, o browser restaurava o snapshot do bfcache com `data-cd="1"` no select, wrappers e listeners obsoletos, travando o dropdown rangeSel. Ctrl-Shift-R não resolvia (bfcache é camada separada do cache HTTP). `no-store` é o vetor canônico pra descartar bfcache em todos os browsers. Afeta `/`, `/history`, `/config`, `/network`, `/users`, `/files`, `/alarms`, `/license`. `/lang.js`, `/login`, chart.js (CDN) seguem cacheados. Diagnóstico via puppeteer-core isolado: JS do `_makeCustomSelect` passou todos os 5 cenários (abrir/fechar/selecionar/reabrir) — bug era 100% restore de bfcache. | ✅ HW validada | — | `v3.28.2` | 2026-05-01 |
|   · v3.28.3: `.csel-menu` ficava cortado por `overflow:hidden` do card chart parent → menu aparecia atrás do painel de logs e não era clicável. Trocado `position:absolute` por `position:fixed` com `top/left/width` calculados via `getBoundingClientRect` no click handler. `z-index` 300→9999. Lógica `.up` (abrir pra cima) substituída por cálculo dinâmico `spaceBelow` vs `spaceAbove`. Fix universal — funciona em todos os selects custom (config/network/alarms/users), inclusive dentro de cards com clip. | ✅ HW validada | — | `v3.28.3` | 2026-05-01 |
|   · v3.28.4: padronização visual dos 5 controles em `.bottom-controls` (◀ rangeSel ▶ ⤓CSV) — todos `width:72px`, `height:38px`, texto centralizado, `font-size:0.85rem`. Setinha `▾` do dropdown oculta (já tem ◀ ▶ visuais ao lado). Removido `font-size:1.1rem` inline das setinhas. Estado `⏳` (export ativo) ocupa o mesmo espaço de `⤓ CSV` — sem rebote. | ✅ HW validada | — | `v3.28.4` | 2026-05-01 |
| **F-META-AUTHOR — Author metadata cleanup** | ✅ Concluída | `stability-fixes-tier1` | `v3.28.5` | 2026-05-02 |
|   · v3.28.5: (a) `git config user.name="Ângelo Moisés Alves"` + `user.email=117550822+angeloINTJ@users.noreply.github.com` (formato GitHub noreply, mantém email pessoal privado em repo público). (b) `git filter-repo` com mailmap reescreveu 177 commits + 19 tags (v3.3.10..v3.28.4) — o autor `Your Name <you@example.com>` (placeholder default do git em commits locais antigos) virou `Ângelo Moisés Alves <...>`. Author + committer agora idênticos em todo o histórico (exceto merge commits do GitHub UI que mantêm `GitHub <noreply@github.com>` como committer — comportamento correto). (c) Tag `@author Ângelo Moisés Alves` adicionada em 68 arquivos `.cpp/.h/.ino` (entre `@target` e `@license` no header doxygen). (d) Force-push em `origin main` + `origin stability-fixes-tier1` + `--tags --force`. Backup pré-rewrite em `.git/filter-repo/commit-map` (mapeia 177 SHAs antigos → novos). | — | — | — | — |
| **F17 etapa 5 — EXT-005 (sub-fase de F17)** | ✅ HW validada (root cause) | `f17-ext005-bisect` | `v3.28.12` | 2026-05-02 |
|   · v3.28.6: retry direto do EXT-005 que foi revertido em v3.25.7. Mesmo transform mecânico aplicado contra o tip atual (v3.28.5): `AppManager.h` ganha forward decls (8 managers) + `std::unique_ptr<T>` + `~AppManager()`; `AppManager_Core.cpp` ganha init list `make_unique<T>()` + `~AppManager() = default`; 7 outros `AppManager_*.cpp` ganham per-file #include dep map (Boot/Loop=8, Commands/HistoryAlarm=6, Events=5, Sensors=3, Graph=2). Bulk sed 549+12+3 sites. HW: **REPRODUZ regressao identica** ao v3.25.5 (`Display launched on Core 1` + 2× `Lockout stuck >10s, restarting Core 1`, tela branca, CLI vivo no Core 0). Confirma que F-LOCKOUT-STUCK fixes não absorveram causa raiz. | 🔴 FAIL | — | `v3.28.6` | 2026-05-02 |
|   · v3.28.7 (BISECT Plan B): apenas `DisplayManager` revertido para membro BSS (por valor), os outros 7 managers continuam em heap via `unique_ptr`. **HW: PASS** — 2 reboots limpos, sequência completa de boot, NTP sync, telemetry, web server, history record. Confirma que DisplayManager-em-heap é o trigger isolado. Análise de código: `DisplayManager::loopCore1()` (no Core 1) faz `multicore_lockout_victim_init()` + `_core1Ready=true` ANTES de 4 `new`s (TftWithOffset, XPT2046, GFXcanvas16 320×45 ~28KB, GFXcanvas16 140×40 ~11KB) e `_tft->begin()` (HW reset ILI9341). Lockout victim handler está registrado mas algo nesses news/inits segura IRQs >10s. Próximo: instrumentação para mapear exato. | ✅ HW PASS | — | `v3.28.7` | 2026-05-02 |
|   · v3.28.8 (BISECT Plan C — INSTRUMENTAÇÃO): voltou para Plan A (8 managers em heap) + `volatile uint32_t DisplayManager::_core1InitStage` atualizado em cada passo de `loopCore1`. Core 0 pola 8s após `startCore1`. **HW: stage trava em 12** (`_ts->begin()`). Stages 1-11 passaram em <2ms (Core 0 só pegou 0→12). Significa: 4 alocações de heap (TftWithOffset, XPT2046, GFXcanvas16 28KB, GFXcanvas16 11KB) **não são gargalo** — fragmentação de heap descartada como causa. Hang está dentro de `_ts->begin()` que faz `SPI.begin()` + `pinMode/digitalWrite/attachInterrupt` da lib XPT2046_Touchscreen. | 🔴 stuck=12 | — | `v3.28.8` | 2026-05-02 |
|   · v3.28.9 (BISECT — sub-stages dentro de `_ts->begin()`): replica chamadas equivalentes antes de `_ts->begin()` com stages 12, 30, 31, 32, 33. Depois chama `_ts->begin()` (idempotente). **HW: stage trava em 33** (`_ts->begin()` entrou e nunca retornou). `SPI.begin()`, `pinMode(TOUCH_CS,OUTPUT)`, `digitalWrite(TOUCH_CS,HIGH)`, `pinMode(TOUCH_IRQ,INPUT)` passaram limpos. O hang está em `_ts->begin()` nas linhas que **NÃO** replicamos: `attachInterrupt(digitalPinToInterrupt(tirqPin), isrPin, FALLING)`, `isrPinptr = this`, `return true`. Última e segunda são triviais — suspeito = `attachInterrupt`. | 🔴 stuck=33 | — | `v3.28.9` | 2026-05-02 |
|   · v3.28.10 (BISECT — isolar `attachInterrupt`): substitui `_ts->begin()` por nosso `attachInterrupt(stub)` + skip. **HW: stage avançou para 15** (`_tft->begin()`). Confirma `attachInterrupt` funciona; o hang era em `_ts->begin()` (linhas pós-attachInterrupt: `isrPinptr = this; return true;` — triviais). Mas agora `_tft->begin()` ALSO trava. Padrão: múltiplos HW init calls travam → não é uma função específica, é estado compartilhado. **Análise de código revelou root cause:** `DisplayManager.h:418-419` tinha `TftWithOffset* _tft;` e `XPT2046_Touchscreen* _ts;` SEM in-class initializer (`_canvasWide/Small` linhas 420-421 já tinham `= nullptr`). Em BSS, C++ zero-init implícito zera ponteiros antes de qualquer ctor; em heap (`make_unique`), ficam com lixo. O `if (!_tft) _tft = new ...` em `loopCore1` pulava a alocação porque `!lixo == false` quase sempre. `_tft`/`_ts` apontavam pra lixo, dereferência travava Core 1. | 🟢 ROOT CAUSE | — | `v3.28.10` | 2026-05-02 |
|   · v3.28.11 (FIX): adiciona `= nullptr` em `DisplayManager.h:418-419`. Reverte instrumentação invasiva de `loopCore1`. **HW PASS**: stages 0→15 em <2ms (alocações), 15→17 em 701ms (`_tft->begin()` HW reset ILI9341 — esperado: 100+100+200ms reset + 300ms init commands), 17→99 em 821ms total. Sistema pronto em 37s, NTP/telemetry/web OK, **zero `Lockout stuck`**. Confirma root cause: ponteiros uninitialized eram o único trigger. Fragmentação de heap nunca foi causa (alocações em <2ms). | ✅ HW PASS | — | `v3.28.11` | 2026-05-02 |
|   · v3.28.12 (CLEAN): remove instrumentação (`_core1InitStage` field + stage markers em `loopCore1` + augment do log "Lockout stuck" + poll loop em Boot.cpp). Mantém apenas o fix funcional (`= nullptr` em `_tft`/`_ts`) e a infra completa de EXT-005 (forward decl + unique_ptr + per-file includes). Release final do bisect. **Lição C++ aprendida e documentada na memória do projeto:** classes que serão alocadas via `new`/`make_unique` precisam de in-class initializer em TODOS os ponteiros membros — BSS zero-init implícito não está disponível no heap. | ✅ Release | — | `v3.28.12` | 2026-05-02 |
| **F-DRAWER-I18N — Ícones do drawer somem em PT (e qualquer não-EN)** | 🟡 HW pendente | `main` | `v3.31.10` | 2026-05-03 |
|   · Diagnóstico: ao trocar idioma para PT (ou qualquer não-EN) no menu hamburguer, os ícones (📊 📈 🔔 ⚙️ 🌐 👤 📁 📜) sumiam dos links de navegação; em EN funcionava. Root cause: `applyLang` em `WebUI.h:4280` (LANG_JS, servida em `/lang.js`) faz `el.innerHTML = dict[lang][key]` no elemento que tem `data-i18n`. O HTML era `<a data-i18n="nav_dash"><span class="ico">📊</span>Dashboard</a>` — o `data-i18n` no `<a>` fazia o innerHTML inteiro virar a string traduzida (`"Painel de Controle"`), destruindo o `<span class="ico">`. Em EN o `data-en` salvo era o innerHTML completo (com o span), por isso a reaplicação era idempotente. `nav_lic` escapava porque o emoji estava inline no texto e nas traduções (`"📜 Licença"`). | 🔴 Confirmado | — | — | — |
|   · Fix v1 (Opção B — refator HTML, v3.31.10): movido `data-i18n` do `<a>` para um `<span>` interno, separando ícone e texto. Padrão novo: `<a href="/" class="active"><span class="ico">📊</span><span data-i18n="nav_dash">Dashboard</span></a>`. Aplicado em 8 drawers × (7 nav links + 1 lic-link) = 64 sites. Para `lic-link` (que tinha emoji inline), criado `<span class="ico">📜</span>` separado. Traduções `nav_lic` em `language_pt-BR.lng` e `language_es-ES.lng` atualizadas removendo `📜 ` do início. CSS `.drawer nav a .ico` inalterado. | 🔴 Falha parcial em HW: lic-link com emoji duplicado em PT | — | `v3.31.10` | 2026-05-03 |
|   · Fix v2 (v3.31.11): lic-link revertido ao formato original (emoji inline `📜` + `data-i18n` no `<a>`). Causa do bug parcial: o `.lng` é lido do **LittleFS** no device (`/lang/language_*.lng`); meu update em `data/lang/` afetou apenas o source, não o device, causando emoji duplo (`<span class="ico">📜</span>` + `📜 Licença` da tradução). Reverter o `lic-link` para o formato pré-v3.31.10 elimina o duplicado sem precisar mexer no FS (uploadfs é destrutivo conforme política de FS backup). Os 7 nav links principais permanecem no formato novo — esses não dependem do `.lng` (vem do EN inline + dict in-RAM). `data/lang/*.lng` revertido para alinhar com o estado do FS no device. | ✅ HW validada | — | `v3.31.11` | 2026-05-03 |
| **F-FILES-BC — Breadcrumb com duas barras (`💾//config`)** | 🟡 HW pendente | `main` | `v3.31.12` | 2026-05-03 |
|   · Diagnóstico: ao entrar em pasta no `/files`, breadcrumb exibia `💾//config` (duas barras coladas). Causa em `fmBuildBreadcrumb` (`WebUI.h:3250`): template inicial do disquete tinha ` /` inline (`'💾 /'`) e o loop adicionava `<span class="sep">/</span>` antes de cada parte → 1 barra do template + 1 do separador = duplicada. Fix: removido ` /` do template do disquete. Raiz agora exibe apenas `💾` (clicável); pastas exibem `💾 / pasta` com separador único do CSS `.sep`. | ✅ HW validada | — | `v3.31.12` | 2026-05-03 |
| **F-FILES-UI — Polish toolbar do /files** | 🟡 HW pendente | `main` | `v3.31.13` | 2026-05-03 |
|   · Removido botão "New Folder" (`btnMkdir`), input `mkdirRow`, funções `fmShowMkdir`/`fmMkdir`, CSS `.fm-mkdir-input`, e ramo de perms que ativava o botão. Backend `/api/mkdir` permanece intacto (acessível via CLI/API direta para casos avançados). | — | — | — | — |
|   · Padronização CSS dos 3 botões da toolbar: classes antigas (`btn-up`/`btn-down`/`btn-del`/`btn-sm`) com `padding`/`radius`/`font` divergentes substituídas por `btn-fm` + variantes semânticas (`btn-fm-pri` = primary accent fill; `btn-fm-out` = outline accent; `btn-fm-dang` = outline danger). Padding 7px 14px, radius 6px, font 0.85rem, weight 600 — alinhado com `#commit-btn`/`.btn-action` do resto do sistema. Hover de Upload (primary) ganha `opacity:0.9 + translateY(-1px)` igual ao `.btn-save`/`button[type=submit]`. | ✅ HW validada | — | `v3.31.13` | 2026-05-03 |
|   · Chaves i18n `fil_mkdir`/`fil_mkname`/`fil_create`/`fil_cancel`/`fil_mkdir_ok`/`fil_mkdir_err` ficam órfãs no `.lng` do LittleFS do device (sem referência no HTML/JS). Sem impacto funcional ou de heap relevante (~bytes). Limpeza adiada para próximo uploadfs (que exige backup full do FS conforme política). | — | — | — | — |
| **Consolidação `v3.32.0`** | ✅ Tag | `main` | `v3.32.0` | 2026-05-03 |
|   · Bump minor consolidando os 3 fixes de UI validados em HW: F-DRAWER-I18N (v3.31.10/11), F-FILES-BC (v3.31.12), F-FILES-UI (v3.31.13). Sem mudança em código de estabilidade/segurança/firmware core. | — | — | `v3.32.0` | 2026-05-03 |
| **F-SOUND-MUTE-PARITY — Paridade Mudo Global TFT vs web** | 🟡 HW pendente | `main` | `v3.32.1` | 2026-05-03 |
|   · Sintoma: usuário reportou silêncio total do buzzer; root cause foi Mudo Global ativado no menu Sons do display, provavelmente por toque acidental. Bizarrice: na web `/alarms`, todos os toggles individuais (Touch/Confirm/Error/Alarm/Web) apareciam "ON" mesmo com Mudo Global "ON" — incoerente, pois a UI web tem lock-out (`WebUI.h:3676`) que ao ligar mute desliga todos os outros e vice-versa. O menu Sons do display NÃO tinha essa lógica — togglava o Mudo Global isoladamente, deixando estado inconsistente. | 🔴 Confirmado | — | — | — |
|   · Fix: aplicada paridade web em `DisplayManager_Touch.cpp` em 3 sites do `MODE_SETTINGS_SOUNDS`: (a) seleção de melodia que liga touch/confirm/error/alarm (linhas 1349-1366) → `_soundSettings.muted = false` após o switch; (b) toggle Web (mapIdx==4) → se ficou true, `muted = false`; (c) toggle Mudo Global (mapIdx==5) → se ficou true, desliga `touchEnabled/confirmEnabled/errorEnabled/alarmEnabled/webEnabled`. Comportamento agora idêntico ao JS da web. Sem alteração em SoundManager ou no path de áudio. | ✅ HW validada | — | `v3.32.1` | 2026-05-03 |
| **F-MUTE-CONFIRM — Tela de confirmação ao ligar Mudo Global** | 🟡 HW pendente | `main` | `v3.32.2` | 2026-05-03 |
|   · Pedido: ao ativar Mudo Global no display (estado OFF→ON), exibir tela de confirmação avisando que todos os sons serão desabilitados, com botões "Voltar" e "Confirmar". Padrão visual coerente com o resto do display (`drawAlarmAction`/`drawCalibrationMessage`). | — | — | — | — |
|   · Implementação: novo `MODE_CONFIRM_MUTE_ALL` em `SystemDefs_Records.h`. `DisplayManager::showMuteConfirm()` + `drawMuteConfirm()` em `DisplayManager_Settings.cpp` — render via strips canvas atomic (6× 40px), header vermelho RGB565(180,30,30) com texto branco, mensagem em 3 linhas centralizadas (font 9pt), 2 botões em y=190..230 (Voltar à esquerda em C_CARD_BG, Confirmar à direita em vermelho destacado). Strings hardcoded EN/PT condicional ao `_currentLangIdx == LANG_PT` (sem mexer em DICTIONARY_EN nem `.lng` do device; TFT só ASCII pela política F-LANGPACK-ASCII). Dispatch em `DisplayManager.cpp:841`. Touch em `DisplayManager_Touch.cpp`: mapIdx==5 (mute toggle) verifica estado: se vai LIGAR chama `showMuteConfirm()`; se vai DESLIGAR aplica direto. Handler do `MODE_CONFIRM_MUTE_ALL` adicionado: Voltar volta ao `MODE_SETTINGS_SOUNDS` sem alterar; Confirmar aplica `muted=true` + desliga todos os sons individuais (paridade com fix anterior). | ✅ HW validada | — | `v3.32.2` | 2026-05-03 |
| **F-SOUND-ATTENTION — 5ª classe de som + confirm no botão Confirmar** | 🟡 HW pendente | `main` | `v3.32.3` | 2026-05-03 |
|   · Pedido: (a) som de confirmação no botão "Confirmar" da tela de mute (estava tocando SND_TOUCH_CLICK curto via acceptTouch); (b) nova classe `SND_ATTENTION` para telas de atenção/confirmação, configurável como os outros sons (toggle + 6 melodias + volume sistema). | — | — | — | — |
|   · Backend (SoundManager): `+SND_ATTENTION` no enum, `+SND_FLAG_ATTENTION=0x40`, `+_enableAttention/_melAttention`, `+attentionEnabled/attentionMelody` em SoundSettingsState, `+MEL_ATTENTION_0..5` (6 melodias: Notify/Bell/Pulse/ChimeLow/Rise/SoftDing). MELODIES[4][6]→[5][6]. update() processa SND_ATTENTION como system sound. **Schema config**: bits livres (0x40 do flags + bits 12-14 do mp) preenchidos. SoundConfigData segue 6B (sem migração). Magic 0xAB→0xAC: legacy lê normal e ganha defaults attention=ON+mel=0 (retrocompat 100%). | — | — | — | — |
|   · Display TFT: TOTAL_ITEMS 8→9 em drawSettingsSounds (3 páginas). Ordem nova: touch/confirm/error/alarm/web/**attention**/mute/volume/alarmVol. drawMelodySelect aceita typeIdx==4 (MEL_NAMES expandido pra [5][6]). Touch handler renumerado: mute mapIdx 5→6, volume 6→7, alarmVol 7→8. Label "Attention/Atencao" hardcoded EN/PT (não adicionado TR_SND_ATTENTION ao enum porque parser do `.lng` exige exatamente TR_KEYS_COUNT linhas — adicionar quebraria a tradução PT do device). showMuteConfirm() chama `requestPreviewSound(SND_ATTENTION, melAttention)` ao entrar na tela. Botão Confirmar do mute toca SND_CONFIRM via requestPreviewSound (gate manual sem acceptTouch para evitar bip duplo). | — | — | — | — |
|   · Web `/alarms`: novo card "Attention" com toggle+dropdown 6 melodias+botão `♩` test. Lock-out com mute global estendido (snd_attention entra na lista de exclusão mútua). JS de loadAlarms / collectAlarmsState / applyPendingToForm aceitam attention/melAttention. testSound('attention') usa novo array `_melodies.attention` (6 melodias com freq+dur idênticas ao firmware). WebManager_Api.handleApiAlarms emite "attention" e "melAttention" no JSON. WebManager_Commit parseia ambos. | ✅ HW validada | — | `v3.32.3` | 2026-05-03 |
| **F-SOUND-MENU-REORDER — Renome + reordenação do menu Sons (TFT) + bug fix attention preview** | 🟡 HW pendente | `main` | `v3.32.4` | 2026-05-03 |
|   · Mudanças no menu Sons no display TFT (`drawSettingsSounds`): (a) volumes (Vol Sistema, Vol Alarme) movidos pro topo da lista (mapIdx 0/1); (b) ordem dos sons individuais: Toque/Confirm/Erro/Alarme/Atenção (mapIdx 2..6); (c) Acesso Web em mapIdx 7; (d) Mudo Global por último (mapIdx 8); (e) labels "Som de Erro" → "Erro", "Som de Alarme" → "Alarme", "Silenciar tudo" → "Mudo Global" — hardcode EN/PT sem mexer no `.lng` do device (as 3 chaves TR_SND_ERROR/ALARM/MUTE continuam no enum mas só são usadas em outras telas, ex.: drawMelodySelect title). Touch handler renumerado de acordo. Sem mudança em SoundManager nem em Web /alarms (lá os labels já não são redundantes — "Error"/"Alarm"/"Mudo Global"). | — | — | — | — |
|   · Bug fix: no `drawMelodySelect` (menu de seleção de melodia, em qualquer tipo de som), o switch que processa o **toque direto sobre a linha** de uma melodia (`DisplayManager_Touch.cpp:1305`) não tinha o `case 4 → SND_ATTENTION`. As setas cima/baixo (linhas 1318/1331) já tinham, então só elas tocavam o preview no menu de Atenção. Tocar diretamente sobre uma linha de melodia ali ficava silencioso (`evType` permanecia em `SND_NONE` → guard `!= SND_NONE` falhava). Adicionado `case 4: evType = SND_ATTENTION;`. Bug introduzido em v3.32.3 (F-SOUND-ATTENTION). | ✅ HW validada | — | `v3.32.4` | 2026-05-03 |
| **F-I18N-AUDIT — i18n PT/ES: 13 chaves faltantes** | 🟡 HW pendente | `main` | `v3.32.5` | 2026-05-03 |
|   · Auditoria de chaves: `data-i18n` no HTML + `t(...)` no JS = 307 chaves; PT.lng e ES.lng tinham 338 cada mas faltavam 13: `alm_attention` (recém-adicionada v3.32.3), `fcp_wel/msg/p1/p2/req/show` (force_chpass page todo em EN mesmo com PT), `fil_uploaded`, `fil_up_err`, `net_saved_port`, `usr_pend_add/del/rst` (defaults JS já em PT, mas faltavam tradução formal). | — | — | — | — |
|   · Estratégia em 2 camadas porque `.lng` no device exige uploadfs (destrutivo conforme política de FS backup): (1) **fallback inline runtime** em `WebUI.h` — `LANG_JS dict.pt` ganha `alm_attention/fil_uploaded/fil_up_err`; `FORCE_CHPASS_PAGE dictFcp.pt` ganha as 6 `fcp_*`. `Object.assign(dict.pt, d)` mescla por cima do que vem de `/api/lang`, então quando `.lng` for atualizado as keys do device prevalecem. (2) **source `.lng`** — `data/lang/language_pt-BR.lng` e `language_es-ES.lng` atualizados com as 13 chaves para o próximo uploadfs. ES inclui mesmo sem suporte UI atual (firmware só EN+PT) por consistência futura. | ✅ HW validada | — | `v3.32.5` | 2026-05-03 |
| **Consolidação `v3.33.0`** | ✅ Tag | `main` | `v3.33.0` | 2026-05-03 |
|   · Bump minor consolidando 5 patches validados em HW: F-MUTE-CONFIRM (v3.32.2), F-SOUND-ATTENTION (v3.32.3), F-SOUND-MENU-REORDER (v3.32.4), F-I18N-AUDIT (v3.32.5), e o fix de paridade web do mute (v3.32.1). Feature observable: nova classe `SND_ATTENTION` + tela de confirmação ao ativar Mudo Global + reorganização do menu Sons (volumes no topo, Mudo Global por último, sem labels redundantes). Sem mudanças em código de estabilidade ou segurança. | — | — | `v3.33.0` | 2026-05-03 |
| **F-BT-NAME — Nome BT visível = `cfg.deviceName`** | 🟡 HW pendente | `main` | `v3.33.1` | 2026-05-03 |
|   · Pedido: substituir o nome BT default da lib SerialBT (`"PicoW Serial XX:XX:XX:XX:XX:XX"`) pelo nome configurado pelo usuário em `/config` (mesmo `cfg.deviceName` que já abastece AP `<name>_SETUP`, mDNS, prompt CLI). | — | — | — | — |
|   · Implementação: `SerialBT.setName(const char*)` da lib arduino-pico aceita renomear apenas enquanto `_running==false`, então a chamada precisa preceder `SerialBT.begin()`. Aplicado em `BluetoothManager::begin(deviceName)` (antes ignorava o parâmetro com comentário "managed by radio firmware" — incorreto). `CommandManager::begin()` ganha parâmetro `btDeviceName` com default `"SIMUT"`. `AppManager_Boot.cpp:136` agora passa `_storageMgr->getConfig().deviceName`. Trocas via web exigem reboot (consistente com fluxo `commit_all` "Salvar e Reiniciar"). Se `cfg.deviceName` for vazio, mantém default da lib (fallback seguro). | ✅ HW validada | — | `v3.33.1` | 2026-05-03 |
| **F-AMBIENT-CALIB — Ambient (DHT22) calibrável via calib.csv** | 🟡 HW pendente | `main` | `v3.33.2` | 2026-05-03 |
|   · Pedido: ambient (DHT22) hoje ignora `calib.csv` (lookup é por ROM, DHT não tem). User quer linhas dedicadas pro ambient com chave = picoUID 16 hex (sem pontos/traços), discriminadas por prefixo `t` (temp) ou `u` (hum) no campo ID. Cada linha: serial, ID, offset, nome. Display TFT continua "AMBIENTE" (TR_AMBIENT). Telemetria usa o ID customizado. **Opção B escolhida**: ID e nome compartilhados entre temp e hum (linha `t` define ambos; linha `u` só serve pra offset de umidade). Sem bump de schema. | — | — | — | — |
|   · Implementação: (a) `StorageManager::getCalibrationDataAmbient(char prefix, outId, outOffset, outName)` — itera `calib.csv`, match por chave==picoUID e prefixo no campo ID; retorna `outId` sem o prefixo. (b) `SensorManager` ganha `calibrationOffsetHum` em `RuntimeSensor` (init=0 em todos; no-op pra DS18B20) e `applyAmbientCalibration(offsetT, offsetH)`. `handleSensorResult` aplica `+ s.calibrationOffsetHum` em v2. (c) `AppManager::loadAndCalibrateSensors()` chama `getCalibrationDataAmbient('t',...)` e `'u',...)` após o loop de DS18B20; aplica id/name (só do t) em `cfg.ambientSensor` e ambos offsets em `_sensorMgr`. (d) `TelemetryManager` em `formatLineCustomBuf` muda `hwid = boardSerial` para `hwid = (cfg.ambientSensor.hwId != "AMB") ? cfg.ambientSensor.hwId : boardSerial` em `{tAMB}` e `{uAMB}` — preserva compat com setups sem calib customizado, mas usa o ID custom quando há (ex: payload sai `"tA1":24.5` em vez de `"t8E7C1A0B...":24.5`). Friendly name fluí para `WebManager_History` (sensor table do .simx export) e CSV exports. + Bonus visíveis no UI/CLI: `show system info` e Display Status (Configurações → Status) agora mostram "Serial: <picoUID>" pra facilitar editar `calib.csv`. | ✅ HW validada | — | `v3.33.2` | 2026-05-03 |
| **F-CALIB-UI — Página `/calibration` na web** | 🟡 HW pendente | `main` | `v3.34.0` | 2026-05-03 |
|   · Pedido: página amigável pra editar `calib.csv` em vez de upload manual. Lista todos os sensores DS18B20 + DHT22 ambient. Sensores não aceitos ganham botão "Aceitar Slot N". Sensores aceitos: editar Nome, ID, e setar valor real medido por padrão de calibração. Sistema calcula offset = offset_atual + (ref − leitura_corrigida). Botão "Atualizar" reescreve calib.csv com VERSION=epoch atual (timestamp). Salva só com NTP sincronizado. Display TFT continua "AMBIENTE". | — | — | — | — |
|   · Decisões: A) leituras em tempo real via poll de `/api/calib` (3s); B) varredura física via `/api/calib_scan` acionada por botão (custoso, ~7s); C) tudo num patch único; D) lista todos os 10 slots + ambient mostrando "vazio/detectado/ativo" — sensor detectado em slot inativo aparece com botão Aceitar. | — | — | — | — |
|   · Backend: novo `WebManager_Calib.cpp` com 4 handlers — GET `/api/calib` (estado leve, sem I/O OneWire), POST `/api/calib` (apply: parse JSON, calcula offsets, reescreve calib.csv via processCalibrationUpload atomic), GET `/api/calib_scan` (varre cada slot via `identifyPhysicalSensor` + DHT via `readDhtBlocking`; atualiza cache em WebManager), POST `/api/sensor_accept` (equivalente `sensor accept N` CLI; reusa lógica de `CMD_ACCEPT_SENSOR`). Helpers `readCalibLines`/`writeCalibLines` preservam linhas de outros sensores ao reescrever. Validação NTP em /api/calib POST: 503 se !`isTimeSynced()`. PERM_SYS_CONFIG em todos. | — | — | — | — |
|   · Frontend: página HTML/CSS/JS (15.7KB raw → 4.9KB gz) servida do LittleFS em `/web/calibration.html.gz` (Caminho 2 — opt-in via FS). Inputs Nome/ID/Ref, cálculo de "novo offset" em tempo real, polling 3s, botão "Atualizar" desabilitado sem NTP. Se arquivo não existe no FS: 404 + drawer JS oculta o link (HEAD `/calibration` no `installDrawer`). i18n PT-BR inline em LANG_JS dict.pt (38 chaves cal_*). | 🟡 Implementado, HW pendente | — | `v3.34.0` | 2026-05-03 |
| **F-WEB-DEDUP — Eliminar redundância de CSS/HTML em todas as páginas** | 🟡 HW pendente | `main` | `v3.34.0` | 2026-05-03 |
|   · Motivação: budget de flash apertado (98.6%) impedia adicionar a feature F-CALIB-UI. Diagnóstico: cada uma das 8 páginas autenticadas duplicava ~3.2KB de CSS comum (`:root`, body, topbar, drawer, breadcrumb, container, .bc-page) + 676B de #net-toast + ~2.4KB de drawer HTML. gzip não compartilha entre arquivos diferentes, então cada cópia paga seu preço (~1.5KB gz). Total redundante: ~30KB raw, ~7KB gz. | — | — | — | — |
|   · Implementação Fase A (CSS): extraído bloco comum body→.bc-page (3212B) + #net-toast (676B) → `STYLE_CSS` PROGMEM único. Servido em GET `/style.css` com `Cache-Control: public, max-age=604800` (7 dias). Cada uma das 8 páginas perde ~3.8KB raw e ganha `<link rel="stylesheet" href="/style.css">`. Browser cacheia → 2ª carga é só HTML específico. Economia: ~4KB gz. | — | — | — | — |
|   · Implementação Fase B (drawer HTML): drawer (`<div class="drawer-bg">` + `<div class="drawer">` com 8 nav links + footer) extraído de 8 páginas pra `LANG_JS`. Cada página agora tem só `<div id="drawer-host"></div>`. JS `installDrawer()` em LANG_JS injeta o HTML no DOM via `outerHTML`, marca link active baseado em `window.location.pathname`. Disparado em DOMContentLoaded. Economia adicional: ~4KB gz (16KB raw). | — | — | — | — |
|   · Implementação Fase C (refactor `WebManager_Calib.cpp`): substituído `std::vector<CalibLine>` (com 3 `String` por slot) por array fixo `CalibChange[14]` com `char[]`. Reescrita do `calib.csv` via streaming 2-pass (lê linha, decide manter/substituir, escreve direto). Object: 21KB → 17KB (text 10KB → 5.5KB). Economia: ~3KB no firmware. | — | — | — | — |
|   · Resultado: overflow inicial v3.34.0 era 14.7KB; após Fases A+B+C+Caminho 2 ficou em 2.4KB — mesmo overflow do baseline `v3.33.2` sem o patch. Adicionei a feature inteira **com custo zero líquido** vs baseline. WebUI.h reduziu de 320281 B → 282759 B (raw); gz total de 80357 B → 73215 B. | — | — | — | — |
|   · **Pivot final** (escolha do user "B - auditoria"): página `/calibration` separada removida, integrada no /dashboard como toggle "Modo Calibração" + inputs inline. Toggle aparece se user tem **PERM_CALIB (0x0200)** — admin sempre tem; admin atribui aos demais via `/users` (checkbox novo). Backend reduzido pra 2 endpoints: GET/POST `/api/calib` (sem `/api/calib_scan` nem `/api/sensor_accept` — usar CLI). Auditoria identificou `Favicon.cpp` ocupando **11KB de flash em PROGMEM** com binário inline; migrado pro LittleFS (`/favicon.ico` servido com Cache-Control 7d, fallback 204 se ausente). Economia total: 11KB flash liberado, **firmware compila com 97.7% flash usado** (1020040 / 1044480 B). Removidos: `Favicon.cpp`, `Favicon.h`, drawer entry "Calibração", chaves i18n cal_*scan/accept/scanning. **Bonus fixes**: (a) `_runtimeSensors[0].config.hwId/friendlyName` agora copia de `cfg.ambientSensor` (era hardcoded "AMB001"/"Ambiente_Fixo" — dashboard mostrava strings erradas); (b) `/api/config` expõe `ambHwId`; (c) Live Preview do `/config` (`_previewCustomLine`) usa `rec.ambHwId` em `{tAMB}`/`{uAMB}`/`{DHT_ID}` em vez do serial fixo, espelhando lógica do firmware (`hwId != "AMB"` → custom, senão fallback boardSerial). | ✅ HW validada | — | `v3.34.0` | 2026-05-03 |
| **F-WEB-LANGSEL — Seletor de idioma dinâmico no drawer** | ✅ HW validada | `main` | `v3.34.1` | 2026-05-03 |
|   · Pedido: ao instalar `.lng` espanhol no FS o display TFT mostra ES, mas o seletor de idioma na web continuava com "🇧🇷 PT" hardcoded. Corrigir pra refletir o `.lng` instalado; se nenhum estiver instalado, esconder o 2º option (só EN visível). | — | — | — | — |
|   · Backend: `DisplayManager::getActiveLangCode()/getActiveLangName()` (estáticos) lêem do header do `.lng` carregado em runtime. `WebManager_Api::handleApiPerms` ganha campos `langCode` + `langName` no JSON. | — | — | — | — |
|   · Frontend (`LANG_JS::installDrawer`): mapa `LANG_FLAGS` (pt/es/en/fr/de/it/ru/zh/ja/ko/nl/pl/sv/tr/ar) com fallback `🌐` para códigos desconhecidos; `langShort()` extrai abrev. (ex: "es-ES" → "ES"). Após injeção do drawer, fetch `/api/perms` atualiza label do 2º option com `flag + ' ' + short` real do `.lng` ativo. Se `langCode` vazio (sem .lng): 2º option é removido (`opts[1].remove()`) e força `localStorage.simut_lang='en'` + re-applyLang (evita fallback quebrado em `dict.pt` vazio). Flash final 98.1%. | ✅ HW validada | — | `v3.34.1` | 2026-05-03 |
| **Fase 18 — Profissionalização (auditoria pós-v3.35.0)** | ✅ Concluída | `main` | `v3.37.0` | 2026-05-03 |
|   · Auditoria executada via 3 sub-agents (memória/estabilidade, qualidade, segurança/produção). 15 achados consolidados ranqueados em 5 sub-fases (18.1..18.5). Falsos positivos validados manualmente: leak de TFT/Touch/Canvas (singletons), cookie sem flags (já tem HttpOnly+SameSite=Strict+Secure), rate limit ausente em login (já tem F15.1). | — | — | — | — |
| **F-HARDENING-WEB (Fase 18.1) — XSS escape + rate-limit /api/calib + mask t_key** | ✅ HW validada | `main` | `v3.36.0` | 2026-05-03 |
|   · **C1+C2 (XSS via innerHTML)**: dados controláveis por user privilegiado (`sn.name`, `t.name`) eram injetados em template literals → `.innerHTML` sem escape em DASH. Atacante com `PERM_CALIB`/`PERM_SYS_CONFIG` poderia configurar nome com payload JS → execução em outras sessões. Fix: `window.escHtml()` global (~80 B) em LANG_JS via `textContent` round-trip; envoltos `${escHtml(sn.id/name/v)}` e `${escHtml(t.id/name)}` em DASH. Validado em HW: presente no /dashboard servido. | — | — | — | — |
|   · **C4 (rate-limit `/api/calib` POST)**: operação flash-heavy (lê/reescreve calib.csv + saveConfiguration + reload) sem throttle — bug de UI ou ataque intencional gerava wear de flash. Fix: `isRateLimited(5000)` no início do handler retorna `429 + Retry-After: 5` se segunda chamada em <5s. Validado em HW vivo: 1ª call HTTP 200 → 2ª (1s depois) HTTP 429. | — | — | — | — |
|   · **A6 (mask `t_key` em `/api/config`)**: API key de telemetria em claro permitiria screenshot/share acidental vazar. Fix: backend emite primeiros 4 chars + `"***"` (vazio fica `""`); commit_all parser detecta `***` no recebido e preserva `cfg.telApiKey` atual (evita sobrescrever com mascarado). User edita normalmente quando quer trocar. Validado em HW: `t_key` mascarado retornado, ambHwId expostos juntos pra Live Preview do `/config`. | — | — | — | — |
|   · **Pula C3 (CSRF tokens)**: `SameSite=Strict` no cookie já mitiga em browsers modernos; uso casa/lab com infra confiável. Reabrir se firmware for exposto na internet. | — | — | — | — |
|   · Smoke tests automatizados: 29 asserts (login + perms + config + calib + rate-limit + style.css + favicon + heap sanity), todos passando. Flash 98.1% (igual antes — escapes adicionaram <100B no gz). | ✅ HW validada (29/29) | — | `v3.36.0` | 2026-05-03 |
| **F-PARSE-ROBUST (Fase 18.2) — parseIntStrict universal + path normalize O(n) + safety counters** | ✅ HW validada | `main` | `v3.36.1` | 2026-05-03 |
|   · **A2 (parseIntStrict universal em `/api/commit_all` system block)**: 14 campos numéricos (`tz`, `res`, `s_int`, `t_port`, `t_int`, `t_bat`, `t_mode`, `t_transport`, `m_qos`, `m_ka`, `h_int`, etc.) usavam `getNum().toInt()` — String::toInt() retorna **0 silenciosamente** para entradas como `"abc"`, `"NaN"`, `""`. Atacante (ou bug de UI) podia zerar configs críticas. Fix: cada campo agora usa `if (parseIntStrict(getNum(k), v) && isInRange(v, MIN, MAX)) cfg.field = v;` — payload inválido = noop. Bounds explícitos por campo (ex: `t_port` 1..65535, `h_int` 1..1440, `m_qos` 0..2). | — | — | — | — |
|   · **M2 (path normalize O(n²) → reject-early + single-pass)**: `WebManager_Files.cpp` tinha `while (dirPath.indexOf("..") >= 0) dirPath.replace("..", "");` que era O(n²) e quadraticamente mais caro com cada `..` adicional. Fix: rejeita early com HTTP 400 se `..` ou `%` presente; depois colapsa `//` em loop O(n) com flag `prev=='/'`. Mais rápido E mais defensivo. | — | — | — | — |
|   · **M8 (parseFloatStrict + safety counters em loops `while(indexOf '{')`)** : 3 loops em `WebManager_Calib.cpp` e `WebManager_Commit.cpp` parseiam arrays JSON manualmente — em payload patológico (sem `'}'` matching) podiam virar O(n) extra ou até loop apertado. Fix: contador local `safety` capa em `MAX_SENSORS+4` (sensors) ou `16` (users actions). Adicionado também `parseFloatStrict()` em `SystemDefs_Validate.h` (rejeita `"NaN"`, espaços, vírgulas decimais; aceita opcional `+`/`-` + `[0-9]+(\.[0-9]+)?`) — gancho para próxima fase aplicar a refTemp/refHum em `/api/calib`. | — | — | — | — |
|   · Smoke tests automatizados: 12 asserts (path traversal `..` rejeitado HTTP 400, `//` colapse, payload inválido preserva h_int/t_port/t_int após reboot, heap saudável 58KB, sanity perms/status). Flash 98.1% (mesmo antes — pure logic). | ✅ HW validada (12/12) | — | `v3.36.1` | 2026-05-03 |
| **F-REFACTOR-Q (Fase 18.3) — Refactor + qualidade (A1+A4+A5+A7)** | ✅ HW validada | `main` | `v3.36.2` | 2026-05-03 |
|   · **A1 (decompor `executeCommand`)**: arquivo `AppManager_Commands.cpp` de 952L com switch/51 cases. Os 8 cases mais longos (>=30L cada — `SENSOR_FIELD`, `ACCEPT_SENSOR`, `USER_ADD`, `RESET_ADMIN`, `SET_TIME`, `IP_CFG`, `SET_DNS_CFG`, `USER_PASS`) extraídos para `AppManager_CmdHandlers.cpp` (336L). Arquivo principal vai pra 624L (-35%). Cada case fica `case CMD_X: cmdHandleX(cmd, cfg, changed); break;`. Semântica idêntica — refactor mecânico só de legibilidade. | — | — | — | — |
|   · **A4 (zero warnings em código próprio)**: build full sem cache revelou 22 sites únicos (não 9 — o cache mascarava 13). Fixados todos: misleading-indentation (`Settings 110/280`, `Touch 726/1011/1012`, `Graph 736/742`), unused vars (`startIdx`, `timeW`, `w`, `tBuf`, `pageColor`, `cfg`), comment-within-comment (`Themes.cpp:272`, `TelemetryManager:1269`), sign-compare (`Auth:76`, `Touch:811`), missing-initializer (`tm = {0}` → `{}`), format `%d` vs int32_t. **Bug real corrigido:** `field == "tmin"` em CMD_SENSOR_FIELD comparava ponteiros (char* vs literal addr) — ramo NUNCA era alcançado e caía sempre em "Campo desconhecido"; agora strcmp(). | — | — | — | — |
|   · **A5 (Reserved.h map)**: criado `SystemDefs_Reserved.h` com `RESERVED_*_OFFSET` constexpr para os 7 overlays em `SystemConfig::reserved[64]` (Touch/Sound/Display/Cli/Web/Setup/NetTime) + `static_assert(RESERVED_FREE_OFFSET <= 64)`. Coexiste com `WEB_CONFIG_OFFSET`/`CLI_CONFIG_OFFSET` legados (mesmos valores). Migração progressiva. | — | — | — | — |
|   · **A7 (observability de broken pipe)**: antes `safeSend()` retornava false silenciosamente quando cliente desconectava — handler dava return sem rastro. Adicionado `maybeLogClientDisconnect()` com throttle 5s em todos os 4 overloads de safeSend (WEB_CLIENT_DISCONNECT=575). Single point cobre 30+ call sites sem migração de cada handler. Origin tag `s/sN/sStr/sP` + `early/post` rastreia qual streaming morreu. | — | — | — | — |
|   · Smoke tests automatizados: 9 asserts (perms/status/config/calib HTTP 200, heap 58KB, uptime válido, M2 traversal ainda rejeitado, device estável após broken pipe induzido). Flash 98.2% (+0.1% por A7+A1 boilerplate). | ✅ HW validada (9/9) | — | `v3.36.2` | 2026-05-03 |
| **F-OBSERVABILITY (Fase 18.4) — M6+M7+M9+M10** | ✅ HW validada | `main` | `v3.36.3` | 2026-05-03 |
|   · **M6 (categoria em logs)**: audit de chamadas `LOG_CODE`/`LogManager::log` em todo o codebase. Distribuição: 35×APP, 33×SEC, 20×TEL, 19×NET, 13×STO, 10×WEB, 5×SENSOR, 4×SYS, 3×HIST, 3×CFG, 2×CLI, 1×DSP. Todas as 12 tags mapeam para `LogTagId` enum (4 bits, MAX 16 — padrão estável). `CompactLogRecord.flags` já carrega `tagId:4` desde EXT-003. **Conclusão**: schema bump não-necessário, M6 é satisfeito pela tag existente. | — | — | — | — |
|   · **M7 (cobertura de teste host)**: `test_validators` ganhou +2 casos para `parseFloatStrict()` adicionado em 18.2 — `test_parseFloatStrict_valid` (8 entradas: "0", "0.0", "3.14", "-2.5", "+100", "999", ".5", "-0.0") e `test_parseFloatStrict_invalid` (12 entradas: "", "abc", "NaN", "1.2.3", "3,14", "1e5", "1.5f", " 1.5", "1.5 ", "-", "+", "."). Stub `test/native_stubs/Arduino.h` atualizado com `String::toFloat()` (estava faltando — quebrava o build host). **27/27 testes pio test -e native passam**. | — | — | — | — |
|   · **M9 (doc OTA)**: `SECURITY.md` §9.1 nova subseção **"Procedimento de update (re-flash USB)"** — pré-requisitos, 6 passos numerados (backup config → backup FS → BOOTSEL → copy UF2 → verify version → restore config), recovery de boot quebrado (BOOTSEL force, factory reset CLI), e semver tagging policy explicada (patch=fix, minor=schema/feature, major=breaking). Reforça: **OTA deliberadamente ausente, único caminho é USB**. | — | — | — | — |
|   · **M10 (test migration light)**: `static_assert(sizeof(SensorRecord) == 79, ...)` em `SystemDefs_Records.h` trava layout do struct mais alterado historicamente. Schema bump deliberado quebra o build no static_assert e força o autor a tocar `attemptLoad`/migration code junto. Test migration completa (carregar blobs v12/v13/v14) deferida — exigia refactor do parser pra desacoplar de LittleFS (~2h trabalho). | — | — | — | — |
|   · Smoke tests automatizados: 10 asserts (perms/status/config/calib OK, heap 58KB, version v3.36.3 exposta via /api/perms, M2 traversal ainda rejeitado, A7 estável após broken pipe, M10 invariante implícita pelo boot OK). Flash 98.2% (mesmo nível — só doc + tests). | ✅ HW validada (10/10) | — | `v3.36.3` | 2026-05-03 |
| **F-POLISH (Fase 18.5) — M3 + M4 + M5 + G3 (fechamento da Fase 18)** | ✅ HW validada | `main` | `v3.37.0` | 2026-05-03 |
|   · **M3 (mutex timeout API)**: adicionado `StorageManager::enterFlashReadLockTimeout(uint32_t ms)` que retorna `bool` em vez de bloquear indefinidamente. Caller decide se aborta (HTTP 503 Busy) quando Core 1 está em flash op longa. Os 46 sites legados de `mutex_enter_blocking` permanecem — migração progressiva. Risk de deadlock raro mas existe; agora há ferramenta pra mitigar onde for crítico. | — | — | — | — |
|   · **M4 (TODO/HACK/FIXME audit)**: grep `\bTODO\b\|\bFIXME\b\|\bHACK\b\|\bXXX\b` em `*.cpp/*.h` retorna **zero matches** no código próprio. Falsos positivos de "TODOS" (palavra portuguesa) descartados. Nada a fazer. | — | — | — | — |
|   · **M5 (drift detection test)**: dedup completa via `build_src_filter` avaliada e descartada — `SystemUtils.cpp` inclui `SystemDefs.h` (heavy) que arrastra deps de hardware no env native, complicando o build host. Solução escolhida: golden-vector tests existentes (`test_dallasCrc8_known_vectors`, `test_floatToI16_basic/clamp/nan`) já detectam qualquer divergência — se o algoritmo do firmware mudar, os tests no host quebram nos valores conhecidos. Doc reforçada no topo do arquivo de cópias. | — | — | — | — |
|   · **G3 (Proposta C comment)**: `/* ── Hamburger Nav (Proposta C) ──────── */` em WebUI.h:3863 (`STYLE_CSS`) era resíduo de exploração; trocado por `/* ── Hamburger Nav ────────────── */`. Single comment, ~10 B saved no PROGMEM gz. | — | — | — | — |
|   · **G1/G2/G4 deliberadamente skip**: cosmético puro (ordem de includes, mix String/char[], `_Ref` naming) — sem ganho funcional, alto risco de regressão. Reabrir só se time grande de devs ingressar. | — | — | — | — |
|   · Smoke tests automatizados: 12 asserts (perms/status/config/calib OK, heap 58KB, M2 ainda ativo, A1/A4/A7 não regrediram, /style.css OK, version exposta, M3 simbolo linkado, M5 drift active). Flash 98.2% (mesmo). | ✅ HW validada (12/12) | — | `v3.37.0` | 2026-05-03 |
|   · **🎯 FECHAMENTO DA FASE 18**: 5 sub-fases entregues acumulando 76+ asserts de teste em HW + 27/27 host tests passam. v3.36.0 → v3.37.0 com tag git. Próximo ciclo: features ou backlog F17 (EXT-005), conforme orientação do user. | ✅ Concluída | — | `v3.37.0` | 2026-05-03 |

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
| SEC-007 | 🟢 | F15 | ✅ | Hash 120→128 bits com migração transparente. |
| SEC-008 | 🟢 | F15 | ✅ | `PASSWORD_HMAC_ROUNDS` 2500→5000. |
| SEC-009 | 🟢 | F15 | ✅ | Salt random por usuário (schema bump). |
| BUG-001 | 🟢 | F14 | ✅ | `timeSince(start, duration)` helper em SystemDefs.h; 45 sites migrados em 8 arquivos (v3.23.12). |
| BUG-002 | 🟡 | F13 | ✅ | Wrappers `requestPreviewSound/requestVolumePreview/requestAlarmVolumePreview` + `__dmb()` nos 3 pares cross-core Core 1 → Core 0. Barrier no producer (`setTelemetrySendStatus`) e readers (`render`/`drawTopBar`) do pack `_pktArrowState` Core 0 → Core 1. `_touchSoundPending`/`_errorSoundPending` fora do escopo (single-flag sem dado emparelhado). + UX fix (F13.3b): touch gates separados para volume no menu Sons. Validado HW. |
| BUG-003 | 🟡 | F13 | ✅ | Template `StorageManager::flashOp<F>()` (private no header) substitui macro local `FLASH_OP` de `saveConfiguration`. `writeHistoryEntryFlash` refatorado em chunks granulares: enforceStorageLimit (se rolagem), open+write+close, fallback enforce+open+write+close — cada um em seu próprio lockout. File handle nunca sobrevive entre chunks. HW validado (testes 1/2/4/5; teste 3 rolagem diária pendente até feature manual time). |
| BUG-004 | 🟢 | F13 | ✅ | Membro `_lastWebBusy` sticky (Core 1 only) em `DisplayManager`. Consumers em `loopCore1` e `handleTouch` atualizam o sticky quando `mutex_try_enter` sucede; usam o sticky como fallback quando falha. Validado HW. |
| BUG-005 | 🟢 | F13 | ✅ | `captureBootSnapshot()` público + chamada explícita em `begin()`; `setModule` não captura mais oportunisticamente; assertion defensiva + guard `_autopsyPerformed` em `performCrashAutopsy` (HW validada por F13.1 row). |
| CON-001 | 🟢 | F14 | ✅ | Bloco `SCRATCH REGISTER MAP` em `LogManager.cpp` + dedup (v3.23.10). |
| CON-002 | 🟢 | F14 | ✅ | `enum LanguageCode` EN+PT + `LANG_COUNT` sentinela (v3.23.4). |
| CON-003 | ⚪ | F14 | ✅ | `DisplayManager.{h,cpp}` "8 languages" → "2 (EN + PT)" (v3.23.10). |
| CON-004 | 🟢 | F14 | ✅ | `_lastSavedCrc` → membro privado de `StorageManager` (v3.24.0, commit `8537feb`). |
| CON-005 | 🟢 | F14 | ✅ | `String` → `char[]` em `CliDemand`/`LoginState` (CON-005a `40795d2` + CON-005b `2e98a3e`, v3.24.0). |
| CON-006 | ⚪ | F14 | ✅ | `DS18B20_CONVERSION_TIME_MS` + `DHT22_READ_TIMEOUT_MS` em `SystemDefs.h` (v3.24.0, commit `9690a90`). |
| MEM-001 | 🟡 | F16 | ✅ | `String` em hot paths → buffer estático (IP, MAC, hist). |
| MEM-002 | 🟡 | F14/F16 | ✅ | Coberto por CON-005 (resolvido junto). |
| MEM-003 | ⚪ | F17 | ✅ | `WebUI.h` `#error` guard adicionado em F17 etapa 6 (v3.25.6, commit `ce2becf`); avaliação de remoção descartada (raw layout preservado). |
| PER-001 | 🟢 | F16 | ✅ | Helper `feedWdt()` (29 substituições em 5 arquivos). |
| PER-002 | 🟢 | F16 | ✅ | Upload batching 8 KB — RenderGuard a cada ~8KB vs cada chunk. |
| PER-003 | ⚪ | F16 | ✅ | Fast-path strlen + check extensão early-return. |
| REF-001 | 🟡 | F17 | ✅ | Split `DisplayManager.cpp` em 9 arquivos (F17 etapa 8, v3.25.13, commit `56b1a60`). |
| REF-002 | 🟡 | F17 | ✅ | Split `AppManager.cpp` em 8 arquivos (F17 etapa 2, v3.25.1, commit `5972b6b`). |
| REF-003 | 🟢 | F17 | ✅ | Split `WebManager.cpp` em 8 arquivos (F17 etapa 1, v3.25.0, commit `61c221b`). |
| REF-004 | 🟢 | F14 | ✅ | Singleton `TouchPriority` (v3.24.0, commit `b8b9314`). |
| REF-007 | 🟢 | F17 | ✅ | `handleApiLogin` decomposto em 7 helpers (F17 etapa 3, v3.25.3, commit `e4d72c4`). |
| DOC-002 | 🟢 | F14 | ✅ | `BOOT_WAIT_DOT/ALARM_ROTATE/ALARM_FLASH/WEB_NOTIFY` nomeados + DHT22 unificado (v3.23.11). |
| DOC-003 | ⚪ | F14 | ✅ | `SECURITY.md` raiz criado (v3.24.0, commit `26ac277`); seção CSV adicionada em v3.28.0. |
| WEB-001 | 🟢 | F14 | ✅ | Escape JSON em `/api/ls` (filename+dirname) + auto-test shell `tools/test_web001.sh` (v3.24.0, commit `1826a85`). |
| EXT-001 | 🟡 | F-BUILD | ✅ | `platformio.ini` reproduzível com lib pins exatos (v3.29.1, herdado de v3.26.0). Destrancada EXT-009. |
| EXT-002 | 🟠 | F-CLEANUP | ✅ | Remover `CMD_DBG_SENSOR_HISTORY_ALL` (TEST-ONLY introduzido em v3.24.12). **Pré-release obrigatório.** |
| EXT-003 | 🟡 | F17 | ✅ | Split `SystemDefs.h` (1342 L) em headers temáticos com facade (F17 etapa 4, v3.25.4, commit `ded4c0a`). |
| EXT-004 | 🟡 | F-I18N-TRIM.2 | ✅ | Remover 6 idiomas mortos em `WebUI.h` (72 markers `@LANG_BEGIN`). |
| EXT-005 | 🟢 | F17 | ✅ | `AppManager.h` forward decl + `unique_ptr` (F17 etapa 5 retry, v3.28.12, commit `e7a78d7`). Root cause de regressão histórica (v3.25.5) resolvido: `_tft`/`_ts` sem `= nullptr` em DisplayManager.h. |
| EXT-006 | 🟢 | F16 | ✅ | `LogManager::resetAfterExternalWipe()` substitui `begin()` em runtime. |
| EXT-007 | 🟢 | F-CLEANUP | ✅ | Apagar docblock obsoleto `.csv` em `SystemUtils.cpp`. |
| EXT-008 | 🟢 | F-DOC-EXT | ✅ | `docs/GLOSSARY.md` in-tree para tags do projeto. |
| EXT-009 | 🟢 | F-BUILD | ✅ | Host-side unit tests (Unity / `pio test -e native`). 25/25 PASSED. test/native_stubs + test/test_validators (v3.29.2). |
| EXT-010 | 🟢 | F16 | ✅ | Promover `ReadGuard` para `StorageManager.h`; aplicar em ~5 sites de `AppManager.cpp`. |
| EXT-011 | 🟢 | F-CLEANUP | ✅ | Polish: dup `watchdog_update()`, `extern "C"` redundante, `releaseIdleResources()` no-op. |
| EXT-012 | 🟢 | F-DOC-EXT | ✅ | Atualizar README ("8 languages" → "EN + PT" pós F-I18N-TRIM.1). |
| U25 | 🔴 | F-BT-LOGIN | ✅ | **Defer flash no login BT (2026-04-25):** `LOG_CODE` dentro de `BluetoothManager::update()` disparava `writeCompactToFlash` síncrono com duplo lockout do Core 1. Sob LittleFS >70%, GC + lockout causavam travamento e possível WDT reset. Fix: `LogManager::setForceBuffer(true/false)` wrappando `_btMgr.update()` em `CommandManager::processInput`. Banner de boas-vindas reordenado antes do `LOG_CODE`. Zero novas alocações de heap. |
| U24 | 🔴 | F11 | ✅ | **Commit-all + reboot pattern (2026-04-19):** rajadas de saves consecutivos eram a fonte original de todos os bugs de concorrência (U16/U21/U23). Mudança arquitetural do modelo UX: interface web acumula mudanças no `sessionStorage` client-side; botão único "Salvar e Reiniciar" no topbar (só aparece se há pendentes). Ao clicar: confirmação com aviso de risco → POST `/api/commit_all` com JSON → server aplica tudo em 1 save → reboot limpo. **Phase A.1** (v3.15.0): migrado `/config`. **Phase A.2** (v3.16.0): migrado `/alarms`; `handleApiSaveAlarms` e `handleSaveSystem` grande removidos. **Phase B** (v3.17.0): migrado `/users` como queue de ações (`add`/`del`/`reset`) com overlay visual + botão ↶ de desfazer; `handleApiUserAdd/Del/Reset` removidos. **Phase C** (v3.18.0): migrado `/network` (ssid, pass, dhcp, ip, mask, gw, dns, ntp_server, web_port); detecção de mudança de porta → redirect automático pro novo host:porta após reboot; `handleSaveNetwork` removido. **Phase D** (v3.19.0): `Pending` + `commitAll` + CSS + botão injeção centralizados em `/lang.js` (removidas ~8KB de código duplicado); botão "Salvar e Reiniciar" agora aparece em TODAS as páginas (dash/hist/file/license/cfg/alarms/users/net); versão do firmware exibida ao lado de "SIMUT" (endpoint `/api/perms` extendido com `version`); botão de toggle tema claro/escuro (`#theme-toggle`) com preferência em `localStorage`; paleta de tema claro refinada (slate + cyan-700 AA-contrast) com overrides cobrindo topbar/drawer/cards/inputs/tabelas/chart/badges/calendar/sounds. Todas as 4 páginas de configuração agora compartilham o mesmo padrão; economia total de ~18KB de flash. |
