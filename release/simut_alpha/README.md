# SIMUT Alpha — Arduino IDE Release

Build alpha com display **HD44780 16x2** (4-bit parallel GPIO),
sem ecrã TFT nem Bluetooth. Para Raspberry Pi Pico W.

## Pré-requisitos

### 1. Arduino IDE + core RP2040 (earlephilhower)

File → Preferences → Additional Boards Manager URLs:
```
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```
Tools → Board → Boards Manager → instala **Raspberry Pi Pico/RP2040/RP2350**

### 2. Configuração da placa

- **Board:** Raspberry Pi Pico W
- **Flash Size:** "2MB (Sketch: 1MB, FS: 1MB)"
- CPU Speed: 200 MHz

### 3. Bibliotecas

**Library Manager:**
| Biblioteca | Versão |
|---|---|
| PubSubClient | >= 2.8 |

**GitHub (extrair em ~/Arduino/libraries/):**
| Biblioteca | URL |
|---|---|
| OneWirePIO_RP2040 | https://github.com/angeloINTJ/OneWirePIO_RP2040 |
| DHT22PIO_RP2040 | https://github.com/angeloINTJ/DHT22PIO_RP2040 |
| TwoWirePIO_RP2040 | https://github.com/angeloINTJ/TwoWirePIO_RP2040 |
| BMx280PIO_RP2040 | https://github.com/angeloINTJ/BMx280PIO_RP2040 |
| BuzzerPIO_RP2040 | https://github.com/angeloINTJ/BuzzerPIO_RP2040 |

## Compilação

1. Abre `simut_alpha.ino`
2. Tools → Board → Raspberry Pi Pico W
3. Tools → Flash Size → "2MB (Sketch: 1MB, FS: 1MB)"
4. Sketch → Upload

**Flash usado:** ~817 KB (78%) | **RAM:** ~90 KB (34%)

## Hardware — Pinos LCD (HD44780 paralelo 4-bit)

| LCD | GPIO |
|---|---|
| RS | 16 |
| EN | 17 |
| D4 | 18 |
| D5 | 19 |
| D6 | 20 |
| D7 | 21 |

Para alterar pinos ou modo (I2C/paralelo), edita `simut_arduino_config.h`
ou o ficheiro central **`simut_config.h`**.

## Customização

Todas as opções configuráveis estão em **`simut_config.h`** — pinos, sensores,
modo LCD, buzzer, mDNS. Edite esse arquivo antes de compilar.

Os valores específicos do Alpha (display HD44780, modo paralelo, pinos)
estão pré-definidos no `simut_arduino_config.h` (antes do include).

## Sensores

BME280, DS18B20 e DHT22 habilitados por padrão.
Para desabilitar, comente em `simut_config.h`.
