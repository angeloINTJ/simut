# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Firmware IoT profissional para Raspberry Pi Pico W

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/Version-v1.0.0-blue.svg)](https://github.com/angeloINTJ/simut/releases)
[![Contributors](https://img.shields.io/badge/All_Contributors-2-orange.svg)](CONTRIBUTORS.md)

[English version](README.md) | **Português**

## Visão Geral

O SIMUT é um firmware IoT profissional para **Raspberry Pi Pico W** que oferece monitoramento de temperatura e umidade em tempo real com arquitetura dual-core. Possui display TFT touchscreen, interface web com controle de acesso, telemetria (HTTP/MQTT), CLI via USB e Bluetooth, e suporte a múltiplos idiomas.

## Por que SIMUT?

| Necessidade | Sketch Arduino | ESPHome/Tasmota | **SIMUT** |
|-------------|:---:|:---:|:---:|
| Autônomo com display | ⚠️ Código manual | ❌ Sem suporte TFT | ✅ UI touch integrada |
| Ambientes regulados | ❌ Sem auditoria | ❌ Sem RBAC | ✅ Multi-usuário, logs |
| Cadeia fria (-80°C a +45°C) | ⚠️ Básico | ✅ Básico | ✅ Calibrado multi-sensor |
| Operação offline | ✅ Sim | ❌ Dependente de nuvem | ✅ Display + web local |
| Atualização OTA | ❌ Regravação manual | ✅ OTA | ✅ OTA + backup/restore |
| Segurança | ❌ Nenhuma | ⚠️ Básica | ✅ HMAC-SHA256, RBAC |

## Instalação Rápida

```bash
git clone https://github.com/angeloINTJ/simut.git
cd simut
pio run -e pico_w_release -t upload
```

## Documentação

| Documento | Descrição |
|-----------|-----------|
| [Manual do Usuário](docs/MANUAL.md) | Guia completo: hardware, display, web, CLI |
| [Guia OTA](docs/OTA_USAGE.md) | Atualização de firmware via web |
| [Guia de Recuperação](docs/RECOVERY.md) | Recovery após falha de OTA |
| [Política de Segurança](SECURITY.md) | Modelo de ameaças e procedimentos |
| [Contribuindo](CONTRIBUTING.md) | Guia para desenvolvedores |

## Hardware

| Componente | Especificação |
|------------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Display | ILI9341 320×240 TFT (SPI) + XPT2046 touch |
| Sensores | DS18B20 (1-Wire, até 10) + DHT22 |
| Buzzer | Piezo passivo (PIO) |
| Armazenamento | 2 MB flash (1 MB firmware + 1 MB LittleFS) |

## Licença

MIT License — veja [LICENSE](LICENSE).

Copyright © 2026 Angelo Moises Alves
