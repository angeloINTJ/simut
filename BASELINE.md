# BASELINE — Estado do Projeto SIMUT antes da Fase 1 (OTA)

> **Documento gerado pela Fase 0 do `IMPLEMENTATION_PLAN.md`.**
> Base de referência imutável: descreve o estado do projeto **antes** de qualquer alteração funcional para OTA.
> Branch: `feature/ota-self-flash` (criada a partir de `main` no commit `cc0a58d` — v3.37.8).
> Data de geração: 2026-05-04.

---

## 1. Toolchain e Versões

| Item | Valor |
|------|-------|
| **IDE/Build system real** | **PlatformIO Core 6.1.19** (NÃO Arduino IDE — divergência §10.1) |
| **Plataforma** | `https://github.com/maxgerhardt/platform-raspberrypi.git` (fork de `platformio/platform-raspberrypi`) |
| **Plataforma instalada (registry)** | `raspberrypi` v1.18.0 (referência apenas — config usa o fork) |
| **Framework** | `framework-arduinopico` v**1.50403.0** (= **arduino-pico 4.4.3** do earlephilhower) |
| **Board** | `rpipicow` (Raspberry Pi Pico W) |
| **Core build** | `earlephilhower` (selecionado via `board_build.core = earlephilhower`) |
| **Toolchain** | `toolchain-gccarmnoneeabi` (gcc 14.3, **sem suporte LTO** — ver `platformio.ini` REF-001) |
| **Bibliotecas externas pinned** | Adafruit GFX 1.12.6, ILI9341 1.6.3, XPT2046_Touchscreen #d57f64c8, PubSubClient 2.8, OneWirePIO_RP2040 v1.0.0, DHT22PIO_RP2040 v1.0.0, BuzzerPIO_RP2040 v2.5.0 |
| **Upload protocol** | `picotool` |
| **Monitor speed** | 115200 |
| **Pre-build script** | `tools/build_webui_gz.py` (regenera `WebUI_GZ.h`) |
| **Versão de firmware atual** | `SIMUT_VERSION = "v3.37.8"` (definida em `SystemDefs_Limits.h:20`) |

### 1.1 Build flags atuais (`platformio.ini`)

```
-DPICO_CYW43_SUPPORTED=1
-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH
-Os
-fmerge-all-constants
-Wall
-Wextra
-Wno-unused-parameter
-Wno-format-truncation
-Wno-implicit-fallthrough
```

> O `-Os` exigido pelo plano **já está ativo**. As flags `-ffunction-sections -fdata-sections -Wl,--gc-sections` foram testadas e **não produziram redução** (`gcc-arm-none-eabi` do core já as aplica implicitamente para Cortex-M0+ embedded). Resultado idêntico (1.026.704 bytes).

---

## 2. Configuração de Particionamento Flash

| Parâmetro | Valor |
|-----------|-------|
| Flash total Pico W | 2 MB (2.097.152 bytes) |
| `board_build.filesystem_size` (configurado) | `1m` (1.048.576 bytes) |
| Slot do sketch (resultado) | 1.048.576 bytes (1 MB) |
| Teto efetivo de programa reportado pelo PIO | **1.044.480 bytes** (1020 KiB) — diferença de 4 KiB reservada provavelmente para EEPROM emulada do core |
| LittleFS | 1.048.576 bytes (1 MB) |

**Observação crítica:** o teto reportado pelo PIO é `1.044.480` (1020 KiB), não `1.048.576` (1024 KiB). Isso significa que a "região do sketch" usável é 4 KiB menor do que o plano assume. Os endereços do `OTA_LAYOUT_H` precisarão ser ajustados em conformidade ou aquela região de 4 KiB precisa ser explicitamente reclamada — **flag para discussão antes da Fase 4**.

---

## 3. Estrutura do Projeto

### 3.1 Layout (`src_dir = .`, flat)

O projeto usa **layout flat** (todos os `.cpp/.h` na raiz). `src_dir = .` no `platformio.ini` com `build_src_filter` que exclui `.pio/`, `.git/`, `.claude/`, `audits/`, `tools/`, `test/`.

### 3.2 Módulos por subsistema

| Subsistema | Arquivos principais |
|------------|---------------------|
| **Entry point** | `SIMUT.ino` (apenas `setup()` + `loop()` com watchdog) |
| **AppManager (orquestrador)** | `AppManager.h`, `AppManager_Core.cpp`, `AppManager_Boot.cpp`, `AppManager_Loop.cpp`, `AppManager_Events.cpp`, `AppManager_Sensors.cpp`, `AppManager_HistoryAlarm.cpp`, `AppManager_Graph.cpp`, `AppManager_Commands.cpp`, `AppManager_CmdHandlers.cpp` |
| **Web** | `WebManager.h`, `WebManager_Core.cpp`, `WebManager_Auth.cpp`, `WebManager_Api.cpp`, `WebManager_Calib.cpp`, `WebManager_Commit.cpp`, `WebManager_Files.cpp`, `WebManager_History.cpp`, `WebManager_Send.cpp`, `WebManager_Util.cpp` |
| **Display (Core 1)** | `DisplayManager.h`, `DisplayManager.cpp`, `DisplayManager_Auth.cpp`, `DisplayManager_Calendar.cpp`, `DisplayManager_Calibration.cpp`, `DisplayManager_Dashboard.cpp`, `DisplayManager_Graph.cpp`, `DisplayManager_Settings.cpp`, `DisplayManager_Touch.cpp`, `DisplayManager_Alarm.cpp`, `DisplayManager_i18n.cpp`, `DisplayManager_LangParser.cpp`, `DisplayManager_Fonts.{h,cpp}`, `DisplayManager_FmtFloat.h`, `TftWithOffset.h`, `Themes.{h,cpp}`, `FreeSansBold24pt7b_subset.h`, `HelpLicenseEN.h` |
| **Storage (LittleFS)** | `StorageManager.{h,cpp}` (64 KB cpp + 14 KB h — grande e crítico) |
| **Sensores** | `SensorManager.{h,cpp}` |
| **Network/WiFi/MQTT** | `NetworkManager.{h,cpp}`, `TelemetryManager.{h,cpp}` |
| **Bluetooth** | `BluetoothManager.{h,cpp}` |
| **Som** | `SoundManager.{h,cpp}` |
| **Logging/Métricas** | `LogManager.{h,cpp}`, `MetricsManager.{h,cpp}` |
| **CLI/Commands** | `CommandManager.{h,cpp}`, `SystemDefs_Cli.h` |
| **Codec histórico** | `HistoryCodec.{h,cpp}` |
| **Headers de definições** | `SystemDefs.h`, `SystemDefs_Limits.h`, `SystemDefs_Logging.h`, `SystemDefs_Network.h`, `SystemDefs_Records.h`, `SystemDefs_Reserved.h`, `SystemDefs_Time.h`, `SystemDefs_Validate.h`, `TouchPriority.h` |
| **WebUI assets** | `WebUI.h` (HTML/CSS/JS bruto, ~288 KB), `WebUI_GZ.h` (gzip, ~460 KB — gerado por `tools/build_webui_gz.py`, **não tracked**) |
| **Utilidades** | `SystemUtils.cpp`, `SystemUtils` em headers |

### 3.3 Pastas auxiliares

- `data/` — fonte da LittleFS (ver §5).
- `docs/` — documentação adicional.
- `tools/` — scripts Python auxiliares (build_webui_gz, test-server, etc.).
- `test/` — testes Unity (env `native` em `platformio.ini`).
- `audits/` — relatórios de auditoria histórica.
- `lib/` — **NÃO existe ainda** (Fase 3 vai criar `lib/uzlib/`).
- `src/` — **NÃO existe ainda** (o plano usa `src/ota/...`; o projeto atual é flat. Decisão necessária na Fase 1: aceitar `src/ota/` como subpasta nova OU manter flat com prefixo `Ota_*.cpp`. Ver divergência §10.2).

---

## 4. Servidor Web e Rotas Existentes

**Tipo:** Arduino `WebServer` (síncrono, do core arduino-pico — `#include <WebServer.h>` em `WebManager.h:19`).
**Porta:** 80 (configurável via `webPort`).
**Concorrência:** sessões múltiplas (até 3 simultâneas), com SendGuard/RenderGuard/HeavyTaskGuard, rate-limit por IP, sessão challenge-response com HMAC-SHA256.
**Total de handlers registrados:** **48** (em `WebManager_Core.cpp:64-124`, mais o `onNotFound`).

### 4.1 Rotas atuais (snapshot — `WebManager_Core.cpp`)

#### Páginas HTML
- `GET /` — handleRoot
- `GET /login` — handleLogin
- `GET /logout` — handleLogout
- `GET /force_chpass` — handleForceChpass
- `GET /config` — handleConfig
- `GET /network` — handleNetwork
- `GET /users` — handleUsers
- `GET /files` — handleFiles
- `GET /alarms` — handleAlarms
- `GET /license` — handleLicense
- `GET /history` — handleHistory

#### Assets
- `GET /lang.js` — handleLangJs
- `GET /style.css` — handleStyleCss
- `GET /favicon.ico` — handleFavicon (servido do LittleFS)
- `GET /apple-touch-icon.png` — 204 inline

#### API — autenticação
- `GET /api/login_init` — handleApiLoginInit
- `POST /api/login` — handleApiLogin
- `POST /api/force_chpass` — handleApiForceChpass
- `POST /api/login_chpass` — handleApiLoginChpass
- `GET /api/perms` — handleApiPerms
- `GET /api/sec_status` — handleApiSecStatus

#### API — leitura de estado
- `GET /api/status` — handleApiStatus (inclui `SIMUT_VERSION`)
- `GET /api/network` — handleApiNetwork
- `GET /api/config` — handleApiConfig
- `GET /api/users` — handleApiUsers
- `GET /api/themes` — handleApiThemes
- `GET /api/alarms` — handleApiAlarms
- `GET /api/lang` — handleApiLang
- `GET /api/calib` — handleApiCalibGet
- `GET /api/logs` — handleApiLogs
- `GET /api/screenshot` — handleApiScreenshot

#### API — escrita / commit
- `POST /api/calib` — handleApiCalibPost
- `POST /api/save_sys` — handleSaveSystem
- `POST /api/commit_all` — handleApiCommitAll
- `POST /api/reset_touch_cal` — handleResetTouchCal
- `POST /api/clear_logs` — handleApiClearLogs
- `POST /api/set_time` — handleApiSetTime

#### API — histórico
- `GET /api/history_multi` — handleApiHistoryMulti
- `GET /api/history_days` — handleApiHistoryDays
- `GET /api/export/history.bin` — handleApiExportHistory (bundle .simx)
- `GET /api/export/logs.bin` — handleApiExportLogs (bundle .simx)

#### API — files (LittleFS)
- `GET /download` — handleDownload
- `POST /api/delete` — handleDelete
- `GET /api/ls` — handleApiLs
- `POST /api/mkdir` — handleApiMkdir
- `POST /api/upload` — handleUploadComplete (+ handleUploadData callback, com batch buffer 8 KB)

#### Catch-all
- `onNotFound` → handleNotFound

### 4.2 Padrões importantes a preservar

- **SendGuard:** timer hardware que estende o WDT durante envios longos (definido em `WebManager.h:23-35`). Qualquer rota OTA nova precisa usar isso ao streamar.
- **RenderGuard / HeavyTaskGuard:** RAII guards que pausam o display e bloqueiam tasks pesadas durante operações sensíveis. **Crítico para `/api/firmware/*` na Fase 5.**
- **TouchPriority::isActive():** rejeita requests com 503 quando o usuário está usando a tela física (`rejectIfTouchPriority()`). Aplicar nos endpoints OTA destrutivos.
- **`_uploadBatchBuf[8192]`:** já há infraestrutura de upload em batches de 8 KB com flush para LittleFS — reutilizar padrão na Fase 5.
- **WebUI já é gzip:** `WebUI_GZ.h` é servido com `Content-Encoding: gzip` quando `_clientAcceptsGzip == true` (ver `safeSend_GZ`). Bom precedente — o cliente já aceita binários gzip.

---

## 5. Conteúdo Atual da LittleFS (`data/`)

Total: **22 arquivos**, ~501 KB (49% da LittleFS de 1 MB).

| Caminho | Tamanho | Notas |
|---------|---------|-------|
| `/favicon.ico` | 11.047 B | servido em `GET /favicon.ico` |
| `/lang/language_es-ES.lng` | 18.188 B | F-LANGPACK |
| `/lang/language_pt-BR.lng` | 22.757 B | F-LANGPACK |
| `/history/20260411.bin` | 22.037 B | histórico diário codificado por `HistoryCodec` |
| `/history/20260412.bin` | 21.983 B | (idem) |
| `/history/20260413.bin` | 21.949 B | |
| `/history/20260414.bin` | 21.948 B | |
| `/history/20260415.bin` | 21.972 B | |
| `/history/20260416.bin` | 21.994 B | |
| `/history/20260417.bin` | 21.995 B | |
| `/history/20260418.bin` | 20.891 B | |
| `/history/20260419.bin` | 14.338 B | |
| `/history/20260420.bin` | 16.143 B | |
| `/history/20260421.bin` | 19.156 B | |
| `/history/20260422.bin` | 21.988 B | |
| `/history/20260423.bin` | 21.958 B | |
| `/history/20260424.bin` | 21.911 B | |
| `/history/20260425.bin` | 21.122 B | |
| `/history/20260426.bin` | 20.607 B | |
| `/history/20260427.bin` | 22.167 B | |
| `/history/20260428.bin` | 22.068 B | |
| `/history/20260429.bin` | 21.274 B | |

> **Observação:** o `data/` no repositório é o **template inicial** (o que o operador faz upload via `pio run -t uploadfs`). Em campo, a LittleFS contém também: `system.bin` (config persistente), `users.csv`, `network.csv`, `calib.csv`, e demais artefatos gerados pelo StorageManager. O `tools/verify_backup.py` da Fase 1 precisa lidar com qualquer conteúdo, não apenas o template.

> **Lição aprendida (memória `feedback_fs_backup_first`):** `pio run -t uploadfs` apaga TODA a LittleFS — antes de qualquer mudança, baixar o FS via API. Isso reforça por que a Fase 1 (backup) é crítica antes da Fase 4 (staging).

---

## 6. Tamanho do Binário Atual

### 6.1 Build atual sem alterações (`pio run -e pico_w_release`)

| Métrica | Valor |
|---------|-------|
| **Flash usado** | **1.026.704 bytes** (≈ 1003 KiB) |
| **Flash teto reportado** | 1.044.480 bytes (1020 KiB) |
| **Ocupação** | **98,3%** |
| **Margem livre** | **17.776 bytes (≈ 17,4 KiB)** |
| RAM usado (.data + .bss) | 107.464 bytes (41,0% de 256 KiB) |
| `firmware.bin` (output) | 1.042.828 bytes (inclui boot2 + alinhamento) |
| `firmware.uf2` | 2.085.888 bytes |

### 6.2 Validação de flags adicionais

Testado: `-ffunction-sections -fdata-sections` (via `PLATFORMIO_BUILD_FLAGS`). **Resultado idêntico** (1.026.704 bytes) — o core já aplica essas flags por padrão.

### 6.3 Stub uzlib

**Tarefa 6 da Fase 0 — POSTERGADA para Fase 3** com justificativa registrada:

- O plano pede um "stub não-funcional (apenas inclusões e símbolos vazios)" para medir overhead. Mas com `--gc-sections` ativo (já é o caso), símbolos sem referência são descartados pelo linker — a medição daria zero, sem valor preditivo.
- Para forçar a inclusão, eu teria que adicionar uma referência real em código de produção (poluindo o baseline) **ou** desabilitar `--gc-sections` (alterando o build para gerar baseline artificial).
- **Decisão:** medir o overhead real na Fase 3, quando `lib/uzlib/` é vendored e referenciado por `src/ota/decompressor.cpp`. Nessa hora, a comparação é meaningful.
- **Estimativa documentada:** uzlib típica adiciona **~2-3 KB de código** ao binário final. Com 17,4 KB de margem atual, o gap esperado é confortável (~14 KB para o resto da lógica OTA).

> **Risco aberto:** se o conjunto OTA inteiro (uzlib + applier + máquina de estados de upload + validação + backup) ultrapassar 17 KB, será necessário escolher entre (a) reduzir features existentes; (b) revisar o split de partições; ou (c) postergar a entrega do OTA. **Acionar o gate humano da seção 8 do plano se isso ocorrer.**

---

## 7. Versionamento

| Item | Valor |
|------|-------|
| Macro | `#define SIMUT_VERSION "v3.37.8"` em `SystemDefs_Limits.h:20` |
| Uso | `WebManager_Api.cpp` (status JSON), `DisplayManager.cpp` (boot screen), `BluetoothManager.cpp` (BT info), `AppManager_Boot.cpp` (Serial), `CommandManager.cpp` (CLI banner/info) |
| Política (memória `feedback_versioning`) | bump **patch** por commit; bump **minor** ao fechar fase; tag git apenas no bump de minor |

**Sem `version.h` dedicado** — a versão vive no `SystemDefs_Limits.h` junto com outros limits. **Decisão para a Fase 1:** o `BackupHeader.firmware_version` precisa ser um `uint32_t` (campo do plano §5.1), mas `SIMUT_VERSION` é uma string. Vamos precisar de um helper que serialize "v3.37.8" → uint32 (ex.: encoding `(major<<16) | (minor<<8) | patch` = 0x030F08 para v3.37.8). **Definir formato exato na Fase 1.**

---

## 8. Uso de Segundo Core (Core 1)

**SIM — o projeto usa Core 1.** Ocorrências em `DisplayManager.cpp`:

- `DisplayManager.cpp:166` — `void DisplayManager::startCore1() { multicore_launch_core1(core1Entry); }`
- `DisplayManager.cpp:174` — relaunch após reset
- `DisplayManager.cpp:329` — relaunch em path de recuperação
- `DisplayManager.cpp:570` — relaunch em outro path
- `DisplayManager.cpp:509` — comentário de documentação

**Função `core1Entry`:** todo o rendering do display TFT roda em Core 1 (renderloop dedicado, sem flash writes diretos do Core 1, mas faz acesso a flash XIP para fontes e assets).

**Implicação CRÍTICA para a Fase 7 (aplicador SRAM):**
- `multicore_lockout_start_blocking()` é **obrigatório** antes de qualquer flash erase/write no Core 0.
- Se Core 1 estiver lendo bytes via XIP no momento do erase, **a leitura retornará lixo e provavelmente travará o Core 1**.
- O plano já reconhece isso (R5 da seção 7), mas agora está confirmado em hardware real: **Core 1 está ativo o tempo todo neste projeto**.

**Implicação para a Fase 4 (staging):**
- Mesmo durante `ota_staging_erase_all()` (apaga 1020 KB da LittleFS), Core 1 não pode estar acessando essa região via XIP. A LittleFS já estará desmontada nesse ponto, mas o Core 1 pode estar lendo fontes/assets que vivem na flash do **slot da app** — verificar se há leitura cruzada na fase de write de staging.

---

## 9. Bibliotecas em Uso (relevantes ao OTA)

| Biblioteca | Uso atual | Relevante para OTA? |
|------------|-----------|---------------------|
| `WiFi` (`#include <WiFi.h>`) | NetworkManager, MQTT | **Sim** — `WiFi.end()` antes de aplicar |
| `WebServer` (`#include <WebServer.h>`) | Toda a UI/API | **Sim** — adicionar rotas `/api/firmware/*` e `/api/backup` |
| `LittleFS` (via core) | StorageManager (configs, history, lang, favicon) | **Sim** — desmontar antes do staging, formatar pós-update |
| `bearssl` (BearSSL hash, `bearssl_hash.h`) | Auth challenge-response, password hashing | **Não** diretamente — mas a função CRC32 da Fase 1 não vai usá-lo |
| `multicore.h` (Pico SDK) | DisplayManager Core 1 | **Sim** — `multicore_lockout_start_blocking` na Fase 7 |
| `hardware/watchdog.h` | SIMUT.ino, LogManager | **Sim** — `watchdog_reboot` no fim do applier |
| `hardware/flash.h` | (não usado diretamente ainda) | **Sim** — Fase 4+ |
| **NÃO está em uso:** `uzlib`, `tinfl`, `miniz`, qualquer descompressor | — | **Adicionar na Fase 3** |

---

## 10. Divergências do Plano (`IMPLEMENTATION_PLAN.md`)

### 10.1 IDE: PlatformIO em vez de Arduino IDE

- **Plano (1.2):** "IDE: Arduino IDE 2.x (Ubuntu)".
- **Realidade:** PlatformIO 6.1.19, com config completa em `platformio.ini`.
- **Impacto:** o plano fala em "Tools → Flash Size" (menu da Arduino IDE). A configuração equivalente é `board_build.filesystem_size = 1m` em `platformio.ini`. Trocar referências.
- **Ação para próximas fases:** todas as instruções de "compilar/flashar" usam `pio run -e pico_w_release` e `pio run -e pico_w_release -t upload`.
- **Coexistência:** existe também um `build/rp2040.rp2040.rpipicow/` com binários da Arduino IDE (1.038.732 B). Histórico, não usado no fluxo atual.

### 10.2 Layout flat vs. `src/ota/`

- **Plano (Fases 1-7):** assume `src/ota/backup_format.h`, `src/ota/applier.cpp`, etc.
- **Realidade:** projeto é flat (`src_dir = .`). Não há `src/`.
- **Decisão pendente para Fase 1:** opção A: criar `src/ota/` e ajustar `build_src_filter` para incluir; opção B: usar prefixo flat (`OtaBackupFormat.h`, `OtaApplier.cpp`...).
- **Recomendação:** opção A (criar `src/ota/`) — agrupa todos os ~10 novos arquivos, isola da raiz que já tem ~70 arquivos. Pedir confirmação antes da Fase 1.

### 10.3 Margem de flash menor que o plano antecipa

- **Plano (Sumário Executivo):** "a app atual ocupa ~99% desse slot" (1015 KB sobre 1024).
- **Realidade:** 98,3% sobre **1020 KiB** (1.026.704 B / 1.044.480 B). Margem livre real: **17,4 KiB**.
- **Impacto:** a Fase 0 do plano pede checar que stub uzlib mantém abaixo de 1 MB. Isso é true (o stub é zero). Mas o conjunto **completo** OTA tem que caber em ~17 KiB — apertado, viável, sem folga.
- **Risco R1 do plano (5.7) é REAL e deve ser ativamente monitorado em cada commit das fases 3-7.**

### 10.4 Reserva de 4 KiB no fim do slot

- O PIO reporta teto de 1.044.480 (1020 KiB), não 1.048.576 (1024 KiB). Os 4 KiB finais do slot do sketch parecem reservados (provavelmente EEPROM emulada do core arduino-pico).
- **Impacto:** o plano §3 define `OTA_METADATA_OFFSET = OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE = 0x101FF000`. Mas isso assume slot do sketch = 1024 KiB e LittleFS começando em `0x10100000`. Confirmar particionamento real do core arduino-pico (gerar map file na Fase 4).
- **Ação:** verificar `arduino-pico` source para `eeprom_data` partition e ver se conflita com `OTA_METADATA_OFFSET`. **Bloqueante para a Fase 4.**

### 10.5 Já existe pipeline de gzip para a UI

- **Plano (ADR-004):** "O cliente envia `.bin.gz`; a aplicação descomprime durante o self-flash".
- **Realidade:** projeto **já gzipa a WebUI** (`tools/build_webui_gz.py` gera `WebUI_GZ.h` em build-time, servida pela rota com `Content-Encoding: gzip`). Há expertise.
- **Implicação positiva:** o `tools/build_release.sh` da Fase 10 pode reutilizar a infra de gzip existente.

### 10.6 Já existe rota `POST /api/upload` (LittleFS files)

- **Plano (Fase 5):** define rotas novas `POST /api/firmware/{begin,chunk,commit,abort}`.
- **Realidade:** já há `POST /api/upload` para enviar arquivos arbitrários para a LittleFS, com batch buffer de 8 KB e flush incremental.
- **Decisão:** **não** reutilizar `/api/upload` (mistura semântica perigosa). Criar rotas separadas e usar a infraestrutura existente (`_uploadBatchBuf`, `handleUploadData` callback) como referência.

### 10.7 SECURITY.md existe e é mantido (memória `feedback_security_md_review`)

- **Implicação:** ao adicionar rotas `/api/firmware/*` e `/api/backup`, **revisar `SECURITY.md`** (auth, RBAC, audit log, threat surface).

---

## 11. Riscos Adicionais Identificados na Descoberta

| # | Risco | Nota |
|---|-------|------|
| RD1 | EEPROM emulada no fim do slot pode conflitar com `OTA_METADATA_OFFSET` | Verificar antes da Fase 4 |
| RD2 | Core 1 (DisplayManager) lê fontes via XIP — qualquer erase do app slot vai travar | Lockout obrigatório; já contemplado no plano R5 |
| RD3 | `WebUI_GZ.h` é gerado por `build_webui_gz.py` e ocupa ~460 KB do binário — se ele também precisar mudar com OTA, considerar split | Aceitável por enquanto; cabe no slot |
| RD4 | StorageManager é grande (64 KB cpp) e crítico para configs — restore na Fase 2 precisa entender o `system.bin`/`users.csv`/`network.csv`/`calib.csv` para validação semântica (ou apenas restaurar bytes brutos) | Decidir escopo na Fase 2 |
| RD5 | TelemetryManager (52 KB cpp) é cliente MQTT — `WiFi.end()` no apply precisa fechar MQTT primeiro pra não travar | Verificar ordem de shutdown na Fase 7 |
| RD6 | BluetoothManager está ativo (`PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`) — também precisa shutdown limpo antes do apply | Adicionar `BluetoothManager.end()` na sequência da Fase 7 |
| RD7 | `_uploadBatchBuf[8192]` em RAM da WebManager — sessão OTA vai precisar ainda mais buffer; medir RAM antes/depois | Atual: 107 KB. Folga: 149 KB |
| RD8 | Comentário em `platformio.ini` (REF-001): toolchain sem LTO → splitar `DisplayManager.cpp` adiciona 2 KB/cpp e estoura flash | **Não** dividir arquivos existentes ao integrar OTA |

---

## 12. Status dos Critérios de Aceitação da Fase 0

| Critério (plano §6 Fase 0) | Status |
|----------------------------|--------|
| `BASELINE.md` existe e responde a todas as perguntas da seção 1.4 | **OK** (este documento) |
| Tamanho do `.bin` com `-Os` documentado e abaixo de 1 MB | **OK** (1.026.704 B sobre teto 1.044.480 — 98,3%) |
| Adição do stub do `uzlib` ainda mantém o `.bin` abaixo de 1 MB | **DIFERIDO** (justificativa em §6.3 — medição na Fase 3) |
| Firmware compilado funciona normalmente em hardware (regressão zero) | **PENDENTE TESTE EM HARDWARE** — nenhuma alteração funcional foi feita; apenas branch nova, build idêntico |

### 12.1 Teste em hardware obrigatório (Fase 0)

**O usuário (operador humano) precisa:**

1. Garantir que está na branch `feature/ota-self-flash` (já está — `git status` confirma).
2. Compilar e fazer upload com:
   ```
   /home/angelo/Documentos/SIMUT/.venv/bin/python3 -m platformio run -e pico_w_release -t upload
   ```
3. Validar funcionamento normal:
   - **Boot:** vê "v3.37.8" na splash do TFT, conecta no WiFi, monitor serial saudável.
   - **Display:** dashboard renderiza, leituras de sensores aparecem.
   - **Web:** acessa `http://<IP>` → login funciona → dashboard carrega → `/api/status` retorna JSON.
   - **Files:** `/files` lista o conteúdo da LittleFS (favicon, lang, history).
   - **Bluetooth:** comando `info` via SerialBT retorna versão.
   - **Touch:** trocar tela no TFT funciona.
4. **Critério de aprovação:** zero regressão. Como nenhum código foi modificado, é puramente verificar que o build idêntico ainda funciona (sanity check de baseline antes de começar a tocar nas próximas fases).

### 12.2 Após validação em hardware

**Solicitar ao supervisor humano:** confirmar (a) regressão zero confirmada em campo; (b) decisão sobre §10.2 (`src/ota/` vs flat); (c) aprovação para iniciar a Fase 1.

---

## 13. Sumário para o Supervisor Humano

**Fase 0 cumprida no que é estaticamente verificável.** Branch criada, baseline documentado, build de baseline confirmado em **1.026.704 bytes (98,3% de ocupação, 17,4 KB de margem)**. Nenhuma alteração funcional feita.

**Pontos que requerem sua decisão antes da Fase 1:**

1. **Layout de arquivos OTA:** `src/ota/` (recomendado) ou `Ota_*.cpp` flat? (§10.2)
2. **Encoding do `firmware_version` uint32 no backup:** `(major<<16) | (minor<<8) | patch` (sugerido)? (§7)
3. **Validação em hardware:** confirmar que o build atual da branch `feature/ota-self-flash` (idêntico ao `main` em conteúdo de código, só branch nova) ainda boota e opera 100% no Pico W de teste.

**Pontos que requerem sua atenção pré-Fase 4 (não bloqueantes da Fase 1):**

- §10.4 — Investigar particionamento real (EEPROM emulada nos 4 KiB finais).
- §11 RD1 — Verificar conflito `OTA_METADATA_OFFSET` com EEPROM emulada.

**Aguardo sua aprovação para prosseguir à Fase 1 (Formato e Geração de Backup).**
