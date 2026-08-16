# HistoryV5 — Instruções de Implementação

| Campo | Valor |
|---|---|
| Documento | Instruções normativas de implementação — firmware SIMUT + ferramenta host |
| Público | Agentes de implementação (IA ou humanos) com acesso ao repositório SIMUT |
| Plataforma | RP2040 (Raspberry Pi Pico W) · Arduino framework (arduino-pico) · LittleFS 1 MB |
| Status | **Este documento é a fonte de verdade do formato V5.** Divergência entre código e este documento é bug do código. |

---

## 1. Missão

Substituir o formato de histórico legado do SIMUT (registros binários fixos de 28 B em arquivos `.bin` diários) pelo formato **V5**: um formato comprimido de série temporal, **genérico e autodescritivo**, baseado em delta encoding com códigos de prefixo em bitstream. O V5 grava os mesmos valores `int16` que o sistema produz hoje, **sem nenhuma perda** (`decode(encode(x)) == x`, bit a bit, incluindo o sentinela NAN), e reduz o consumo de flash de ~40 KiB/dia para ~12,5 KiB/dia na configuração atual (12 canais, 1 registro/min), elevando a autonomia de retenção para ≥ 62 dias na partição de 1 MB.

Entregáveis:

1. Núcleo V5 no firmware: `BitWriter`, `BitReader`, `HistoryV5Encoder`, `HistoryV5Decoder`, `HistoryV5Scan` (§5).
2. Integração nos módulos existentes sem remover nenhuma assinatura pública (§6).
3. Ferramenta host `tools/history_v5.py` — implementação de referência completa e conversor do formato legado (§9).
4. Suíte de testes e critérios de aceite cumpridos (§12–§13).

### 1.1 Requisitos invioláveis

| ID | Requisito |
|---|---|
| R1 | Funcionalidade preservada: gravação por minuto, arquivos diários, rotação a 86%, NAN explícito, correção retroativa de timestamps, leitura por web/telemetria/display, export CSV via web. |
| R2 | Compressão **lossless**: reconstrução bit-exata de valores e timestamps para qualquer sequência válida. |
| R3 | Fallback RAW por bloco: dado incompressível nunca ocupa mais que ~38 KiB/dia (12 canais). |
| R4 | **Autodescrição**: qualquer leitor que implemente este documento decodifica um `.h5` corretamente (canais, grandezas, escalas) sem conhecer o firmware que o gravou. |
| R5 | Mudança do conjunto de canais no meio do dia sem perda nem ambiguidade (novo SCHEMA no mesmo arquivo). |
| R6 | Gráficos: decimação por envelope min/máx por canal — picos nunca desaparecem; latências dentro dos orçamentos do §10. |
| R7 | O firmware **não contém** leitor do formato legado. |
| R8 | Power-loss: perda máxima de 10 min de dados; sistema de arquivos íntegro após corte em qualquer instante. |
| R9 | Caminho quente sem heap e sem FPU; RAM adicional ≤ 2,2 KiB estáticos; toda escrita em flash sob a disciplina `flashOp` (§2). |

---

## 2. Contexto do sistema — o que você precisa saber antes de tocar no código

**Arquitetura.** RP2040 dual-core: Core 0 roda toda a lógica (sensores, rede, web, telemetria, storage); Core 1 é exclusivo do display TFT. A flash é XIP: qualquer program/erase exige parar o Core 1 (`multicore_lockout`) e desligar IRQs no Core 0 — janelas que historicamente causaram instabilidade de Wi-Fi e display. **Minimizar a frequência dessas janelas é objetivo de projeto do V5** (o caminho quente de gravação passa a ser 100% RAM; flash só a cada hora e no flush de segurança).

**Disciplinas obrigatórias do repositório** (já existem — use-as, não as reimplemente):

| Mecanismo | Uso |
|---|---|
| `enterFlashSafeMode()` / `exitFlashSafeMode()` (ou o helper `flashOp(lambda)`) | Envolve **toda** operação LittleFS de escrita. Alimenta o WDT antes e depois. |
| `enforceStorageLimit()` | Rotação: apaga arquivos mais antigos de `/history` até uso < 86%, com budget de 4 s. Mantenha a política; ajuste apenas o filtro de extensão para `.h5`. |
| `LOG_*` / códigos `STO_*` | Todo caminho de erro loga código estruturado; nenhum caminho de erro pode travar ou resetar. |
| Escrita atômica de config (`.tmp` + `rename`) | Reuse o mesmo padrão para o rewrite da correção retroativa (§7.3). |

**Padrões de código obrigatórios.** Comentários objetivos em toda lógica não trivial e Doxygen em toda declaração pública; identação de 4 espaços, impecável e consistente com o restante do repositório; `static_assert(sizeof(...))` em todo struct `packed`; **nenhuma função pública existente é removida ou renomeada** — assinaturas antigas delegam às novas classes.

---

## 3. Especificação normativa do formato V5

Princípio que organiza o formato: **cada informação é paga na taxa em que muda.** Valores mudam por minuto → bits no payload. Estatística muda por hora → header do bloco. Significado (quais canais, o que medem, em que escala) muda por reconfiguração → chunk SCHEMA, fora do caminho quente.

Arquivo diário: `/history/AAAAMMDD.h5` — sequência contígua de chunks, **little-endian**, sem padding entre chunks, sem índice externo. **Todo arquivo inicia com um chunk SCHEMA**; cada DATA pertence ao SCHEMA mais recente que o precede.

### 3.1 Preâmbulo comum de chunk — 8 bytes

| Offset | Tam. | Campo | Valor/semântica |
|---|---|---|---|
| 0 | 2 | `magic` | `0x4835` ("H5") |
| 2 | 1 | `version` | `0x02` (versão do formato em disco) |
| 3 | 1 | `type` | `0x01` SCHEMA · `0x02` DATA |
| 4 | 1 | `flags` | DATA: bit0 `RAW`, bit1 `PARTIAL`, bit2 `CLOCK_SYNCED` · SCHEMA: 0 |
| 5 | 1 | `a` | SCHEMA: `nCh` (1…16) · DATA: `count` (1…60) |
| 6 | 1 | `b` | SCHEMA: `schemaSeq` (0,1,… no dia) · DATA: `nCh` |
| 7 | 1 | `rsv` | `0xFF` |

### 3.2 Chunk SCHEMA — `8 + 4·nCh + 2` bytes

Preâmbulo + `nCh` descritores de 4 B + `crc16`. O descritor:

| Campo | Tam. | Semântica |
|---|---|---|
| `id` | 1 | Identidade estável do canal no equipamento. Nunca reciclada. Sobrevive a remapeamento físico de GPIO/ROM. |
| `kind` | 1 | Grandeza física (enum §4) — dá nome/unidade a gráficos, CSV e alarmes. |
| `scaleExp` | 1, c/ sinal | `valor_real = raw × 10^scaleExp` (ex.: °C ×100 ⇒ −2; hPa ×10 ⇒ −1). |
| `flags` | 1 | Reserva = 0. |

### 3.3 Chunk DATA — header `16 + 6·nCh` bytes + payload

Parte fixa (16 B): preâmbulo (8 B) + `t0 u32` (epoch UTC, em segundos, do 1º registro do bloco) + `payloadLen u16` (bytes do bitstream, **após** as caudas) + `crc16 u16`. Caudas, nesta ordem, todas `int16` na ordem de canais do SCHEMA: `keyframe[nCh]` (registro nº 1, absoluto), `chMin[nCh]`, `chMax[nCh]` (envelope do bloco; canal 100% NAN no bloco ⇒ min = máx = `0x8000`). `count = 1` ⇒ `payloadLen = 0` é legal.

Um bloco cobre no máximo `H5_BLOCK_MAX_RECORDS = 60` registros (1 h na cadência de 1/min). Bloco é a unidade de compressão, de CRC e de decimação: **decodifica sozinho**, sem depender de blocos vizinhos. Atenção: 60 registros **não** implicam 60 × intervalo de tempo — registros entram quando são tomados, e um bloco que atravessou queda de sensor ou trecho sob gate cobre muito mais tempo do que a contagem sugere. Nada deve derivar limite temporal da contagem.

**`flags.CLOCK_SYNCED` (bit 2)** — o relógio que carimbou o bloco era real (NTP ou ajuste manual), não o provisório da semeadura de boot. Escrito apenas no snapshot `.wip` e lido apenas pela semeadura do relógio no boot seguinte: o que decide se um `t0` pode ser acreditado não é o seu valor, e sim a procedência, que só é conhecida no instante da escrita. Blocos adotados de um `.wip` levam o bit para o arquivo do dia, onde permanece verdadeiro e inofensivo. Leitores ignoram bits de `flags` que não conhecem, então o bit não quebra compatibilidade e não muda `H5_VERSION`. O bit está dentro da cobertura do CRC — forjá-lo invalida o bloco.

### 3.4 CRC — definição única para todo o formato

**CRC-16/CCITT-FALSE**: polinômio `0x1021`, init `0xFFFF`, sem reflexão de entrada/saída, xorout `0x0000`. Vetor de teste obrigatório: `CRC("123456789") = 0x29B1`. Cobertura: todos os bytes do chunk **exceto o próprio campo `crc16`** (SCHEMA: preâmbulo + descritores; DATA: header fixo + caudas + payload).

### 3.5 Payload comprimido (flags.RAW = 0)

Bitstream **MSB-first** (bit mais significativo de cada byte primeiro; campos multi-bit emitidos do bit mais significativo para o menos). Contém os registros 2…`count`; o registro 1 vive em `t0` + `keyframe`. Cada registro emite **1 símbolo de tempo + nCh símbolos de valor, na ordem do SCHEMA**.

**Símbolos de tempo.** Estado: `prevDelta`, inicializado com o **intervalo nominal configurado** (60 s por padrão — ler da config, não hardcode). Para o registro n: `delta = tₙ − tₙ₋₁`; `dod = delta − prevDelta`; após emitir, `prevDelta = delta`.

| Prefixo | Payload | Significado | Custo |
|---|---|---|---|
| `0` | — | dod = 0 | 1 bit |
| `10` | 7 bits c/ sinal | dod ∈ [−64, +63] s | 9 bits |
| `110` | 12 bits c/ sinal | dod ∈ [−2048, +2047] s | 15 bits |
| `111` | 32 bits | Resync: epoch absoluto de tₙ (salto de relógio); `prevDelta` volta ao nominal | 35 bits |

**Símbolos de valor.** Estado por canal: `prev[c]`, inicializado com `keyframe[c]`. Delta `d = vₙ[c] − prev[c]` codificado em **zigzag**: `z = (uint16_t)((d << 1) ^ (d >> 15))` (shift aritmético); decodificação `d = (int16_t)((z >> 1) ^ (uint16_t)(-(z & 1)))`.

| Prefixo | Payload | Faixa do Δ | Custo |
|---|---|---|---|
| `0` | — | Δ = 0 | 1 bit |
| `10` | 3 bits (zigzag) | −4 … +3 | 5 bits |
| `110` | 6 bits (zigzag) | −32 … +31 | 9 bits |
| `1110` | 10 bits (zigzag) | −512 … +511 | 14 bits |
| `1111` | 16 bits | **Valor absoluto** (reinicia a cadeia do canal) | 20 bits |

**Semântica NAN** (sentinela `0x8000`, idêntica ao restante do sistema): transição valor→NAN emite `1111` + `0x8000`; NAN→valor emite `1111` + valor; enquanto o canal permanece NAN, cada registro emite `0` (1 bit). Deltas **nunca** são calculados de/para `0x8000`.

### 3.6 Payload RAW (flags.RAW = 1)

`(count − 1)` registros consecutivos de `2 + 2·nCh` bytes: `dt u16` (segundos desde `t0`) + `nCh × int16` na ordem do SCHEMA. O `seal()` compara os tamanhos e grava **o menor** entre comprimido e RAW — o V5 nunca fica pior que ~o formato legado.

### 3.7 Regras semânticas

1. Sensor offline **transitório** não é mudança de schema — é NAN (1 bit/min).
2. Adição/remoção/retroca **permanente** de canal: selar o bloco corrente com `PARTIAL`, gravar novo SCHEMA (`schemaSeq + 1`) no mesmo arquivo, iniciar bloco novo.
3. Leitores validam `DATA.nCh == SCHEMA.nCh` vigente; divergência ⇒ rejeitar **o chunk** (não o arquivo), logar e continuar no próximo.
4. Chunk com CRC inválido ⇒ rejeitado integralmente; nunca ler payload parcial de chunk inválido.
5. Blocos `PARTIAL` (`count < 60`) são plenamente válidos: ocorrem em boot no meio da hora, flush de segurança, recuperação e troca de schema.

---

## 4. Código de referência normativo (SystemDefs.h)

```cpp
/* ===========================================================================
 *  HistoryV5 — formato de histórico comprimido, genérico e autodescritivo
 *  Regra de ouro: mudar QUALQUER layout abaixo exige bump de H5_VERSION.
 *  Os static_assert transformam deslize de layout em erro de build.
 * ======================================================================== */
#define H5_MAGIC                0x4835              /* "H5" little-endian    */
#define H5_VERSION              0x02                /* versão em disco       */
#define HISTORY_FILE_EXT        ".h5"               /* /history/AAAAMMDD.h5  */

#define H5_CHUNK_SCHEMA         0x01
#define H5_CHUNK_DATA           0x02

#define H5_MAX_CHANNELS         16                  /* teto de compilação    */
#define H5_BLOCK_MAX_RECORDS    60                  /* 1 reg/min → 1 h       */
#define H5_NAN_SENTINEL         ((int16_t)0x8000)

/* Tamanhos derivados (n = canais do schema vigente). */
#define H5_SCHEMA_CHUNK_SIZE(n) (8u + 4u * (n) + 2u)
#define H5_DATA_HEADER_SIZE(n)  (16u + 6u * (n))
#define H5_RAW_RECORD_SIZE(n)   (2u + 2u * (n))
#define H5_BLOCK_MAX_BYTES      (H5_DATA_HEADER_SIZE(H5_MAX_CHANNELS) +      \
                                 (H5_BLOCK_MAX_RECORDS - 1)                  \
                                 * H5_RAW_RECORD_SIZE(H5_MAX_CHANNELS))
                                                    /* = 2.118 B             */

/** Grandezas físicas. Adicionar valores é livre; renumerar é proibido. */
enum H5Kind : uint8_t {
    H5_KIND_TEMP_C    = 0x01,   /* °C   — escala típica −2 (×100)           */
    H5_KIND_HUM_PCT   = 0x02,   /* % UR — típica −2                         */
    H5_KIND_PRESS_HPA = 0x03,   /* hPa  — típica −1 (×10)                   */
    H5_KIND_CO2_PPM   = 0x04,   /* ppm  — típica 0                          */
    H5_KIND_VOC_IDX   = 0x05,   /* índice adimensional — típica 0           */
    H5_KIND_GENERIC   = 0x7E,   /* UI exibe raw × 10^scaleExp, sem unidade  */
    /* 0x80..0xFF reservados p/ famílias não-lineares (ex.: log-lux)        */
};

/** Descritor de canal — vive no chunk SCHEMA (4 B por canal). */
struct __attribute__((packed)) H5ChannelDesc {
    uint8_t id;                 /* identidade estável no equipamento        */
    uint8_t kind;               /* H5Kind                                   */
    int8_t  scaleExp;           /* real = raw × 10^scaleExp                 */
    uint8_t flags;              /* reserva (0)                              */
};
static_assert(sizeof(H5ChannelDesc) == 4, "descritor de canal quebrado");

/** Preâmbulo comum a todo chunk (§3.1). */
struct __attribute__((packed)) H5ChunkPreamble {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  a;                 /* SCHEMA: nCh · DATA: count                */
    uint8_t  b;                 /* SCHEMA: schemaSeq · DATA: nCh            */
    uint8_t  rsv;               /* 0xFF                                     */
};
static_assert(sizeof(H5ChunkPreamble) == 8, "preambulo de chunk quebrado");

/** Parte fixa do header DATA (§3.3); caudas via accessors abaixo. */
struct __attribute__((packed)) H5DataHeader {
    H5ChunkPreamble pre;
    uint32_t t0;                /* epoch UTC (s) do 1º registro do bloco    */
    uint16_t payloadLen;        /* bytes do bitstream após as caudas        */
    uint16_t crc16;             /* §3.4 — cobre tudo exceto este campo      */
};
static_assert(sizeof(H5DataHeader) == 16, "parte fixa do DATA quebrada");

/* Accessors das caudas — o ÚNICO ponto do código que conhece o layout
 * variável. Todo acesso a keyframe/min/máx/payload passa por aqui.        */
static inline int16_t* h5Keyframe(uint8_t* d, uint8_t n)
                       { (void)n; return (int16_t*)(d + 16); }
static inline int16_t* h5ChMin  (uint8_t* d, uint8_t n)
                       { return (int16_t*)(d + 16 + 2u * n); }
static inline int16_t* h5ChMax  (uint8_t* d, uint8_t n)
                       { return (int16_t*)(d + 16 + 4u * n); }
static inline uint8_t* h5Payload(uint8_t* d, uint8_t n)
                       { return d + H5_DATA_HEADER_SIZE(n); }

/** Schema compilado desta entrega (§8). A ordem É a ordem de emissão e
 *  coincide com a ordem dos campos do BinaryHistoryRecord em RAM.          */
static const H5ChannelDesc kCompiledSchema[] = {
    { 0, H5_KIND_TEMP_C, -2, 0 },  { 1, H5_KIND_TEMP_C, -2, 0 },
    { 2, H5_KIND_TEMP_C, -2, 0 },  { 3, H5_KIND_TEMP_C, -2, 0 },
    { 4, H5_KIND_TEMP_C, -2, 0 },  { 5, H5_KIND_TEMP_C, -2, 0 },
    { 6, H5_KIND_TEMP_C, -2, 0 },  { 7, H5_KIND_TEMP_C, -2, 0 },
    { 8, H5_KIND_TEMP_C, -2, 0 },  { 9, H5_KIND_TEMP_C, -2, 0 },
    { 10, H5_KIND_TEMP_C, -2, 0 },              /* DHT22 — temperatura      */
    { 11, H5_KIND_HUM_PCT, -2, 0 },             /* DHT22 — umidade          */
};
#define H5_COMPILED_NCH  (sizeof(kCompiledSchema) / sizeof(kCompiledSchema[0]))
static_assert(H5_COMPILED_NCH <= H5_MAX_CHANNELS, "schema excede o teto");
```

---

## 5. API a implementar (StorageManager)

Contratos resumidos; parâmetros de schema sempre explícitos — **nada além de `kCompiledSchema` e da validação do §8 pode assumir 12 canais**.

```cpp
/* Assinaturas públicas preservadas (delegam ao núcleo V5) ------------------ */
bool   writeHistoryEntry(const BinaryHistoryRecord& rec);
String getHistoryFileName();                        /* extensão .h5          */
void   enforceStorageLimit();                       /* política inalterada   */

/* Núcleo novo -------------------------------------------------------------- */
class BitWriter {   /* MSB-first sobre buffer estático; overflow ⇒ flag
                       pegajosa consultável; nunca escreve fora do buffer   */ };
class BitReader {   /* simétrico; eof()/underflow ⇒ flag pegajosa           */ };

/**
 * @brief Comprime um bloco em RAM para o schema fornecido.
 * @details Sem heap. Estado ≈ 90 B + buffer estático H5_BLOCK_MAX_BYTES.
 *          seal() monta header+caudas+payload no buffer de saída, escolhe
 *          comprimido vs RAW (o menor), preenche payloadLen e crc16, e
 *          retorna o tamanho total do chunk (0 em erro).
 */
class HistoryV5Encoder {
public:
    void    begin(const H5ChannelDesc* schema, uint8_t nCh,
                  uint16_t nominalIntervalS);
    void    reset(uint32_t epoch, const int16_t* v);    /* v[nCh]            */
    bool    add  (uint32_t epoch, const int16_t* v);    /* false = cheio     */
    size_t  seal (uint8_t* out, size_t cap, uint8_t extraFlags);
    uint8_t count() const;
};

/** Decodificador-iterador de um chunk DATA já validado (magic+CRC+nCh). */
class HistoryV5Decoder {
public:
    bool begin(const uint8_t* chunk, size_t len,
               const H5ChannelDesc* schema, uint8_t nCh);
    bool next (uint32_t& epoch, int16_t* v);            /* v[nCh]            */
    const H5DataHeader& header() const;
};

/**
 * @brief Varre chunks de um arquivo SEM decodificar payload.
 * @details Salta DATA via payloadLen (O(1) por bloco; ≤ 24 hops/dia).
 *          Entrega SCHEMAs e headers DATA (com caudas) na ordem do arquivo.
 *          Valida CRC de cada chunk entregue; chunk inválido é pulado
 *          com log, nunca propagado.
 */
class HistoryV5Scan {
public:
    bool open(const String& path);
    bool nextSchema(H5ChannelDesc* out, uint8_t& nCh, uint8_t& seq);
    bool nextData  (H5DataHeader& hdr, int16_t* kf, int16_t* mn, int16_t* mx);
    bool seek(uint32_t epoch);          /* posiciona no bloco que contém t   */
};
```

---

## 6. Integração módulo a módulo

| Módulo | Tarefas |
|---|---|
| **StorageManager** | Dono do formato. `writeHistoryEntry()` alimenta o encoder (RAM, sem flash). Criar arquivo novo ⇒ gravar SCHEMA (do `kCompiledSchema`) antes do 1º DATA. Selagem horária e flush do `.wip` (§7). Rotação: apenas trocar o filtro para `.h5`. Correção retroativa: §7.3. |
| **AppManager** | Agendar `sealHour()` na virada da hora e `flushWip()` a cada 10 min. Expor hook `onSensorSetChanged()` que executa a regra §3.7-2. |
| **WebManager** | Endpoint de gráfico com dois caminhos (§10): envelope (janelas ≥ 24 h — só headers via `HistoryV5Scan`) e decode (janelas < 24 h). Export CSV: gerar on-the-fly via decoder; **linha de cabeçalho derivada do SCHEMA** (`id`, unidade do `kind`, valores já escalados por `10^scaleExp`). |
| **TelemetryManager** | Ler faixas via decoder-iterador. Payload de upload inalterado (envia valores, não bytes de arquivo). |
| **DisplayManager** | Nenhuma mudança de API; gráficos locais usam os mesmos dois caminhos. |
| **SensorManager** | Nenhuma mudança nesta entrega. |

---

## 7. Fluxos normativos

### 7.1 Escrita (caminho quente, 1×/min)

```
SensorManager ──(BinaryHistoryRecord)──▶ writeHistoryEntry()
                                              │  100% RAM (µs, sem flash)
                                              ▼
                                     HistoryV5Encoder.add()
                                              │
        arquivo novo? ── sim ─▶ flashOp{ grava SCHEMA + append }
              │                               │
        hora fechou ──────────────────────────┤────────── a cada 10 min
              │                               │                 │
              ▼                               │                 ▼
    seal() → flashOp{ append          (nada até lá)   flashOp{ regrava
    /history/AAAAMMDD.h5 }                             /history/.wip }
```

O arquivo diário é **append-only** (regime em que o LittleFS é eficiente e previsível). Resultado esperado: ~24 appends + 144 regravações do `.wip` por dia, contra 1.440 escritas/dia do formato legado — ~8× menos janelas de lockout do Core 1.

### 7.2 `.wip`, boot e recuperação (R8)

`/history/.wip` contém **exatamente um** chunk DATA selado com `PARTIAL` (sem SCHEMA — o schema é o compilado, validado na recuperação). A cada 10 min: `seal()` do estado corrente para o buffer estático → regravar `.wip` inteiro sob `flashOp` → **o encoder continua acumulando o mesmo bloco** (o `.wip` é um snapshot, não um fechamento).

Boot: montar FS → se `.wip` existe e o CRC fecha → garantir que o arquivo diário do seu `t0` existe **começando por SCHEMA** (criar se necessário) → append do chunk como está → truncar `.wip`. `.wip` inválido ⇒ descartar com log (perda ≤ 10 min, nunca crash). Iniciar encoder novo (`PARTIAL` se no meio da hora).

### 7.3 Correção retroativa de timestamps

Apenas `t0` dos headers DATA muda (SCHEMA não tem tempo; o interior do bloco é relativo a `t0`). LittleFS não suporta overwrite parcial confiável ⇒ stream-rewrite: ler chunk a chunk, ajustar `t0` dos DATA, recalcular `crc16`, gravar em `.tmp`, `rename()` atômico (mesmo padrão do config). Custo ~12,5 KiB por dia afetado. O bloco corrente em RAM ajusta `t0` no estado do encoder.

---

## 8. Escopo desta entrega — schema compilado

Nesta entrega o firmware **grava sempre `kCompiledSchema`** e, na leitura, valida cada SCHEMA encontrado byte a byte contra ele. SCHEMA divergente ⇒ `STO_SCHEMA_MISMATCH` + arquivo pulado (nunca crash, nunca leitura parcial). O núcleo (§5) permanece parametrizado por schema — é o que garante que a evolução futura para schema em runtime não exigirá mudança de formato nem reescrita do núcleo.

**Fora de escopo** (não implementar): schema fornecido em runtime pelo registry de sensores; UI/alarmes/calibração indexados dinamicamente por `id`; kinds não-lineares (0x80+).

**Proibições:** heap no caminho de dados; leitor do formato legado no firmware; quantização/downsampling ("lossy"); reordenar/realinhar campos de qualquer struct `packed`; `count > 60` ou `nCh > 16`; escrita em flash fora de `flashOp`; remoção/renomeação de função pública.

---

## 9. Ferramenta host — `tools/history_v5.py`

Python 3, sem dependências externas. É a **implementação de referência** (encoder + decoder plenos, qualquer `nCh` de 1 a 16) e o **oráculo** dos testes do firmware.

| Comando | Função |
|---|---|
| `--convert entrada.bin saida.h5` | Converte arquivo do formato legado para V5 com o schema padrão (tabela §4). |
| `--dump-csv arquivo.h5` | Decodifica para CSV com cabeçalho derivado do SCHEMA e valores escalados. |
| `--stats arquivo.h5 [...]` | Taxa de compressão, bits médios por canal, histograma de símbolos. |
| `--selftest` | Vetores do §3 (CRC `0x29B1`, zigzag, limites de cada prefixo, NAN, resync). |

Layout do formato legado, necessário apenas ao conversor (nunca ao firmware): registros consecutivos de **28 B** = `epoch u32` + `12 × int16` na ordem do schema padrão; NAN = `0x8000`; arquivos `.bin` diários; little-endian.

---

## 10. Orçamentos — são requisitos, não metas

| Operação (nCh = 12, RP2040 @ 133 MHz, display + web ativos) | Limite |
|---|---|
| `writeHistoryEntry()` (caminho quente) | ≤ 50 µs, zero flash |
| `seal()` + append horário | ≤ 30 ms |
| Decodificar 1 bloco (60 reg × 12 ch) | ≤ 1 ms |
| Gráfico 24 h (decode de 24 blocos) | ≤ 60 ms |
| Gráfico 7 d (envelope, 168 headers) | ≤ 25 ms |
| Gráfico 30 d (envelope, 720 headers) | ≤ 80 ms |
| RAM estática adicional total | ≤ 2,2 KiB |
| Flash de **código** adicional (medida ao fim do WP2) | ≤ +2 KB |
| Flash de **dados**: consumo diário (12 ch) | ~12,5 KiB (SCHEMA 58 B + 24 blocos ≈ 531 B) |
| Autonomia projetada com dados reais | ≥ 62 dias |
| Pior caso (blocos 100% RAW) | ≤ ~38 KiB/dia |

---

## 11. Migração

Primeiro boot com V5: remover de `/history` todo arquivo que **não** comece com `magic 0x4835` (inclui os `.bin` legados e lixo), logando `STO_LEGACY_PURGED <n>` e emitindo um aviso único na web/UI. O histórico recomeça vazio. A preservação de dados antigos é responsabilidade do usuário **antes** do update: baixar os `.bin` pelo export web e converter no host com `--convert`. Nada é reimportado ao equipamento.

---

## 12. Testes obrigatórios

| Camada | Cobre | Critério de verde |
|---|---|---|
| Unitário (host, C++) | BitWriter/Reader (roundtrip, fronteiras de byte, overflow/underflow), zigzag, limites exatos de cada prefixo (±4/±32/±512), NAN (entrar/sair/permanecer), resync, escolha RAW, CRC (vetor `0x29B1`), accessors de caudas para nCh ∈ {1, 3, 12, 16} | 100% dos símbolos e tamanhos exercitados |
| Propriedade (host) | `decode(encode(s)) == s` — ≥ 10⁶ séries aleatórias + sintéticas (rampas, degraus, ruído, NAN intermitente), por nCh | Bit-exato vs `history_v5.py` |
| Golden replay | Arquivos `.bin` reais → `--convert` → decodificados pelo firmware; séries idênticas valor a valor | Também **mede** a taxa real de compressão |
| Autodescrição (R4) | Arquivos gravados pelo firmware lidos pela ferramenta **sem** `kCompiledSchema` (só este documento) | CSV do host ≡ CSV do firmware |
| Rejeição | SCHEMA divergente, `nCh` corrompido, DATA antes de SCHEMA, CRC inválido, arquivo com troca de schema no meio do dia (gerado no host) | Log correto, chunk/arquivo pulado, zero crash/WDT |
| Power-cut | Corte durante: append horário, regravação do `.wip`, `rename` da correção | FS monta; perda ≤ 10 min; nenhum chunk inválido aceito |
| Desempenho no alvo | Tabela do §10 | Todos os limites, com margem de WDT ≥ 50% |
| Soak | 72 h contínuas com Wi-Fi instável induzido | Heartbeat do Core 1 sem quedas; contadores de retry estáveis |

---

## 13. Ordem de trabalho e gates

| WP | Conteúdo | Gate de saída |
|---|---|---|
| **WP1** | `tools/history_v5.py` completo (+ `--selftest`) | Selftest verde; taxa medida nos `.bin` reais ≥ 3,0×; larguras dos prefixos congeladas a partir daqui |
| **WP2** | Núcleo firmware (§5) + testes unitário/propriedade em build host nativo | Paridade bit-exata com WP1; flash de código medida ≤ +2 KB |
| **WP3** | Integração (§6, §7) sob `#define HISTORY_V5_ENABLED` | Golden replay, autodescrição, rejeição e power-cut verdes; verificação automatizada de que nenhuma assinatura pública sumiu |
| **WP4** | Finalização: flag removida, purge ativo (§11), soak, release notes com o passo de backup | Aceite A1–A7 abaixo |

**Aceite final:** A1 gráficos lado a lado idênticos ou melhores (picos preservados; sem regressão perceptível de latência) · A2 export CSV com os mesmos valores da build atual para a mesma série (cabeçalho novo, derivado do SCHEMA) · A3 correção retroativa validada com salto de relógio induzido · A4 perda ≤ 10 min em 20 cortes de energia aleatórios · A5 todos os limites do §10 cumpridos · A6 heartbeat/WDT sem regressão no soak de 72 h · A7 autodescrição provada (ferramenta decodifica os arquivos do campo apenas com este documento).

---

## 14. Decisões prontas para dúvidas previsíveis

1. **Bloco não coube no buffer?** Impossível por construção: `seal()` compara com RAW e o RAW cabe em `H5_BLOCK_MAX_BYTES` por definição. Se a comparação indicar RAW, use RAW — sem exceções.
2. **Relógio saltou (NTP/correção)?** Símbolo de resync (`111` + epoch). Não abra bloco novo por causa de tempo.
3. **Sensor sumiu no meio do bloco?** NAN (§3.5). Schema só muda por reconfiguração explícita (§3.7).
4. **`.wip` corrompido no boot?** Descartar, logar, seguir. Nunca tentar "consertar" um chunk.
5. **Intervalo de gravação ≠ 60 s?** Suportado: `nominalIntervalS` vem da config; bloco continua fechando por **contagem** (60 registros), não por relógio.
6. **Virada de dia no meio de uma hora?** Selar `PARTIAL`, abrir o arquivo novo (com SCHEMA) e seguir — a virada de arquivo é a única fronteira que força selagem além da hora cheia e do shutdown.
7. **Dois SCHEMAs idênticos consecutivos?** Não gravar o segundo; SCHEMA novo só quando o conteúdo difere.
