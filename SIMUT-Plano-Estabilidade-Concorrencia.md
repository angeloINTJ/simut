# SIMUT — Análise de Estabilidade de Concorrência e Plano de Tratamento

**Base:** branch `claude/project-analysis-l77lfr` @ `7692a67` (CLI + HistoryV4 já corrigidos) · **Data:** 2026-07-22
**Método:** auditoria estática do código com evidência `arquivo:linha`, cruzada com o histórico de
falhas do próprio repo (CHANGELOG, `ANALISE_INSTABILIDADE_SENSORES.md`, commits `e5549d7…d089a81`).

---

## 0. Status de execução — 2026-07-22 (sessão de bancada)

Validado em hardware real (protoboard: Pico W + TFT + DS18B20/GP0, DHT22/GP2-3, BMP280/GP4-5).
Autópsias via watchdog scratch (T0.4) foram o instrumento decisivo em todos os diagnósticos.

| Item | Estado | Evidência |
|---|---|---|
| T0.1 métricas FLASH_OP | ✅ (wave 1) | `show metrics`: Flash ops/média/pior/>50ms |
| T0.2 métricas de heap | ✅ (wave 1) | `show metrics`: Heap min + maior bloco |
| T0.3 lwIP PBUF stats | ✅ (4c56c11) | `LWIP_STATS`+`MEMP_STATS` sempre ligados via lwipopts patchado (~300 B, sem prints — divergência deliberada do "env debug": a bancada roda release); `show net status` imprime uso/pico/total/falhas do PBUF pool; verificado no ELF (`lwip_stats` 212 B, pool=12) |
| T0.4 autópsia de reboot | ✅ (wave 1) | 4 autópsias distintas capturadas em bancada hoje |
| T1.1 quiesce antes do reset | ✅ ampliado | wave 1 no quiet mode; hoje estendido ao lockout do `pauseRendering` + relançamento seguro (mutex/flags) + **`mutex_init(&_stateMutex)` no instante do kill** nos 2 pontos (autópsias `C0=[CLI]`/`C0=[SAVE_CFG]` do save-storm) |
| T1.2 sem heap no render Core 1 | ✅ | 6 `String` removidos de `DisplayManager_Settings.cpp` (grep String nos caminhos de render = 0; restantes são setters Core-0/comentários) |
| T1.3 BMP280 em I2C hardware | ✅ (wave 2 + hoje) | flag `SIMUT_SENSOR_BME280` religado; chip cid=0x58 detectado no HW I2C; slot T+H+P ativo |
| T1.4 enforceStorageLimit fatiado | ✅ (wave 1) | 2 deleções/chamada + fatias de manutenção ≥15 s |
| T1.5 watchdog do quiet mode | ✅ (wave 1) | `_quietSince` leak watchdog |
| T1.6 assert de invariante | ✅ (wave 2) | `ConcurrencyAsserts.h` + tripwire no FLASH_OP |
| T1.7 delay(50) runtime | ⚠️ exceção documentada | os 2 restantes são assentamento de HW pós-reset do Core 1; convertê-los em espera com bombas de serviço criaria reentrância (mesma classe da issue web-histórico) |
| T2.1 batch de histórico | ✅ (69535dd) | batch RAM de 4 amostras, dreno em UMA pausa do Core 1 (cheio ou 5 min); generaliza o pending de 1 slot; dreno antes de reboot/write memory; ~4× menos janelas de erase (intervalo default = 1 min) |
| T2.2 PBUF 12→16 | ⬜ aguarda T0.3 | decidir por dado |
| T2.3 de-String Core 0 | ⬜ pendente | caminhos quentes auth/telemetria |
| T2.4 invariantes em docs | ✅ (wave 1) | `docs/CONCURRENCY.md` |
| **Extra** proteção Core 1 em flash write | ✅ **causa-raiz** | `Core1FlashPause` em history V4/legado/GC (`e035791`): `idleOtherCore()` do arduino-pico é no-op sem setup1/loop1 — comentário antigo do FLASH_OP era falso; autópsia `C0=[HIST_FLASH]` 2× idêntica, morte a cada ~103 s eliminada (8,5 min soak limpo) |
| Validação #1 soak 72 h | ⬜ iniciar | dispositivo pronto na bancada |
| Validação #2 save-storm | 🔶 parcial | storm 500× pendente; storm curto expôs e validou o fix do mutex-at-kill |
| Validação #3 tempestade combinada | ✅ **zero reboots** | 3 rodadas de 10 min (web streaming + saves + touch): build antigo = 2 reboots/2 min (`C0=[CLI]`, `C0=[SAVE_CFG]`); +mutex-at-kill = 1 reboot/10 min (`C0=[WEB_SERVER]`, spinlock da fila); +**ring SPSC** = 0 reboots, 39/39 saves, 117 toques, 195×HTTP 200, heap estável (10:54 uptime) |
| **Extra** fila UI sem spinlock | ✅ | `queue_t` → ring SPSC lock-free (Core 1 produz, Core 0 consome): o lockout podia congelar o Core 1 dentro do `queue_try_add` e o pump de eventos (inclusive via yield do web) girava para sempre no spinlock — R1(b) eliminado por construção |
| Obs. R6 (PBUF) | 🔶 dado novo | 25 erros TCP transitórios/10 min sob tempestade (recuperados via retry) — reforça implementar T0.3 antes de decidir T2.2 |
| Validação #4 PIO 24 h | ⬜ iniciar | BMP280 precisa de 24 h sem fallback bit-bang |

Issues fora do plano — todas resolvidas em 2026-07-22:
- ~~Log persistido "vazio"~~ (89020fb + e23a4be): o writer sempre funcionou (1.156 registros no `.blog`); o leitor do CLI lia os nomes CSV extintos E o renderizador legado descartava linhas sem `;`; o `clear log` também só removia os nomes antigos. Tudo corrigido — 111 linhas legíveis no CLI.
- ~~Gráficos de histórico sempre vazios~~ (dc5e0bb): writer impecável (103 registros decodificados no host com o codec do projeto); o leitor web perdia os dados em 3 estágios — cursor após header-read (irmão do scan bug v1.5.3 nunca aplicado à cópia inline), extensão `.sim` legada nos ranges <1M, e sem observabilidade. Com contadores permanentes (`filesTried/filesOpened/recs`) no envelope. Range 24h → 34 pontos reais.
- Assinatura nova em vigília: `C0=[WIFI]` 1×/100 ciclos (save-storm, durante amostragem de RSSI no `show metrics`) — próximo alvo de investigação.
- ~~Nota UX: decimação fixa~~ **resolvido (722cd53)** — decimação adaptativa (~600 pts/range).
- ~~Gráficos web/TFT em branco~~ **resolvido** — cadeia de 6 defeitos (cursor de header em 5 cópias do leitor V4, extensão .sim legada, datas malformadas do calendário, slot-10 fantasma no frontend, sign-extension em canais unsigned do codec, mapper array-vs-objeto) + minificador comendo JS com aspas em comentário (guard de sintaxe `node --check` agora aborta o build).
- ~~uBMP 102.2~~ **resolvido de verdade (eedd599, cebola de 3 camadas)**: (1) guard só no chamador periódico; (2) `getChipID()` faz leitura I2C viva por chamada e glitcha pós-readAll → guard não-determinístico → trocado pelo `isBME280()` **cacheado**; (3) a compensação de H do BMP280 produz **+INFINITY** e `isnan(inf)==false` — atravessava addSample→assembly→FromFloat e virava 1022 no clamp. Os 3 portões agora usam **isfinite**; FromFloat mapeia não-finito→sentinela. Forense duplamente mascarada pelo printf do newlib-nano (NaN imprime "inf"). Regra nova do projeto: **isfinite, nunca isnan, em portões de sanidade**.
- ~~Pressão BMP NaN em regime~~ **resolvida de carona** — com o inf fora da cadeia, o histórico grava P real (validado: 1011.9 hPa em regime, rec-a-rec).
- **Novos achados (corrigidos no mesmo lote)**: delete web do arquivo de histórico vivo não invalidava o codec (writer seguia em arquivo headerless até reboot); /api/delete devolvia 200 para caminho inexistente. Protocolo de bancada: todo flash verificado por marcador `SIMUT_VERSION` via serial.
- Cadência de teste ativa: lotes de 10 ciclos (diretriz do usuário) — último lote: 10/10 saves, zero reboots.

---

## 0.1 Status de execução — 2026-07-23 (fechamento do T0.1 e correção do T1.6)

Sessão focada em **tornar avaliáveis** os critérios de aceite que estavam
escritos mas não mediam o que diziam medir.

### Correções de rumo (itens que constavam ✅ e não estavam)

| Item | O que se descobriu | Estado agora |
|---|---|---|
| **T0.1** | A métrica entregue (`flashOpMaxMs`) cronometra o bloco `FLASH_OP` inteiro — mutex, contabilidade do LittleFS e a escrita. O critério do plano é sobre a **janela de IRQ desligada**, que é subconjunto estrito disso (o LittleFS envolve cada program/erase em `noInterrupts( )`, `LittleFS.cpp:181-212`). O número que tínhamos não conseguia responder à pergunta que o soak de 72 h existe para fazer. | ✅ **fechado de verdade** (`e6f1480`): os dois primitivos da SDK são interceptados por `-Wl,--wrap` e cronometrados diretamente. `show metrics` agora traz `IRQ-off max/media` em µs e contadores erase/prog/>1ms. |
| **T1.6** | `SIMUT_CONCURRENCY_ASSERTS` **não estava definido em nenhum ambiente**. O tripwire da invariante 3 sempre compilou para nada, em toda imagem já gerada — o aceite ("teste que viola dispara o assert") nunca foi cumprido. O ambiente onde a flag deveria morar, `pico_w_debug`, não linkava. | 🔶 **parcial** (`fb5a4b1`): criado `pico_w_asserts` (= release + flag, cabe em 97,6%, roda em velocidade de bancada). O tripwire agora pode ser exercitado; falta rodar o teste que o viola. |

### Risco novo — **R10: teto de flash**

O slot de aplicação tem 1020 KB e a imagem de release ocupa **1.017.880 B
(97,5 %)** — sobram **~26 KB**. Maior consumidor isolado: o blob de firmware do
rádio CYW43439 (`wb43439A0_7_95_49_00_combined`), **232 KB = 22 % do slot**. A
UI web já está otimizada (gzip, 77 KB embarcados; o `WebUI.h` cru de 304 KB é
build-time e não linka).

Consequências práticas, já sentidas nesta sessão:
- `pico_w_debug` **não linka**: em `-Og` e sem `--gc-sections` a imagem
  estoura o slot em ~69 KB. Forçar `-Os` e devolver `--gc-sections` foi
  tentado e piorou. Ficou documentado no `platformio.ini` em vez de
  maquiado — resolver exige folga de flash que hoje não existe.
- Qualquer feature nova relevante esbarra em flash antes de esbarrar em RAM
  (RAM está em 49,9 %).

Duas falhas de link do `pico_w_debug` **foram** corrigidas: `DisplayManager_Alpha.cpp`
agora é excluído como no release (redefine membros que os `DisplayManager_*.cpp`
já fornecem), e `NetworkManager::MAX_RECONNECT_DELAY` / `TelemetryManager::BACKOFF_MAX_MS`
viraram `constexpr` — eram `static const` passados a `min( )`, que liga
referência e portanto os ODR-usa; em `-Os` o valor é dobrado e nenhum símbolo é
necessário, em `-Og` o link quebrava.

### Armadilha no protocolo de validação #2 (save-storm)

A primeira execução da tempestade reportou 155/155 saves com zero reboots — e
**não validava nada**. `saveConfiguration( )` faz checagem de CRC e retorna
cedo quando a config não mudou: antes do `WdtWindow`, antes do `BigSaveGuard`,
**antes de o quiet mode ser sequer requisitado** — mas ainda incrementa
`configSaves`. Os 155 saves produziram 8 operações de flash, e essas 8 eram o
timer do histórico, não os saves.

Portanto: **uma tempestade de saves idênticos exercita zero de T1.1/T1.2.** O
`tools/save_storm.py` agora muda o nome do dispositivo a cada ciclo (CRC sempre
difere) e o sumário se recusa a declarar PASS se as operações de flash não
cresceram junto com os ciclos.

### Ferramental de bancada versionado

- `tools/save_storm.py` — protocolo #2. JSONL incremental, detecta reboot por
  regressão de uptime, restaura o nome do rig ao final.
- `tools/sensor_soak.py` — protocolo #4. Vigia o WARN de queda para bit-bang,
  e amostra o pool de PBUF sob tráfego real para **decidir T2.2 por dado**
  (a única medição existente era em vigília: pico 2/12, que não sustenta gastar
  6 KB de RAM).
- `tools/check_flash_probe.py` — guarda de build (ver invariante 8).

### Invariante nova (nº 8, em `docs/CONCURRENCY.md`)

**Tudo alcançável a partir de uma chamada `flash_range_*` mora em SRAM.** O
`--wrap` coloca os nossos shims também no caminho do `ota_applier_run( )`, que
**apaga o slot inteiro da aplicação** rodando de SRAM. Um shim no slot seria
buscado de um setor recém-apagado na chamada seguinte e travaria o chip com a
imagem pela metade — brick irrecuperável. Daí `__not_in_flash_func` nos dois
shims, leitura crua de `timer_hw->timerawl` em vez de `time_us_32( )`, e
aritmética presa em 32 bits no caminho quente. `tools/check_flash_probe.py`
quebra o build se qualquer shim linkar fora de SRAM — **verificado removendo o
`__not_in_flash_func` de propósito**: a guarda pegou o símbolo em `0x1001fcb4`
e falhou o build. A desmontagem confirma um único `bl`, para
`flash_range_erase` em `0x20000978` (SRAM), sem helper de libgcc.

### Resultados de validação — 2026-07-23

**Protocolo #2 — save-storm 500× · ✅ PASSA**
(`docs/test_reports/save_storm_20260723/summary.json`, 50 min de execução)

| Medida | Resultado |
|---|---|
| Saves | **500/500 OK**, 0 falhas |
| Toques simulados | 1000 |
| Reboots | **0** (uptime contínuo 1830 s → 4846 s) |
| Reconexões de porta | 0 |
| `Core 1 dead` no log | 0 |
| `lockout` no log | 0 |
| Operações de flash | **+2550** (5,1 por save — os saves foram reais) |
| Heap | **55832 B do início ao fim**; mínimo 55808 B **não se moveu** |
| Maior bloco | 35015 B, mínimo 35000 B — sem fragmentação |
| Leituras de sensor | 3294 → 8075, **0 erros** |

O heap e o maior bloco imóveis ao longo de 500 quiet modes são a evidência
direta que o aceite pedia: o lock do alocador nunca ficou preso e o Core 1
sempre voltou. **T1.1 e T1.2 validados.**

Dado colateral relevante para R2: sob tempestade o pior `FLASH_OP` subiu de
184 ms para **202 ms**, e as operações >50 ms passaram de 39 para 732 (de 2550).
Quanto disso é IRQ desligada continua desconhecido — é exatamente o número que
a sonda nova mede e que ainda não rodou em hardware.

**Protocolo #4 — soak do PIO 24 h · 🔄 em execução**
Iniciado em 2026-07-23 19:38, término previsto 2026-07-24 19:38
(`docs/test_reports/pio_soak_20260723/`, amostra a cada 5 min). Roda na imagem
`pico_w_asserts` recém-gravada, portanto coleta **também** a janela de IRQ-off
e mantém o tripwire da invariante 3 armado. Vigia o WARN de queda para
bit-bang, contadores de erro dos sensores, deriva de heap, reboots e o pico do
pool de PBUF. Primeira amostra: uptime 112 s, heap 55696, 163 leituras, 0
erros, PBUF 1/12, **IRQ-off max 59724 µs**.

Observação sobre os campos `slots_*` do soak: são contagens de ocorrência do
nome do sensor na saída de `show sensors`, não número de slots. O sinal útil é
a **constância** — uma queda indica sensor sumindo do inventário.

### Primeira observação em hardware da janela de IRQ-off — e ela quase reprova

Imagem `pico_w_asserts` gravada e verificada por picotool em 2026-07-23 19:36.
Primeiro `show metrics` após o boot (39 s de uptime):

```
 Flash ops: 0 (media 0 ms)
 Pior op: 0 ms | >50ms: 0
 IRQ-off max: 55568 us | media: 4506 us
 IRQ-off erase: 14 | prog: 148 | >1ms: 14
```

Três leituras diretas desse bloco:

1. **`Flash ops: 0` com 162 operações de flash já contabilizadas.** As
   operações de boot (mount do LittleFS, carga de config) acontecem **fora**
   de qualquer bloco `FLASH_OP` — a métrica da onda 1 era literalmente cega
   para elas. Não era só imprecisa: não as via.
2. **14 erases, 14 janelas >1 ms, 148 programs.** A correspondência exata
   diz que *todo* erase passa de 1 ms e *nenhum* program passa. O custo está
   inteiramente nos erases; os programs são ruído.
3. **Pior janela: 55,6 ms de IRQ desligada** — e o soak registrou **59,7 ms**
   com 112 s de uptime. O critério do protocolo #1 é `flash_irqoff_max_ms
   < 60 ms`. Estamos a **0,3 ms** dele, ainda em regime de boot, sem
   rollover de histórico nem GC. Sob a tempestade de saves o pior `FLASH_OP`
   bateu 202 ms; se a fração de IRQ-off acompanhar, o critério cai.

Ou seja: **R2 deixou de ser hipótese**. Cada erase de setor cega o rádio por
até ~60 ms, e a métrica antiga nunca teria mostrado isso — nem pelo valor, nem
pela existência.

Consequência para o planejamento: o soak de 72 h (protocolo #1) provavelmente
vai **reprovar** no critério como escrito. Isso é resultado útil, não fracasso:
o alvo passa a ser reduzir a janela (fatiar erases, adiar GC para fora de
janelas de rádio) e não apenas medi-la.

### Nota de versionamento — resolvido em **1.5.2-rc4**

O rótulo rc3 chegou a cobrir dois binários distintos (com e sem a sonda),
porque a imagem instrumentada foi gravada antes do bump. `SIMUT_VERSION` subiu
para **1.5.2-rc4**, restaurando o protocolo de verificação por versão: a
partir do próximo flash, `show system info` distingue as imagens sozinho.

**Resolvido**: a bancada foi regravada com `pico_w_asserts` e reporta
`1.5.2-rc4`. Repo e dispositivo alinhados; o rótulo volta a identificar a
imagem sozinho.

Correção de procedimento: **flash não exige BOOTSEL físico**. O registro
anterior estava errado. `pio run -e <env> -t upload` faz o reset por toque de
1200 bps com o dispositivo rodando e grava sem intervenção. O `picotool load`
direto é que exige BOOTSEL prévio — daí a conclusão equivocada.

---

## 1. Sumário executivo

A instabilidade percebida em **três frentes distintas** (Wi-Fi oscilando, sensores caindo,
display travando/reiniciando) converge em **três causas raiz de concorrência**:

| # | Causa raiz | Frentes que contamina |
|---|---|---|
| C1 | **Janelas de IRQ desligada no Core 0** durante *program/erase* de flash (XIP) e durante I2C bit-bang do BMP280 | Wi-Fi/BT (cyw43 e lwIP perdem timing), sensores, CLI-BT |
| C2 | **Hard-reset do Core 1 ("quiet mode") pode matá-lo segurando um lock** que o Core 0 precisa depois (heap, spinlock da fila, log) | Display (reboot em loop via WDT), sistema inteiro |
| C3 | **Conflito de slots do PIO0** empurra o BMP280 para bit-bang com `critical_section` | Sensores (on/offline) **e** C1 acima |

O sistema já tem defesas maduras (seção 3) — o plano (seção 5) fecha as lacunas restantes
em três ondas: **medir → cirúrgico → estrutural**, cada ação com critério de aceite.

---

## 2. Mapa de concorrência (quem toca o quê)

```
                    ┌───────────── RP2040 ─────────────┐
  CORE 0 (cooperativo, 1 thread)        CORE 1 (loopCore1)
  ├ AppManager loop + watchdog owner    ├ DisplayManager (render+touch)
  ├ SensorManager (PIO 1-Wire/DHT,      ├ SPI0: ILI9341 + XPT2046 (CS próprios)
  │  BMP280 I2C — hoje bit-bang!)       ├ Touch IRQ no NVIC do Core 1
  ├ Network/lwIP/BT (cyw43, IRQ+alarm)  └ victim do multicore_lockout
  ├ Web server, Telemetry, CLI USB/BT
  └ StorageManager → LittleFS ──────────► FLASH QSPI (XIP!)
                                          program/erase = flash_safe_execute:
                                          IRQs OFF no Core 0 + Core 1 parado
  RECURSOS COMPARTILHADOS ENTRE NÚCLEOS
  · _sharedState (SystemState) ......... mutex_t _stateMutex   [DisplayManager.h:316-318]
  · _eventQueue (UiEvent×10) ........... queue_t (SPINLOCK)    [DisplayManager.cpp:123]
  · flags one-shot ..................... dado + __dmb() + flag [DisplayManager.h:274-276]
  · heartbeat/pause .................... volatile + __atomic   [DisplayManager.h:322-326]
  · log buffer ......................... mutex_t _logMutex     [LogManager.h:196]
  · HEAP (malloc/free) ................. lock do runtime — usado pelos DOIS núcleos
```

---

## 3. Defesas já existentes (não regredir)

| Mecanismo | Evidência | Estado |
|---|---|---|
| `FLASH_OP` com `mutex_enter_timeout` 100 ms + `watchdog_update` + teto 5 s | `StorageManager.cpp:40-54` | ✅ maduro |
| `ReadGuard` RAII p/ leituras de FS · `HeavyTaskGuard` p/ handlers longos | `StorageManager.h:68-72` | ✅ |
| Regra de ouro do WDT: Core 0 sempre alimenta; saúde do Core 1 é separada | `main.cpp` | ✅ |
| Heartbeat do Core 1 → `restartCore1()` após 10 s morto; *pause stuck* >5 s → `forceUnpause` | `AppManager_Loop.cpp:55-78` | ✅ |
| Limpeza do estado de *lockout* antes do reset do Core 1 (deadlock recorrente resolvido) | `DisplayManager.cpp:165-181`, commit `d089a81` | ✅ |
| Quiet mode com refcount reentrante; `_stateMutex` re-inicializado pós-reset | `DisplayManager.cpp:582-628` | ✅ parcial (ver R1) |
| Padrão publish `dado+__dmb+flag`; Core 1 lê compartilhados com `mutex_try_enter` (nunca bloqueia render) | `DisplayManager.h:274,373` | ✅ |
| BT defere gravações de log em flash durante `update()` | `CommandManager.cpp:91-99` | ✅ |
| Sensores DS18B20/DHT22 em PIO (imunes a jitter de IRQ) | README/drivers | ✅ |
| Touch: NVIC re-anexado a cada relaunch; bug do wake XPT2046 corrigido | `DisplayManager.cpp:636-650`, CHANGELOG | ✅ |

---

## 4. Riscos identificados

### 🔴 R1 — Reset do Core 1 com lock em mãos (classe inteira, 2 casos abertos)

`requestQuietMode()` faz `multicore_reset_core1()` **em qualquer instrução** do Core 1
(`DisplayManager.cpp:585-603`). O design já trata `_stateMutex` (re-init) e o *lockout* do
SDK — mas **qualquer outro lock que o Core 1 possa estar segurando no instante do reset
fica preso para sempre**:

| Caso | Evidência | Consequência |
|---|---|---|
| **a) Lock do heap** — Core 1 usa `new` no boot do loop e `String` em telas (25 usos em `DisplayManager*.cpp`, ex. `String msg = tr(...)` no redraw de tema, `DisplayManager.cpp:~700`) | reset dentro de `malloc/free` deixa o mutex do alocador travado | próxima alocação do Core 0 trava → WDT → reboot "do nada" durante um save |
| **b) Spinlock da `_eventQueue`** — `queue_try_add` nos caminhos de touch (`DisplayManager_Touch.cpp:119,255,381`) | spinlock do `queue_t` não tem timeout | ambos os núcleos travam no próximo acesso à fila |
| c) `_logMutex` — Core 1 tem 1 `LOG_CODE` (`DisplayManager.cpp`) | idem, janela minúscula | log inteiro trava |

Chamadores do quiet mode (janela de risco a cada save): `AppManager_Commands.cpp:519`,
`AppManager_CmdHandlers.cpp:384`, `WebManager_Calib.cpp:397`, `AppManager_HistoryAlarm.cpp:36`.

### 🔴 R2 — Janelas de IRQ-off no Core 0 a cada program/erase de flash

Flash é XIP: `flash_safe_execute` desliga IRQs no Core 0 durante a operação. Um *append*
de histórico que cruza um bloco dispara **erase de 4 KB (~45 ms com IRQ off)**; o
`enforceStorageLimit` apaga arquivos em rajadas com orçamento de **4 s**
(`StorageManager.cpp`, budget/30 iterações). Nessas janelas o cyw43/lwIP/BT não é servido:
retransmissões TCP, NTP perdido, keepalive MQTT em risco, CLI-BT engasgando — o sintoma
"Wi-Fi instável" com periodicidade suspeita (intervalo do histórico = 1 min).

### 🔴 R3 — PIO0 estourado → BMP280 em bit-bang com `critical_section` (doc do repo, §1)

`OneWirePIO` (27 instr) + `WirePIO I2C` (32) não cabem no pio0 → fallback silencioso para
GPIO bit-bang com **IRQs desligadas ~1,6 ms por transação** e `gpio_init()` a cada leitura.
Isso liga a instabilidade dos **sensores** diretamente à do **Wi-Fi** (mesma classe de C1)
e explica o on/offline do BMP280. A solução D do próprio doc (I2C de hardware
`Wire`/`Wire1`) zera o problema e ainda libera o pio0.

### 🟠 R4 — Invariante de ordem de locks não é imposta

Se qualquer código do Core 0 segurar `_stateMutex` e entrar num `FLASH_OP`, o Core 1
(bloqueado no mutex) não responde ao *lockout* → deadlock → WDT. Hoje os *setters* são
curtos (`DisplayManager.h:151-154`) e nada viola — mas **nada impede regressão**.

### 🟠 R5 — Vazamento do refcount do quiet mode = display morto sem resgate

O restart automático exige `isCore1Ready() && pauseStartTime==0`
(`AppManager_Loop.cpp:66-72`). Com `_quietModeActive` preso (um `release` a menos em
qualquer caminho de erro dos 4 chamadores), o Core 1 fica desligado **para sempre** e o
watchdog de heartbeat não age.

### 🟠 R6 — PBUF pool reduzido 24→12 (economia de 18 KB, `arduino_pico_overrides`)

Streaming do histórico web + telemetria + BT simultâneos podem exaurir o pool → lwIP
descarta pbufs → stalls que parecem "Wi-Fi ruim". Hoje não há métrica para confirmar.

### 🟡 R7 — `delay()` no caminho quente do Core 0

43 ocorrências em `.cpp`; maioria é boot, mas há runtime: `delay(50)` a cada quiet mode
(`DisplayManager.cpp:592`) e `delay(50)` no `restartCore1`. Cada uma congela lwIP/BT.

### 🟡 R8 — Buffers `static` são Core-0-only por convenção implícita

Os `static` de `WebManager_History`/`Telemetry`/`Graph`/`scanHistoryFileV4` (fix de stack
`66d8df1`) são seguros **porque** o Core 0 é cooperativo e o Core 1 nunca os toca. A
invariante não está escrita — uso futuro em IRQ/Core 1 corrompe silenciosamente.

### 🟡 R9 — Heap compartilhado: fragmentação + contenção

313 usos de `String` no Core 0 e 25 no Core 1: contenção no lock do alocador gera jitter
de frame no display, e fragmentação em uptime longo ameaça `malloc` (sem métrica hoje).

---

## 5. Plano de tratamento

### Onda T0 — Instrumentar antes de mexer (1 tarde) → transforma hipótese em número

| Ação | Trata | Aceite |
|---|---|---|
| T0.1 `MetricsManager`: histograma de duração de `FLASH_OP` e **maior janela IRQ-off** (medir `save_and_disable_interrupts`→restore em torno do erase via hook/log) | R2 | `show metrics` exibe `flash_irqoff_max_ms` |
| T0.2 Métrica de heap: `free`, `largest block`, mínimos históricos; alerta em log <8 KB | R9 | visível no `show system info` |
| T0.3 lwIP stats: pico de uso do PBUF pool (compilar `LWIP_STATS` no env debug) | R6 | `pbuf_low_water` no `show net status` |
| T0.4 Log de causa de reboot: watchdog scratch com "último estado" (em quiet? em FLASH_OP? em enforce?) | R1,R5 | boot imprime o motivo do reset anterior |

### Onda T1 — Correções cirúrgicas (1–2 dias)

| Ação | Trata | Como | Aceite |
|---|---|---|---|
| T1.1 **Quiesce-antes-do-reset**: Core 1 checa `_quiescePlease` a cada volta e estaciona num ponto seguro (sem heap/fila/log) sinalizando `_parked`; Core 0 espera ≤200 ms e só então reseta; hard-reset vira **fallback de timeout** | R1 | teste dirigido: 500 saves seguidos sem travar `malloc`/fila |
| T1.2 **Banir heap/log/fila no regime permanente do Core 1**: trocar `String msg = tr(...)` por buffer fixo; `LOG_CODE` do Core 1 → ring lock-free (slot `dado+__dmb+flag`, já é o padrão da casa); `queue_try_add` só quando `!_quiescePlease` | R1 | `grep "String " DisplayManager_*` = 0 no caminho de render |
| T1.3 **BMP280 no I2C de hardware** (`Wire`/`Wire1`), opção D do doc de sensores; remove `critical_section` e libera pio0 | R3,C1 | BMP280 estável 24 h; `show metrics` sem quedas |
| T1.4 **Espalhar o `enforceStorageLimit`**: 1 arquivo por tick de manutenção (em vez de rajada de 4 s); nunca durante upload de telemetria ativo | R2 | `flash_irqoff_max_ms` cai; sem burst >100 ms |
| T1.5 **Watchdog do quiet mode**: `_quietModeActive` por > 15 s sem chamador → força `releaseQuietMode` + LOG_ERROR; auditoria pareada dos 4 sites request/release | R5 | injeção de vazamento se recupera sozinha |
| T1.6 **Assert de invariante de lock**: em build debug, `FLASH_OP` verifica `!mutex_try_enter(_stateMutex)`-owner (ou flag "stateMutexHeld") e aborta com log | R4 | teste que viola dispara o assert |
| T1.7 Converter `delay(50)` runtime em espera com `tight_loop_contents()` + teto, servindo BT/log quando aplicável | R7 | nenhuma pausa >10 ms fora de boot |

### Onda T2 — Estrutural (quando T0 confirmar necessidade)

| Ação | Trata | Nota |
|---|---|---|
| T2.1 **Batch de histórico**: acumular N registros em RAM (o mecanismo *pending* do V4 já existe) e gravar a cada 5 min → 5× menos janelas de erase | R2 | perda máx. aceitável em queda de energia = N min; tail-repair do patch V4 cobre o resto |
| T2.2 PBUF 12→16 **se** `pbuf_low_water` provar exaustão; alternativa: serializar telemetria × streaming web via `HeavyTaskGuard` | R6 | custo 6 KB RAM, decidir por dado |
| T2.3 De-`String` progressivo dos caminhos quentes do Core 0 (auth, telemetria) — já mapeado na análise inicial | R9 | fragmentação estável em soak |
| T2.4 Registrar as **invariantes de concorrência** em `docs/CONCURRENCY.md` (seção 6) e citá-las no CONTRIBUTING | R4,R8 | revisão de PR checa a lista |

### Protocolo de validação (fecha o plano)

1. **Soak 72 h** com T0 ativo: zero WDT-reboot; `flash_irqoff_max_ms` < 60 ms; heap mínimo estável.
2. **Save-storm**: 500 × `write memory` alternando com touch contínuo → Core 1 sempre volta, `malloc` nunca trava (valida T1.1/T1.2).
3. **Tempestade combinada**: streaming de histórico web + `tel sync` + CLI-BT + limpeza de storage forçada (flash a 87 %) simultâneos → sem 503 em cascata, sem queda de Wi-Fi (valida T1.4/T2.2).
4. **PIO**: `show sensor types` + leitura tripla (DS18B20 + DHT22 + BMP280) por 24 h sem fallback bit-bang no log (valida T1.3).

---

## 6. Invariantes de concorrência (regras permanentes do projeto)

1. **Core 1 nunca toca flash/LittleFS** (leituras de `.lng`/licença só no Core 0 — já é assim: `DisplayManager.cpp:184-190`).
2. **Core 1, em regime permanente, não usa heap, `LOG_CODE` nem `queue_*`** — apenas nos pontos seguros do quiesce (T1.2).
3. **Nunca segurar `_stateMutex` através de qualquer FLASH_OP/`LittleFS`** (T1.6 vigia).
4. **Todo `request*` tem `release*` no mesmo escopo (RAII quando possível)** — quiet mode, heavy task, read guard.
5. **`static` de trabalho em Web/Telemetry/Graph/Storage são Core-0-only e não reentrantes** — documentado no próprio buffer.
6. **Reset do Core 1 só após quiesce confirmado ou timeout explícito e logado.**
7. **Operações longas de flash são fatiadas** (≤1 arquivo/tick; WDT alimentado entre fatias).

---

*Riscos R1–R3 explicam, juntos, os três sintomas relatados; T0 existe para provar isso com
números antes das mudanças maiores. Nenhuma ação do plano remove funcionalidade existente.*
