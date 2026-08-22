# Análise de Compatibilidade PIO — SIMUT v1.5.0-beta

> 📌 **Fotografia da v1.5.0-beta.** A recomendação final (BME280 via `beginGPIO`) foi SUPERADA: desde a v1.5.1 o driver prefere I2C de hardware (`SensorManager.cpp`) com PIO só como fallback. As tabelas de orçamento de PIO/DMA seguem sendo o único registro dessa alocação.

## Hardware do Usuário

| Slot | Sensor | GPIOs | Biblioteca | PIO Block |
|------|--------|-------|-----------|-----------|
| 0/1 | BMP280 | GP0 (SDA), GP1 (SCL) | WirePIO → BMx280PIO | **pio0** (default) |
| 2 | DHT22 | GP2 | DHTBus → DHT22PIO | **pio1** (hardcoded) |
| 3 | DHT22 | GP3 | DHTBus → DHT22PIO | **pio1** (hardcoded) |
| 4 | DS18B20 | GP4 | OneWirePIO | **pio0** (hardcoded) |
| — | Buzzer | GP22 | BuzzerPIO | **pio1** (hardcoded) |
| — | WiFi | GP23-29 | CYW43 SPI | **pio1** (auto) |
| — | TFT | GP16-20,26-28 | HW SPI0 | — (não usa PIO) |

## Arquitetura PIO do RP2040

```
Cada PIO block: 4 State Machines (SM0-SM3) + 32 instruction slots
┌─────────── pio0 ───────────┐  ┌─────────── pio1 ───────────┐
│ SM0  SM1  SM2  SM3         │  │ SM0  SM1  SM2  SM3         │
│ [ 32 slots de instrução ]  │  │ [ 32 slots de instrução ]  │
└────────────────────────────┘  └────────────────────────────┘
```

## Alocação PIO por Biblioteca

| Biblioteca | PIO | SMs | Slots Instr. | DMA | Fallback se falhar |
|-----------|-----|-----|-------------|-----|--------------------|
| **OneWirePIO** (DS18B20) | pio0 | 1 | 27 | 0 | ❌ **NENHUM** — sensor morre |
| **WirePIO** (BMP280 I2C) | pio0 | 1 | **32** | 2 | ✅ GPIO bit-bang automático |
| **DHTBus** (DHT22) | pio1 | 1 | 17 | 0 | ❌ NENHUM — sensor morre |
| **BuzzerPIO** | pio1 | 2 | 4 | 0 | ✅ Tenta o outro bloco |
| **CYW43** (WiFi) | pio1 | 1 | ~6 | 2 | ✅ Auto-seleciona bloco |
| **TFT SPI** | — | — | — | — | Hardware SPI (não PIO) |

## Conflito no pio0

```
OneWirePIO (27 slots) + WirePIO (32 slots) = 59 slots > 32 disponíveis
```

**Ordem de boot no SIMUT:** BMP280 (slot 0) → DHT22 (slot 2) → DHT22 (slot 3) → DS18B20 (slot 4)

1. BMP280 inicia → WirePIO::begin() → beginGPIO() ✅ → beginPIO(pio0) carrega 32 slots → **pio0 cheio!**
2. DHT22 inicia → DHTBus(pio1) carrega 17 slots → pio1: 17/32 ✅
3. DHT22 inicia → DHTBus(pio1) reusa programa → só aloca SM → ✅
4. DS18B20 inicia → OneWirePIO(pio0) → `pio_can_add_program()` falha → `begin()` retorna false → **❌ DS18B20 MORTO!**

## Conflito no pio1

```
DHTBus x2 (17 slots, 2 SMs) + BuzzerPIO (4 slots, 2 SMs) + CYW43 (~6 slots, 1 SM)
= 27/32 slots, 5/4 SMs → ❌ EXCEDE EM SMs!
```

**5 SMs precisam de 4 disponíveis.** O BuzzerPIO tenta alocar 2 SMs no pio1, mas DHT22 x2 já pegou 2 + CYW43 1 = 3 SMs. Sobra 1 SM livre, mas BuzzerPIO precisa de 2.

**Porém:** BuzzerPIO tem fallback automático — se pio1 falhar, tenta pio0. Como pio0 tem as SMs todas cheias (WirePIO 1 + OneWirePIO 1 = 2/4, mas WirePIO ocupa todos os 32 slots), o BuzzerPIO **também pode falhar**.

## Tabela de Compatibilidade

| # | BMP280 | DHT22 (1x) | DHT22 (2x) | DS18B20 | Buzzer | WiFi | pio0 Slots | pio1 Slots | Resultado |
|---|--------|-----------|-----------|---------|--------|------|-----------|-----------|-----------|
| 1 | ✅ | ❌ | ❌ | ✅ | ✅ | ✅ | 27(1W)+32(Wire)=59 ❌ | 4(Buz)+6(WiFi)=10 ✅ | **DS18B20 morre** |
| 2 | ✅ | ✅ | ❌ | ❌ | ✅ | ✅ | 32(Wire) ✅ | 17(DHT)+4(Buz)+6(WiFi)=27 ✅ | ✅ FUNCIONA |
| 3 | ✅ | ❌ | ❌ | ✅ | ❌ | ✅ | 27(1W)+32(Wire)=59 ❌ | 6(WiFi) ✅ | **DS18B20 morre** |
| 4 | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | 32(Wire) ✅ | 17(DHT)+4(Buz)+6(WiFi)+1SMextra=27slots/4SMs ❌ | **Buzzer pode falhar** |
| 5 | ❌ | ✅ | ❌ | ✅ | ✅ | ✅ | 27(1W) ✅ | 17(DHT)+4(Buz)+6(WiFi)=27 ✅ | ✅ FUNCIONA |
| 6 | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ | 27(1W) ✅ | 17(DHT)+4(Buz)+6(WiFi)+1SM=27/4SMs ❌ | **Buzzer pode falhar** |
| 7 | ❌ | ❌ | ❌ | ✅ | ✅ | ✅ | 27(1W) ✅ | 4(Buz)+6(WiFi)=10 ✅ | ✅ FUNCIONA |
| 8 | ✅ (GPIO-only) | ✅ | ✅ | ✅ | ✅ | ✅ | 27(1W) ✅ | 17(DHT)+4(Buz)+6(WiFi)+1SM=27/4SMs ❌ | **2x DHT + Buzzer = 5 SMs no pio1** |

## Análise Detalhada por Cenário

### Cenário 8: CONFIGURAÇÃO DO USUÁRIO (tudo ligado)

**Problema 1 — pio0**: BMP280 (WirePIO 32 slots) + DS18B20 (OneWirePIO 27 slots) = 59 > 32
- **Solução**: Forçar WirePIO a usar apenas GPIO bit-bang (não aloca PIO)
- BME280Driver.h precisa passar `WIREPIO_MODE_GPIO_ONLY` ou o BMx280PIO precisa ser criado com `_force_gpio = true`

**Problema 2 — pio1**: 2× DHTBus (2 SMs) + BuzzerPIO (2 SMs) + CYW43 (1 SM) = 5 SMs > 4
- **Solução**: BuzzerPIO tem fallback para pio0. Se pio0 só tem OneWirePIO (1 SM), sobram 3 SMs livres → Buzzer migra para pio0.
- **MAS**: se WirePIO estiver no pio0 (32 slots), OneWirePIO não consegue carregar. Se WirePIO só usar GPIO, OneWirePIO usa 27/32 slots no pio0, sobrando 5 slots e 3 SMs para o Buzzer.

### Solução Proposta para o Cenário 8 (TUDO funcionando)

**Arquivo: `src/sensors/BME280Driver.h`** — adicionar modo GPIO-only:

A forma mais simples: o BMx280PIO_RP2040 aceita o modo GPIO via flag `_force_gpio`. Precisamos expor isso no BME280Driver.

OU — mais simples ainda — **inverter a ordem de inicialização**: fazer o DS18B20 inicializar ANTES do BMP280. Assim:
1. DS18B20 carrega primeiro no pio0 (27/32 slots, 5 livres)
2. WirePIO tenta carregar no pio0 → `pio_can_add_program(32)` falha → GPIO fallback ✅
3. BMP280 funciona via GPIO bit-bang (mais lento mas funcional)

Isso requer mudar a ordem dos slots (GPIO 4 antes do GPIO 0/1).

### Solução Definitiva (recomendada)

**Modificar `BME280Driver.h`** para criar o BMx280PIO com força GPIO quando pio0 está congestionado:

```cpp
// BME280Driver.h — begin() method
void begin(uint8_t sda, uint8_t scl) {
    _sensor = new BMx280PIO_RP2040(sda, scl, BME280_ADDR_PRIMARY, 200000, pio0);
    _sensor->setForceGpio(true);  // NÃO usar PIO, só GPIO bit-bang
    ...
}
```

Isso libera o pio0 inteiro para o OneWirePIO (DS18B20).

## Verificação de Compilação

| Config | pio_w_release | pio_w_alpha |
|--------|--------------|-------------|
| Todos sensores + TFT + Buzzer + mDNS | ✅ SUCCESS | ✅ SUCCESS |

## Conclusão

| Sensor | Compila? | Funciona com PIO? | Observação |
|--------|---------|-------------------|------------|
| BMP280 (GP0/1) | ✅ | ⚠️ GPIO bit-bang | Precisa de fallback GPIO para coexistir |
| DHT22 (GP2) | ✅ | ✅ pio1 | Sem conflitos |
| DHT22 (GP3) | ✅ | ✅ pio1 | +1 SM no pio1 |
| DS18B20 (GP4) | ✅ | ❌ pio0 cheio | **MORRE** se WirePIO ocupar pio0 primeiro |
| Buzzer (GP22) | ✅ | ⚠️ pio1 lotado | Precisa migrar para pio0 |
| WiFi | ✅ | ✅ pio1 | Auto-seleciona bloco |
| TFT | ✅ | N/A | Hardware SPI |

**Ação necessária**: Modificar `BME280Driver.h` para usar `beginGPIO()` apenas (sem PIO), liberando o pio0 para DS18B20 e Buzzer.
