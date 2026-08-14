# Patch de correção — ecossistema V4 do SIMUT

**Base:** `main` @ `416f2dc` · **Arquivo:** `simut-v4-diagnostico-fixes.patch`
**Aplicação:** `git apply simut-v4-diagnostico-fixes.patch` (verificado em clone limpo)
**Validação:** 49/49 testes host da suíte `native_history_v4`, 0 falhas.

---

## 1. O que foi corrigido

| ID | Arquivo · símbolo | Correção |
|---|---|---|
| **A1** | `HistoryV4.h` · novo `histV4DecodeNextRefill` | Reabastecimento **após** a falha do decode, em template com functor. Substitui as 5 cópias do laço. |
| **A1-b** | `HistoryV4.cpp` · `histV4Decode` | Decode de delta em duas passadas (parse / commit). **Pré-requisito do A1.** |
| **A2** | `StorageManager.{h,cpp}` · `writeHistoryEntryV4`, `getHistoryFileNameV4(uint32_t)` | Flush do batch na virada do dia + caminho do arquivo derivado do epoch da entrada. |
| **A3** | `HistoryV4.h` · `histV4FromFloat` | Valor legítimo que caia no padrão all-ones é deslocado 1 LSB. |
| **A4** | `StorageManager.cpp` | `Core1FlashPause` RAII em `flushCursorIfDirty`, `createHistoryFileV4WithSchema`, `repairHistoryTailV4`. |
| **M1** | `AppManager_HistoryAlarm.cpp` · `preloadMinMax` | `measSlot[]` resolvido uma vez por arquivo. |
| **L1** | `SystemDefs_Limits.h` + 3 chamadores | `HIST_EPOCH_MIN` único (1,6e9). |
| **L2** | `TelemetryManager.cpp` · novo `historyDayIsBefore` | Corte por 8 dígitos de data, não por nome com sufixo. |

Consumidores migrados para o helper A1: `preloadMinMax`, `AppManager_Graph`,
`WebManager_History` (multi), `TelemetryManager::collectBatch`,
`StorageManager::scanHistoryFileV4`.

Nenhuma função foi removida. `getHistoryFileNameV4()` sem argumento e as duas
sobrecargas existentes permanecem.

---

## 2. Evidência experimental

Sonda compilada contra os dois codecs, mesmo binário de teste:

```
===== CODEC ORIGINAL (416f2dc) =====
A3   FromFloat(-0.01) raw=-1  IsNan=1  ToFloat=nan
     => -0,01 °C virou NaN
A1-b cortes que sujaram o estado: 6 de 9
     => após falha+retry, campo0 = 2000 (correto: 1500)
A1   padrão antigo decodificou 15 de 120 registros
     => leitor parou no meio do arquivo

===== COM A PATCH =====
A3   FromFloat(-0.01) raw=-2  IsNan=0  ToFloat=-0.020000
A1-b cortes que sujaram o estado: 0 de 9
A1   helper da patch decodificou 120 de 120 registros
```

O resultado do A3 reproduz exatamente a sonda citada no diagnóstico.

---

## 3. Correções ao diagnóstico

Três pontos do documento não se sustentaram contra o código em `416f2dc`.

### 3.1 A4 não procede como descrito

`setLastSentTimestamp` (`StorageManager.cpp:1690`) **não grava flash** — só marca
`_cursorDirty` e o instante de coalescência. A escrita real está em
`flushCursorIfDirty`, que **já** chamava `enterFlashSafeMode()` — e essa função
*é* a pausa; `Core1FlashPause` é apenas RAII em volta dela.

Sobra um risco menor e real: o par era manual, então qualquer `return` futuro no
meio do corpo deixaria o Core 1 congelado. Convertido para RAII. As duas
defensivas do documento procedem e foram aplicadas.

### 3.2 M3 já está implementado

`FlashIrqProbe.cpp` já conta exatamente a invariante pedida:
`g_flashIrqExposed` incrementa quando `g_core1Running && g_core1FlashSafeDepth <= 0`
durante um `flash_range_program/erase`, e o valor é exposto no `CommandManager`.
Não há tripwire a construir — há um contador a ler num soak.

Um `LOG_ERROR` **não pode** ir para dentro do wrapper: ele é `__not_in_flash_func`,
roda com IRQ desabilitada e XIP potencialmente fora; `tools/check_flash_probe.py`
falha o build se ele sair da SRAM. A verificação tem de ficar num ponto de
inspeção periódica, fora do caminho crítico.

### 3.3 A quinta cópia do A1 está em outro lugar — e é a mais grave

O documento aponta o export `.simx` (`WebManager_History.cpp`) usando
`HIST_V2_MAX_DELTA_SIZE` num laço V4. Não é o caso: aquele laço é **V2 legítimo**
(`historyDecodeRecord` + `HistoryCodecState`) e a constante está certa.

A quinta cópia V4 real é **`StorageManager::scanHistoryFileV4`**, e a
consequência lá é qualitativamente diferente das outras quatro: o chamador usa o
`tornAt` devolvido para acionar `repairHistoryTailV4`, que **corta o arquivo**.
Um delta grande na borda do buffer não truncava uma exibição — apagava o resto do
dia no flash.

---

## 4. Achado novo: A1-b (bloqueante para o A1)

O `histV4Decode` gravava `state.lastAnchor[i]` e `state.fieldHasValid[i]` dentro
do laço de parsing e podia sair com `return 0` no meio, ao encontrar um varint
truncado. O estado ficava meio aplicado.

Enquanto o único tratamento de falha era `break`, isso passava despercebido — o
leitor abandonava o arquivo em seguida e o estado sujo morria com ele. **Com o
retry pós-refill do A1, o mesmo registro é decodificado de novo sobre um estado
já avançado: os campos aplicados na primeira passada somam o delta duas vezes.**
A sonda mostra o campo 0 lendo 2000 onde o correto é 1500, e a corrupção se
propaga por todos os registros seguintes do dia — sem erro, sem log.

Aplicar o A1 sem isto seria trocar uma leitura truncada por dados errados. A
correção do documento estava certa; faltava a pré-condição.

---

## 5. Decisão tomada sem confirmação

O A2 do documento corrige no ponto de buffer. Isso resolve o caso normal, mas se
o `flushHistoryBatch` falhar parcialmente o batch volta a misturar dias e o dreno
segue resolvendo o caminho por "agora".

Incluí também o overload `getHistoryFileNameV4(uint32_t epoch)`, usado por
`writeHistoryEntryFlashV4`. É aditivo e torna o dreno correto por construção.

**Efeito colateral a observar em bancada:** `writeHistoryEntryFlashV4` usa
`path != _v4CurrentLogFileName` para detectar virada de dia e invalidar o codec.
Com caminhos por epoch, um dreno que contenha os dois dias alterna o caminho e
dispara `enforceStorageLimit` + rescan a cada alternância. O flush preventivo do
A2 torna isso raro (só no caminho degradado), mas é o ponto a instrumentar no
teste de meia-noite. Se preferir escopo estrito ao documento, reverta os dois
hunks de `getHistoryFileNameV4` — o resto do A2 é independente.

---

## 6. Não incluído

- **M2** (pressão como canal de segunda classe) — exige decisão de produto entre
  (a) documentar como "gráfico/telemetria apenas" e (b) adicionar caches,
  campos no `GraphStatsPackage`, cartões e `pressure[MAX_SENSORS]` no carrier.
  Não é correção de defeito.
- **M4** (`.bin` vence `.sim4` no dia da migração) — transitório de 1 dia; a
  correção completa muda a ordem de leitura em 3 consumidores e merece teste
  próprio.
- **L3** (gráfico TFT sem `CH_PRESS`) — limitação de design, absorvida por M2.
- **L4** (inits `±1000.0f`) — só passa a importar se M2-b for adotado.

---

## 7. O que ainda precisa de bancada

A suíte host cobre A1, A1-b e A3. Os demais dependem de hardware:

1. **A2** — `conf time 2026-07-24 23:58:00`, aguardar 4 amostras, conferir via
   `/api/history_days` + export que os arquivos 24 e 25 contêm apenas os
   próprios epochs.
2. **A4** — soak com `pico_w_asserts`; conferir `g_flashIrqExposed == 0` no
   relatório de métricas após save-storm + telemetria.
3. **M1** — dia sintético de 1.440 registros × 4 medições; preload deve completar
   sem `APP_PRELOAD_BUDGET` no log.
4. **L2** — cursor de telemetria posicionado no meio de um dia com `.bin` e
   `.sim4` coexistindo.

A patch não foi compilada para RP2040: o registry do PlatformIO
(`api.registry.platformio.org`) está fora da allowlist deste ambiente, então o
toolchain arduino-pico não pôde ser baixado. Rode `pio run -e pico_w_release`
antes de gravar.
