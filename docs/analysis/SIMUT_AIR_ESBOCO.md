# SIMUT Air — Esboço de Projeto (build headless com hibernação)

> **Status:** ESBOÇO para avaliação. Nada foi implementado ainda.
> **Branch:** `feature/simut-air`
> **Base:** `main` (v2.3.9-beta, `SIMUT_VERSION` em `src/SystemDefs_Limits.h`, `CONFIG_VERSION = 21`)
> **Idioma:** pt-BR (espelha os docs `docs/analysis/ANALISE_*.md`).

---

## 1. Objetivo

**SIMUT Air** é uma variante de firmware do SIMUT para Raspberry Pi Pico W **sem display**,
otimizada para operação em bateria: a maior parte do tempo o pico fica **hibernando**
(modo *dormant* do RP2040) e, em **períodos definidos**, ele:

1. **acorda** (alarme do RTC);
2. **lê os sensores até a estabilização** (janela de média aparada completa);
3. **em paralelo, verifica se a rede Wi-Fi configurada está presente**;
4. **se a rede NÃO estiver presente** → armazena os valores (histórico local) e volta a hibernar;
5. **se a rede ESTIVER presente** → conecta e envia a telemetria pendente, depois hiberna.

É essencialmente o SIMUT atual com o loop reduzido a um **ciclo de amostragem +
store-and-forward**, e sem toda a camada de UI/display/web que domina o flash e o consumo.

---

## 2. O que já existe e vamos REAPROVEITAR (sem reescrever)

O ponto forte deste projeto é que quase todo o comportamento pedido já está implementado
e testado na base atual:

| Necessidade do Air | Onde já existe hoje |
|---|---|
| Ler sensores até estabilizar | `SensorManager` — média aparada (`MOVING_AVG_WINDOW=10`), `RuntimeSensor::bufferFull()` marca a janela completa |
| Armazenar valores localmente | `HistoryV5` (encoder em RAM → snapshot `.wip` → arquivo diário `.h5`) + `StorageManager::writeHistoryEntryV5` |
| Telemetria store-and-forward | `TelemetryManager` — cursor persistido (`/config/t_cursor.bin` + `getLastSentTimestamp`) + `collectBatch()` + `forceSync()` |
| Wi-Fi / conexão / NTP | `NetworkManager` (STA, backoff, NTP, relógio provisório) |
| Configuração persistida | `SystemConfig` binário com CRC + banco duplo (`StorageManager`) |

Ou seja: **armazenar e enviar depois** já é o comportamento nativo do SIMUT — o cursor de
telemetria só avança quando um envio é confirmado, então os registros gravados durante os
períodos sem Wi-Fi ficam automaticamente pendentes e são despachados na próxima conexão.
O Air não precisa inventar um buffer de pendências; precisa apenas orquestrar o ciclo de
dormir/acordar em volta disso.

---

## 3. Visão geral do ciclo Air

```
                      ┌────────────────────────────────────────────┐
                      │              BOOT (reset / wake)           │
                      │  recover_from_sleep() → motivo do wake      │
                      └──────────────────────┬─────────────────────┘
                                             │
                                             ▼
                      ┌────────────────────────────────────────────┐
                      │ 1. WARMUP dos sensores (liga VCC via GPIO)  │
                      └──────────────────────┬─────────────────────┘
                                             │
                                             ▼
         ┌──────────────────────┐    ┌──────────────────────────────┐
         │ 2a. AMOSTRAGEM       │    │ 2b. SCAN Wi-Fi (SSID alvo)   │
         │ pump SensorManager   │◄──►│ (intercalado no mesmo loop)   │
         │ até bufferFull() ou  │    │                              │
         │ timeout              │    │                              │
         └──────────┬───────────┘    └──────────────┬───────────────┘
                    └──────────────┬────────────────┘
                                   ▼
                    ┌──────────────────────────────┐
                    │ 3. DECIDE: rede presente?     │
                    └──────┬───────────────┬───────┘
                      NÃO  │               │ SIM
                           ▼               ▼
              ┌────────────────────┐  ┌───────────────────────────────┐
              │ 4a. PERSISTE (hist) │  │ 4b. CONECTA + NTP              │
              │  grava amostra V5   │  │  + telemetry flush (forceSync) │
              └─────────┬──────────┘  └──────────────┬────────────────┘
                        └──────────────┬─────────────┘
                                       ▼
                        ┌──────────────────────────────┐
                        │ 5. HIBERNA (dormant + alarme) │
                        │  desliga sensores + CYW43     │
                        └──────────────────────────────┘
                                       │ (RTC dispara)
                                       └──────────────► BOOT
```

Os passos 2a e 2b não são threads: rodam **intercalados no mesmo loop do Core 0**
(leitura de DS18B20/DHT22 é bloqueante na casa de centenas de ms a ~2 s; o scan do
CYW43 avança por polling `cyw43_poll`). O objetivo do paralelismo é aproveitar o tempo
morto da conversão dos sensores para já descobrir se vale a pena conectar.

---

## 4. Máquina de estados do ciclo (proposta)

Um `AirManager` (espelhando o padrão de `TelemetryManager`) é dono da máquina:

| Estado | Ação | Sai quando |
|---|---|---|
| `AIR_BOOT` | Detecta wake-from-dormant via `sleep_get_status()` / scratch; restaura relógio (`recover_from_sleep`) | setup termina |
| `AIR_WARMUP` | Liga VCC dos sensores (GPIO de power-gating); aguarda estabilização de alimentação | timeout curto (~200–500 ms) |
| `AIR_SAMPLE` | Bombeia `SensorManager::update()`; dispara 1º scan Wi-Fi do SSID alvo | todos os canais ativos com `bufferFull()` **ou** `STAB_TIMEOUT` |
| `AIR_DECIDE` | Resultado do scan (SSID presente?) | imediato |
| `AIR_PERSIST` | `writeHistoryEntryV5` + `flushWipV5` (sem Wi-Fi) | snapshot confirmado |
| `AIR_CONNECT` | `NetworkManager::update()` até `NET_READY`/timeout; NTP se possível | conectado+enviado, ou timeout |
| `AIR_FLUSH` | `TelemetryManager::forceSync()` (drena o cursor pendente) | fila zerada ou timeout |
| `AIR_SLEEP` | Desliga sensores; `cyw43_arch_deinit()`; desarma WDT; agenda RTC; `sleep_goto_dormant_until()` | — (reset no próximo wake) |

Estados `AIR_CONNECT`/`AIR_FLUSH` só são alcançados quando o scan encontrou o SSID.
`AIR_PERSIST` é o caminho sem rede — os dados já ficam pendentes no cursor de telemetria.

---

## 5. Hibernação no RP2040 (a decisão técnica central)

O RP2040 (SDK `pico/sleep.h`) oferece dois níveis relevantes:

### 5.1 `sleep` (clock-switching) — retém SRAM

- `sleep_run_from_xosc()` + `sleep_goto_sleep_until()`;
- a SRAM é preservada e a execução **continua** no wake (callback restaura clocks);
- mais simples (o `AppManager` inteiro sobrevive);
- consumo na faixa de ~1–2 mA (XOSC rodando).

### 5.2 `dormant` (o mais profundo) — perde SRAM

- `sleep_goto_dormant_until(datetime)` / `_until_edge_high/low(pin)`;
- a SRAM **não** é preservada: no wake o chip **reseta** (só sobrevivem RTC + scratch/watchdog);
- `recover_from_sleep(sleep_get_status())` no boot identifica o motivo (`RTC_ALARM` / `EDGE_*`);
- consumo do RP2040 na faixa de **µA**;
- exige que **todo** estado esteja em flash antes de dormir (é exatamente o nosso caso: a
  amostra já foi gravada no histórico e o cursor de telemetria já está persistido).

### Recomendação

**dormant**, porque é o que hibernação pede para operação em bateria. A perda de SRAM
aqui não é custo — o firmware já é desenhado para reconstruir estado a partir do flash no boot
(config + histórico + cursor). O marcador de wake-do-Air usa `sleep_get_status()` e, se
preciso, um registrador scratch do watchdog para carregar contexto entre ciclos.

### Atenções obrigatórias (validação no bench)

1. **CYW43 (Wi-Fi)**: precisa ser desligado antes de dormir (`cyw43_arch_deinit()` no nível SDK
   / `WiFi.end()` no nível arduino-pico) e reinicializado no boot. É o ponto de maior risco de
   integração — arduino-pico não tem um sleep de primeira classe para o Pico W; validar
   desligar/religar o chip sem corromper o stack Wi-Fi.
2. **Watchdog**: o WDT fica no domínio always-on; se ficar armado ele dispara durante o
   dormant e parece um reset. Desarmar antes de `sleep_goto_dormant_until()` e re-armar no
   `setup()`.
3. **Consumo real da placa Pico W**: em dormant a placa fica em ~1,3 mA (corrente de repouso
   do CYW43 + regulador). Para µA reais é preciso **power-gating** do chip wireless (mod de
   hardware no `3V3_EN`/VBUS) — decisão de hardware, não de firmware.
4. **Relógio entre ciclos**: o RTC continua em dormant (XOSC); validar que `recover_from_sleep`
   + o relógio provisório do `NetworkManager` mantêm `time()` coerente sem NTP (o V5 já marca
   `H5_FLAG_CLOCK_SYNCED` para separar amostra com relógio bom de provisória).
5. **Sensores**: para bateria, os sensores também precisam ser desligados (GPIO de power-gating
   ligando o barramento de VCC dos sensores). DS18B20/DHT22/BME280 têm conversão lenta — o
   tempo de estabilização é o maior custo de tempo acordado do ciclo.

---

## 6. Detalhamento dos passos do ciclo

### 6.1 Estabilização dos sensores

- Reutiliza `SensorManager::update()` e a média aparada existente (10 amostras, descarta
  outliers);
- estável = `RuntimeSensor::bufferFull()` (janela `CH_TEMP` completa) em todos os canais
  ativos, **ou** `STAB_TIMEOUT` (a definir, ex. 15–30 s) para não ficar acordado à toa;
- `SensorManager` já carrega curvas de calibração e já aplica a média — nada novo aqui.

### 6.2 Verificação de presença do Wi-Fi

- Scan direcionado: `WiFi.scanNetworks()` e compara com `cfg.wifiSsid` (presença, sem conectar);
- se presente → `AIR_CONNECT` (reusa `NetworkManager::begin/update`);
- se ausente → `AIR_PERSIST` (economiza o ciclo completo de conexão + NTP quando não há rede);
- timeout do scan curto (~2–4 s); falha de scan = tratar como ausente.

### 6.3 Store-and-forward (sem código novo de pendência)

- `AIR_PERSIST`: grava a amostra via `writeHistoryEntryV5` e força o snapshot `.wip`
  (`flushWipV5`) — os dados ficam no arquivo diário e o cursor de telemetria não avança;
- `AIR_FLUSH`: `TelemetryManager::forceSync()` drena do cursor até a confirmação (HTTP 2xx /
  MQTT PUBACK), com o backoff exponencial existente;
- se o envio estourar o timeout, volta a hibernar e tenta de novo no próximo wake — o cursor
  garante que nada é enviado duas vezes nem perdido.

---

## 7. Variante de build (`[env:pico_w_air]`)

Segue o padrão já estabelecido por `[env:pico_w_alpha]` (build sem TFT/touch):

- `extends = pico_base`;
- flags: `-DSIMUT_DISPLAY_TFT=0 -DSIMUT_DISPLAY_ALPHA=0 -DSIMUT_AIR=1`
  `-DSIMUT_MDNS=0 -DSIMUT_CLI_FULL=0 -DSIMUT_BLUETOOTH=0`;
- `build_src_filter` exclui: `DisplayManager_*.cpp`, `WebManager_*.cpp`, `WebUI_GZ.h`,
  `CommandManager*`, `SoundManager*`, `Themes*`, `BluetoothManager.cpp`, `Favicon*`,
  `PromMetrics*` (decidir), fontes/display;
- `lib_ignore` repete a lista de `pico_w_alpha` (ILI9341, GFX, XPT2046) e acrescenta o que
  sobrar (buzzer, mDNS);
- resultado esperado: **folga grande de flash/RAM** (a medir no bench, como manda o
  `docs/ANALISE_FLASH_RAM.md`) — sem UI, sem web e sem TLS de UI o orçamento muda de
  ~87% cheio para folga.

**Ponto de atenção de implementação:** o `AppManager` hoje referencia `_displayMgr`
incondicionalmente (`begin()`, `startCore1()`, `getHeartbeat()` no loop). O Air precisa de uma
destas duas saídas:

- **(a) stub `DisplayManager_None.cpp`** (troca por `build_src_filter`, como o alpha troca por
  `DisplayManager_Alpha.cpp`) — menor raio de mudança; ou
- **(b) guardas `#if SIMUT_AIR`** ao longo do `AppManager` — mais invasivo, porém remove o
  código em vez de stubar.

Recomendo **(a)**, e **Air roda single-core** (sem Core 1 de display) — o que também simplifica
o `flash_safe_execute` (sem lockout multicore durante gravações).

---

## 8. Configuração nova (`AirConfig`)

`reserved[]` está CHEIO (comentário em `SystemDefs_Records.h`), então o Air acrescenta um
campo **tail-append** `AirConfig` (mesmo padrão do `AlarmTelConfig` v21) e sobe
`CONFIG_VERSION` de 21 → 22:

```
struct __attribute__((packed)) AirConfig {
  uint32_t wakeIntervalSec;   // período entre wakes (default a definir, ex. 300 s)
  uint16_t stabTimeoutMs;     // teto de estabilização dos sensores
  uint16_t wifiScanTimeoutMs; // teto do scan de presença
  uint16_t connectTimeoutMs;  // teto de conexão + NTP
  uint16_t flushTimeoutMs;    // teto do flush de telemetria
  uint8_t  sensorPowerPin;    // GPIO de power-gating dos sensores (255 = desligado)
  uint8_t  flags;             // reservado (LED de status, etc.)
};
```

Migração v21→v22 é tail-append puro (mesma técnica de `loadMigrateV20Blob`): bloco antigo
lido na cabeça do struct, cauda preenchida com defaults de fábrica.

---

## 9. Estrutura de arquivos proposta

```
src/AirManager.h                  // máquina de estados do ciclo Air
src/AirManager.cpp
src/AppManager_Air.cpp            // integração no loop (pump do AirManager)
src/display/DisplayManager_None.cpp  // stub headless (alternativa (a))
src/SystemDefs_Records.h          // + AirConfig (tail) + CONFIG_VERSION 22
src/SystemDefs_Network.h          // + constantes de timeout do Air
src/simut_config.h                // + seção AIR (flags e defaults)
platformio.ini                    // + [env:pico_w_air]
docs/analysis/SIMUT_AIR_ESBOCO.md // este documento
```

---

## 10. Estimativas (a validar no bench — cultura do repo)

| Item | Estimativa | Base |
|---|---|---|
| Flash | economia significativa (UI+web+themes+BT fora) | medir com `arm-none-eabi-size` |
| RAM | sem Core 1 de display, folga de heap | idem |
| Corrente em dormant | ~1,3 mA (placa Pico W sem mod) / µA (com power-gating) | dados de comunidade + bench |
| Tempo acordado por ciclo | dominado pela estabilização (DS18B20 ~750 ms/conversão, DHT22 ~2 s/leitura) | drivers atuais |
| Desgaste de flash | 1 registro por wake + seal horário — irrelevante (LittleFS wear-leveling) | V5 |

---

## 11. Decisões em aberto (para você avaliar)

1. **Provisionamento** — como um aparelho sem display recebe SSID/senha/servidor?
   - **(A) config herdada**: configurar uma vez pelo SIMUT normal (web/CLI) e então flashar o
     Air (UF2 preserva o LittleFS) — zero código novo;
   - **(B) AP de provisionamento** no Air (reusa `beginAP` + web mínimo) acionado por botão/
     primeira inicialização — custa flash e precisa de trigger físico;
   - recomendação: **(A) como primário**, com um escape hatch de AP na primeira inicialização
     (SSID vazio).
2. **Profundidade do sono**: `dormant` (µA, reset no wake) vs `sleep` (mA, estado preservado).
   Recomendo `dormant`; confirmar se a perda de SRAM não atrapalha o relógio/telemetria.
3. **Power-gating dos sensores e do CYW43**: precisa de decisão de hardware (GPIO de VCC, mod
   de `3V3_EN`). Sem isso, a hibernação economiza pouco na placa Pico W.
4. **Wake por GPIO** além do RTC (ex. botão para forçar um ciclo manual / estado de emergência)?
5. **Intervalo default** e se ele é editável (nova página de config? CLI? arquivo?).
6. **LED de status** (GPIO do LED onboard) para sinalizar amostra/enviou/erro — útil sem display.
7. **`PromMetrics`/`Syslog`/`HaDiscovery`** ficam ou saem do Air? (economia × telemetria de
   diagnóstico.)

---

## 12. Riscos e plano de validação

| Risco | Mitigação / validação |
|---|---|
| Desligar/religar CYW43 corrompe o Wi-Fi | teste dedicado: N ciclos dormant→connect, medir taxa de falha |
| WDT dispara durante o dormant | desarmar antes / re-armar no boot; teste de ciclo longo |
| Relógio drift entre wakes sem NTP | validar `recover_from_sleep` + relógio provisório; comparar carimbo vs NTP quando conecta |
| Estabilização longa demais (bateria) | `STAB_TIMEOUT` + amostra parcial aceitável; medir tempo acordado |
| Migração de config v21→v22 | reusar teste/estratégia do `AlarmTelConfig`; bloco antigo não corrompe |
| Regressão do build principal | Air é env separado; `pio run -e pico_w_release` continua intacto |

Validação segue o padrão do repo: suites em `tools/` + medições documentadas (estilo
`docs/ANALISE_FLASH_RAM.md`, `docs/promotion/PLANO-VALIDACAO-*.md`).

---

## 13. Próximos passos (esqueleto do plano de implementação)

Depois da sua aprovação deste esboço, detalhamos o plano em fases. Rascunho:

1. **F1 — Build headless**: `[env:pico_w_air]` + stub de display + `SIMUT_AIR`; garantir que
   compila e que `pico_w_release`/`pico_w_alpha` continuam intactos.
2. **F2 — Config**: `AirConfig` + `CONFIG_VERSION 22` + migração + defaults em `simut_config.h`.
3. **F3 — `AirManager`**: máquina de estados do ciclo (WARMUP/SAMPLE/DECIDE/PERSIST/CONNECT/
   FLUSH/SLEEP) integrada ao `AppManager::loop()` sob `#if SIMUT_AIR`.
4. **F4 — Hibernação real**: `cyw43_arch_deinit()`/`sleep_goto_dormant_until()`/WDT desarmar +
   `recover_from_sleep()` no boot; power-gating dos sensores.
5. **F5 — Store-and-forward**: amarrar `writeHistoryEntryV5` + `forceSync()` no ciclo; validar
   que nada é duplicado/perdido (teste de queda de rede no meio do envio).
6. **F6 — Validação**: bench de corrente em dormant, tempo acordado, confiabilidade do
   reconnect, drift de relógio, desgaste de flash; documentar em `docs/`.

---

_Esboço gerado em `feature/simut-air` — sujeito a ajustes após a sua avaliação._
