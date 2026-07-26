# Análise de Flash e RAM — SIMUT v1.5.4-beta

**Data:** 2026-07-26
**Alvo:** Raspberry Pi Pico W (RP2040), framework arduino-pico (earlephilhower)
**Commit analisado:** `fa56142` (`release: v1.5.4-beta`), árvore limpa
**Hardware de bancada:** Pico W `E6642815E34C1824` em `/dev/ttyACM1`, 4 sensores ativos, WiFi conectado

Todos os números deste documento foram **medidos**, não estimados. Onde há estimativa,
está marcado explicitamente como tal. As medições de RAM em runtime foram tiradas do
dispositivo físico, e a maior recomendação foi **validada gravando o firmware no ferro**
e comparando lado a lado com a imagem oficial.

---

## 1. Sumário executivo

### O estado atual

| Recurso | Usado | Total | Livre |
|---|---:|---:|---:|
| **Flash (slot da aplicação)** | 1.039.740 B | 1.044.480 B | **4.740 B (0,45 %)** |
| **RAM estática** (`.data`+`.bss`+vetores) | 131.436 B | 262.144 B | — |
| **Heap** (o que sobra da SRAM) | 98.484 B em uso | 130.704 B | 32.220 B livres |
| **Maior bloco contíguo no heap** | — | — | **11.483 B** |
| **LittleFS** | 954.368 B | 1.048.576 B | 94.208 B (9,0 %) |

O projeto está a **4.740 bytes** de não linkar. Não é um alarme retórico: é 0,45 % de
folga num slot de 1020 KB, e qualquer feature nova esbarra nisso.

### O achado principal

**O Bluetooth está linkado no firmware e nunca é usado.**

`platformio.ini` passa `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` em `[pico_base]`, o que
faz o framework escolher a variante `liblwip-bt.a` (lwIP + BTstack completo) e o blob de
rádio *combinado* WiFi+BT. Ao mesmo tempo, `BluetoothManager.cpp` é **excluído do
`build_src_filter` de todos os ambientes que embarcam** (release, debug, alpha), e
`simut_config.h` mantém `SIMUT_BLUETOOTH = 0`, o que reduz a classe inteira a stubs vazios
em `BluetoothManager.h`. Ou seja: paga-se a pilha inteira para não chamá-la uma vez.

Medido, com A/B de um único flag:

| | Flash usada (real) | Folga real | RAM estática | Heap |
|---|---:|---:|---:|---:|
| **1.5.4-beta como está** | 1.039.740 B | **4.740 B** | 131.436 B | 130.704 B |
| **Sem `PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`** | 975.008 B | **69.472 B** | 115.020 B | 147.120 B |
| **Ganho** | **−64.732 B** | **+64.732 B (14,7×)** | **−16.416 B** | **+16.416 B** |

E validado no dispositivo físico, mesma configuração, mesmo teste de carga:

| Medição no ferro | 1.5.4-beta (com BT) | Sem BT | Ganho |
|---|---:|---:|---:|
| Heap livre (idle, TLS já ativo) | 32.220 B | 48.860 B | **+16.640 B** |
| Heap livre (mínimo sob carga) | 32.012 B | 48.660 B | +16.648 B |
| **Maior bloco contíguo** | 11.483 B | **35.776 B** | **+24.293 B (3,1×)** |
| Requisições HTTP / erros | 679 / 0 | 678 / 0 | — |
| Bytes servidos | 9.584.085 | 9.569.970 | — |
| Leituras de sensor / erros | 2409 / 0 | 726 / 0 | — |
| Core1 exposto a flash | 0 ops | 0 ops | — |

O salto no **maior bloco contíguo** é mais importante que o heap livre total. O guarda de
telemetria em `TelemetryManager.cpp:251` aborta o ciclo abaixo de 24.576 B livres, e o
comentário logo acima explica por quê: o BearSSL precisa de um bloco contíguo grande e a
métrica de heap livre sozinha ignora fragmentação. Hoje o dispositivo opera com 11,5 KB de
maior bloco — é exatamente isso que obrigou o `setBufferSizes(4096, 512)` em
`TelemetryManager.cpp:665`, já que o padrão pede 16 KB contíguos. Sem BT, 35,8 KB
contíguos removem essa restrição.

### As três recomendações de maior retorno

| # | Ação | Ganho de flash | Ganho de RAM | Esforço | Risco |
|---|---|---:|---:|---|---|
| 1 | Remover `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` + `lib_ignore = SerialBT` | **64.732 B** | **16.416 B** | 2 linhas | Baixo — validado no ferro |
| 2 | Mover páginas `WebUI_GZ` para o LittleFS (`FS_PAGES`) — candidata: `HIST_PAGE`, 18.940 B | **até ~83.000 B** | 0 | Médio | Médio — LittleFS a 91 % |
| 3 | Alocar sob demanda o scratch de histórico só usado no boot | 0 | **~14.600 B** | Médio | Baixo |

Aplicando só a #1, a folga de flash sai de 0,45 % para 6,65 % e o heap ganha 12,6 %.

> **A recomendação #1 foi aplicada e gravada em 2026-07-26.** `platformio.ini` não passa
> mais `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` e ganhou `lib_ignore = SerialBT` em
> `[pico_base]` (repetido em `[env:pico_w_alpha]`, que declara a própria lista e por isso
> substituiria a herdada). Detalhes da gravação e da validação na seção 8.1.

---

## 2. Metodologia — por que o número do PlatformIO não serve

O PlatformIO reporta, para esta build:

```
RAM:   [=====     ]  50.1% (used 131212 bytes from 262144 bytes)
Flash: [==========]  98.3% (used 1026816 bytes from 1044480 bytes)
```

Os 1.026.816 B não batem com a realidade: a imagem ocupa **1.039.740 B**. Faltam 12.924 B
na conta porque o PlatformIO ignora:

1. A seção **`.ota` (10.228 B)**, que fica em endereço fixo no começo do flash.
2. O **padding de alinhamento** entre `.text` e `.rodata` (892 B nesta build).
3. A cópia de carga de `.data`, contabilizada de forma inconsistente.

A `.rodata` é alinhada em página de 4096 B. O efeito prático é traiçoeiro: adicionar ou
remover poucos bytes de asset **não muda a folga** até cruzar o degrau — que é exatamente
o sintoma do `region FLASH overflowed by N bytes` em que o `N` não se move quando você
encolhe os assets.

### Medição correta

```bash
arm-none-eabi-size -A .pio/build/pico_w_release/firmware.elf
# folga = (0x10000000 + 1044480) − (addr(.rodata) + size(.rodata) + size(.data))
```

O `.data` entra porque sua **cópia de carga** mora no flash, logo depois da `.rodata`.

Para o mapa por objeto foi gerado um linker map real, relinkando com `-Wl,-Map`:

```bash
# a linha de link sai de `pio run -e pico_w_release -v`
arm-none-eabi-g++ ... -Wl,-Map=firmware.map -o firmware_map.elf
```

O map é a única fonte confiável de atribuição por arquivo-objeto, porque com
`--gc-sections` somar símbolos dos `.o` conta código que foi descartado.

### Uma armadilha do próprio map

O map lista `126.445 B` em seções `.rodata.str1.*`. Esse **não** é o custo real: essas
seções são `SHF_MERGE|SHF_STRINGS` e o linker deduplica strings entre objetos. A varredura
do binário final encontra **67.832 B** de literais de string efetivamente presentes na
imagem. É esse o número a usar (6,5 % do slot).

---

## 3. Mapa de Flash

### 3.1 Seções

| Seção | Tamanho | Nota |
|---|---:|---|
| `.boot2` | 256 B | segundo estágio do bootloader |
| `.ota` | 10.228 B | metadados de OTA, endereço fixo (invisível ao PlatformIO) |
| `.partition` | 1.804 B | 1.788 B disso é padding |
| `.text` | 601.220 B | código |
| padding `.text`→`.rodata` | 892 B | alinhamento de página de 4 KB |
| `.rodata` | 417.792 B | constantes (inclui o blob do rádio e as páginas web) |
| `.data` (cópia de carga) | 7.548 B | |
| **Total ocupado** | **1.039.740 B** | de 1.044.480 B → **folga 4.740 B** |

### 3.2 Por biblioteca (do linker map)

| Componente | `.text` | `.rodata` | `.data` | Total |
|---|---:|---:|---:|---:|
| `liblwip-bt.a` (lwIP + BTstack + blob CYW43) | 40.306 | 251.109 | 117 | **291.532** |
| Bibliotecas do projeto (LittleFS, WebServer, WiFi, LEAmDNS, HTTPClient…) | 89.000 | 9.015 | 100 | 98.115 |
| BearSSL | 64.748 | 15.917 | 0 | 80.665 |
| newlib (printf/scanf/strtod/dtoa/locale) | 60.000 | 6.095 | 5.924 | 72.019 |
| Core arduino-pico | 51.242 | 9.958 | 134 | 61.334 |
| SDK pico | 26.914 | 430 | 3.614 | 30.958 |
| libstdc++ / libgcc | 6.680 | 983 | 8 | 7.671 |
| **Aplicação (`src/`)** | ~262.000 | ~163.000 | ~400 | **~425.500** |

### 3.3 Os maiores símbolos individuais

| Bytes | Símbolo | Comentário |
|---:|---|---|
| 232.408 | `wb43439A0_7_95_49_00_combined` | firmware do rádio CYW43439 — **22,3 % do slot**. Versão *combinada* WiFi+BT. Sem BT vira `w43439A0_...` com **225.240 B** (−7.168 B) |
| 18.940 | `WebUI_GZ::HIST_PAGE_GZ` | página `/history` comprimida |
| 10.314 | `WebUI_GZ::LANG_JS_GZ` | |
| 8.192 | `__bluetooth_tlv` | região reservada para o TLV do BTstack — **nunca usada** |
| 7.761 | `WebUI_GZ::ALARMS_PAGE_GZ` | |
| 6.970 | `cyw43_btfw_43439` | firmware BT do rádio — **nunca usado** |
| 5.909 | `WebUI_GZ::FILE_PAGE_GZ` | |
| 5.514 | `WebUI_GZ::LOGIN_PAGE_GZ` | |
| 5.450 | `WebUI_GZ::DASH_PAGE_GZ` | |
| 4.284 | `WebUI_GZ::LICENSE_PAGE_GZ` | |

**Total das páginas `WebUI_GZ` embutidas: 70.902 B** (10 páginas + CSS) na linha de base
analisada. A `/config` estava fora, no LittleFS, via `FS_PAGES` — o mecanismo existe e
está provado nas duas direções.

> **Estado atual (2026-07-26, seção 8.2):** a `/config` voltou para o firmware, então são
> **11 páginas + CSS = 83.054 B** embutidos. O `FS_PAGES` ficou vazio, mas funcional.

### 3.4 Código da aplicação por subsistema

| Subsistema | Flash |
|---|---:|
| Pool de literais de string do programa (mesclado) | ~55.300 B |
| UI / Display (TFT, touch, temas, fontes, i18n) | 88.608 B |
| Ativos web embutidos (`WebUI_GZ`) | 70.902 B (hoje 83.054 — ver 8.2) |
| Aplicação / CLI (`AppManager*`, `CommandManager`, `CommandParser`) | ~74.300 B |
| Servidor web (handlers) | 63.347 B |
| Armazenamento + codecs de histórico | 25.031 B |
| Telemetria | 16.633 B |
| Log + métricas + som + sondas | 13.055 B |
| Sensores | 7.896 B |
| OTA | 5.656 B |
| Rede (camada da aplicação) | 4.685 B |

> O pool de 55.320 B aparece atribuído a `AppManager_Boot.cpp.o` no map
> (`.rodata._ZN10AppManager5setupEv.str1.1`). Isso é artefato do merge do linker: strings
> deduplicadas do programa inteiro vão para o primeiro objeto que as fornece. Não é código
> do `AppManager_Boot`.

---

## 4. Mapa de RAM

O RP2040 tem 264 KB de SRAM: 256 KB no banco principal + 2 × 4 KB nos bancos *scratch*.
Os stacks dos dois núcleos (2.048 B cada) moram nos bancos scratch, fora do orçamento
principal. O heap **é o que sobra** do banco principal depois da RAM estática.

### 4.1 Layout

| Região | Bytes | % dos 256 KB |
|---|---:|---:|
| `.ram_vector_table` | 192 | 0,07 % |
| `.data` | 7.548 | 2,88 % |
| `.bss` | 123.660 | 47,17 % |
| `.noinit` + `.uninitialized_data` | 36 | 0,01 % |
| **RAM estática** | **131.436** | **50,14 %** |
| `.heap` | 130.704 | 49,86 % |

Cada byte tirado da `.bss` vira um byte de heap. Não há desperdício de arredondamento.

### 4.2 Os maiores consumidores de `.bss`

| Bytes | Símbolo | Categoria |
|---:|---|---|
| 18.387 | `memp_memory_PBUF_POOL_base` | lwIP — pool de PBUF (12 envelopes) |
| 16.403 | `ram_heap` | lwIP — `MEM_SIZE` 16 KB |
| 10.240 | `AppManager::preloadMinMax()::s_batchVals` | scratch de histórico, **só usado no boot** |
| 7.400 | `hci_connection_storage` | **BTstack — nunca usado** |
| 4.096 | `ota::s_applier_buf` | buffer do aplicador de OTA |
| 2.624 | `AppManager::renderGraphOptimized(...)::pkg` | scratch de histórico |
| 2.624 | `AppManager::openStatsScreen(int)::pkg` | scratch de histórico |
| 2.504 | `cyw43_state` | driver do rádio |
| 2.056 | `hci_stack_static` | **BTstack — nunca usado** |
| 2.048 × 11 | `hdrBuf`, `v4st`, `g4st`, `pState`, `chunkBuf`, `rdBuf`… | scratch de histórico (ver 4.3) |

**Subtotais por dono:**

| Dono | `.bss` |
|---|---:|
| Pilha lwIP + WiFi (pools, `ram_heap`, `cyw43_state`, DNS, USB) | 43.290 B |
| **Scratch de decodificação de histórico** | **43.392 B** |
| **BTstack** | **15.617 B** |
| OTA (`s_applier_buf` + `s_tmp_pool`) | 6.144 B |
| `_licenseBuf` (duplica o @LICENSE do pack de idioma) | 2.048 B |
| Restante (core, temas, display, sensores…) | ~13.000 B |

### 4.3 O scratch de histórico — 43,4 KB de `.bss`

Cinco caminhos diferentes decodificam arquivos `.sim4`, e cada um declara seu próprio
conjunto de buffers `static`:

| Função | Buffers | Total |
|---|---|---:|
| `AppManager::preloadMinMax()` | `s_batchVals[20][64]` 10.240 + `pState` 2.048 + `hdrBuf` 2.048 + `pRdBuf` 256 | **14.592 B** |
| `AppManager::renderGraphOptimized()` | `pkg` 2.624 + `hdrBuf` 2.048 + `g4st` 2.048 + `g4vals` 512 + `g4RdBuf` 256 | **7.488 B** |
| `WebManager::handleApiHistoryMulti()` | `v4st` 2.048 + `hdrBuf` 2.048 + `chunkBuf` 2.048 + `v4vals` 512 + `rdBuf` 256 | **6.912 B** |
| `StorageManager` (`scanHistoryFileV4` + `createHistoryFileV4WithSchema`) | 2 × `hdrBuf` 2.048 + `rdBuf` 2.048 + `values` 512 + `buf` 256 | **6.912 B** |
| `TelemetryManager::collectBatch()` | `v4st` 2.048 + `hdrBuf` 2.048 + `v4vals` 512 + `rdBuf` 256 | **4.864 B** |
| `AppManager::openStatsScreen()` | `pkg` 2.624 | **2.624 B** |

O `HistV4State` sozinho tem 2.048 B, e há **quatro instâncias estáticas** dele.
A estrutura é dimensionada por `HIST_V4_MAX_MEASUREMENTS = 64` e
`HIST_V4_MAX_SENSORS = 32`, enquanto a configuração aceita `MAX_SENSORS = 16` slots.
Um sensor produz no máximo 3 canais (T+H+P), então o teto realista é 16 × 3 = 48
medições e 17 sensores (16 + ambiente).

### 4.4 Heap em runtime — medido no dispositivo

Composição estimada dos 98.484 B em uso na imagem oficial:

| Item | Bytes | Origem |
|---|---:|---|
| `GFXcanvas16(320, 45)` | 28.800 | `DisplayManager.cpp:953` — `malloc(w*h*2)` |
| `GFXcanvas16(140, 40)` | 11.200 | `DisplayManager.cpp:954` |
| Pack de idioma residente | ~15.700 | `DisplayManager_LangParser.cpp:113` (após a excisão do `@WEBDICT`) |
| Contexto TLS BearSSL + buffers 4096/512 | ~9.500 | `TelemetryManager.cpp:665` |
| Restante (`String`s, WebServer, drivers, overhead do alocador) | ~33.300 | — |

**Os dois canvas somam 40.000 B — 30,6 % do heap inteiro**, e são alocados no boot e
nunca liberados.

### 4.5 Fragmentação — e a descoberta de que ela é do boot

Teste de carga com 3 threads martelando `/api/lang` por 70 s, amostrando `show metrics`
a cada 2 s:

| | Imagem oficial | Sem BT |
|---|---:|---:|
| Heap livre em repouso | 32.220 B | 48.860 B |
| Heap livre mínimo sob carga | 32.012 B | 48.660 B |
| **Variação total sob carga** | **208 B** | **200 B** |
| Maior bloco em repouso | 11.483 B | 35.776 B |
| Maior bloco mínimo sob carga | 11.409 B | 35.910 B |
| Requisições OK / erro | 679 / 0 | 678 / 0 |
| Pico de PBUF | 6 / 12 | 4 / 12 |

**Conclusão: não há vazamento no caminho HTTP.** O heap se move menos de 210 B sob 9,6 MB
de tráfego servido. A fragmentação (32 KB livres mas só 11,5 KB contíguos, fator 2,8×)
é um estado **estabelecido durante o boot** e depois congelado — não é degradação por uso.

Isso muda o alvo: não adianta caçar vazamento no runtime, o que importa é a **ordem e o
tamanho das alocações do boot**. E o mecanismo mais suspeito está identificado:

`DisplayManager_LangParser.cpp:367` faz `malloc(newSize)` para a excisão do `@WEBDICT`
**enquanto ainda segura o buffer original**. Para o pack pt-BR isso é um pico transitório
de 28.077 + 13.953 ≈ **42 KB**, e ao liberar o grande sobra um buraco de 28 KB *abaixo* de
um bloco vivo de 14 KB. A economia de 14 KB do fix é real e está medida — mas o custo
colateral é um heap partido ao meio no instante do boot.

---

## 5. Achados

### F1 — Bluetooth linkado e nunca usado · **64.732 B de flash + 16.416 B de RAM**

**Evidência.** `platformio.ini:56` passa `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` em
`[pico_base]`, herdado por release, debug e alpha. `BluetoothManager.cpp` está excluído do
`build_src_filter` dos três. `simut_config.h` mantém `SIMUT_BLUETOOTH = 0`, e
`BluetoothManager.h` reduz a classe a stubs vazios nesse caso. A única referência viva é
`BluetoothManager _btMgr;` em `CommandManager.h:100` — um objeto de stubs.

O que entra no binário por causa do flag:

| Item | Flash | RAM |
|---|---:|---:|
| Blob combinado do rádio (`wb43439…` vs `w43439…`) | 7.168 B | — |
| `hci.c.o` | 27.441 B | — |
| `btstack_flash_bank.cpp.o` | 9.086 B | — |
| `__bluetooth_tlv` (região reservada) | 8.192 B | — |
| `cybt_shared_bus.c.o` | 7.690 B | — |
| `cyw43_btfw_43439` | 6.970 B | — |
| Resto do BTstack (`hci_cmd`, `tlv_flash_bank`, run loop, `l2cap`, `sm`…) | ~9.000 B | — |
| `hci_connection_storage` | — | 7.400 B |
| `hci_stack_static` | — | 2.056 B |
| `hci_packet_with_pre_buffer`, `hids_client_storage`, `avrcp/avdtp/hfp/l2cap pools` | — | ~6.100 B |
| **Total medido (A/B de build)** | **64.732 B** | **16.416 B** |

**Correção.**

```ini
[pico_base]
build_flags =
    -DPICO_CYW43_SUPPORTED=1
    ; -DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH   ← remover
    ...
lib_ignore = SerialBT
```

O `lib_ignore = SerialBT` é **obrigatório**. Sem ele o build quebra em
`_needsbt.h:4: static assertion failed: This library needs Bluetooth enabled`: o LDF do
PlatformIO enxerga `#include <SerialBT.h>` dentro do `#if SIMUT_BLUETOOTH` em
`BluetoothManager.h` e puxa a biblioteca mesmo com o ramo desligado.

**Se um dia o BT voltar**, o caminho certo é um ambiente separado
(`[env:pico_w_bt]` = release + os dois flags + `BluetoothManager.cpp` no filter), não o
flag no `[pico_base]`.

**Validado no ferro.** Imagem gravada via `pio run -t upload`, boot limpo, WiFi no mesmo
IP, 4 sensores lendo, telemetria TLS enviando, 678 requisições HTTP sem erro, `Core1
exposed: 0`, LittleFS intacto. A imagem oficial foi restaurada ao fim do teste.

---

### F2 — 43,4 KB de `.bss` em scratch de histórico, com 14,6 KB usados só no boot

**Evidência.** `preloadMinMax()` é chamada em exatamente dois lugares
(`AppManager_Boot.cpp:675` e `:709`), **ambos dentro de `AppManager::setup()`**. Os
14.592 B dos seus buffers ficam reservados pelo resto do uptime sem serem tocados.

**Correção de menor risco (não mexe em concorrência):**

1. **Alocar sob demanda o scratch de `preloadMinMax`.** Como só roda no boot, um
   `malloc`/`free` em volta devolve **14.592 B** ao heap permanentemente. É a mudança mais
   barata e a de melhor relação ganho/risco desta seção.
2. **Reduzir as dimensões de `HistV4State`.** `HIST_V4_MAX_MEASUREMENTS` 64 → 48 e
   `HIST_V4_MAX_SENSORS` 32 → 17 encolhem a struct de 2.048 para ~1.530 B. Com 4 instâncias
   estáticas, ~2.070 B; com `s_batchVals[20][48]`, mais 2.560 B. Total ~4,6 KB.
   **Atenção:** mudar essas constantes muda a capacidade máxima do *formato de arquivo* —
   precisa de verificação contra os `.sim4` existentes e dos testes `native_history_v4`.

**Sobre unificar tudo numa arena compartilhada.** É tentador (43 KB → ~7 KB) e a análise
inicial sugere que é seguro, mas exige cuidado documentado:

- Os cinco caminhos rodam **todos no Core 0** e o laço da aplicação está ou dentro do
  handler web ou na seção app/telemetria, nunca nos dois.
- O `_lightYieldCb` chamado pelos handlers de histórico (`WebManager_History.cpp`, 4
  pontos) faz `feedWdt()` e, a cada 3 s, `_sensorMgr->update()` + `updateLiveDisplay()` —
  **não** alcança `renderGraphOptimized` nem `preloadMinMax`.
- O `_yieldCb` pesado (que chama `core0Yield()`, o qual *pode* disparar renderização de
  gráfico) está **registrado em `AppManager_Boot.cpp:650` e nunca é invocado** —
  `grep -rn "_yieldCb" src/` só encontra a declaração e o setter. O comentário em
  `AppManager_Events.cpp:41` ("can be called from within web handlers via `_lightYieldCb`")
  está **desatualizado** e descreve um caminho que não existe mais.
- Há aninhamento real: `scanHistoryFileV4` é chamada de dentro de outros caminhos
  (`StorageManager.cpp:2105`, `:2270`) com seu próprio `hdrBuf`. Uma arena precisa de
  disciplina de pilha (empilha/desempilha), não de um único slot global.

Ou seja: a arena é viável, mas o que a torna segura hoje é uma fiação morta. Se o
`setYieldCallback` voltar a ser chamado no futuro, a arena vira corrupção silenciosa.
**Recomendação:** ou remover o `_yieldCb` morto e a fiação junto (deixando a exclusão
mútua explícita e testável), ou proteger a arena com um guarda de posse que caia num
buffer de fallback quando já estiver tomada.

---

### F3 — 70,9 KB de páginas web embutidas no flash da aplicação (hoje 83,1 KB)

**Evidência.** 10 páginas + CSS em `WebUI_GZ`, somando 70.902 B de `.rodata`:
`HIST` 18.940, `LANG_JS` 10.314, `ALARMS` 7.761, `FILE` 5.909, `LOGIN` 5.514, `DASH` 5.450,
`LICENSE` 4.284, `USR` 4.005, `NET` 3.878, `FORCE_CHPASS` 3.414, `STYLE_CSS` 1.433.

**O mecanismo já existe e está provado.** `tools/build_webui_gz.py` tem o dicionário
`FS_PAGES`; uma página listada ali é gravada em `data/web/<nome>.gz` em vez de virar array
PROGMEM, e `WebManager::serveProtectedFsPage` a transmite do LittleFS com
`Content-Encoding: gzip`. A `/config` percorreu esse caminho nos dois sentidos.

**A restrição real é o LittleFS, não o mecanismo.** A partição está com **954.368 de
1.048.576 B usados (91,0 %)** — 94.208 B livres. Mover as 70,9 KB deixaria ~23 KB, e o GC
já dispara em 87 %. Ordem correta: **primeiro reduzir o histórico sintético de ~50 dias,
depois migrar as páginas**. Migrar por partes (começando por `HIST_PAGE`, 18,9 KB, a maior)
permite parar em qualquer ponto.

> **Revisão de 2026-07-26.** Com a folga de flash em 69.472 B depois do F1, a decisão foi
> **na direção oposta** para a `/config`: ela voltou para o firmware (seção 8.2). O motivo
> não foi espaço, foi robustez — num dispositivo recém-formatado o arquivo não existe e
> `/config`, que é a página usada para configurar o aparelho, respondia *"Page asset
> missing"*. O `FS_PAGES` continua sendo a saída correta se a folga apertar de novo, mas o
> candidato passa a ser `HIST_PAGE` (18.940 B): maior, e não é necessária para levantar um
> device. O porém é que `/history` é aberta com muito mais frequência que `/config`, então
> falha o critério "raramente aberta" documentado no próprio script — **medir a latência
> extra antes de mover**.

> **Não desligue o gzip para "economizar".** A `CFG_PAGE` crua tem 55.294 B contra
> 10.637 B comprimida (5,2×). O ganho vem de mudar de partição, não de descomprimir.

> **Nunca rode `pio run -t uploadfs` num dispositivo com dados** — reformata a partição e
> leva junto `/history`, logs e `calib.csv`. Publique página por página via
> `POST /api/upload` (o caminho de destino vai no *filename*; o parâmetro `dir` é ignorado).

---

### F4 — O knob de mDNS documentado não funciona · 15.036 B inalcançáveis

**Evidência.** `simut_config.h:210` faz `#define SIMUT_MDNS 1`, mas `NetworkManager.cpp:156`,
`:204` e `NetworkManager.h:22`, `:147` testam com **`#ifdef SIMUT_MDNS`**. Como o símbolo
está sempre definido, o valor nunca é consultado.

Provado por build A/B: compilar com `-DSIMUT_MDNS=0` produz uma imagem de **tamanho
idêntico** (975.008 B reais, `.bss` idêntica) e o `nm` continua achando **236 símbolos
`MDNSResponder`** — exatamente os mesmos 236 do build sem o flag. Apenas 22 bytes de
962.972 diferem entre as duas imagens.

Além disso o comentário em `NetworkManager.cpp:155` está errado nos dois sentidos:
diz *"Disabled by default — enable with -DSIMUT_MDNS to save ~196KB flash"*, quando o
responder está **ligado** por padrão e custa **15.036 B**, não 196 KB.

**Correção:** trocar os quatro `#ifdef SIMUT_MDNS` por `#if SIMUT_MDNS` e corrigir o
comentário. O knob passa a valer 15.036 B para quem não usa `SIMUT.local`.

---

### F5 — `NDEBUG` não definido no release · 6.600 B

**Evidência.** O build de release não passa `-DNDEBUG`, então os `assert()` do LittleFS,
do pico-SDK e do newlib ficam armados. O binário carrega os textos das expressões
(`lfs->cfg->inline_max == (lfs_size_t)-1 || …`) e **caminhos absolutos da máquina de
build** (`/home/angelo/.platformio/packages/framework-arduinopico/...`, 7 ocorrências,
788 B) — que além do custo vazam o layout do ambiente de quem compilou.

Medido: `-DNDEBUG` leva o build sem BT de 962.972 → **956.372 B (−6.600 B)**.

**Ressalva honesta:** desarmar asserts troca "para com mensagem" por "segue com estado
inválido". Num sistema que já teve bugs de concorrência caros de achar, isso não é
gratuito. Recomendação: aplicar em release, **manter armado** em `pico_w_asserts`.

---

### F6 — Um único `sscanf` custa 7.532 B

**Evidência.** `grep` sobre os `.o` da aplicação mostra **um** arquivo referenciando
`sscanf`: `AppManager_CmdHandlers.cpp:46`, com `sscanf(cmd.strVal2, "%d,%d", …)`.
Pela cadeia do map, isso puxa `libc_a-sscanf.o` → `__ssvfscanf_r` → `svfscanf.o` (5.071 B)
→ `svfiscanf.o` (4.746 B) → `mbrtowc.o` + locale.

O irônico é que o projeto já sabe disso: `AppManager_CmdHandlers.cpp:478-479` documenta
*"Substituiu sscanf(...) porque sscanf puxa __ssvfscanf_r/__ssvfiscanf_r (~12KB de flash)"*.
Ficou uma chamada para trás.

**Medido** substituindo por `strtol` (patch temporário, revertido): 962.972 → **955.440 B
(−7.532 B)**, sem mudança de comportamento para as entradas válidas.

**Nota relacionada:** `atof` também está linkado, mas vem de
`libFrameworkArduino.a(String.cpp.o)` — é `String::toFloat()`/`toDouble()` do core, não
código do SIMUT. Não dá para remover sem patchar o framework. O `ParseFloat.h` do projeto
já evita `atof` no código da aplicação; o custo restante é do core.

---

### F7 — 40 KB de canvas no heap (30,6 %)

**Evidência.** `DisplayManager.cpp:953-954` aloca `new GFXcanvas16(320, 45)` e
`new GFXcanvas16(140, 40)`. O construtor do Adafruit_GFX faz `malloc(w * h * 2)`:
28.800 B e 11.200 B. Alocados no boot, nunca liberados.

**Opções, em ordem de risco:**

1. **Mover para `.bss`.** Usar o construtor de 3 argumentos
   (`GFXcanvas16(w, h, false)`) e apontar `buffer` para arrays estáticos. O total de SRAM
   não muda, mas tira 40 KB do heap e **elimina a maior causa de fragmentação do boot** —
   que é exatamente o problema medido em 4.5. Risco baixo, ganho estrutural alto.
2. **Encolher o canvas grande.** 320×45 é uma faixa de painel; 320×24 custaria 15.360 B
   (−13.440 B). Exige revisar `blitCanvas`/`beginScreenRender` e o layout dos painéis.
3. **Compartilhar um só canvas** entre os dois usos, com o maior dimensionando. Economiza
   11.200 B mas serializa renderizações que hoje podem se sobrepor — precisa de auditoria.

---

### F8 — O pack de idioma é carregado sempre, e o pico do boot parte o heap

**Evidência.** `AppManager_Boot.cpp:461` chama `DisplayManager::findAndLoadLangFile()`
**incondicionalmente**, antes de `setLanguage(cfg.displayLang)`. A função
(`DisplayManager_LangParser.cpp:441`) varre `/lang/` e carrega o **primeiro arquivo em
ordem alfabética** — a configuração de idioma não entra na decisão, e mudar para inglês
não chama `unloadLang()`. Quem usa inglês paga o pack inteiro.

Custo residente hoje (com a excisão do `@WEBDICT` já aplicada): ~15,7 KB.
Confirmado no ferro: `GET /api/lang` devolve 14.115 B, o tamanho do `@WEBDICT` pt-BR.

**Dois problemas separados:**

1. **Carga incondicional.** Consultar `cfg.displayLang` antes de carregar, e chamar
   `unloadLang()` ao trocar para inglês, devolve ~15,7 KB ao heap para usuários en.
2. **Ordem alfabética.** `data/lang/` do repositório contém `language_es-ES.lng` (18.181 B)
   **e** `language_pt-BR.lng` (28.075 B). Se os dois forem parar no dispositivo, ele carrega
   **es-ES em silêncio**, porque ordena antes. O `LOG_WARN` existe, mas a escolha é
   arbitrária.
3. **O pico transitório.** A excisão em `DisplayManager_LangParser.cpp:367` aloca o buffer
   novo enquanto segura o velho (~42 KB de pico) e deixa um buraco de 28 KB no heap. Fazer
   o parse em duas passadas — medir o tamanho final, alocar só ele, copiar — elimina o pico
   e o buraco. É a intervenção mais promissora contra a fragmentação de 4.5.

**Bônus:** o `_licenseBuf` estático de 2.048 B duplica o `@LICENSE` que já está no buffer
do pack. Sob demanda, são 2 KB de `.bss` de volta.

---

### F9 — O patch do lwIP mora fora da árvore de build

**Evidência.** `tools/arduino_pico_overrides/` guarda um `lwipopts.h` patchado
(`PBUF_POOL_SIZE` 24 → 12, `LWIP_STATS`/`MEMP_STATS` ligados) que precisa ser aplicado
**manualmente** por `patch.sh` sobre
`~/.platformio/packages/framework-arduinopico/include/lwipopts.h`.

Isso significa que **um clone limpo + PlatformIO limpo compila com `PBUF_POOL_SIZE = 24`**,
ou seja, +18 KB de `.bss` e sem os contadores de PBUF que `show net status` reporta.
Os 18.387 B economizados dependem de um passo manual não verificado pelo build.

**Correção:** adicionar uma verificação em `extra_scripts` (como já se faz com
`check_flash_probe.py`) que falhe o build se o `lwipopts.h` instalado não tiver o patch.
Custa ~20 linhas e transforma uma armadilha silenciosa em erro de build.

**Sobre reduzir mais o pool:** o pico medido foi **6 de 12** sob carga de 3 threads e
**1 de 12** em repouso, com 0 falhas de alocação. Há margem para 8 (economizando ~6,1 KB),
mas o pico de 6 foi obtido com um único cliente HTTP sintético. Recomendo **não** mexer
sem um teste com o navegador real carregando a página de histórico, que é o caminho de
maior pressão de PBUF.

---

### F10 — A fragmentação do heap nasce no boot, não no uso

Já detalhado em 4.5. O resumo operacional: **32.220 B livres, 11.483 B contíguos**. O
guarda de telemetria exige 24.576 B livres e o BearSSL precisou ser reconfigurado para
4096/512 porque o padrão pede 16 KB contíguos — que hoje não existem.

Os dois candidatos a causa estão em F7 (40 KB de canvas alocados cedo) e F8 (pico de 42 KB
do pack de idioma que deixa um buraco de 28 KB). Ambos são do boot, ambos são corrigíveis
sem tocar em concorrência, e a medição sem BT mostra que arrumar a vizinhança do boot
já leva o maior bloco de 11,5 KB para 35,8 KB.

---

## 6. Não-achados — o que foi testado e **não** economiza nada

Vale tanto quanto os achados, porque evita trabalho perdido.

| Hipótese | Resultado medido | Por quê |
|---|---|---|
| `-fno-exceptions -fno-rtti -fno-unwind-tables` | **0 B** (962.972 → 962.972) | O maquinário de EH vem dos `.a` pré-compilados do framework, não do código da aplicação. Recompilar só `src/` não muda nada. |
| Remover `-Wl,-u,_printf_float` | **0 B** (962.972 → 962.972) | O float no printf já é puxado pelos 39 usos reais de `%f`/`%.Nf` no código. O flag é redundante, não causador. |
| `-DSIMUT_MDNS=0` | **0 B** — imagem de tamanho idêntico, 236 símbolos mDNS presentes | O código testa `#ifdef`, não `#if`. Ver F4. |
| Tirar o gzip das páginas web | **piora muito** | `CFG_PAGE` crua 55.294 B vs 10.637 B comprimida (5,2×). |

E uma ressalva de ferramenta: **`pico_w_debug` não linka**. Em `-Og` e sem
`--gc-sections` a imagem estoura o slot em ~69 KB. Com F1 aplicado (+64,7 KB de folga)
esse ambiente passa a ficar a ~4 KB de linkar — vale reavaliar depois.

---

## 7. Plano priorizado

| # | Ação | Flash | RAM | Esforço | Risco | Status |
|---|---|---:|---:|---|---|---|
| 1 | Remover `PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` + `lib_ignore = SerialBT` | **−64.732** | **−16.416** | 2 linhas | Baixo | **Medido no ferro** |
| 2 | `preloadMinMax`: scratch sob demanda | 0 | **−14.592** | Baixo | Baixo | Analisado |
| 3 | Canvas do display → `.bss` (ou encolher 320×45) | 0 | 0 líquido, **−40.000 do heap** | Médio | Baixo | Analisado |
| 4 | Migrar `WebUI_GZ` → LittleFS (`FS_PAGES`) | até **−83.054** | 0 | Médio | Médio (LittleFS 91 %) | Mecanismo provado nos 2 sentidos; ver revisão em F3 |
| 5 | `#ifdef SIMUT_MDNS` → `#if` (destrava o knob) | −15.036* | −? | 4 linhas | Baixo | Provado que hoje é no-op |
| 6 | `-DNDEBUG` no release | **−6.600** | 0 | 1 linha | Médio (perde asserts) | Medido |
| 7 | Trocar o `sscanf` de `AppManager_CmdHandlers.cpp:46` | **−7.532** | 0 | Baixo | Baixo | Medido |
| 8 | Pack de idioma: respeitar `cfg.displayLang`, parse em 2 passadas | 0 | **−15.700** (en) + fim do buraco de 28 KB | Médio | Baixo | Analisado |
| 9 | `_licenseBuf` sob demanda | 0 | −2.048 | Baixo | Baixo | Analisado |
| 10 | Guarda de build para o patch do `lwipopts.h` | 0 | protege −18.387 | Baixo | Nenhum | Analisado |
| 11 | `HIST_V4_MAX_MEASUREMENTS` 64→48, `MAX_SENSORS` 32→17 | 0 | −4.600 | Médio | **Médio — muda o teto do formato** | Analisado |
| 12 | Arena compartilhada de histórico | 0 | até −36.000 | Alto | **Alto** — ver F2 | Não recomendado sem antes limpar o `_yieldCb` morto |

\* O ganho de mDNS só se materializa se o responder for de fato desligado; a correção do
`#ifdef` apenas torna o knob funcional.

**Combinando 1 + 6 + 7** (as três medidas e de baixo risco): flash de 1.039.740 →
**~960.900 B**, folga de 4.740 → **~83.600 B**. Isso é 17,6× mais espaço, sem tocar em
nenhuma feature.

---

## 8. Testes executados no hardware

Todos contra o Pico W físico (`E6642815E34C1824`), com o dispositivo em operação normal.

| Teste | Resultado |
|---|---|
| Testes unitários nativos (`native`, `native_history`, `native_history_v4`, `native_cli`) | **4/4 PASSED** |
| CLI serial — modos EXEC e privilegiado, 12 comandos | OK |
| WiFi + HTTP, imagem oficial: 679 requisições, 9.584.085 B | **0 erros** |
| WiFi + HTTP, imagem sem BT: 678 requisições, 9.569.970 B | **0 erros** |
| Estabilidade de heap sob carga (oficial) | variação de 208 B em 70 s |
| Estabilidade de heap sob carga (sem BT) | variação de 200 B em 70 s |
| Leituras de sensor (4 sensores: DS18B20, 2× DHT22, BMP280) | 2409 + 726 leituras, **0 erros** |
| Telemetria TLS (BearSSL) | envio OK nas duas imagens |
| Gravação da imagem sem BT + boot + WiFi + web + sensores | **OK** |
| Restauração da imagem oficial 1.5.4-beta + verificação | **OK** |
| LittleFS após duas trocas de firmware | **intacto** (954.368 B, 4 sensores, config preservada) |
| Sonda de IRQ-off (`-Wl,--wrap` nos primitivos de flash) | `IRQ-off max: 60.848 µs`, `Core1 exposed: 0 ops` |

**Estado final do dispositivo:** firmware oficial 1.5.4-beta restaurado, WiFi em
192.168.3.24, web respondendo, sensores lendo, heap 32.924 B, dados intactos.

### 8.1 Aplicação da recomendação #1 — gravada em 2026-07-26

A remoção do Bluetooth deixou de ser experimento e virou a configuração de release.

**Mudança em `platformio.ini`** (4 pontos):

1. `[pico_base] build_flags` — removido `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`.
2. `[pico_base]` — adicionado `lib_ignore = SerialBT`.
3. `[env:pico_w_alpha] lib_ignore` — `SerialBT` repetido, porque um env que declara a
   própria lista **substitui** a herdada em vez de estendê-la.
4. Comentário do `[env:pico_w_debug]` atualizado (ver abaixo).

**Verificação do binário antes de gravar** — nenhum símbolo de BT sobreviveu:

| Símbolo | Ocorrências |
|---|---:|
| `hci_stack_static`, `hci_connection_storage`, `__bluetooth_tlv`, `cyw43_btfw_43439`, `btstack_run_loop`, `hci_run`, `packet_handler`, `l2cap_init`, `sm_init` | **0** cada |
| Blob do rádio | `w43439A0_7_95_49_00_combined`, 225.240 B (era `wb43439…`, 232.408 B) |

`tools/check_flash_probe.py` passou: shims em `0x20000180` e `0x2000020c` (SRAM),
invariante 8 preservada.

**Build final:** `Flash 962.972 B` / `RAM 114.796 B` — idêntico ao experimento.
Folga real **69.472 B**, `.bss` 107.252 B, heap 147.120 B.

**Medições no ferro, com a imagem definitiva gravada:**

| Métrica | Antes (com BT) | Depois (gravado) |
|---|---:|---:|
| Heap livre no boot (~25 s) | 32.924 B | **49.340 B** |
| Maior bloco contíguo no boot | 12.189 B | **35.782 B** |
| Heap livre em regime (TLS ativo) | 32.220 B | **48.788 B** |
| Heap mínimo sob 70 s de tempestade | 32.012 B | **48.532 B** |
| Maior bloco mínimo sob carga | 11.409 B | **35.723 B** |
| Requisições / erros | 679 / 0 | **706 / 0** |
| Bytes servidos | 9.584.085 | **9.965.190** |
| Pico de PBUF | 6 / 12 | **4 / 12** |

Sensores (4 slots), WiFi (mesmo IP), telemetria TLS, todas as páginas web e o LittleFS
(954.368 B, config preservada) verificados após a gravação.

**Efeito colateral bem-vindo:** o `[env:pico_w_debug]`, documentado como "estoura ~69 KB",
passou a estourar **16.576 B**. Ainda não linka, mas está ao alcance — os próximos 16 KB
saem das páginas `WebUI_GZ` (F3) ou do `-DNDEBUG` (F5). O comentário no `platformio.ini`
foi corrigido com o número novo.

**O que a versão não distingue.** A imagem continua reportando `1.5.4-beta`, então o
rótulo agora cobre dois binários diferentes. O marcador funcional para saber qual está
gravado é o `show metrics`: **heap ~49 KB e maior bloco ~36 KB** = sem BT;
**~32 KB e ~11 KB** = com BT. Um bump de versão deve acompanhar o próximo release.

**O que ficou de fora, de propósito.** `BluetoothManager.{h,cpp}` continua na árvore e
`CommandManager` ainda tem o membro `_btMgr` — que já era um stub vazio e não custa nada
no binário. Remover a classe é refatoração de código-fonte, não exclusão do BT do
firmware, e não foi pedido.

**Falha não relacionada.** `pico_w_alpha` não linka:
`undefined reference to DisplayManager::showTouchSensitivity()`. Confirmado que é
**pré-existente** — reproduz igual com o `platformio.ini` original. `pico_w_release` e
`pico_w_asserts` (964.820 B) compilam sem erro.

### 8.2 `/config` de volta para o firmware — gravada em 2026-07-26

Decisão tomada depois do F1: com 69.472 B de folga, a `/config` deixou de precisar morar
no LittleFS. **Não foi por espaço, foi por robustez** — num dispositivo recém-formatado ou
recém-montado o `config.html.gz` não existe ainda, e `/config`, que é justamente a página
usada para configurar o aparelho, respondia com *"Page asset missing — upload
data/web/config.html.gz"*. O bootstrap dependia de um upload manual.

**Mudança** (3 pontos):

1. `tools/build_webui_gz.py` — `FS_PAGES = {}` (o dicionário fica, vazio e documentado).
2. `src/WebManager_Auth.cpp:143` — `handleConfig()` passa de `serveProtectedFsPage(...)`
   para `serveProtectedPage(PERM_SYS_CONFIG, WebUI_GZ::CFG_PAGE_GZ, WebUI_GZ::CFG_PAGE_GZ_LEN)`.
3. `WebManager::serveProtectedFsPage` — **mantida sem chamadores**, com comentário
   explicando o porquê: é a metade de runtime do `FS_PAGES`, o `--gc-sections` a remove da
   imagem (custo zero), e apagá-la significaria reescrever um helper testado para voltar
   atrás.

**Custo medido: 11.544 B** — menos que os 12.152 B do array. A diferença é o item 3: a
`/config` era o **único** chamador de `serveProtectedFsPage`, então a função e sua página
de erro HTML viram código morto e saem no `--gc-sections`. Confirmado no ELF: o símbolo
some, e `CFG_PAGE_GZ` (12.152 B) aparece.

| | Flash usada | Folga real | RAM estática | Heap |
|---|---:|---:|---:|---:|
| Sem BT (8.1) | 975.008 B | 69.472 B | 115.020 B | 147.120 B |
| Sem BT + `/config` dentro | 986.552 B | **57.928 B** | 115.020 B | 147.120 B |
| Delta | +11.544 B | −11.544 B | **0** | **0** |

A RAM não se move: a mudança é só de onde os bytes da página são lidos.

**Validação no ferro.** Boot limpo, heap 49.076 B / maior bloco 35.934 B (idênticos ao
build de 8.1, como esperado), WiFi no mesmo IP, sensores lendo, `/config` respondendo 302
para o login sem sessão. Prova mais forte que o teste de rota: `serveProtectedFsPage` não
existe no binário, então **não há caminho de código** que leia o arquivo.

Tempestade HTTP de 45 s: **466 requisições, 0 erros, 6.577.590 B servidos**, heap entre
48.692 e 48.956 B (variação de 264 B), maior bloco nunca abaixo de 35.740 B, pico de PBUF
6/12. Mesmo perfil das corridas de 8.1 — como tem de ser, já que a `.bss` não mudou um byte.

**Pendência operacional — 12 KB ainda presos no LittleFS.** Gravar firmware não apaga o
filesystem, então `/web/config.html.gz` continua no dispositivo, órfão. O storage seguiu em
954.368 B usados. Para recuperar os ~12.288 B (3 blocos de 4096), apague o arquivo pela
página `/files` ou com `POST /api/delete?file=/web/config.html.gz` — **não** use
`pio run -t uploadfs`, que reformata a partição e leva `/history` junto. Não havia como
fazer isso a partir daqui: a CLI serial não tem comando de apagar arquivo e as rotas web
exigem sessão autenticada.

### Uma nota sobre `tools/pico_test_suite.py`

A suíte não completou: ela varre `sorted(glob.glob('/dev/ttyACM*'))` e **trava ao abrir
`/dev/ttyACM0`**, que hoje é o PicoHand (Pico comum), não o alvo. O alvo migrou para
`/dev/ttyACM1`. A varredura deveria filtrar por
`/dev/serial/by-id/*Pico_W*` (`ID_MODEL=Pico_W`), que é estável, em vez de por número de
ACM, que não é. As validações desta análise foram feitas com um driver serial próprio
apontado explicitamente ao `by-id` do alvo.

---

## 9. Como reproduzir

Os ambientes de experimento estão versionados em **`platformio_memstudy.ini`**, na raiz.
Ele não participa da build normal — só é lido com `-c` — e usa
`build_dir = .pio/build_memstudy` para não colidir com a build oficial. Está no repo para
que qualquer número deste relatório possa ser refeito.

```bash
# Baseline oficial
~/.platformio/penv/bin/pio run -e pico_w_release

# A/B do Bluetooth (F1)
~/.platformio/penv/bin/pio run -c platformio_memstudy.ini -e exp_baseline -e exp_nobt

# NDEBUG (F5)
~/.platformio/penv/bin/pio run -c platformio_memstudy.ini -e exp_ndebug

# Não-achados (excecoes/RTTI e printf float)
~/.platformio/penv/bin/pio run -c platformio_memstudy.ini -e exp_noexcept -e exp_nofloatprintf

# Folga real e comparação (script em anexo ao relatório)
arm-none-eabi-size -A .pio/build/pico_w_release/firmware.elf

# Maiores símbolos de RAM
arm-none-eabi-nm --print-size --size-sort --radix=d -C \
  .pio/build/pico_w_release/firmware.elf | awk '$3=="b"||$3=="B"'

# Linker map real (para atribuição por objeto)
#   pegue a linha de link com `pio run -e pico_w_release -v`
#   e reexecute-a acrescentando -Wl,-Map=firmware.map
```

Para as medições no ferro, o alvo deve ser endereçado por `by-id`:

```bash
~/.platformio/penv/bin/pio run -e pico_w_release -t upload \
  --upload-port /dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
```

`pio run -t upload` faz o reset por toque de 1200 bps sozinho — **não** é preciso pôr o
Pico em BOOTSEL. E gravar firmware **não** apaga o LittleFS (só `-t uploadfs` apaga).

---

## 10. Apêndice A — centros de custo de flash

> **Esta lista não é uma soma.** Os itens se sobrepõem de propósito, porque as duas
> perguntas úteis ("de qual biblioteca vem?" e "que tipo de coisa é?") cortam o binário em
> eixos diferentes. Os literais de string, por exemplo, estão espalhados por praticamente
> todas as outras linhas. Além disso, o linker mescla strings entre objetos, então a
> atribuição por arquivo-objeto superestima cada dono em algum grau (ver seção 2).
> Só o total ocupado e a folga são exatos.

**Total exato:** 1.039.740 B ocupados de 1.044.480 B → **4.740 B livres (0,45 %)**

| Centro de custo | Bytes | % do slot | Eixo | Ação |
|---|---:|---:|---|---|
| Blob do rádio CYW43439 (versão combinada WiFi+BT) | 232.408 | 22,3 % | biblioteca | −7.168 B com F1 |
| UI / display (TFT, touch, temas, fontes, i18n) | 88.608 | 8,5 % | aplicação | — |
| BearSSL (TLS) | 80.665 | 7,7 % | biblioteca | necessário para telemetria HTTPS |
| Aplicação / CLI | 74.300 | 7,1 % | aplicação | — |
| newlib (printf/scanf/strtod/dtoa/locale) | 72.019 | 6,9 % | toolchain | **−7.532 B** (F6) |
| Páginas web embutidas (`WebUI_GZ`) | 83.054 | 8,0 % | asset | até **−83.054 B** (F3) |
| Literais de string (programa inteiro, pós-merge) | 67.832 | 6,5 % | *transversal* | — |
| BTstack + `__bluetooth_tlv` + firmware BT | 64.732 | 6,2 % | biblioteca | **−64.732 B** (F1) |
| Servidor web (handlers) | 63.347 | 6,1 % | aplicação | — |
| Core arduino-pico | 61.334 | 5,9 % | framework | — |
| SDK pico | 30.958 | 3,0 % | framework | — |
| Armazenamento + codecs de histórico | 25.031 | 2,4 % | aplicação | — |
| Telemetria | 16.633 | 1,6 % | aplicação | — |
| LEAmDNS | 15.036 | 1,4 % | biblioteca | knob quebrado (F4) |
| Log + métricas + som + sondas | 13.055 | 1,3 % | aplicação | — |
| `.ota` (fixa, invisível ao PlatformIO) | 10.228 | 1,0 % | infra | — |
| Sensores + OTA + rede | 18.237 | 1,7 % | aplicação | — |

## 11. Apêndice B — orçamento de RAM, em uma tela

Este, ao contrário do apêndice A, **é aditivo**: as linhas são disjuntas e fecham nos
totais. Os itens da RAM estática são medidos por símbolo em `.bss`; os 7.548 B de `.data`
e os 192 B da tabela de vetores estão dentro da linha "demais".

```
SRAM principal ................................ 262.144 B  (256 KB)
│
├─ RAM ESTÁTICA .............................. 131.436 B  50,1 %
│  ├─ scratch de histórico (.bss) .............. 43.392 B  ← 14.592 usados só no boot
│  ├─ lwIP + WiFi (pools, ram_heap, cyw43) ..... 43.290 B
│  ├─ BTstack .................................. 15.617 B  ← 100 % desperdício
│  ├─ OTA (applier_buf + tmp_pool) .............. 6.144 B
│  ├─ _licenseBuf (duplicado) ................... 2.048 B
│  └─ demais (core, temas, display, sensores) .. ~21.000 B
│
└─ HEAP ...................................... 130.704 B  49,9 %
   ├─ GFXcanvas16 320×45 ....................... 28.800 B  ← alocado no boot, nunca liberado
   ├─ GFXcanvas16 140×40 ....................... 11.200 B  ← idem
   ├─ pack de idioma residente ................ ~15.700 B  ← carregado mesmo em inglês
   ├─ TLS BearSSL (ctx + 4096/512) ............. ~9.500 B
   ├─ Strings / WebServer / drivers / overhead . ~33.300 B
   └─ LIVRE .................................... 32.220 B  ← maior bloco contíguo: 11.483 B

stacks (bancos scratch, fora do orçamento acima): 2.048 B core0 + 2.048 B core1
```

---

## 12. Limitações desta análise

- Os testes de carga usaram um cliente HTTP sintético (3 threads em `/api/lang`), não um
  navegador real carregando a página de histórico — que é o caminho de maior pressão de
  PBUF e de `String`. Os números de PBUF (pico 6/12) devem ser relidos sob carga de
  navegador antes de qualquer decisão sobre reduzir o pool.
- A imagem sem BT rodou ~17 minutos no ferro. Isso valida boot, WiFi, web, sensores,
  telemetria e estabilidade de heap, mas **não substitui um soak longo**. O gap de
  estabilidade conhecido (corrida do heartbeat do Core 1 sob carga pesada de flash) é
  ortogonal a tudo o que está aqui e não foi reexercitado.
- A composição do heap em 4.4 é atribuição por dedução (soma dos alocadores conhecidos
  contra o total medido), não instrumentação do alocador. Os 33,3 KB de "restante" são
  um resíduo, não uma medição.
- Os ganhos de F2, F3, F8 e F9 são análises de código com aritmética verificada, mas
  **não foram construídos e medidos** como F1, F5 e F6 foram. Trate-os como estimativas
  firmes, não como fatos medidos.
