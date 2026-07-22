# Análise Completa do Sistema de Armazenamento de Histórico

**Data:** 2026-07-21
**Formato:** V4 Universal Binary (.sim4)

---

## 1. ARQUITETURA GERAL

```
SensorManager::processPeriodicReads()
  └→ addSample() → avgValue[]
       └→ AppManager::processHistoryLogging()
            ├─ getEpoch() > 1600000000? ── não → SKIP (sem tempo)
            ├─ getV4Schema() != null?    ── não → SKIP (sem schema)
            └→ writeHistoryEntryV4()
                 ├─ !_isMounted?            → SKIP
                 ├─ epoch < 1700000000?     → SKIP (timestamp inválido)
                 ├─ TouchPriority?          → buffer RAM
                 └→ writeHistoryEntryFlashV4()
                      ├─ buildMeasureSchema() → header + string pool
                      ├─ histV4Encode() → bit-packed anchor/delta
                      └─ LittleFS write → /history/YYYYMMDD.sim4
```

## 2. ESTRUTURA DO ARQUIVO V4 (.sim4)

### 2.1 Header fixo (16 bytes)
```
[4B magic "SIM4"] [2B version 0x0004] [2B headerSize] [2B anchorPeriod=60]
[1B sensorCount] [1B measureCount] [1B flags] [1B strPoolSize] [2B reserved]
```

### 2.2 Tabelas variáveis
```
[SensorDef * sensorCount]  — 10 bytes cada (hwId, name, type, channelMask)
[MeasureDef * measureCount] — 12 bytes cada (sensorIdx, channel, bitWidth, scale, unit)
[String Pool * strPoolSize] — strings concatenados (hwIds, nomes, unidades)
```

### 2.3 Registros
- **Anchor:** a cada 60 registros (HIST_V4_ANCHOR_PERIOD)
  - 32 bits epoch + N × bitWidth para cada medição, bit-packed
- **Delta:** entre anchors
  - Máscara de bits (ceil(N/8) bytes) + zigzag-varint Δepoch + varint por campo alterado

## 3. CAMINHO DE ESCRITA DETALHADO

### 3.1 Gatilho (`AppManager_Loop.cpp:194`)
```cpp
if (timeSince(_lastHistoryTime, _storageMgr->getHistoryIntervalMin() * 60000UL)) {
    if (!_storageMgr->isHeavyTaskLocked() && !isUserInteracting()) {
        processHistoryLogging();
    }
}
```
- **Intervalo padrão:** 1 minuto
- **Bloqueado por:** HeavyTask ativo OU touch interagindo

### 3.2 Validação de tempo (`AppManager_HistoryAlarm.cpp:332`)
```cpp
time_t now = _netMgr->getEpoch();
if (now <= 1600000000) return; // ← BLOQUEIA TUDO sem tempo válido
```
- **Sem NTP e sem provisional:** epoch ~0 → histórico NUNCA é escrito
- **Com NTP:** epoch real → OK
- **Com provisional (fix aplicado):** SIMUT_BUILD_EPOCH → OK

### 3.3 Schema V4 (`AppManager_HistoryAlarm.cpp:352`)
```cpp
const HistV4State* schema = _storageMgr->getV4Schema();
if (!schema || schema->measureCount == 0) return;
```
- Schema é construído a partir dos sensores ativos no `_currentConfig`
- **Sem sensores ativos:** schema vazio → histórico NUNCA é escrito
- `_histV4CodecValid` é invalidado no rollover de dia

### 3.4 Construção do schema (`StorageManager.cpp:1283`)
```cpp
bool buildMeasureSchema(sensors, sc, measures, mc, pool, sp) {
    for (int slot = 0; slot < MAX_SENSORS; slot++) {
        if (!_currentConfig.sensors[slot].active) continue;
        // ... adiciona sensor + medições ...
    }
    return sensorCount > 0 && measureCount > 0;
}
```
- Stack: `HistV4SensorDef[32]` + `HistV4MeasureDef[64]` + `uint8_t[200]` ≈ 1288 bytes
- String pool limitado a HIST_V4_MAX_STRPOOL (200 bytes)

### 3.5 Validação de epoch (`StorageManager.cpp:2000`)
```cpp
if (epoch < 1700000000UL) return false;  // < 2023-11-14
if (nowEpoch > 1700000000 && epoch > nowEpoch + 86400) return false; // futuro >1 dia
```

### 3.6 Escrita Flash (`StorageManager.cpp:2028`)
- Cria/abre arquivo `/history/YYYYMMDD.sim4`
- FLASH_OP → `mutex_enter_blocking(&_fsReadMutex)` → **pode bloquear loop principal**
- Se arquivo novo: escreve header com schema → re-lê header → populate codec state
- Codifica registro com `histV4Encode()` → append ao arquivo

### 3.7 Storage Enforcement (`StorageManager.cpp:1554`)
- Dispara quando flash > 86% usado
- Deleta arquivo mais antigo em `/history/`
- Pula o arquivo do dia atual (`_currentLogFileName`)
- Budget: 4 segundos, máx 30 iterações
- Dentro de FLASH_OP → mutex bloqueante

## 4. CAMINHO DE LEITURA

### 4.1 API Web (`WebManager_History.cpp:59`)
```
GET /api/history_multi?sensors=0,3&range=2
```
- **Guards:** PERM_HISTORY, TouchPriority, `_inHistoryHandler` atômico, HeavyTaskGuard
- **Range→arquivos:** 0=1h, 1=6h, 2=24h, 3=7d, 4=1M, 5=1Y, 6=MAX
- **Decimação:** {1, 1, 3, 15, 60, 240, 240}
- **V4 decode:** `histV4DecodeNext()` em batch de 20 registros
- **Buffer de chunk:** 2048 bytes, flush a cada 1500 bytes
- **ReadGuard:** mutex `_fsReadMutex` durante leitura do arquivo

### 4.2 Gráfico no Display (`DisplayManager_Graph.cpp`)
- `openStatsScreen(sensorId)` → `static GraphDataPackage pkg` (3.2KB)
- Renderização no Core 1: usa dados pré-carregados, sem acesso direto a flash
- Dados vazios: mostra "No Data" (sem crash)

### 4.3 Preload Min/Max (`AppManager_HistoryAlarm.cpp:198`)
- Executado durante boot
- Lê arquivo do dia atual (.sim4)
- Budget de 5 segundos
- ReadGuard (mutex) durante leitura

## 5. PROBLEMAS ENCONTRADOS

### 5.1 🔴 CRÍTICO: FLASH_OP bloqueia loop principal
```cpp
#define FLASH_OP(BLOCK) do { \
    mutex_enter_blocking(&_fsReadMutex); \  // ← BLOQUEIO INFINITO
    ...
```
- Se `handleApiHistoryMulti` segura o ReadGuard por 30+ segundos (arquivo grande)
- `processHistoryLogging()` → FLASH_OP → `mutex_enter_blocking` → **main loop trava**
- Sem `watchdog_update()` → WDT timeout → **REBOOT**

**Solução:** Usar `mutex_enter_timeout_ms(&_fsReadMutex, 500)` com watchdog feed:
```cpp
while (!mutex_enter_timeout_ms(&_fsReadMutex, 100)) {
    watchdog_update();
}
```

### 5.2 🔴 CRÍTICO: enforceStorageLimit em FLASH_OP
- Roda dentro de `mutex_enter_blocking`
- Itera até 30 arquivos com operações LittleFS
- Se demorar > WDT timeout → reboot

### 5.3 🟠 ALTO: Stack alocado em writeHistoryEntryFlashV4
```cpp
uint8_t hdrBuf[2048];  // linha 2067 — 2KB no stack!
uint8_t buf[HIST_V4_MAX_HEADER];  // linha 2091 — mais 2KB!
uint8_t encBuf[HIST_V4_MAX_DELTA];  // linha 2104 — 256 bytes
```
- **Total stack:** ~4.3KB em uma única função
- RP2040 stack por thread: ~4KB
- **Risco de stack overflow** → corrupção de memória → hard fault

**Solução:** Mover `hdrBuf` para alocação estática ou heap

### 5.4 🟠 ALTO: _currentLogFileName é String (heap)
```cpp
_currentLogFileName = path;  // heap allocation
```
- Fragmentação de heap ao longo de dias de operação
- Se heap esgotado → malloc falha → comportamento indefinido

### 5.5 🟡 MÉDIO: buildMeasureSchema retorna false sem log
- Se `sensorCount == 0 || measureCount == 0` → retorna false silenciosamente
- Nenhum LOG_CODE para diagnosticar
- Schema nunca é criado, histórico nunca é escrito

### 5.6 🟡 MÉDIO: _histV4CodecValid reset no rollover
- Força reconstrução completa do schema a cada novo dia
- `scanHistoryFileV4` lê arquivo inteiro para reconstruir codec state
- Pode ser lento para arquivos grandes

### 5.7 🟡 MÉDIO: EPOCH_MIN muito restritivo
```cpp
if (epoch < 1700000000UL) return false;  // 2023-11-14
```
- Se o dispositivo for usado antes dessa data (RTC resetado), TODOS os históricos são descartados
- O check em `processHistoryLogging` é 1600000000, mas aqui é 1700000000

## 6. RECOMENDAÇÕES

### Imediatas (estabilidade):
1. **Trocar `mutex_enter_blocking` por timeout com watchdog feed** no FLASH_OP
2. **Mover `hdrBuf[2048]` para static** — evita stack overflow
3. **Adicionar watchdog_update** entre chunks no `enforceStorageLimit`

### Curtas (diagnóstico):
4. **Adicionar LOG_CODE** quando schema está vazio
5. **Alinhar EPOCH_MIN** entre `processHistoryLogging` (1600000000) e `writeHistoryEntryV4` (1700000000)
6. **Substituir `_currentLogFileName` String** por buffer fixo de 32 chars

### Médias (qualidade):
7. **Adicionar validação de CRC32** nos arquivos .sim4 (flag já reservado no header)
8. **Pré-alocar buffer de encode** como static para evitar fragmentação
