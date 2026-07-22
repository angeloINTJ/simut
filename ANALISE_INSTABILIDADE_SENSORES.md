# Análise Completa de Instabilidades — Sensores BMP280 e DS18B20

**Data:** 2026-07-21
**Versão do Firmware:** 1.5.1-beta
**Target:** Raspberry Pi Pico W (RP2040)

---

## SUMÁRIO EXECUTIVO

Foram identificados **5 problemas críticos** que explicam as instabilidades dos sensores
BMP280 e DS18B20 (ficando on/offline) e a impossibilidade de visualizar histórico/logs:

| # | Problema | Severidade | Impacto |
|---|----------|-----------|---------|
| 1 | **PIO0 — Conflito de memória de instruções** | CRÍTICO | BMP280 cai para GPIO bit-bang |
| 2 | **Driver BME280 estático — apenas 1 sensor** | CRÍTICO | Múltiplos BMP280 se destroem |
| 3 | **DS18B20 hardwareMismatch irreversível** | ALTO | Sensor "morre" permanentemente |
| 4 | **I2C GPIO bit-bang reinicializa pinos** | ALTO | Conflito com outros usos de GPIO |
| 5 | **Histórico/Logs bloqueados por TouchPriority** | MÉDIO | API retorna 503 durante uso do display |

---

## 1. CONFLITO DE MEMÓRIA DE INSTRUÇÕES PIO0 (CRÍTICO)

### O problema

O RP2040 possui 2 blocos PIO (pio0 e pio1), cada um com **32 slots de instrução**.
Três periféricos disputam esses recursos:

| Biblioteca | Bloco PIO | Instruções | State Machines |
|------------|-----------|------------|----------------|
| OneWirePIO (DS18B20) | **pio0** (hardcoded) | **27** | 1 SM |
| WirePIO I2C (BMP280) | **pio0** (default) | **32** | 1 SM + 2 DMA |
| DHT22PIO | pio1 | 17 | 1 SM |

**Soma no pio0: 27 + 32 = 59 > 32 slots disponíveis!**

### Código relevante

**DS18B20Driver.h:39** — hardcoded para pio0:
```cpp
DS18B20Driver() : bus(pio0), sensor(bus) {}
```

**BMx280PIO_RP2040.h:131** — default para pio0:
```cpp
BMx280PIO_RP2040(uint8_t sda, uint8_t scl, ..., PIO pio = pio0);
```

**WirePIOTransport.cpp:259** — `pio_add_program` falha silenciosamente:
```cpp
_offset = pio_add_program(_pio, &i2c_master_program);
if (_offset < 0) return false;  // FALHA — sem espaço!
```

### Consequência

Quando o PIO+DMA falha, o BMP280 faz fallback para **GPIO bit-bang** (WirePIO.cpp:92-93):
```cpp
if (!_transport->beginPIO(_pioBlock)) {
    // PIO failed — GPIO bit-bang fallback still works
}
```

O GPIO bit-bang:
- Usa `critical_section_enter_blocking()` — **bloqueia interrupções** durante transações I2C
- Cada leitura de 8 bytes leva ~1.6ms com interrupções desligadas
- Re-inicializa pinos GPIO com `gpio_init()` a cada transação (ver problema #4)
- É ordens de magnitude mais lento que DMA

### Solução proposta

Mover o BMP280 para **pio1** (que tem 32 - 17 = 15 slots livres para os 32 do I2C... ainda não cabe).

**Melhor solução**: Mover o OneWirePIO (DS18B20) para **pio1** (27 instruções + 17 do DHT22 = 44 > 32... também não cabe).

**Solução viável**: Usar **pio1** para o I2C do BMP280 exclusivamente (32 instruções, cabe nos 32 slots), e manter DS18B20 no pio0 (27 instruções, também cabe). O DHT22 ficaria sem slot... mas DHT22 (17 instr) + BMP280 (32 instr) = 49 > 32.

**Solução definitiva**: Três opções:

**A) Dividir entre pio0 e pio1 estrategicamente:**
- pio0: OneWirePIO (27 instr) — cabe com folga
- pio1: WirePIO I2C (32 instr) — usa todos os 32 slots
- DHT22PIO (17 instr) → mover também para pio0 → 27 + 17 = 44 > 32!

Não funciona com os 3 simultâneos. Seria necessário:

**B) Usar hardware I2C para o BMP280:**
O RP2040 tem 2 periféricos I2C de hardware (I2C0 e I2C1). Usar `Wire` ou `Wire1`
em vez de WirePIO eliminaria completamente o uso de PIO pelo BMP280.

**C) Otimizar/reduzir o programa PIO do I2C para caber em ~5 instruções (32 - 27):**
Praticamente impossível — o programa I2C master mínimo precisa de ~14-20 instruções.

**D) (RECOMENDADO) Usar hardware I2C (Wire/Wire1) para BMP280:**
Modificar `BME280Driver::begin()` para usar o construtor `BMx280PIO_RP2040(TwoWire &wire, addr)`
em vez do construtor PIO com pinos SDA/SCL. Isso libera completamente o pio0 para
OneWirePIO e o pio1 para DHT22PIO.

---

## 2. DRIVER BME280 ESTÁTICO — APENAS 1 SENSOR (CRÍTICO)

### O problema

`SensorManager::_getOrCreateBmeDriver()` em **SensorManager.cpp:745-771** usa
alocação estática:

```cpp
static BME280Driver s_bmeDrv;  // ÚNICA instância!
// ...
BME280Driver* drv = &s_bmeDrv;  // Sempre o mesmo ponteiro
if (drv->begin(sda, scl, addr)) {
    for (size_t i = 0; i < _bmeDrivers.size(); i++) {
        if (_bmeDrivers[i] == drv) return (int8_t)i;  // Já existe, retorna índice
    }
    _bmeDrivers.push_back(drv);  // Só pusha 1 vez
```

**TODOS os sensores BMP280/BME280 compartilham o mesmo driver!**

### Cenário de falha

1. Primeiro BMP280 configurado em GP4(SDA)/GP5(SCL) addr=0x76 → `begin()` cria `BMx280PIO_RP2040`, inicializa com sucesso, `_bmeDrivers = [&s_bmeDrv]`, retorna índice 0
2. Segundo BMP280 em GP6(SDA)/GP7(SCL) addr=0x76 → `begin()` **deleta o BMx280PIO_RP2040 antigo** (linha 754: `delete s_bmeDrv._sensor`), cria um novo → `_bmeDrivers[i] == drv` é true → retorna índice 0
3. **O primeiro sensor agora tem `bmeDriverIdx = 0` mas o BMx280PIO_RP2040 foi destruído!**
4. Leituras do primeiro sensor vão acessar o driver com pinos/config do **segundo** sensor

### Evidência no código

**SensorManager.cpp:754:**
```cpp
if (s_bmeDrv._sensor) { delete s_bmeDrv._sensor; s_bmeDrv._sensor = nullptr; }
```

Isso **destrói** o sensor anterior antes de criar o novo. Se havia 2+ BMP280,
apenas o **último** a ser inicializado funciona.

### Solução proposta

Substituir a alocação estática por alocação dinâmica (std::vector):
```cpp
int8_t SensorManager::_getOrCreateBmeDriver(uint8_t sda, uint8_t scl, uint8_t addr) {
    // Criar novo driver para cada combinação (sda, scl, addr) única
    auto* drv = new BME280Driver();
    if (drv->begin(sda, scl, addr)) {
        _bmeDrivers.push_back(drv);
        return (int8_t)(_bmeDrivers.size() - 1);
    }
    delete drv;
    return -1;
}
```

**Nota:** O comentário menciona "heap may be fragmented/exhausted during boot",
mas o `new (std::nothrow)` já é usado com sucesso em `BME280Driver::begin()`.
Se o heap for realmente uma preocupação, usar um pool pré-alocado de N drivers
(onde N = número máximo de barramentos I2C, ex: 4).

---

## 3. DS18B20 hardwareMismatch IRREVERSÍVEL (ALTO)

### O problema

Em **SensorManager.cpp:438-450**, quando `hardwareMismatch` é `true`, o sensor
é **permanentemente ignorado** nas leituras periódicas:

```cpp
if (s.hardwareMismatch) {
    if (!s.inErrorState) {
        LOG_CODE(LOG_ERROR, "SENSOR", ERR_SENSOR_MISMATCH, ...);
    }
    s.inErrorState = true;
    s.buffers[0].clear();
    s.avgValue[0] = NAN;
    s.consecutiveSuccess = 0;
    s.lastReadTime = now;
    __atomic_store_n(&_newDataAvailable, true, __ATOMIC_RELEASE);
    continue;  // <<< PULA este sensor para sempre!
}
```

O `hardwareMismatch` é setado em **SensorManager.cpp:463** quando a verificação
de ROM (a cada 5 leituras) detecta diferença:

```cpp
if (s.totalReadings % 5 == 0) {
    uint8_t currentRom[8];
    if (_ds18.readROM(s.config.pins[0], currentRom)) {
        if (!_ds18.checkRomMatch(currentRom, s.config.rom)) {
            romVerified = false;
            failReason = "ROM Mismatch";
            s.hardwareMismatch = true;  // <<< SETADO AQUI, nunca limpo!
        }
    }
}
```

### Caminho de recuperação (frágil)

A única maneira de limpar `hardwareMismatch` é via `AppManager::checkAndAutoHealSensors()`
(**AppManager_Sensors.cpp:19-52**), chamado a cada 3 segundos:

```cpp
if (_sensorMgr->identifyPhysicalSensor(gpio, foundRom)) {
    if (memcmp(cfg.sensors[gpio].rom, foundRom, 8) != 0) {
        _sensorMgr->setHardwareMismatch(gpio, true);   // re-set
    } else {
        _sensorMgr->setHardwareMismatch(gpio, false);  // clear
    }
}
```

**Problemas neste caminho:**
1. Só é chamado a cada 3 segundos — o sensor fica offline nesse intervalo
2. Se a leitura de ROM falhar (ruído no barramento 1-Wire), o mismatch **não** é limpo
3. Se o sensor foi temporariamente desconectado e reconectado, o ROM pode ser diferente
   (outro sensor físico) → o mismatch é **correto**, mas não há log claro para o usuário

### Solução proposta

Adicionar recuperação automática no próprio loop de leitura. Se `hardwareMismatch`
estiver setado, tentar re-verificar o ROM periodicamente (ex: a cada 30s) em vez
de pular o sensor para sempre:

```cpp
if (s.hardwareMismatch) {
    // Tentar re-verificar ROM a cada 30s
    if (s.totalReadings % 30 == 0) {
        uint8_t currentRom[8];
        if (_ds18.readROM(s.config.pins[0], currentRom)) {
            if (_ds18.checkRomMatch(currentRom, s.config.rom)) {
                s.hardwareMismatch = false;
                s.inErrorState = false;
                s.consecutiveErrors = 0;
                LOG_CODE(LOG_INFO, "SENSOR", LOG_SENSOR_REC, ...);
                // continuar com a leitura normal abaixo
            }
        }
    }
    if (s.hardwareMismatch) {
        // Ainda em mismatch — pular
        s.lastReadTime = now;
        continue;
    }
}
```

---

## 4. I2C GPIO BIT-BANG REINICIALIZA PINOS (ALTO)

### O problema

Quando o BMP280 opera em modo GPIO bit-bang (fallback do problema #1),
o método `_i2c_read()` em **BMx280PIO_RP2040.cpp:226-256** chama
`gpio_init()` nos pinos SDA/SCL **a cada byte lido**:

```cpp
for (size_t i = 0; i < len; i++) {
    gpio_init(sd);                // <<< REINICIALIZA SDA
    gpio_set_dir(sd, GPIO_IN);
    gpio_pull_up(sd);
    gpio_init(sc);                // <<< REINICIALIZA SCL
    gpio_set_dir(sc, GPIO_OUT);
    gpio_put(sc, 1);
    // ... transação I2C ...
}
```

### Consequências

- `gpio_init()` reseta TODAS as configurações do pino (função, pulls, direção, etc.)
- Se o mesmo pino for usado por múltiplos periféricos (ex: compartilhado com sensor
  scan), o estado é perdido
- Atrasos cumulativos: cada byte lido requer ~200µs de bit-bang + delays
- O `critical_section` bloqueia interrupções durante toda a transação

### Solução proposta

**Primária:** Resolver o problema #1 (usar hardware I2C) elimina completamente
o fallback GPIO.

**Secundária:** Corrigir o fallback GPIO para só chamar `gpio_init()` uma vez
(na primeira transação) e cachear o estado:

```cpp
static bool gpioReady = false;
if (!gpioReady) {
    gpio_init(sd);
    gpio_set_dir(sd, GPIO_IN);
    gpio_pull_up(sd);
    gpio_init(sc);
    gpio_set_dir(sc, GPIO_OUT);
    gpio_put(sc, 1);
    gpioReady = true;
}
```

---

## 5. HISTÓRICO / LOGS BLOQUEADOS POR TouchPriority (MÉDIO)

### O problema

As APIs de histórico e logs verificam `TouchPriority::isActive()` e retornam
**HTTP 503** se o display estiver em uso:

**WebManager_History.cpp (handleApiHistoryMulti):**
```cpp
if (TouchPriority::isActive()) {
    sendJsonError(503, "Display in use");
    return;
}
```

**WebManager_History.cpp (handleApiLogs):**
```cpp
if (TouchPriority::isActive()) {
    sendJsonError(503, "Display in use");
    return;
}
```

### Consequências

- Se alguém estiver usando o display físico (touch), **nenhuma** requisição de
  histórico ou log funciona
- O header `Retry-After: 3` é enviado, mas clients que não implementam retry
  simplesmente falham
- O dashboard web fica sem dados de histórico se o display estiver ativo

### Outras causas de bloqueio de histórico/logs

1. **`_inHistoryHandler` flag atômica** — apenas 1 handler de histórico por vez
2. **`HeavyTaskGuard`** — se storage está em operação pesada, retorna 503
3. **Rate limiting de logs** — 200ms mínimo entre requisições (HTTP 429)
4. **Permissões RBAC** — usuário precisa de `PERM_HISTORY` (0x0002) e `PERM_LOGS` (0x0004)
5. **LittleFS não montado** — APIs retornam vazio silenciosamente

### Solução proposta

Permitir leitura de histórico/logs mesmo durante TouchPriority, já que são
operações somente leitura. O bloqueio atual existe para evitar contenção de
flash entre Core 0 (leitura) e Core 1 (renderização). Alternativas:

- Usar `enterFlashReadLock()` (mutex leve) em vez de bloquear completamente
- Fila de requisições pendentes que executam quando o display é liberado
- Cache em RAM dos últimos N registros de histórico para acesso rápido

---

## 6. PROBLEMAS ADICIONAIS ENCONTRADOS

### 6a. BME280_MEAS_TIME_MS muito curto (15ms)

**BME280Driver.h:28:**
```cpp
#define BME280_MEAS_TIME_MS  15  /* ~9.3ms actual (osrs ×1) + margin */
```

O datasheet Bosch especifica:
- T_setup = 1.25ms
- T_measure per oversampling unit: ~2.3ms para temperature, ~2.3ms para pressure
- Com osrs_t=1, osrs_p=1: tempo mínimo ≈ 1.25 + 2.3 + 2.3 + 0.575 = 6.4ms
- Com osrs_t=1, osrs_p=1, osrs_h=1 (BME280): +2.3 + 0.575 = ~9.3ms

**15ms é suficiente para o caso nominal**, mas se houver:
- Flash GC durante a medição (LittleFS pode pausar por 50-200ms)
- WiFi IRQ (CYW43 pode roubar ciclos)
- Variação de clock

...o sensor pode não ter completado a medição quando `getResults()` é chamado.

**Solução:** Aumentar para 25ms ou verificar o bit `status.measuring` (bit 3 do
registrador 0xF3) antes de ler, como já é feito em `takeForcedMeasurement()`.

### 6b. Inicialização dos sensores durante o boot — sem feedback de falha

**AppManager_Boot.cpp:507-510:**
```cpp
_sensorMgr->begin();
loadAndCalibrateSensors();
```

Se `_getOrCreateBmeDriver()` falhar (retorna -1), o sensor é **silenciosamente
ignorado**. O `bmeDriverIdx` fica como -1 e as leituras nunca acontecem.

### 6c. Log rotacionado perde dados antigos

**LogManager.cpp:354-358:**
```cpp
if (_currentLineCount >= MAX_RECORDS_PER_FILE) {
    if (LittleFS.exists(LOG_FILE_OLD)) LittleFS.remove(LOG_FILE_OLD);
    LittleFS.rename(LOG_FILE_CURRENT, LOG_FILE_OLD);
    _currentLineCount = 0;
}
```

Apenas **2 arquivos** são mantidos (current + old). Quando o current enche:
1. old é deletado (dados antigos PERDIDOS)
2. current vira old
3. novo current é criado

Isso significa que logs históricos são perdidos após 2 rotações.

### 6d. DS18B20 ROM verification bloqueia todas as leituras

**SensorManager.cpp:457-467:**
```cpp
if (s.totalReadings % 5 == 0) {
    uint8_t currentRom[8];
    if (_ds18.readROM(s.config.pins[0], currentRom)) {
        // ...
    }
}
```

A leitura de ROM é feita **durante** o ciclo de leitura de temperatura. Se houver
múltiplos DS18B20 no barramento, cada verificação de ROM é uma transação 1-Wire
adicional que pode interferir com a conversão de temperatura em andamento.

---

## PLANO DE AÇÃO RECOMENDADO

### Imediato (correções de bugs)

1. **Consertar `_getOrCreateBmeDriver()`** para suportar múltiplos BMP280
   - Substituir alocação estática por `std::vector<std::unique_ptr<BME280Driver>>`
   - Cada combinação única (sda, scl, addr) ganha seu próprio driver

2. **Mover BMP280 para hardware I2C** (Wire/Wire1)
   - Usar `BMx280PIO_RP2040(TwoWire &wire, addr)` em vez do construtor PIO
   - Elimina completamente o conflito de PIO0
   - Libera pio0 para OneWirePIO e pio1 para DHT22PIO

3. **Adicionar recuperação de hardwareMismatch no loop de leitura**
   - Re-verificar ROM a cada 30s mesmo com mismatch ativo
   - Limpar flag se o ROM voltar a bater

### Curto prazo (melhorias)

4. **Aumentar BME280_MEAS_TIME_MS para 25ms** e verificar status bit
5. **Otimizar GPIO bit-bang** para não reinicializar pinos a cada byte
6. **Implementar fila de requisições** para histórico/logs durante TouchPriority

### Médio prazo (arquitetura)

7. **Adicionar suporte a hardware I2C (I2C0/I2C1)** como opção de configuração
8. **Aumentar retenção de logs** (manter N arquivos em vez de 2)
9. **Adicionar telemetria de saúde dos sensores** (taxa de erro, latência)
