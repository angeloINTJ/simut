# SIMUT TFT Touch — Arduino IDE Release

Build completa com display **ILI9341 320x240 TFT** + touch XPT2046.
Para Raspberry Pi Pico W.

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
| Adafruit GFX Library | >= 1.12 |
| Adafruit ILI9341 | >= 1.6 |
| XPT2046_Touchscreen | >= 1.4 |

**GitHub (extrair em ~/Arduino/libraries/):**
| Biblioteca | URL |
|---|---|
| OneWirePIO_RP2040 | https://github.com/angeloINTJ/OneWirePIO_RP2040 |
| DHT22PIO_RP2040 | https://github.com/angeloINTJ/DHT22PIO_RP2040 |
| TwoWirePIO_RP2040 | https://github.com/angeloINTJ/TwoWirePIO_RP2040 |
| BMx280PIO_RP2040 | https://github.com/angeloINTJ/BMx280PIO_RP2040 |
| BuzzerPIO_RP2040 | https://github.com/angeloINTJ/BuzzerPIO_RP2040 |

## Compilação

1. Abre `simut_tft.ino`
2. Tools → Board → Raspberry Pi Pico W
3. Tools → Flash Size → "2MB (Sketch: 1MB, FS: 1MB)"
4. Sketch → Upload

**Flash usado:** ~909 KB (87%) | **RAM:** ~95 KB (36%)

## Sensores

BME280, DS18B20 e DHT22 habilitados. Para desabilitar, comenta em `simut_arduino_config.h`.
