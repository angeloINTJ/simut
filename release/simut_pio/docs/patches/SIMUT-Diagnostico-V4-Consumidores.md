# SIMUT — Diagnóstico completo do ecossistema V4 (todos os consumidores)

**Base:** `main` @ `416f2dc` · **Data:** 2026-07-24
**Método:** leitura crítica de todos os leitores/escritores `.sim4` no código atual
**+ sondas compiladas contra o codec real** (host, `test/native_stubs`) onde havia suspeita.
Cada achado traz evidência `arquivo:linha`, mecanismo, sintoma e **instruções de correção**.

---

## 0. Sumário executivo

O ecossistema V4 está **maduro nos fundamentos** após a caçada recente (cursor de header
corrigido nas 5 cópias, sign-extension, isfinite, estatísticas por canal na web). O que resta
são **três defeitos altos** — dois de família (o mesmo padrão copiado entre leitores) e um de
borda no codec, **confirmado por sonda** — mais um escritor fora da disciplina de pausa do
Core 1:

| ID | Achado | Onde dói |
|---|---|---|
| **A1** | Condição de *refill* errada em **4 cópias** (+1 com constante errada) — leitor pode girar até estourar o orçamento quando um delta grande cruza a borda do buffer | preload parcial no boot, gráfico TFT/web/export incompletos, telemetria atrasada |
| **A2** | Batch T2.1 × meia-noite: o dreno grava epochs de ontem no arquivo de **hoje** | até 3 registros/dia no arquivo errado; consultas por dia os perdem |
| **A3** | **−0,01 °C colide com o sentinela NaN** (sonda: `FromFloat(-0.01) → raw=-1 → 0xFFFF → IsNan=1 → roundtrip = nan`) | freezer cruzando 0 °C grava buraco no histórico |
| **A4** | `setLastSentTimestamp` grava flash **sem `Core1FlashPause`** — mesma classe do `e035791` | cada upload de telemetria programa flash com Core 1 vivo em XIP |

---

## 1. Mapa de consumidores auditados

| Consumidor | Arquivo · função | Estado |
|---|---|---|
| Logger (escrita) | `AppManager_HistoryAlarm.cpp` · `processHistoryLogging` | ✅ V4-only, sentinela p/ ausentes, isfinite |
| Batch T2.1 | `StorageManager.cpp:2133-2150` · `flushHistoryBatch` | ⚠️ **A2** (meia-noite) |
| Preload min/max (dashboard/TFT) | `AppManager_HistoryAlarm.cpp:198` · `preloadMinMax` | ⚠️ **A1** + **M1** + **M2** (sem CH_PRESS) |
| Gráfico TFT | `AppManager_Graph.cpp:135-230` | ⚠️ **A1** (l.192) · L3 (sem canal P, por design) |
| Web `/api/history_multi` | `WebManager_History.cpp:59-520` | ⚠️ **A1** (l.276) · resto do 416f2dc ✅ |
| Web export (`.simx`) | `WebManager_History.cpp:523+` | ⚠️ **A1-var** (l.168, constante V2 no loop V4) |
| Web dias (`/api/history_days`) | `WebManager_History.cpp:1171` | ✅ (.sim4 listado, strip na ordem certa) |
| Telemetria | `TelemetryManager.cpp:368` · `collectBatch` | ✅ cursor por **epoch** (imune a âncora posicional) · ⚠️ **A1** (l.95) · **A4** · M2 (1 campo de pressão) · L1/L2 |
| Alarmes (limites) | `SensorManager` (leituras vivas) | ✅ não consomem V4 — fora do risco |
| Stats TFT | `openStatsScreen` ← caches do preload | herda M1/M2 |
| Codec | `HistoryV4.{h,cpp}` | ⚠️ **A3** · resto ✅ (39/39 + sondas) |
| Escritores flash | `ensureV4Schema`/`writeFlashV4`/`flush` ✅ pausados · `create`/`repair` cobertos por aninhamento | ⚠️ **A4** + **M3** (tripwire) |

---

## 2. Achados e instruções de correção

### 🔴 A1 — Família do *refill*: leitor trava quando um delta > `anchorByteSize` cruza a borda

**Evidência (5 cópias):**
`AppManager_HistoryAlarm.cpp:~256` · `AppManager_Graph.cpp:192` ·
`TelemetryManager.cpp:~(V4 loop)` · `WebManager_History.cpp:276` (multi) ·
`WebManager_History.cpp:168` (export — usa `HIST_V2_MAX_DELTA_SIZE`, constante do formato errado).

**Mecanismo.** O padrão `if (rdFilled < v4st.anchorByteSize && f.available())` só reabastece
quando o buffer cai abaixo do tamanho da **âncora** — mas um DELTA pode ser maior
(máscara + Δepoch varint + N×varint ≈ até `1+5+5·N` bytes). Se o buffer contém
`anchorByteSize ≤ bytes < tamanho_do_delta`, o decode devolve 0, o refill não dispara e o
laço gira em vazio até o orçamento (preload → `APP_PRELOAD_BUDGET` parcial; web/TFT →
gráfico truncado; telemetria → lote vazio até o próximo tick). O leitor legado ao lado
(`AppManager_Graph.cpp:279`) usa o limiar certo para V2 — só as cópias V4 herdaram o errado.

**Correção (uma forma, cinco lugares).** Trocar a *condição de limiar* por *retry pós-falha*:

```cpp
size_t cons = histV4DecodeNext(rdBuf, rdFilled, st, vals, &epoch);
if (cons == 0) {
	/* Registro maior que o disponível? Reabastece e tenta UMA vez. */
	if (f.available( ) > 0 && rdFilled < sizeof(rdBuf)) {
		int rN = f.read(rdBuf + rdFilled, sizeof(rdBuf) - rdFilled);
		if (rN > 0) { rdFilled += (size_t)rN;
			cons = histV4DecodeNext(rdBuf, rdFilled, st, vals, &epoch); }
	}
	if (cons == 0) break;   /* fim real ou tail rasgado */
}
```

Remover o pré-refill por limiar (vira redundante) ou mantê-lo com
`rdFilled < sizeof(rdBuf)`. No export, eliminar `HIST_V2_MAX_DELTA_SIZE` do caminho V4.

**Validação.** Teste host novo em `test_history_v4`: stream com um delta forjado de
`1+5+5·N` bytes posicionado para sobrar `anchorByteSize` no buffer → leitor no padrão
antigo trava, no novo decodifica tudo. + bancada: dia cheio com 4 medições variando forte.

---

### 🔴 A2 — Batch cruza a meia-noite gravando no dia errado

**Evidência.** `flushHistoryBatch` → `writeHistoryEntryFlashV4(values, count, epoch)` →
`path = getHistoryFileNameV4( )` = **data de agora**, ignorando o epoch da entrada
(`StorageManager.cpp:~2070`). Entradas bufferizadas 23:57–23:59 drenam ~00:01 no arquivo
do dia novo.

**Sintoma.** Até `HIST_BATCH_N−1 = 3` registros/dia com epoch de ontem dentro do `.sim4`
de hoje: consulta de ontem os perde; o gráfico de hoje ganha pontos antes de 00:00; o
preload de hoje absorve extremos de ontem.

**Correção (no ponto de buffer, não no dreno).** Em `writeHistoryEntryV4`
(`StorageManager.cpp:2133`), antes de anexar ao batch:

```cpp
/* A2: nunca deixar o batch cruzar a meia-noite — o dreno grava no
 * arquivo do dia corrente, então esvazia ANTES de aceitar amostra
 * de um dia diferente do primeiro item bufferizado. */
if (_histBatchLen > 0 && !sameLocalDay(_histBatch[0].epoch, epoch)) {
	flushHistoryBatch( );
}
```

`sameLocalDay`: comparar `tm_yday`+`tm_year` via `localtime_r` (helper de 6 linhas em
`SystemUtils`). O flush no caminho de reboot/`write memory` já existe e permanece.

**Validação.** Bancada: `conf time 2026-07-24 23:58:00`, aguardar 4 amostras, verificar via
`/api/history_days` + export que os arquivos 24 e 25 têm apenas seus próprios epochs.

---

### 🔴 A3 — −0,01 °C vira NaN (colisão de valor legítimo com o sentinela) — **confirmado por sonda**

**Evidência (sonda no codec do `main`):**

```
FromFloat(-0.01) raw=-1  sentinela=65535  IsNan(raw)=1
roundtrip anchor: dec=65535 → ToFloat=nan   (esperado -0.01)
```

**Mecanismo.** `raw = round(v·scale)`; −0,01·100 = −1, cujo padrão em 16 bits é `0xFFFF` —
exatamente o sentinela all-ones. O registro grava NaN; num delta com `prevValid`, o campo
ainda derruba `fieldHasValid` (próximo valor vai absoluto — consistente, mas o ponto se
perde). Cenário real do produto: **freezer cruzando 0 °C**. Simétrico para unsigned: o topo
teórico da faixa (ex.: umidade 102,3 % em bw10·scale10) também é o sentinela — hoje
inalcançável por clamps físicos, mas sem guarda no codec.

**Correção (1 ponto, universal).** Em `histV4FromFloat`, após escala e clamp de faixa:

```cpp
/* A3: o padrão all-ones é reservado ao sentinela NaN. Um valor
 * legítimo que caia exatamente nele (ex.: -0.01°C em s16·100) é
 * deslocado em 1 LSB — erro máximo de 1 unidade da escala,
 * documentado, em vez de um furo de NaN no histórico. */
if ((raw & mask) == mask) raw -= 1;   /* mask = (1<<bw)-1 sobre o padrão */
```

(Implementar sobre a representação empacotada que o encode usa, sinalizada ou não —
o teste é sempre contra o padrão all-ones da largura.)

**Validação.** Dois testes novos na suíte: `-0.01f` roundtrip → `-0.02` (não-NaN) em s16·100;
e topo unsigned `102.3` em u10·10 → `102.2`. + o teste existente de NaN real segue passando.

---

### 🔴 A4 — Cursor da telemetria grava flash sem `Core1FlashPause`

**Evidência.** `setLastSentTimestamp` (`StorageManager.cpp`): `pause=0` no corpo; grava o
arquivo de cursor a cada upload bem-sucedido. É exatamente a classe raiz do `e035791`
("FLASH_OP sozinho ≠ proteção" — o comentário em `StorageManager.cpp:43` exige a pausa
para todo program/erase).

**Correção.** Primeira linha do corpo: `Core1FlashPause _c1(this);` (refcount aninha de
graça se algum chamador já pausou). Defensivo no mesmo lote: adicionar a pausa também em
`createHistoryFileV4WithSchema` e `repairHistoryTailV4` — hoje cobertas só por aninhamento
dos 2 chamadores atuais; um chamador futuro sem pausa reabre a classe.

**Validação.** O tripwire do M3 (abaixo) + soak: zero autópsias `C0=[TELEMETRY]`/
`C0=[STO]` com Core 1 em XIP.

---

### 🟠 M1 — Preload resolve hwId **por registro** (cópia nº 6 do bug de custo do 416f2dc)

`preloadMinMax` faz `histV4StrPoolGet` + varredura de 16 slots para **cada medição de cada
registro** (`AppManager_HistoryAlarm.cpp:~278-296`) — o mesmo padrão que derrubava o
handler web por watchdog. Aqui o teto de 5 s salva do reboot, mas entrega **cache parcial**
em dias cheios (min/max errados no dashboard após reboot à tarde).
**Correção:** espelhar o 416f2dc — após o parse do header, montar `int8_t measSlot[MC]`
uma única vez (−1 = sem dono) e usar no laço quente. **Validação:** dia sintético de 1.440
registros × 4 medições pré-carrega completo em ≪ 5 s (log sem `APP_PRELOAD_BUDGET`).

### 🟠 M2 — Pressão: canal de segunda classe nos consumidores

`preloadMinMax` descarta `CH_PRESS` (só TEMP/HUM); `openStatsScreen` não tem campos de
pressão; os cartões web cobrem T/H; e o carrier da telemetria tem **um único**
`rec.pressure` global (`TelemetryManager.cpp:115`) — dois BMP280 colidem (último vence).
**Decisão de produto + correções pontuais:** (a) declarar P como "gráfico/telemetria
apenas" e documentar; **ou** (b) adicionar `_cachedPressMin/Max[slot]`, campos no
`GraphStatsPackage`, cartões `minP/maxP` e `pressure[MAX_SENSORS]` no carrier + chaves
`p<hwId>` já existentes no payload. Custo de (b): ~64 B RAM + UI.

### 🟠 M3 — Tripwire de pausa: transformar a disciplina do `e035791` em invariante mecânica

A regra "todo program/erase precede de `Core1FlashPause`" hoje é convenção. **Correção:**
sob `SIMUT_CONCURRENCY_ASSERTS` (env `pico_w_asserts` já existe), no wrap dos primitivos
(`e6f1480` já intercepta `flash_range_program/erase` p/ métricas): se `pauseDepth == 0` **e**
Core 1 vivo → `LOG_ERROR "INVARIANTE-8: program/erase sem Core1FlashPause"` com o módulo
do TRACE. Expor `pauseDepth` via contador atômico incrementado no ctor/dtor do guard.
**Candidatos a flagrar na primeira rodada:** `/api/delete` de histórico, gravações do
LogManager (`.blog`), `system format`, além do A4. **Validação:** save-storm + telemetria
com a flag ligada → zero disparos após as correções.

### 🟠 M4 — Dia de migração: `.bin` vence `.sim4` no mesmo dia

Gráfico TFT (`AppManager_Graph.cpp:135`), export (`WebManager_History.cpp:121`) e preload
abrem `.bin` primeiro e **ignoram** o `.sim4` do mesmo dia — no dia do primeiro flash V4, a
tarde some dessas visões (telemetria não sofre: varre ambos por epoch).
**Correção mínima:** documentar (1 dia, transitório). **Correção completa:** nesses 3
leitores, quando ambos existem, ler `.bin` e **depois** `.sim4` do mesmo dia em sequência.

### 🟡 Leves

- **L1** `EPOCH_MIN` divergente: escritor/gate = 1,6e9; telemetria (`TelemetryManager.cpp:60`)
  e leitores = 1,7e9. Unificar em `SystemDefs_Limits.h` (`HIST_EPOCH_MIN`).
- **L2** Corte de arquivos da telemetria monta `minFileName` com sufixo `.bin` fixo
  (`TelemetryManager.cpp:47`) — funciona por acaso (dígitos comparam antes do sufixo).
  Trocar por comparação da parte de data (8 chars).
- **L3** Gráfico TFT não plota `CH_PRESS` (V1/V2 = T/H) — limitação de design; registrar no
  manual (a decisão M2 pode absorver).
- **L4** Inits `±1000.0f` de min/max nos leitores: seguros para T/H; se P entrar nos
  cartões (M2-b), usar `±INFINITY` + flag de "houve amostra".

---

## 3. Verificado OK (não re-caçar)

- **Cursor da telemetria é por epoch** com re-decodificação a partir do header — imune à
  inferência posicional de âncoras; guarda de cursor-no-futuro presente.
- Fix do cursor de header presente nas 4 cópias lidas (preload/graph/telemetria/web) —
  a família do `dc5e0bb/03f7e64` está fechada.
- NaN/sentinela respeitado em **todos** os laços de estatística lidos (`histV4IsNan` +
  `isnan/isfinite` pós-`eedd599`); médias/σ/gaps do TFT idem.
- Estatísticas web por canal e por sensores selecionados (`416f2dc`) corretas na leitura.
- Logger V4-only (sem dual-write → sem duplicação na telemetria); listagem de dias inclui
  `.sim4` com strip na ordem certa; delete de arquivo vivo invalida o codec (fix recente).
- Escritores principais pausados: `ensureV4Schema`, `writeHistoryEntryFlashV4`,
  `flushHistoryBatch` (1 pausa para o dreno inteiro), fatia do `enforceStorageLimit`.
- Codec: 39/39 na suíte + sondas de NaN-transição da v1.5.3 seguem válidas.

## 4. Ordem de execução sugerida

1. **A3 + A1** (host-testáveis): corrigir codec + os 5 leitores, adicionar os 3 testes novos
   à suíte `native_history_v4` — CI prova antes do flash.
2. **A4 + M3**: pausa no cursor (+2 defensivas) e o tripwire; rodar save-storm + telemetria
   com `pico_w_asserts` para flagrar o que sobrou.
3. **A2** (bancada com relógio manual) e **M1** (dia sintético).
4. **M2/M4/L***: decisão de produto da pressão primeiro; leves entram de carona.

*Nenhuma correção acima remove função existente; A1/A3 têm forma única aplicada em N
pontos — candidatas a helper compartilhado (`histV4ReadNextRecord(File&, buf, state, …)`)
para extinguir a família de cópias de leitor de uma vez.*
