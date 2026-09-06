# SIMUT Air — Esboço de Projeto (build headless com hibernação)

> **Status:** Implementado na branch `feature/simut-air`.
> **Nota de implementação:** a hibernação usa **SLEEP (deep sleep via WFI)**, e não
> DORMANT — o modo DORMANT (escrita `"coma"` no ROSC) mostrou-se não-determinístico
> na bancada (corre contra o sincronizador lento do ROSC/clk_rtc e acorda na hora
> errada). Ver §5.
> **Branch:** `feature/simut-air`
> **Base:** `main` (v2.3.9-beta — `SIMUT_VERSION` em `src/SystemDefs_Limits.h`).
> **Idioma:** pt-BR (espelha `docs/analysis/ANALISE_*.md`).

---

## 0. Decisões confirmadas (incorporadas nesta revisão)

| # | Decisão | Impacto no desenho |
|---|---|---|
| D1 | Modo **dormant** (não `sleep`) | ~~hibernação profunda, SRAM perdida~~ → na implementação virou **SLEEP (deep sleep)**; ver §5 |
| D2 | Configuração via **serial + bluetooth + web** | o Air mantém os 3 canais de configuração ativos no modo inicial |
| D3 | Ao ligar na alimentação = **SIMUT Alpha sem display** (modo operacional) | cold boot entra em M0 com stack completo |
| D4 | Entra em hibernação **por comando** (serial/BT/web) ou **após 5 min sem comando** | M0 tem timer de inatividade + comando explícito |
| D5 | `PromMetrics`/`Syslog`/`HaDiscovery` **só no modo inicial** | no ciclo de hibernação ficam desligados |
| D6 | **`CONFIG_VERSION` NÃO muda** (fica 21) | config do Air vai para arquivo separado no LittleFS |
| D7 | **Intervalo de wake configurável** via web e serial | novo arquivo de config do Air + CLI `air` + seção web |

---

## 1. Objetivo

**SIMUT Air** é a variante do SIMUT para Raspberry Pi Pico W **sem display**, com um
**ciclo de hibernação em modo dormant**: o pico passa a maior parte do tempo dormindo e,
em **períodos definidos**, acorda para:

1. **acordar** (alarme do RTC);
2. **ler os sensores até a estabilização** (janela de média aparada completa);
3. **em paralelo, verificar se a rede Wi-Fi configurada está presente**;
4. **sem rede** → armazena os valores (histórico local) e volta a hibernar;
5. **com rede** → conecta, envia a telemetria pendente, volta a hibernar.

A diferença em relação à v1 do esboço: o Air **não nasce já hibernando**. Ele tem **dois
modos** — um modo operacional (Alpha headless) para configuração/manutenção e um modo de
hibernação (ciclo dormant). Ver §3.

---

## 2. O que já existe e vamos REAPROVEITAR (sem reescrever)

| Necessidade do Air | Onde já existe hoje |
|---|---|
| Ler sensores até estabilizar | `SensorManager` — média aparada (`MOVING_AVG_WINDOW=10`), `RuntimeSensor::bufferFull()` |
| Armazenar valores localmente | `HistoryV5` (encoder RAM → `.wip` → `.h5`) + `writeHistoryEntryV5` |
| Telemetria store-and-forward | `TelemetryManager` — cursor persistido (`/config/t_cursor.bin`) + `collectBatch()` + `forceSync()` |
| Wi-Fi / conexão / NTP | `NetworkManager` (STA, backoff, NTP, relógio provisório) |
| Config serial / bluetooth | `CommandManager` + `CommandParser` (mesmo parser no Serial e no SerialBT) |
| Config web | `WebManager` + `/api/commit_all` |
| Config persistida | `StorageManager` (`SystemConfig` binário, CRC, banco duplo) |
| `PromMetrics` / `Syslog` / `HaDiscovery` | já existem e ficam **no modo inicial** (D5) |

**Store-and-forward já é nativo**: o cursor de telemetria só avança quando o envio é
confirmado; registros gravados sem Wi-Fi ficam pendentes e são despachados na próxima conexão.
O Air só precisa orquestrar o ciclo dormir/acordar em volta disso.

---

## 3. Os dois modos do Air

### 3.1 M0 — Modo Operacional (Alpha headless)

Roda no **cold boot** (alimentação ligada / reset normal). Stack completo, igual ao SIMUT
Alpha **menos o display**:

- sensores lendo continuamente + telemetria ao vivo;
- servidor web (config via UI);
- CLI serial (completa, `SIMUT_CLI_FULL=1`);
- CLI Bluetooth (mesmo parser);
- `PromMetrics` + `Syslog` + `HaDiscovery` **ativos** (D5);
- alarmes/limites operando normalmente.

**Saídas de M0** (ambas levam a M1):
- **comando explícito** `air hibernate` (serial/BT) ou ação web (`/api/air/hibernate`);
- **timer de inatividade**: 5 min sem nenhum comando (serial/BT/web) → hiberna (D4).

### 3.2 M1 — Ciclo de Hibernação (dormant)

Máquina de estados mínima que executa, a cada wake do RTC, o ciclo de amostragem +
store-and-forward (§4). Aqui **não** sobem web, `PromMetrics`, `Syslog` nem `HaDiscovery`
(D5) — só o essencial para ler, decidir, gravar/enviar e voltar a dormir.

### 3.3 Detecção M0 × M1 e transições

```
  ligar alimentação / reset          wake do RTC (dormant)
        │                                    │
        ▼                                    ▼
   ┌─────────┐  comando air hibernate   ┌─────────┐
   │   M0    │ ────────────────►          │   M1    │
   │ Alpha   │   ou 5 min inativo         │ ciclo   │
   │ headless│ ◄────────────────          │ dormant │
   └─────────┘   (re)ligar alimentação    └─────────┘
```

- **Detecção**: marcador mágico em `watchdog scratch[0]` (mesma técnica do
  `POST_OTA_APPLY_MAGIC` em `AppManager_Boot.cpp`) gravado antes de `sleep_goto_dormant_until()`;
  no boot, scratch[0] == magia → M1 (wake de dormant); senão → M0 (cold boot). `recover_from_sleep()`
  restaura o relógio nos dois casos.
- **M1 → M0**: desligar e religar a alimentação (cold boot). *Open question*: um botão (wake por
  GPIO edge) para forçar M0 sem tirar da tomada — ver §11.

---

## 4. Ciclo de hibernação (M1) — máquina de estados

Um `AirManager` (padrão de `TelemetryManager`) é dono da máquina de M1:

| Estado | Ação | Sai quando |
|---|---|---|
| `AIR_BOOT` | scratch[0] confirma wake-de-dormant; `recover_from_sleep()` | setup termina |
| `AIR_WARMUP` | Liga VCC dos sensores (GPIO de power-gating) | timeout curto (~200–500 ms) |
| `AIR_SAMPLE` | Bombeia `SensorManager::update()`; dispara 1º scan Wi-Fi do SSID alvo | canais ativos `bufferFull()` **ou** `STAB_TIMEOUT` |
| `AIR_DECIDE` | Resultado do scan (SSID presente?) | imediato |
| `AIR_PERSIST` | `writeHistoryEntryV5` + `flushWipV5` (sem rede) | snapshot confirmado |
| `AIR_CONNECT` | `NetworkManager::update()` até `NET_READY`/timeout; NTP se possível | conectado ou timeout |
| `AIR_FLUSH` | `TelemetryManager::forceSync()` (drena o cursor) | fila zerada ou timeout |
| `AIR_SLEEP` | Desliga sensores; `cyw43_arch_deinit()`; desarma WDT; grava scratch[0]=magia; agenda RTC; `sleep_goto_dormant_until()` | — (reset no próximo wake) |

`AIR_CONNECT`/`AIR_FLUSH` só rodam quando o scan encontrou o SSID. `AIR_PERSIST` é o caminho
sem rede — os dados já ficam pendentes no cursor de telemetria. O intervalo de wake (D7) é lido
do arquivo de config do Air no `AIR_SLEEP`, na hora de agendar o alarme do RTC.

Diagrama do ciclo (idêntico ao da v1, agora rotulado como M1):

```
   BOOT (wake dormant) → WARMUP → [ AMOSTRAGEM ⇄ SCAN Wi-Fi ] → DECIDE
                                     rede ausente │          │ rede presente
                                                  ▼          ▼
                                          PERSIST (hist)   CONNECT + FLUSH (tel)
                                                  └─────┬────┘
                                                        ▼
                                              HIBERNA (dormant + RTC)
```

---

## 5. Hibernação no RP2040 — SLEEP (deep sleep) na implementação

> **Mudança de D1:** o modo DORMANT (escrita `"coma"` no ROSC_DORMANT + clk_sys→ROSC)
> foi substituído por **SLEEP (deep sleep via `__wfi`)**. DORMANT mostrou-se
> não-determinístico na bancada: a escrita "coma" e a troca para o ROSC disputam o
> sincronizador lento do ROSC/clk_rtc e acordam na hora errada. SLEEP usa o mesmo
> alarme do RTC, é determinístico e custa só ~0,25 mA a mais (~1,2 mA vs ~0,95 mA).

- `sleep_goto_sleep_until(datetime, cb)` vendado em `src/air/pico_sleep.c`
  (clk_sys/clk_ref→XOSC, `sleep_en0`=RTC, `__wfi` + alarme do RTC);
- o set do RTC usa `airRtcSetDatetime()` (segura o bit LOAD por 1 ms — o SDK escreve
  LOAD+ENABLE back-to-back e perde o LOAD a 46875 Hz);
- antes do `__wfi` desabilita todas as IRQs exceto a do RTC (senão um IRQ pendente de
  USB/UART acorda imediatamente);
- o wake é um **resume**; o firmware faz `SYSRESETREQ` logo após o retorno para o boot
  ROM reinicializar os clocks, e `scratch[0]` (always-on) discrimina M1 vs M0;
- consumo do RP2040 na faixa de **~1,2 mA** em sleep (XOSC + RTC); como todo estado
  relevante (amostra, cursor de telemetria) já está em flash antes de dormir, a perda de
  SRAM não é custo — o boot reconstrói tudo a partir do flash.

### Atenções obrigatórias (validação no bench)

1. **CYW43 (Wi-Fi)**: desligar antes de dormir (`cyw43_arch_deinit()` / `WiFi.end()`) e
   reinicializar no boot. Maior risco de integração — validar desligar/religar sem corromper o stack.
2. **Watchdog**: fica no domínio always-on; se armado, dispara durante o dormant e parece reset.
   Desarmar antes de dormir e re-armar no `setup()`. (Em M0 o WDT segue como hoje.)
3. **Consumo real da placa Pico W**: em dormant ~1,3 mA (repouso do CYW43 + regulador). Para µA
   reais, power-gating do chip wireless (hardware) — decisão fora do firmware.
4. **Relógio entre ciclos**: RTC continua em dormant (XOSC); validar `recover_from_sleep` + relógio
   provisório do `NetworkManager` mantendo `time()` coerente sem NTP (V5 já usa `H5_FLAG_CLOCK_SYNCED`).
5. **Sensores**: desligar via GPIO de power-gating; conversão lenta (DS18B20 ~750 ms, DHT22 ~2 s)
   domina o tempo acordado.

---

## 6. Detalhamento dos passos do ciclo (M1)

### 6.1 Estabilização dos sensores
- Reusa `SensorManager::update()` e a média aparada (10 amostras, descarta outliers);
- estável = `RuntimeSensor::bufferFull()` em todos os canais ativos **ou** `STAB_TIMEOUT` (ex. 15–30 s);
- calibração já é aplicada pelo `SensorManager` — nada novo.

### 6.2 Verificação de presença do Wi-Fi
- `WiFi.scanNetworks()` e compara com `cfg.wifiSsid` (presença, sem conectar);
- presente → `AIR_CONNECT`; ausente → `AIR_PERSIST`;
- timeout do scan curto (~2–4 s); falha de scan = tratar como ausente.

### 6.3 Store-and-forward (sem código novo de pendência)
- `AIR_PERSIST`: `writeHistoryEntryV5` + `flushWipV5` — cursor de telemetria não avança;
- `AIR_FLUSH`: `TelemetryManager::forceSync()` drena do cursor (HTTP 2xx / MQTT PUBACK) com backoff;
- timeout de envio → volta a dormir e tenta no próximo wake; o cursor garante sem duplicação/perda.

---

## 7. Variante de build (`[env:pico_w_air]`)

Padrão de `[env:pico_w_alpha]`, mas mantendo os 3 canais de configuração (D2):

- `extends = pico_base`;
- flags: `-DSIMUT_DISPLAY_TFT=0 -DSIMUT_DISPLAY_ALPHA=0 -DSIMUT_AIR=1`
  `-DSIMUT_CLI_FULL=1 -DSIMUT_BLUETOOTH=1 -DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`
  `-DSIMUT_MDNS=0` (reavaliar);
- `build_src_filter` exclui o display e a UI gráfica: `DisplayManager_*.cpp`, `AppManager_Graph.cpp`,
  `AppManager_HistoryAlarm.cpp` (telas), fontes, temas, touch;
  **mantém**: `WebManager_*`, `CommandManager*`, `BluetoothManager.cpp`, `TelemetryManager*`,
  `SyslogManager*`, `PromMetrics*`, `HaDiscovery*`;
- `lib_ignore` repete a lista de `pico_w_alpha` (ILI9341, GFX, XPT2046) + buzzer, se for dropado;
- **display compilado fora** via stub `DisplayManager_None.cpp` (alternativa (a)) — o `AppManager`
  referencia `_displayMgr` incondicionalmente; Air roda **single-core** (sem Core 1 de display).

**Orçamento de flash (a medir)**: Air ≈ Alpha sem o HD44780/display, porém com CLI completa e
Bluetooth. Se apertar, os levers já documentados em `[env:pico_w_test]` estão disponíveis
(`-DNDEBUG`, `-DSIMUT_LICENSE_STUB`, `-DSIMUT_MDNS=0`, páginas web em LittleFS via
`custom_fs_pages`). Validar com `arm-none-eabi-size` antes de fechar os flags.

---

## 8. Configuração do Air SEM mudar `CONFIG_VERSION` (D6)

O `reserved[]` do `SystemConfig` está cheio e **não** vamos bump o `CONFIG_VERSION` (21). Então
a config do Air vive num **arquivo separado** no LittleFS, com ciclo de vida próprio:

```
/config/air.bin        // blob binário com magic + versão própria + CRC32
                       // (mesmo padrão de banco duplo do system.bin, mas isolado)
```

```
struct __attribute__((packed)) AirConfig {
  uint32_t magic;           // próprio, ex. AIR1
  uint16_t version;         // próprio, independente do CONFIG_VERSION
  uint32_t wakeIntervalMin; // D7 — período entre wakes (default em simut_config.h)
  uint16_t idleTimeoutSec;  // D4 — inatividade p/ auto-hibernar (default 300 s)
  uint16_t stabTimeoutMs;   // teto de estabilização dos sensores
  uint16_t wifiScanTimeoutMs;
  uint16_t connectTimeoutMs;
  uint16_t flushTimeoutMs;
  uint8_t  sensorPowerPin;  // 255 = desligado
  uint8_t  flags;           // LED de status, etc.
};
```

### 8.1 Escrita (web)
- Nova seção Air na página de config + campos no handler de `/api/commit_all` (ou endpoint
  dedicado `/api/air`) que gravam `air.bin` via `StorageManager` (write atômico `.tmp` + rename).
- **Não** passa por `SystemConfig` → `CONFIG_VERSION` fica 21 e o `system.bin` existente não é tocado.

### 8.2 Escrita (serial / bluetooth)
- Novos comandos no `CliCommand` (`SystemDefs_Cli.h`) — o `CommandParser` é compartilhado entre
  Serial e SerialBT, então funcionam nos dois canais sem código extra:
  - `air interval <min>` — define o período entre wakes (D7);
  - `air idle <sec>` — define o timer de inatividade (default 300);
  - `air hibernate` — entra em M1 agora (D4, explícito);
  - `air status` — mostra a config atual + motivo do próximo wake.

### 8.3 Leitura / defaults
- `StorageManager` ganha `loadAirConfig()`/`saveAirConfig()`; arquivo ausente → defaults de
  `simut_config.h` (nova seção AIR), gravado na primeira escrita.
- `AIR_SLEEP` lê `wakeIntervalMin` para agendar o alarme do RTC; M0 lê `idleTimeoutSec` para o timer.

---

## 9. Estrutura de arquivos proposta

```
src/AirManager.h                    // máquina de estados de M1 (ciclo dormant)
src/AirManager.cpp
src/AppManager_Air.cpp              // integração no loop: timer de inatividade (M0) + pump (M1)
src/display/DisplayManager_None.cpp // stub headless (alternativa (a))
src/SystemDefs_Cli.h                // + CMD_AIR_* (interval, idle, hibernate, status)
src/SystemDefs_Network.h            // + constantes de timeout do Air
src/simut_config.h                  // + seção AIR (defaults)
src/StorageManager.*                // + loadAirConfig/saveAirConfig (arquivo air.bin)
src/WebManager_Commit.cpp           // + seção Air no commit
platformio.ini                      // + [env:pico_w_air]
docs/analysis/SIMUT_AIR_ESBOCO.md   // este documento
```

---

## 10. Estimativas (a validar no bench — cultura do repo)

| Item | Estimativa | Base |
|---|---|---|
| Flash | Air ≈ Alpha menos display; com CLI full + BT, a medir; levers do `pico_w_test` disponíveis | `arm-none-eabi-size` |
| RAM | sem Core 1 de display, folga de heap | idem |
| Corrente em dormant | ~1,3 mA (placa Pico W sem mod) / µA (com power-gating) | bench |
| Tempo acordado por ciclo | dominado pela estabilização (DS18B20 ~750 ms, DHT22 ~2 s) | drivers |
| Desgaste de flash | 1 registro por wake + seal horário — irrelevante (wear-leveling) | V5 |

---

## 11. Decisões em aberto (para você avaliar)

1. **Volta M1 → M0**: só power-cycle? Ou adicionar um **botão** (wake por GPIO edge) para forçar
   M0 / cancelar a hibernação durante o wake?
2. **O que conta como comando** para resetar o timer de 5 min: só comandos CLI/BT + requests web
   autenticados, ou qualquer request web (inclusive `/metrics` do Prometheus)?
3. **Som (buzzer)** fica no Air? (status sonoro sem display) — ou dropa junto com a UI?
4. **`SIMUT_MDNS`**: manter `.local` no modo operacional ou cortar para economizar flash?
5. **LED de status** (GPIO onboard) no M1: pisca para amostrou, enviou, erro?
6. **Wake por GPIO** além do RTC (botão de ciclo manual / emergência) já no M1?
7. **Intervalo default** de wake e se a mudança via web exige reboot (como `commit_all`) ou vale na
   hora (só M1 lê no `AIR_SLEEP`).

---

## 12. Riscos e plano de validação

| Risco | Mitigação / validação |
|---|---|
| Desligar/religar CYW43 corrompe o Wi-Fi | teste: N ciclos dormant→connect, medir taxa de falha |
| WDT dispara durante o dormant | desarmar antes / re-armar no boot; teste de ciclo longo |
| Relógio drift entre wakes sem NTP | validar `recover_from_sleep` + relógio provisório |
| Estabilização longa demais (bateria) | `STAB_TIMEOUT` + amostra parcial; medir tempo acordado |
| Timer de 5 min dispara durante config demorada | resetar o timer a cada comando/request (definição em `11.2`) |
| `air.bin` corrompido | magic + CRC + fallback para defaults (não afeta `system.bin`) |
| Regressão do build principal | Air é env separado; `pico_w_release`/`pico_w_alpha` intactos |

---

## 13. Próximos passos (esqueleto do plano de implementação)

1. **F1 — Build headless**: `[env:pico_w_air]` + `DisplayManager_None.cpp` + `SIMUT_AIR`;
   compilar e garantir que `pico_w_release`/`pico_w_alpha` seguem intactos.
2. **F2 — Config do Air**: `AirConfig` + `air.bin` (load/save) + defaults em `simut_config.h`;
   **sem** tocar em `CONFIG_VERSION` (D6).
3. **F3 — `AirManager` (M1)**: máquina WARMUP/SAMPLE/DECIDE/PERSIST/CONNECT/FLUSH/SLEEP
   integrada ao loop sob `#if SIMUT_AIR`.
4. **F4 — Dormant**: `cyw43_arch_deinit()`/`sleep_goto_dormant_until()`/WDT desarmar +
   `recover_from_sleep()` + scratch[0]=magia; power-gating dos sensores.
5. **F5 — M0 e transição**: boot = Alpha headless; comando `air hibernate` (CLI/BT) + endpoint web;
   timer de 5 min de inatividade; desligar `PromMetrics`/`Syslog`/`HaDiscovery` na transição (D5).
6. **F6 — Store-and-forward**: `writeHistoryEntryV5` + `forceSync()` no ciclo; teste de queda de
   rede no meio do envio (sem duplicação/perda).
7. **F7 — Validação**: bench de corrente em dormant, tempo acordado, confiabilidade do reconnect,
   drift de relógio, desgaste de flash; documentar em `docs/`.

---

_Esboço (revisão 2) em `feature/simut-air` — pronto para a sua avaliação antes de abrirmos o plano detalhado._
