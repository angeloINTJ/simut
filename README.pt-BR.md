<p align="center">
  <img src="docs/images/logo-wordmark.svg" alt="SIMUT" height="76">
</p>

# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Firmware IoT de nível profissional para Raspberry Pi Pico W

[English](README.md) | [Português](README.pt-BR.md) | [Español](README.es-ES.md)

[![License: MIT](https://img.shields.io/badge/Licença-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Plataforma-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)
[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/angeloINTJ/simut?label=Release&color=blue)](https://github.com/angeloINTJ/simut/releases/latest)
[![Docs](https://img.shields.io/badge/Docs-GitHub_Pages-34D058.svg)](https://angelointj.github.io/simut/)
[![Contributors](https://img.shields.io/badge/Contribuidores-5-orange.svg)](#contribuidores-)
[![Contributions Welcome](https://img.shields.io/badge/Contribuições-Bem--vindas-brightgreen.svg)](CONTRIBUTING.pt-BR.md)

<p align="center">
  <img src="docs/images/tft-tour.gif" alt="Tour do TFT do SIMUT — dashboard, gráficos de histórico, calendário e configurações" width="400">
</p>

## Visão geral

O SIMUT é um firmware IoT de nível profissional para o **Raspberry Pi Pico W** que fornece monitoramento de temperatura, umidade e pressão em tempo real com arquitetura dual-core. Traz dashboard local em TFT touchscreen, interface web embarcada com controle de acesso por papéis, histórico binário no dispositivo com gráficos decimados no navegador, telemetria (HTTP/MQTT, com MQTT Discovery do Home Assistant), rota `/metrics` para Prometheus, encaminhamento remoto de syslog (RFC 5424), atualização OTA e CLI pela serial USB.

## Por que SIMUT?

| Necessidade | Sketch Arduino DIY | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Autônomo com display | ⚠️ Codificação manual | ❌ Sem suporte a TFT | ✅ UI touch embutida |
| Ambientes regulados | ❌ Sem trilha de auditoria | ❌ Sem RBAC de usuários | ✅ Multiusuário, logs de auditoria |
| Cadeia fria (−55 °C e sondas abaixo de zero) | ⚠️ Leituras básicas | ✅ Monitoramento básico | ✅ Multissensor calibrado |
| Operação offline | ✅ Sim | ❌ Frequentemente depende de nuvem | ✅ Web local completa + display |
| Atualização OTA | ❌ Regravação manual | ✅ OTA | ✅ OTA + backup/restore |
| Segurança | ❌ Nenhuma | ⚠️ Básica | ✅ HMAC-SHA256, RBAC, rate limiting, HTTPS opcional |
| Home Assistant | ⚠️ Integração manual | ✅ Nativa | ✅ MQTT Discovery (opcional) |
| Métricas Prometheus | ❌ Nenhuma | ✅ Embutido | ✅ Rota `/metrics` |
| Log de auditoria remoto | ❌ Nenhum | ⚠️ Complemento | ✅ Syslog (RFC 5424 / UDP) |

**O SIMUT é para você se:** precisa de um sistema de monitoramento de temperatura autônomo, seguro e auditável, que funcione com ou sem internet — típico de laboratórios, farmácias, bancos de sangue, armazenamento de vacinas e cadeias frias de alimentos.

**ESPHome/Tasmota podem ser melhores se:** você não precisa de display local e prefere configuração YAML a uma interface web embutida. (Se o que te prendia lá era o Home Assistant: o SIMUT agora fala MQTT Discovery.)

## Arquitetura

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                   │
│  ┌──────────────────────┐  ┌────────────────────────────┐│
│  │      Core 0          │  │        Core 1              ││
│  │  (Loop principal)    │  │  (Loop do display)         ││
│  │                      │  │                            ││
│  │  ◆ AppManager ───────┼──┼─ estado/snapshots ─────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ Renderizador DMA        ││
│  │  ◆ CommandManager    │  │  ◆ Temas                   ││
│  │  ◆ StorageManager    │  │  ◆ i18n (packs EN/PT/ES)   ││
│  │  ◆ NetworkManager    │  │                            ││
│  └──────────┬───────────┘  └────────────────────────────┘│
│             │                                            │
│  ┌──────────┴──────────────────────────────────────────┐ │
│  │  Interfaces de hardware                             │ │
│  │  ◆ SPI → TFT ILI9341 320×240 + Touch XPT2046        │ │
│  │  ◆ GP0–GP15 → 16 slots universais de sensor:        │ │
│  │      DS18B20 (1-Wire) · DHT22 · BMP280/BME280 (I2C) │ │
│  │  ◆ USB CDC → CLI Serial                             │ │
│  │  ◆ WiFi (CYW43439) → Servidor HTTP + Telemetria     │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │Sensores │          │ Web UI │         │ Telemetria │
    │ DS18B20 │          │Navegador│        │ HTTP/MQTT  │
    │  DHT22  │          │ (RBAC) │         │  Servidor  │
    │ BMx280  │          └────────┘         └────────────┘
    └─────────┘
```

## Telas

| Dashboard TFT | Gráfico de histórico no TFT | Dashboard web | Alpha inicial |
|:---:|:---:|:---:|:---:|
| ![Dashboard TFT](docs/images/screens/dashboard.png) | ![Gráfico TFT](docs/images/screens/graph.png) | ![Dashboard web](docs/images/web-dashboard.png) | [![Vídeo do alpha](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> 📸 Todas as telas do display, capturadas do framebuffer do painel real: [docs/images/screens/screens.md](docs/images/screens/screens.md).
>
> 🎥 O vídeo do **alpha inicial** mostra o primeiro protótipo TFT + touch — a interface foi redesenhada desde então.

## Hardware

| Componente | Especificação |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, dual-core) |
| Display | TFT ILI9341 320×240 (SPI, com DMA) |
| Touch | Touchscreen resistivo XPT2046 |
| Sensores | **16 slots universais em GP0–GP15** — qualquer mistura de DS18B20 (1-Wire), DHT22, BMP280/BME280 (I2C, 2 pinos) |
| Buzzer | Piezo passivo (via PIO) |
| Armazenamento | Flash interna de 2 MB (slot de firmware de 1 MB + LittleFS de 1 MB) |

Veja o **[Guia de Fiação](docs/WIRING.md)** para a pinagem completa e os diagramas de ligação.

## Recursos principais

### Sensoriamento
- **16 slots universais de sensor** — GP0–GP15; cada slot aceita DS18B20, DHT22 ou BMP280/BME280; tipo e pinos definidos em tempo de execução, sem recompilar
- **Temperatura, umidade e pressão** como grandezas de primeira classe, offsets de calibração por sensor e curvas de calibração multiponto
- **Pipeline de sensores com confiança zero** — verificação de ROM, detecção de troca de hardware, histerese de erro
- **Alarmes por sensor** — limiares com melodias no buzzer e sinalização visual no TFT

### Display e UI
- **TFT ILI9341 320×240** — dashboard, gráficos de histórico em baldes com banda mín/máx, estatísticas, calendário, configurações por toque
- **Caminho rápido de renderização com DMA** — composição em canvas na velocidade do barramento, zero tearing
- **Teclado de senha para a ponta do dedo** — 8 teclas de grupo + popup; qualquer um dos 91 caracteres em exatamente dois toques
- **Área segura de 4 px em toda a UI** — o offset de alinhamento da tela (±4 px por eixo) nunca corta conteúdo
- **Temas personalizados** carregados da LittleFS (até 8, editor offline em `tools/theme-editor/`); packs de temas disponíveis em tempo de compilação
- **Sistema de sons** — classes Toque / Confirmação / Erro / Alarme / Atenção com melodias e volume configuráveis

### Conectividade e Web
- **Servidor web embarcado** — sessões multiusuário, RBAC (10 bits de permissão), gerenciador de arquivos, dashboard ao vivo com painel de captura do display
- **WebUI comprimida em gzip** — páginas minificadas inline, cacheáveis no navegador, temas claro e escuro
- **Gráficos de histórico decimados no navegador** — a página baixa os arquivos binários diários crus e faz o bucketing mín/máx no cliente; o dispositivo só serve bytes
- **Exportação CSV no navegador** — decodificada dos mesmos arquivos crus pela própria página
- **Telemetria** — HTTP POST e MQTT com payloads JSON / CSV / template customizado, suporte a TLS, lote adaptativo

### Tempo e armazenamento
- **Sincronização NTP** — backoff exponencial, fallback multisservidor, RTC virtual semeado do histórico entre reboots
- **Histórico binário compacto (V5)** — codificação delta + âncora a ~5,4 bytes/registro ≈ 116 dias de registros na flash (11 canais na cadência de 1 min)
- **LittleFS** — config em banco duplo com CRC32, arquivos de histórico por dia, log compacto rotativo

### Segurança
- **Autenticação endurecida** — HMAC-SHA256 com salt aleatório por usuário, 5000 rodadas
- **Senha de admin aleatória no reset de fábrica** — 8 caracteres exibidos uma única vez no TFT, nunca persistidos
- **Rate limiter** — LRU de 16 slots com TTL de 15 min, evicção ciente de lockout, backoff exponencial
- **Uploads protegidos contra path traversal** — `..`, percent-encoding, bytes de controle e caracteres reservados bloqueados
- **[SECURITY.md](SECURITY.md)** com modelo de ameaças, política de rotação e resposta a incidentes

### Resiliência e forense
- **Forense de crash** — caixa-preta com autópsia dos scratch registers do watchdog a cada boot
- **Disciplina de flash dual-core** — Core 1 comprovadamente pausado em toda escrita de flash (medido, não presumido)
- **Disciplina de watchdog** — alimentado em torno de toda operação da LittleFS; clientes HTTP lentos não travam o loop

### Atualização OTA
- **Atualização de firmware OTA** — upload pela web, aplicada in-place com preservação de snapshot da config (Wi-Fi, usuários e slots de sensores sobrevivem)
- **Backup e restore** — backup/restore completo da LittleFS com verificação de integridade CRC32
- **[Guia de recuperação](docs/RECOVERY.md)** — caminhos BOOTSEL e picotool para todo modo de falha

### Internacionalização
- **3 idiomas de interface** — inglês embutido; português e espanhol via packs `.lng` externos carregados da LittleFS no boot

## Início rápido

### Pré-requisitos
- [PlatformIO](https://platformio.org/) (Core 6.x ou superior)
- Raspberry Pi Pico W
- Sem toolchain local? `docker compose run build` compila num container — o caminho que o [CONTRIBUTING.pt-BR.md](CONTRIBUTING.pt-BR.md) recomenda para novos contribuidores

### Compilar e gravar

```bash
# Clonar o repositório
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar o firmware
pio run -e pico_w_release

# Gravar no Pico W (auto-reset via toque de 1200 bps; BOOTSEL também funciona)
pio run -e pico_w_release -t upload

# SÓ na primeira gravação: subir os dados da LittleFS (packs de idioma, favicon).
# ⚠️ uploadfs REFORMATA a partição LittleFS — num dispositivo já em uso ele
# destrói histórico, config e calibração. Nunca repita depois que o
# dispositivo tiver dados; packs de idioma podem subir depois pelo
# gerenciador de arquivos da web.
pio run -e pico_w_release -t uploadfs
```

Prefere não compilar? Todo [release](https://github.com/angeloINTJ/simut/releases/latest) traz um `simut_vX.Y.Z.uf2` pronto (arrastar e soltar com BOOTSEL segurado), além de bundles de fonte para PlatformIO e Arduino IDE.

### Primeiro boot
1. O dispositivo inicia no dashboard e, numa unidade recém-saída de fábrica, mostra uma **senha de admin aleatória de 8 caracteres no TFT** — anote, ela não aparece de novo.
2. Configure o Wi-Fi pelas configurações do display touch, **ou** segure o dedo na tela por ~3 s durante o boot para entrar no modo AP de setup — o dispositivo anuncia a rede **`simut_SETUP`** por 15 minutos.
3. Abra a interface web em `http://simut.local` (mDNS) ou no IP mostrado no display e entre como `admin` com a senha do passo 1. A troca de senha será exigida.
4. Adicione sensores em **Config → Sensors & GPIO** (ou deixe que apareçam com *Scan for probes*).

## Estrutura do projeto

```
simut/
├── src/                    # Todo o código do firmware
│   ├── main.cpp            # Ponto de entrada
│   ├── AppManager*         # Máquina de estados da aplicação
│   ├── DisplayManager*     # Display TFT, touch, temas (Core 1)
│   ├── WebManager*         # Servidor web, API, endpoints de OTA
│   ├── StorageManager*     # LittleFS, config, histórico
│   ├── SensorManager*      # Drivers DS18B20 / DHT22 / BMx280
│   ├── NetworkManager*     # WiFi, mDNS, modo AP de setup
│   ├── TelemetryManager*   # Telemetria MQTT e HTTP
│   ├── CommandManager*     # Parser da CLI
│   ├── LogManager*         # Logs e forense de crash
│   ├── history/            # Codec V5 de histórico
│   └── SystemDefs*.h       # Constantes e limites do sistema
├── data/                   # Assets da LittleFS (packs de idioma, favicon)
├── test/                   # Testes unitários nativos (Unity)
├── tools/                  # screen_mapper, scripts de release, editor de temas…
├── docs/                   # Documentação + site GitHub Pages
├── WebUI.h                 # Fonte da web UI (vira WebUI_GZ.h no build)
└── platformio.ini          # Configuração de build
```

## Compilação

### Ambientes

| Ambiente | Propósito |
|-------------|---------|
| `pico_w_release` | Firmware de produção — **a imagem publicada nos releases** |
| `pico_w_test` | Mesmo firmware + CLI completa de 56 comandos para as suítes de bancada |
| `pico_w_asserts` | Release + asserções de concorrência |
| `pico_w_alpha` | Build headless (LCD 16×2, sem TFT) |
| `native`, `native_history_v4/v5`, `native_cli` | Testes unitários no host |
| `native_logpolicy` | Filtro de persistência de logs edge-triggered (18 testes) |

> `pico_w_debug` existe mas não linka — em `-Og` a imagem estoura o slot de 1020 KB. A flash é apertada: a imagem release usa ~97 % do slot.

### Flags de build
- `-Os` — otimização por tamanho
- `-Wall -Wextra` — warnings elevados
- `-specs=nano.specs` — newlib-nano para binário menor
- LTO desabilitado (limitação do toolchain com o Arduino-Pico earlephilhower)

## Configuração

### CLI
Uma interface de linha de comando está disponível pela serial USB (115200 baud).

- A **imagem release** traz um console de emergência mínimo de 10 comandos: `show net status`, `show system info`, `show system log`, `debug on|off`, `system admin reset`, `system format`, `system factory`, `system https off`, `reload`, `help`.
- A **imagem `pico_w_test`** traz a CLI completa estilo Cisco (56 comandos, modos `enable` / `configure terminal`) — veja o [Manual do CLI](docs/CLI-Manual.md).

A configuração do dia a dia foi desenhada para acontecer no display touch e na interface web, que são sempre completos.

### API Web
O dispositivo expõe uma API REST em `http://<ip-do-dispositivo>/api/`. A tabela completa de rotas está no [Manual do Usuário](docs/MANUAL.pt-BR.html).

## Testes

```bash
# Testes unitários no host
pio test -e native            # validadores, CRC, conversão float, lógica de tempo
pio test -e native_history_v5 # codec V5 de histórico (54 testes)
pio test -e native_cli        # parser da CLI

# Checagens de referência do codec V5 (Python vs C++, 20 mil casos aleatórios)
python3 tools/check_history_v5_parity.py --cases 20000
python3 tools/history_v5.py --selftest --trials 200000
```

## Documentação

| Documento | Descrição |
|----------|-------------|
| [Manual do Usuário (pt-BR)](docs/MANUAL.pt-BR.html) | Manual completo em português, com telas reais |
| [User Manual (EN)](docs/MANUAL.md) | Montagem, display/web/CLI, OTA, referência da API, troubleshooting |
| [Guia de Fiação](docs/WIRING.md) | Pinagem completa e diagramas de ligação |
| [Guia de Recuperação](docs/RECOVERY.md) | Recuperação de brick — BOOTSEL, picotool, reset 1200 bps |
| [Manual do CLI](docs/CLI-Manual.md) | Referência completa do console `pico_w_test` |
| [Política de Segurança](SECURITY.md) | Modelo de ameaças, tratamento de credenciais, resposta a incidentes |
| [Changelog](CHANGELOG.pt-BR.md) | Histórico de versões e mudanças |

## Contribuindo

Contribuições são bem-vindas! Leia o [CONTRIBUTING.pt-BR.md](CONTRIBUTING.pt-BR.md) para setup de desenvolvimento, convenções de código e o processo de pull request.

Todos os contribuidores devem seguir o [Código de Conduta](CODE_OF_CONDUCT.pt-BR.md).

## Suporte

- **Bugs:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- **Pedidos de recurso:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- **Vulnerabilidades de segurança:** veja [SECURITY.md](SECURITY.md) — não abra issue pública
- **Dúvidas:** abra uma discussão ou issue

## Contribuidores ✨

Agradecimentos a estas pessoas maravilhosas:

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/angeloINTJ"><img src="https://avatars.githubusercontent.com/u/117550822?v=4?s=100" width="100px;" alt="Ângelo Moisés Alves"/><br /><sub><b>Ângelo Moisés Alves</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Documentation">📖</a> <a href="#design-angeloINTJ" title="Design">🎨</a> <a href="#hardware-angeloINTJ" title="Hardware">🔌</a> <a href="#security-angeloINTJ" title="Security">🛡️</a> <a href="#maintenance-angeloINTJ" title="Maintenance">🚧</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/LorenzoLongaretto"><img src="https://avatars.githubusercontent.com/u/165825895?v=4?s=100" width="100px;" alt="Lorenzo Longaretto"/><br /><sub><b>Lorenzo Longaretto</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/JohnMartin0301"><img src="https://avatars.githubusercontent.com/u/112761826?v=4?s=100" width="100px;" alt="John Martin"/><br /><sub><b>John Martin</b></sub></a><br /><a href="#infra-JohnMartin0301" title="Infrastructure">🚇</a> <a href="https://github.com/angeloINTJ/simut/commits?author=JohnMartin0301" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/f-p-0"><img src="https://avatars.githubusercontent.com/u/239882173?v=4?s=100" width="100px;" alt="f p"/><br /><sub><b>f p</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=f-p-0" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/drmikecrypto"><img src="https://avatars.githubusercontent.com/u/91358784?v=4?s=100" width="100px;" alt="Mike"/><br /><sub><b>Mike</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Documentation">📖</a></td>
    </tr>
  </tbody>
</table>
<!-- markdownlint-restore -->
<!-- prettier-ignore-end -->
<!-- ALL-CONTRIBUTORS-LIST:END -->

Este projeto segue a especificação [all-contributors](https://allcontributors.org).

## Powered by SIMUT

Seu produto ou projeto usa o SIMUT? Adicione este selo ao seu README, documentação ou página de produto:

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)

**Versão grande** (para apresentações, pôsteres ou embalagem de produto):

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)

---

## Licença

Licença MIT — veja [LICENSE](LICENSE) para os detalhes.

Copyright © 2026 Ângelo Moisés Alves
