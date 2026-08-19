# Análise ponta a ponta — Gráficos de histórico na interface web

**Data:** 2026-08-13
**Base:** v2.1.7-beta (rig `pico_w_release`, imagem do release) → correção aplicada na working tree e validada no ferro
**Sintoma relatado:** os dados estão íntegros nos `.h5`, mas os gráficos aparentam dados faltando ou bagunçados
**Restrição de projeto:** a solução se limita ao processamento dos arquivos `.h5` e ao desenho do gráfico **no navegador** (nenhuma mudança no C++ do firmware)

---

## 1. Sumário executivo

O diagnóstico confirmou o sintoma e localizou a causa **fora dos arquivos**: os `.h5`
estavam perfeitos; quem destruía a informação era a **reamostragem no servidor**
(`/api/history_multi`) e o **desenho ingênuo do resultado** na página. Nas condições
mais comuns o gráfico chegava a ser desenhado com **3 pontos (1h)**, **13 (6h)** e
**51 (24h)** — e esses poucos pontos eram o *envelope de bloco* (mín em t0, máx em
t0+30min) ligados como se fossem a série real, produzindo uma serra artificial que
duplicava eventos (um degelo virava dois picos).

A correção inverte o fluxo, exatamente na direção que o usuário apontou: **a página
baixa os próprios `.h5` por `/download`** (mesmo caminho do export CSV, que já
funcionava), decodifica tudo no navegador com o `h5Decode` existente, e reduz para a
tela com **decimação por balde de pixel guardando mínimo, máximo e média** — banda
min/máx honesta + linha média. Resultado medido no rig com 64 dias de dados sintéticos
(86% da LittleFS): **1h 3→60 pontos, 6h 13→360, 24h 51→1398 (resolução plena),
7d 339→885, MAX ~854 baldes** — em qualquer faixa o número de pontos acompanha a
largura do canvas, picos de 1 minuto sobrevivem por construção, e lacunas reais viram
lacunas desenhadas.

---

## 2. O pipeline ANTES (como era)

```
[Encoder V5 em RAM] ──seal 60 recs──▶ /history/YYYYMMDD.h5   (dados ÍNTEGROS ✓)
                                            │
              GET /api/history_multi?range=N&end=E
                                            │
   ┌── estRecs = bytes_dos_ARQUIVOS_VARRIDOS / 6 ── decimation = estRecs/600
   │
   ├─ decimation == 1 → caminho DECODE: decodifica tudo, emite 1 registro a cada N (stride)
   ├─ decimation  > 1 → caminho ENVELOPE: 2 pontos por bloco (mín@t0, máx@t0+1800)
   └─ resposta grande → sliceRequired → cliente refaz em fatias ?from&to&mode=envelope
                                            │
                    página: data[] → Chart.js linha única, tension 0.25
```

### Defeitos identificados (cada um confirmado por medição no rig)

**D1 — Envelope desenhado como linha.** O envelope emite mín@t0 e máx@t0+30min *na
mesma série*. O comentário do firmware diz que o gráfico deveria "desenhar uma banda";
a página nunca implementou banda — liga os pontos alternados e o resultado é uma
oscilação min↔máx por hora que não existe no dado. Pior: entre o máx de um bloco e o
mín do seguinte o traço desce e sobe de novo — **um degelo único da geladeira aparece
como dois picos com vale no meio** (captura `caps_antes/past_24h.png`, degelo das
~10:40).

**D2 — O estimador decide pelos arquivos varridos, não pela janela.** A escolha
decode/envelope usa `histBytes` dos **arquivos listados** (1–2 dias para faixas ≤24h).
Um dia de 1 min tem ~1400 registros ≈ 8,5 KiB → `estRecs/600 ≥ 2` → envelope — mesmo
para uma janela de **1 hora** (que tem ≤60 registros e caberia inteira). Consequência
medida: 1h ancorado no passado = **3 pontos**; e o comportamento **muda com a hora do
dia** (de manhã o arquivo corrente é pequeno → decode denso; à noite ou após a
meia-noite → envelope esparso). É a assinatura de "qualquer condição" do relato.

**D3 — `sliceRequired` para janelas minúsculas.** Pelo mesmo estimador, um 1h/6h
ancorado no passado respondia `sliceRequired` com `estPoints=48` — o cliente então
buscava fatias `mode=envelope`. Medido: 1h → 3 pontos, 6h → 13, 24h → 51.

**D4 — Stride no caminho decode.** Quando decode decimava (1 a cada N), o pico de 1
minuto tinha 1/N de chance de sobreviver — foi para isso que o envelope existiu. Os
dois caminhos perdiam: um perdia pico, o outro perdia forma.

**D5 — Gráfico-fantasma no "sem dados".** `myChart.destroy()` sem `myChart = null`:
num dia sem arquivo (overlay "sem dados"), o objeto destruído ainda respondia com os
datasets da consulta anterior (medido na varredura: o dia-buraco reportava 36 pontos
do gráfico anterior).

**D6 — Suavização sobre subamostragem.** `tension: 0.25` sobre 3 pontos de envelope
desenha uma curva lisa e plausível — 100% fabricada (captura
`caps_antes/past_1h.png`). Suavizar dado denso é cosmético; suavizar dado
subamostrado é mentira visual.

**Nota de honestidade:** os badges MAX/MIN sempre foram corretos — vinham do full-scan
(ou do cabeçalho do envelope) no servidor, pré-decimação. O gráfico é que não
acompanhava os próprios badges.

### Números do ANTES (rig com 64 dias sintéticos, sensor 0)

| condição | pontos | caminho |
|---|---|---|
| 1h ancorado 12/08 15:00 | **3** | fatia envelope |
| 6h idem | **13** | fatia envelope |
| 24h idem | **51** | fatia envelope |
| 7d idem | 339 | fatia envelope |
| 24h "agora" | 81 | envelope |
| 1M/1A/MAX | 1501/3120/3121 | fatias envelope (serra) |
| dia 14/07 (apagão 09:47–16:22) | 36 | envelope |
| dia 20/06 (sem arquivo) | 36 **fantasmas** (D5) | — |

---

## 3. Dados sintéticos e carga do rig (86% da LittleFS)

Gerador: `scratchpad/gen_synth.py`, usando o codec de referência `tools/history_v5.py`
(o mesmo que o firmware espelha byte a byte). Fidelidade ao dispositivo:

- **linha do tempo contínua** (sem emenda na meia-noite), blocos fecham por contagem
  (60) e um bloco pode cruzar a meia-noite indo para o arquivo do dia do seu `t0`,
  como no `sealHourV5`;
- schema real lido dos arquivos do rig: 8 canais (t×5, u×2, p×1; ids 0, 8, 24, 25,
  32, 34, 80, 81) e, nos 5 dias mais antigos, o schema de **6 canais** da era
  pré-BMP280 (caso multi-schema real do histórico);
- **quantização física** por sensor (DS18B20 1/16 °C, DHT22 0,1, BMP280 0,01/0,1) —
  sem ela os arquivos saíam 34% maiores que os reais; com ela, 8,30 KiB/dia = paridade
  com a bancada;
- curvas com assinatura por sensor: geladeira (dente de serra do compressor 42 min +
  degelo ~12h + ruído), adega lenta, salas com ciclo diurno + AC, ambiente externo com
  chuva, pressão com ondas de dias + maré semidiurna;
- **eventos plantados com gabarito** (`synth_h5/manifest.json`): 37 lacunas de reboot,
  apagão longo (14/07 09:47–16:22), tarde de flapping (24/07), 2 dias sem arquivo
  (20–21/06), 106 spikes de 1 minuto, 61 corridas de NaN (sensor desconectado).

Validação antes de tocar no rig: os 90 arquivos decodificam na referência Python
(129 033 registros, 0 erros) e o `h5Decode` **da própria página** (extraído do
`WebUI.h` e rodado no Node) produz saída **byte-idêntica** à referência nos arquivos
de amostra.

Carga: backup verificado dos 90 arquivos reais
(`scratchpad/history_backup_20260813_pre_synth/`, tamanhos conferidos), troca pelos
sintéticos até a zona do GC de projeto (o firmware **apaga os mais antigos acima de
86%** — `StorageManager.cpp:1091`). Estado final: **64 arquivos, 09/06→13/08, 85,9–86,3%**
(o arquivo de hoje continua real e crescendo; o GC mantém o regime).

> Armadilha operacional descoberta: o `fs_u` do `/api/status` atualiza **em degraus
> defasados** (chegou a reportar 48% com a FS já em ~85%). Parar a carga pelo medidor
> defasado estourou para 100% — corrigido apagando e recarregando com contabilidade
> local (~12 KiB de disco por arquivo de dia: 3 blocos LittleFS + metadados).

---

## 4. O fix (como ficou)

Somente `WebUI.h` (JS da página de histórico; o gzip vai em PROGMEM no build — flash
final 94,8%, ~54 KiB de folga). O C++ do firmware não mudou; `/api/history_multi`
continua existindo para compatibilidade/ferramentas.

```
página ──▶ /api/status   (intervalo nominal, slots→hwId, RELÓGIO do aparelho p/ janela)
       ──▶ /api/ls?dir=/history  (índice YYYYMMDD.h5, TTL 45 s)
       ──▶ /download?file=/history/D.h5   (só os dias que TOCAM a janela, ±1 dia
            de folga p/ bloco que cruza meia-noite; do MAIS NOVO para o mais velho;
            cache por (nome,tamanho) — dia fechado nunca é rebaixado; o dia corrente
            é sempre rebuscado)
       ──▶ h5Decode (o já existente) → colunar (Float64Array t + Int16Array/canal)
       ──▶ /api/history/open  (hora aberta, POR ÚLTIMO — lacuna nunca duplicata)
       ──▶ montagem da janela + extremos full-res na MESMA passada
       ──▶ _h5Decimate: ≤ ~1,6×largura → CRU; senão balde de pixel com
            mín/máx/média por balde; balde vazio → null (lacuna real desenhada);
            o registro mais novo sai SEMPRE com o próprio carimbo (borda em dia)
       ──▶ desenho: linha média + BANDA mín-máx (plugin de canvas local, com clip
            e respeito a null) + ponto isolado ganha raio visível
```

Decisões de desenho e por quê:

- **Banda, não linha, para dado comprimido** — é o que o envelope do V5 sempre quis
  ser: em 7d/1M o gráfico mostra a faixa real percorrida pelo sinal e a média por
  balde; o pico de 1 min sobrevive porque o extremo É o ponto do balde.
- **`tension: 0`** — com ≥300 pontos por série, suavização não acrescenta nada e o
  D6 provou que ela fabrica forma onde faltam pontos.
- **Lacunas**: no cru, `dt > 3,5×intervalo` insere `null` (o Chart quebra a linha);
  na banda, balde vazio vira `null`. `spanGaps: false` em tudo.
- **Ponto isolado visível** (`pointRadius` 2,4 quando os dois vizinhos são null) —
  uma amostra entre duas lacunas antes não desenhava NADA (raio 0, sem segmento).
- **Identidade nunca só pela cor**: umidade/pressão mantêm as cores fixas da grandeza
  e ganham **tracejado distinto por sensor** quando há mais de um.
- **Tooltip nearest** mostra a série real; na banda anexa `[mín … máx]` do balde.
- **Extremos no cliente, full-res, mesma passada** — badges continuam nunca
  discordando do desenho (agora por construção local).
- **`myChart = null` após destroy** (mata o D5).
- **Relógio do aparelho** (skew de `sys.time`) define a janela — PC dessincronizado
  não desloca o gráfico.
- **CSV reusa o cache de bytes** do gráfico (baixa só o que faltar).
- Cancelamento preservado: aborta o download corrente e desenha o parcial (a parte
  recente, porque a ordem é novo→velho), com toast de parcial.

Removidos: probe/`sliceRequired` no cliente, `_gchWhole/_gchSliced/_gchMerge`,
`estSizes` (progresso agora é por arquivo: `k/N + KiB`).

## 5. Validação (DEPOIS, no ferro)

Varredura idêntica à do ANTES (Playwright headless, sessão real, mesma matriz):

| condição | ANTES | DEPOIS | modo |
|---|---|---|---|
| 1h ancorado | **3** | **60** (todos os registros) | cru |
| 6h ancorado | **13** | **360** | cru |
| 24h ancorado | **51** | **1398** (resolução plena; 2 nulls = lacunas reais) | cru |
| 7d ancorado | 339 | **885** | banda |
| 24h agora | 81 | 751 (lacuna real da tarde preservada) | cru |
| 1M / MAX | serra min-máx | ~875 / ~854 baldes | banda |
| 1A | 3120 (serra) | 159 baldes ocupados (dado ocupa 18% do eixo — honesto) | banda |
| dia do apagão 14/07 | 36 | **1045 + lacuna sem ponte** | cru |
| dia sem arquivo 20/06 | 36 fantasmas | **"Sem dados" limpo, 0 datasets** | — |
| flapping 24/07 | 67 | 301 (microlacunas visíveis) | cru |
| 5 sensores × 24h | 8×51 | 8×~884 | cru |
| hum+press MAX (multi-schema) | — | 854/787 baldes, fusão 6ch↔8ch sem erro | banda |

Provas cruzadas:

- **Node×Python**: pipeline da página (extraído do `WebUI.h` real) sobre
  `20260812.h5` → n=1428, mín −20,5, máx −6,5, 1 lacuna — **idêntico** à referência
  Python (n=1428, −20,50, −6,50, 1).
- **Extremos**: badges 7d (−21,2/−6,5) = verdade Python (−21,2/−6,5). Badge MIN do
  MAX (−28,3 °C) é o spike plantado de −9 °C do manifest — capturado E visível.
- **CSV**: export do 24h = 1399 linhas = 1398 pontos do gráfico + cabeçalho.
- **Console**: zero erros de JS em toda a matriz.
- **Visual** (`scratchpad/caps_antes/` × `caps_depois/`): o 24h passou da serra
  uniforme para os ~30 ciclos reais do compressor com os 2 degelos únicos; o 1h
  passou da curva fabricada de 3 pontos para 60 amostras com os degraus de
  quantização do DS18B20 visíveis.
- **Log do dispositivo limpo**: os únicos `[FTL]` da noite são o falso-positivo
  documentado do reset por toque 1200 bps do flash (`ctx=209/205`, `up=8s`), e o
  `code=2` é o `reload confirm` do procedimento. Zero reboots sob a carga nova.
  Selagens e registros seguem 1/min.

## 6. Custos, limites e próximos passos

- **Rede**: a 1ª carga de MAX baixa todos os dias (~600 KiB, ~30–60 s, com progresso
  por arquivo e cancelamento útil); depois, qualquer troca de faixa/sensor é
  cache-quente (só o dia corrente + hora aberta). Faixas ≤7d baixam 2–8 arquivos.
  Bônus de robustez: cada arquivo é uma requisição curta — imune ao RST que o roteador
  injeta em fluxos longos na porta 80 (a resposta única de 500 KB do caminho antigo
  era exatamente a vítima).
- **Memória da página**: ~0,8 MB de bytes + ~3–4 MB decodificado com o dispositivo
  INTEIRO em cache — folgado em desktop e celular atual.
- **Flash**: 94,8% (novo JS gzip incluído; ~54 KiB de folga no slot).
- **Dependência de CDN** (pré-existente, fora do escopo): `chart.js` vem de
  `cdn.jsdelivr.net` — sem internet, a página de gráficos não desenha. Recomendo
  avaliar embarcar o Chart.js (ou um renderer próprio) num próximo ciclo.
  > **RESOLVIDO na v2.2.11-beta (18/08/2026).** O caminho avaliado foi o segundo:
  > embarcar o Chart.js foi medido e recusado — podá-lo ao que a página usa leva de
  > 70.592 para 56.818 B em gzip, e o núcleo sozinho, sem nada registrado, já são
  > 43.527 B que não desenham pixel nenhum. O renderizador próprio (`h5g`, embutido
  > no `WebUI.h`) custa **4.721 B** e substitui a tag do CDN. Detalhe do sintoma que
  > esta análise não tinha: sem internet o `catch` do carregador engolia o
  > `ReferenceError` e a página dizia **"Connection lost."** — culpando a rede por um
  > script ausente.
- **Kinds genéricos** (CO2, VOC, GENERIC): a tabela `H5G_KIND` do cliente cobre
  t/u/p; grandeza nova = 1 linha ali (mesma filosofia da SensorChannelTable).
- **Fuso**: nomes de arquivo usam o dia LOCAL do aparelho; a página assume navegador
  no mesmo fuso (mesma premissa que o export CSV já fazia). A folga de ±1 dia na
  seleção de arquivos absorve diferenças pequenas.
- **Evolução natural**: cache persistente (IndexedDB) para MAX instantâneo entre
  visitas; zoom/pan com redecimação local (os dados já estão no navegador).

## 7. Reprodução e restauração

```bash
# gerar sintéticos (schema real vem do backup) e validar
python3 scratchpad/gen_synth.py
python3 tools/history_v5.py --stats scratchpad/synth_h5/20260812.h5

# varredura de capturas (antes/depois)
scratchpad/pwenv/bin/python scratchpad/capture_graphs.py <outdir>

# restaurar o histórico REAL do rig (quando os testes acabarem)
python3 scratchpad/rig.py delete /history/<sinteticos>...
python3 scratchpad/rig.py upload scratchpad/history_backup_20260813_pre_synth/*.h5
```

Estado do rig ao fim da fase web: `pico_w_release` da working tree (JS novo),
64 `.h5` (63 sintéticos 09/06→12/08 + 13/08 real), FS ~86%, 5 sensores lendo,
NTP ok, heap estável (lb 46,6 KB).

---

# Parte 2 — Os gráficos de histórico do DISPLAY (TFT)

Mesma campanha, mesmo dado sintético, agora no caminho do firmware:
`AppManager_Graph.cpp` (Core 0 carrega o `GraphDataPackage`) →
`DisplayManager_Graph.cpp` (Core 1 desenha em strips de canvas). Faixas do
display: 1H, 6H, 12H, 24H, 7D + calendário + navegação ◀/▶.

## 8. Defeitos encontrados (T1–T5)

**T1 — Stride fixo por faixa derruba picos.** A decimação era 1-em-N com N
cravado para cadência de 1/min (1, 2, 4, 8, **51**). No 7D um pico de 1 minuto
tinha 1/51 de chance de ser desenhado. Prova visual (`tft_antes/frz_7d.png`):
os degelos da geladeira — **todos idênticos por construção** — saíam cada um com
uma altura aleatória, e vários sumiam por completo.

**T2 — Eixo Y honesto sobre curva desonesta.** A escala Y usa os extremos REAIS
(full-scan) — correto — mas a curva stride perdia o pico: o eixo anunciava
−6,5 °C e a linha nunca chegava lá (o marcador ◆ do "máximo desenhado"
contradizia o rótulo do próprio eixo).

**T3 — X por índice, não por tempo.** O comentário do loader prometia posição
"proporcional ao tempo" (`tsPoints[]` existe no pacote) e o renderer nunca usou:
`px[i] = i·gw/(count−1)`. Uma lacuna de 6,5 h virava ~1 px invisível e a linha
do tempo inteira se distorcia ao redor de qualquer buraco.

**T4 — Cadência de 1/min pressuposta.** Com `hi` ≠ 1 min as contagens
desmoronam (24H a 10 min/registro = 18 pontos com o stride 8). E no 7D cheio os
arquivos sozinhos geravam 197 pontos: o teto `GRAPH_WIDTH=200` **cortava a cauda
da hora aberta** — a borda direita ficava velha, exatamente o que a regra da
borda-em-dia proíbe.

**T5 — Estatísticas enviesadas pela decimação.** `MÉDIA/DESVIO/Δ` do detalhe
numérico eram computados sobre os pontos DESENHADOS (n=180 no 24H), não sobre a
janela real.

## 9. O fix

- **Baldes uniformes no tempo** (`n = clamp(janela/intervalo_nominal, 40, 200)`),
  cada um com **mín/máx/média**; balde vazio = NAN = lacuna desenhada com a
  largura verdadeira (com baldes uniformes, o X por índice do renderer virou
  proporcional ao tempo de graça). Acumulador em função estática única
  (`graphAccumRecord` + `gAcc`) — dois pontos de chamada (arquivos + cauda RAM)
  sem clonar o corpo.
- **Banda mín/máx** pintada atrás da linha média (substitui o preenchimento até
  a base): o degelo de 1 minuto sobrevive porque o extremo É a aresta do balde.
  Marcadores ◆ agora sentam na aresta da banda do balde do extremo real — eixo,
  badge e marcador finalmente concordam.
- **Ponto isolado** (balde com dado entre duas lacunas) ganha um dot 3×3 nas
  duas séries — antes não desenhava nada.
- **Estatísticas full-res** (todas as amostras da janela) e `n=` do detalhe
  mostra `sampleCount` real.
- Cadência configurável respeitada; teto nunca corta a cauda (baldes não
  crescem com registros).
- Custo: +3,2 KB no `GraphDataPackage` (×2 cópias) + ~7 KB do acumulador
  estático → **RAM 41,6% → 47,0%**; flash +~0,9 KB. Menor bloco de heap
  low-water ~29 KB (ainda folgado para os 16 KB contíguos do TLS).

## 10. Validação no ferro (test build, mesma matriz)

| medição | ANTES | DEPOIS |
|---|---|---|
| 24H detalhe `n=` | 180 | **1 435** (todas as amostras) |
| 7D geladeira | degelos com alturas aleatórias, vários ausentes | **13/13 degelos uniformes tocando o topo** |
| pico vs eixo Y | eixo −6,5 / curva parava antes | **marcador na aresta = eixo = badge** |
| lacuna real (apagão/hoje) | ~1 px invisível | **vão em branco na largura verdadeira** |
| BMP280 24H | linha de pressão stride | temp + pressão com banda, eixo 1011,2–1024,9 hPa |
| estatísticas | sobre 180 pontos | sobre 1 435 registros |

Capturas: `scratchpad/tft_antes/` × `scratchpad/tft_depois/` (via
`/api/screenshot` + navegação `screen`/`touch sim` do build de teste —
receita em `scratchpad/tft_caps*.py`).

## 11. Achados operacionais desta fase

- **`pico_w_test` NÃO LINKA com o fix web** (+6,2 KB de JS gz): estourou o teto
  em 6 184 B. Capturas rodaram com o `WebUI.h` do HEAD (stash) e, para caber o
  fix do TFT (+~0,9 KB), com a licença embutida temporariamente reduzida
  (restaurada no build final; release nunca foi afetada — 94,9% com tudo).
  **O env de teste precisa de dieta real antes do próximo ciclo.**
- A paginação do rodapé da dashboard **persiste entre telas** — um `touch sim`
  cego seleciona o slot errado; só o reboot volta à página 1. Roteiros de
  captura devem rebootar antes de selecionar sensor.
- `/api/screenshot` falha intermitente (~20%) sob toque+render simultâneos —
  sempre capturar em dupla.
- Toques `touch sim` durante um render de 7D podem cair no debounce — settles
  de ≥5 s entre zoom e captura.

Estado FINAL do rig: `pico_w_release` da working tree com os DOIS fixes
(web + TFT), md5 novo, 2.1.7-beta + working tree; sintéticos e hoje-real na
LittleFS a ~86%; suíte de release intacta (licença completa, 9 comandos).
