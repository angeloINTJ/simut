# Glossário de Tags — SIMUT

*Tags de versão como v3.x/v4.x em entradas históricas referem-se ao esquema de numeração pré-1.0 do projeto, mantidas por proveniência; o firmware atual segue a linha 2.x.*

[English](GLOSSARY.md) | **Português** | [Español](GLOSSARY.es-ES.md)

> Dicionário de tags inline usadas em comentários do código-fonte. Consulte este arquivo antes de interpretar qualquer tag como `F-*`, `BUG-*`, `SEC-*`, `Patch X`, `Fase N`, `#N` encontrada nos fontes.

## Como usar

Cada tag aparece em comentários no formato:

```cpp
/* F-LOCKOUT-STUCK: explicação curta do que este bloco resolve. */
/* Patch C: cast signed para tolerar cross-core race. */
```

Este glossário decodifica o significado de cada tag. Se você encontrar uma tag não listada aqui, ela pode ser nova — adicione-a seguindo o padrão abaixo.

---

## Tags de Feature (F-*)

| Tag | Descrição |
|-----|-----------|
| `F-LOCKOUT-STUCK` | Refactor cross-core: Core 0 não conseguia pegar `multicore_lockout` enquanto Core 1 estava em path pesado. Solução: quiet mode cooperativo com hard-reset do Core 1 (`multicore_reset_core1`) + re-launch. Touch re-init a cada launch, TFT begin only-on-first. |
| `F-I18N-TRIM.1` | Redução de 8 para 2 idiomas no display TFT (EN + PT) para economizar flash. Firmware desde v3.22.0. |
| `F-I18N-TRIM.2` | Remoção dos 6 idiomas mortos (es/de/fr/it/ru/zh) do `WebUI.h`, consistente com F-I18N-TRIM.1. |
| `F-NET-TIME.1` | Overlay `NetworkTimeData` em `reserved[28..47]` — dados de configuração de rede/hora. |
| `F-NET-TIME.2` | Consumer no `NetworkManager` — flags DNS/NTP lidas do overlay. |
| `F-NET-TIME.3a` | Back-end web: GET/POST `/api/set_time` + `setManualTime()`. |
| `F-NET-TIME.3b` | Front-end: página `/network` com DNS separado + `/config` com data/hora + i18n PT. |
| `F-NET-TIME.4` | CLI: comandos `conf ntp`, `conf time`, `conf net dns` + tokenizer 5→6 slots. |
| `F-NET-TIME.5a` | Cursor-no-futuro auto-reset + hint `t_int=0` na UI. |
| `F-NET-TIME.5b` | Fechamento — regressão acumulada ao longo do path F-NET-TIME. |
| `F-IP-FIX` | Correção de assinatura de `WiFi.config`: arduino-pico usa `(ip, dns, gateway, subnet)`, diferente do ESP32. |
| `F-BT-LOGIN` | Defer flash no login Bluetooth: `LOG_CODE` bufferizado em RAM durante `_btMgr.update()`, eliminando lockout do Core 1 + risco de WDT reset. |

## Tags de Bug (BUG-*)

| Tag | Descrição |
|-----|-----------|
| `BUG-002` | Ordem de publicação cross-core: producer deve gravar **dados** antes da **flag**, com `__dmb()` entre os dois. Consumer lê flag + `__dmb()` + dados. |
| `BUG-003` | Chunking granular de `enterFlashSafeMode`/`exitFlashSafeMode`. Antes: 1 lockout cobria tudo. Agora: 1 lockout por operação LittleFS, Core 1 renderiza entre chunks. Macro `FLASH_OP(...)` em `StorageManager.cpp`. |
| `BUG-004` | Mutex `_stateMutex` pode falhar `mutex_try_enter`; usar `_lastWebBusy` como fallback sticky — evita flicker do overlay "web busy". |
| `BUG-005` | `captureBootSnapshot()` público + chamada explícita em `LogManager::begin()`. `setModule` não captura mais oportunisticamente. Guard `_autopsyPerformed` em `performCrashAutopsy()` evita falsa autópsia em chamadas subsequentes de `begin()` (ex: `clear log`). |

## Tags de Segurança (SEC-*)

| Tag | Descrição |
|-----|-----------|
| `SEC-001` | Path traversal em upload: `isSafeUploadFilename()` bloqueia `..`, `\`, `:`, `<`, `>`, `|`, `?`, `*`, `%`, controle, len>64. |
| `SEC-002` | Bypass de `replace("..","")` com `....` ou `%2e%2e`: rejeição por `indexOf("..")>=0` + `indexOf('%')>=0`. |
| `SEC-003` | Senha admin aleatória 8 chars `[A-Z2-9]` via `rp2040.hwrand32()` no factory reset. Plaintext em RAM (`_initialAdminPassword[9]`), nunca em flash. |
| `SEC-004` | PIN `1234` força troca: flag `FLAG_MUST_CHANGE_PIN` em `reserved[26..27]`, overlay `SetupFlagsData` com magic `0xBE`. |
| `SEC-005` | Limite de linha CLI: `CLI_LINE_MAX=256`, helper `appendCharWithLimit()`, anti-spam de warning. |
| `SEC-006` | LRU evict de `_loginStates[16]` preserva slots com lockout ativo não-expirado. |
| `SEC-007` | Hash de senha 120→128 bits (32 hex chars). Migração transparente: login detecta hash antigo (30 chars), valida com truncate, re-hash silencioso. |
| `SEC-008` | `PASSWORD_HMAC_ROUNDS` — número de rounds HMAC-SHA256 para hash de senha. |
| `SEC-009` | Salt random por usuário (`UserAccount.salt[8]`) via `hwrand32()`. Bump `CONFIG_VERSION` com rotina de migração. |

## Tags de Consistência (CON-*)

| Tag | Descrição |
|-----|-----------|
| `CON-001` | Bloco autoritativo `SCRATCH REGISTER MAP` em `LogManager.cpp` — mapa único dos scratch registers do watchdog. |
| `CON-002` | Enum `LanguageCode` com `LANG_EN`, `LANG_PT`, `LANG_COUNT` sentinela + `static_assert` contra `LANG_NAMES`. |
| `CON-003` | Headers "8 languages" → "2 languages (EN + PT)" em `DisplayManager.{h,cpp}`. |
| `CON-004` | `_lastSavedCrc` como membro privado de `StorageManager` (era `static` local). Skip de save quando CRC32 idêntico. |
| `CON-005a` | `LoginState.nonce` como `char[65]` em vez de `String`. |
| `CON-005b` | `CliDemand.strVal1/strVal2` como `char[64]` com helpers `setStrVal1/setStrVal2`. |
| `CON-006` | `DS18B20_CONVERSION_TIME_MS`, `DHT22_READ_TIMEOUT_MS` movidos de macros locais para `SystemDefs.h`. |

## Tags de Documentação (DOC-*)

| Tag | Descrição |
|-----|-----------|
| `DOC-002` | Constantes de timing nomeadas: `BOOT_WAIT_DOT_INTERVAL_MS`, `ALARM_ROTATE_INTERVAL_MS`, `ALARM_FLASH_INTERVAL_MS`, `WEB_NOTIFY_DURATION_MS`. |
| `DOC-003` | `SECURITY.md` na raiz com threat model, defesas, operações e resposta a incidente. |

## Tags de Refatoração (REF-*)

| Tag | Descrição |
|-----|-----------|
| `REF-004` | Singleton `TouchPriority` com `setProvider`/`isActive` — substitui 3 setters + 3 membros + 3 lambdas duplicadas em 5 managers. |
| `REF-007` | Decomposição de `handleApiLogin` (~130 linhas) em 6 helpers: `findLoginStateForIp`, `checkLockout`, `validateNonce`, `verifyPasswordFor`, `allocSessionSlot`, `completeLogin`. |

## Patches (Patch X)

| Tag | Descrição |
|-----|-----------|
| `Patch A` | Autópsia: guarda `elapsed` real (tempo desde último heartbeat) em `scratch[7]` em vez de `now - moduleStartTime`. |
| `Patch B` | (Reservado — não implementado) |
| `Patch C` | Cast signed `(int32_t)(now - lastBeat)` em `checkCrossCoreHealth` e `AppManager:446`. Tolera cross-core race onde `lastBeat` fica levemente adiantado → underflow ≈ UINT32_MAX causava falso-positivo de soft panic. |

## Fases (Fase N)

| Tag | Descrição |
|-----|-----------|
| `Fase 1` | Graph: remover pontos antigos que saíram da janela de visualização. |
| `Fase 2` | Graph: ler novos registros do arquivo binário de histórico. |
| `Fase 3` | Graph: recalcular estatísticas (ignorando NANs). |
| `Fase 4` | CLI deferral durante touch: comandos USB/BT enfileirados em ring buffer de 2 slots enquanto touch ativo. Drain 1-por-loop no topo de `update()`. |
| `Fase 5` | Flush pós-toque coordenado: na transição touch-active→touch-free, dispara flush em série de logs → hist record → cursor. Fecha janela "dado em RAM não em flash" de minutos para <100ms. |

## Tickets numéricos (#N)

| Tag | Descrição |
|-----|-----------|
| `#4` | Sufixo `confirm` obrigatório na CLI para comandos sensíveis. |
| `#5` | Ofuscação de campos sensíveis em logs e console. |
| `#7` | Paridade CLI ↔ Web: comandos disponíveis em ambos os canais. |
| `#8` | Heap high-water mark: tracking de mínimo histórico de heap livre. |
| `#11` | Flag `intVal1Valid` em `CliDemand` para distinguir "não informado" de "valor 0". |

## Outros

| Tag | Descrição |
|-----|-----------|
| `U3` | Porta web configurável (não hardcoded 80). |
| `U11` | Heartbeat de falha suprimida: 1 log/hora após supressão de logs de telemetria. |
| `U14` | Watchdog false-positive: underflow em unsigned subtract cross-core. Corrigido com cast signed (Patch C). |
| `U15` | WDT feeds entre operações LittleFS em `writeCompactToFlash`/`flushPendingLogs`. |
| `U16` | Rajadas de save web: watchdog 30s, CRC skip, rate-limit 1s, dirty tracker client-side. |
| `U17` | Toast de notificação na web UI: infra compartilhada em 9 páginas, i18n PT. |
| `U18` | CLI deferral durante touch (Fase 4): ring buffer de 2 slots, drain pós-toque. |
| `U19` | Flush pós-toque (Fase 5): logs, hist record, cursor — fecha janela de vulnerabilidade. |
| `U23` | Instrumentação de autópsia: `MOD_SAVE_CONFIG`, `MOD_LOG_FLASH`, `MOD_HIST_FLASH`, `MOD_CORE1_LOCK` + `TraceScope` RAII. |
| `U24` | Batch commit-all: interface web acumula mudanças no `sessionStorage`, botão único "Salvar e Reiniciar". |
| `U25` | Flash deferido no login Bluetooth: `setForceBuffer(true/false)` wrappando `_btMgr.update()`. |
| `WEB-001` | JSON escape em `/api/ls`: filename e dirname escapados para bytes de controle (0x00-0x1F/0x7F). |
| `N9` | Cert TLS >16 KB rejeitado no boot para evitar OOM. |
| `F12.1–F12.5` | Endurecimentos de segurança (ver SEC-001 a SEC-005). |
| `F13.1–F13.4` | Bugs latentes (ver BUG-002 a BUG-005). |
| `F15.1–F15.2.a` | Hash migration: LRU evict (SEC-006), schema bump v14→v15 (SEC-009). |

---

## Como adicionar novas tags

1. Escolha um prefixo consistente com a categoria: `F-` para features, `BUG-` para bugs, `SEC-` para segurança, `CON-` para consistência, `DOC-` para documentação, `REF-` para refatoração, `EXT-` para achados externos.
2. Adicione a tag neste arquivo com uma linha descrevendo o que ela significa.
3. Use a tag em comentários inline no formato: `/* TAG: descrição curta. */`
4. Mantenha este glossário como fonte autoritativa — se uma tag ficar obsoleta, marque com `[OBSOLETO]` em vez de remover.

> **Nota:** Tags `EXT-*` referem-se a achados de auditoria externa. Elas são convertidas para o prefixo adequado (`F-`, `SEC-`, etc.) quando implementadas.
