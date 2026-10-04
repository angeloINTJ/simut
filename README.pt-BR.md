<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-wordmark-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/logo-wordmark.svg">
    <img src="docs/images/logo-wordmark.svg" alt="SIMUT" height="64">
  </picture>
</p>

# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Monitoramento de temperatura, umidade e pressão para o Raspberry Pi Pico W, com ou sem rede

[English](README.md) | [Português](README.pt-BR.md) | [Español](README.es-ES.md)

[![License: MIT](https://img.shields.io/badge/Licença-MIT-1f6355?style=flat-square&labelColor=5f5b54)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Plataforma-RP2040-1f6355?style=flat-square&labelColor=5f5b54)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-1f6355?style=flat-square&labelColor=5f5b54)](https://arduino-pico.readthedocs.io/)
[![CI](https://img.shields.io/github/actions/workflow/status/angeloINTJ/simut/build.yml?branch=main&label=CI&style=flat-square&labelColor=5f5b54)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/angeloINTJ/simut?label=Release&color=1f6355&style=flat-square&labelColor=5f5b54)](https://github.com/angeloINTJ/simut/releases/latest)
[![Docs](https://img.shields.io/badge/Docs-GitHub_Pages-1f6355?style=flat-square&labelColor=5f5b54)](https://angelointj.github.io/simut/)
[![Contributors](https://img.shields.io/badge/Contribuidores-5-1f6355?style=flat-square&labelColor=5f5b54)](#contribuidores)
[![Contribuições bem-vindas](https://img.shields.io/badge/Contribuições-bem--vindas-1f6355?style=flat-square&labelColor=5f5b54)](CONTRIBUTING.pt-BR.md)

<p align="center">
  <img src="docs/images/tft-tour.gif" alt="Tour do TFT do SIMUT — dashboard, gráficos de histórico, calendário e configurações" width="400">
</p>

## Visão geral

O SIMUT é um firmware IoT para o **Raspberry Pi Pico W** que monitora temperatura, umidade e pressão em até 16 sensores, e continua funcionando com ou sem rede. Um único código-fonte gera três aparelhos publicados:

- um **monitor com painel touch** (TFT 320×240);
- um **monitor com LCD de caracteres** (16×2, o *alpha*);
- um **registrador a bateria** que hiberna entre as leituras (o *Air*, experimental).

Os três compartilham o mesmo núcleo:
- histórico binário no aparelho e alarmes por canal;
- 32 contas de usuário com 13 bits de permissão;
- interface web embarcada;
- telemetria por HTTP(S) ou MQTT(S), com uma linha separada para alarmes;
- MQTT Discovery do Home Assistant, rota `/metrics` para Prometheus e syslog remoto (RFC 5424);
- atualização pelo ar e console serial.

## Estado do projeto

| | |
|---|---|
| **Release atual** | **v2.10.0** (03/10/2026); o [changelog](CHANGELOG.pt-BR.md) diz o que cada versão mudou. O SIMUT saiu do beta com a v2.7.0, com base em medições: soak de 8,18 h sem nenhum reboot e 6 de 6 atualizações pelo ar sem perder nada. |
| **Imagens publicadas** | Três imagens, cada uma em `.uf2` e `.bin`: `release` (painel touch TFT), `alpha` (LCD 16×2 com console Bluetooth) e `air` (registrador a bateria sem display). Junto vêm os packs de idioma pt-BR e es-ES e um manifesto de OTA. Uma imagem com outro conjunto de recursos sai do [configurador de build](https://angelointj.github.io/simut/configurador/), e o CI a compila da `main`. |
| **Maturidade** | <ul><li>`release`: **estável**.</li><li>`alpha`: publicado e testado na bancada, com o LCD 16×2 incluído desde 26/09/2026.</li><li>`air`: **experimental**. O único soak longo dele falhou: um sono no ciclo 119 nunca acordou (F28). Um watchdog ao longo do wake hoje mitiga o problema; a causa raiz não foi confirmada.</li></ul> |
| **Testes** | Todo pull request roda 629 casos de teste no host em 9 suítes, 60 s de fuzzing e análise estática, e compila as sete imagens de firmware com o cache frio. O comportamento no hardware real é verificado numa bancada — ver [Verificação no hardware](docs/VERIFICATION.pt-BR.md). |

**Limitações conhecidas.** Cada uma está documentada onde se aplica.
- **Atualização.** A atualização pelo ar reformata o sistema de arquivos:
  - Wi-Fi, contas e slots de sensor atravessam a atualização. Histórico, arquivos de calibração e packs de idioma não, por isso a página web baixa um backup antes de começar, e restaurá-lo traz tudo de volta.
  - Há um único slot de firmware e nenhum rollback. Uma gravação ruim se recupera com BOOTSEL e cabo USB.
  - Desde a v2.9.0, só uma imagem que o projeto assinou se instala pelo ar: o `.bin` de uma release, ou uma build do configurador. Uma build sua vai pelo USB.
- **Resets sem explicação.** Um reset de watchdog (`ctx=209` ou `ctx=455`) apareceu três vezes na imagem de teste em 20–21 de setembro, e não desde então; o que foi capturado com o contexto tinha o Core 0 no console (`ctx=209`). Os dois núcleos agora estão instrumentados para explicar o próximo. Um `ctx=455` (trace vazio) no primeiro boot depois de `picotool load -x` não é isso: esse reinício passa pelo watchdog, e o registro apareceu depois de 11 de 11 gravações assim e de nenhum de 7 resets pelo pino (30/09/2026).
- **Conexões ociosas.** No soak da v2.7.0, 7,1 % das respostas numa conexão keep-alive ociosa chegaram cortadas. O aparelho derruba um fluxo que não consegue enviar por 4 s.
- **Respostas chunked.** Lido num laço apertado, 0,15–0,6 % das respostas do `/api/status` chegam com o enquadramento chunked quebrado ([#189](https://github.com/angeloINTJ/simut/issues/189)). O aparelho não reinicia e a requisição seguinte funciona; a página perde uma atualização.
- **Não é instrumento certificado.** O SIMUT não é um instrumento metrológico certificado. Valide-o contra a sua própria referência antes de confiar nele para armazenamento regulado.

## Por que SIMUT?

| Necessidade | Sketch Arduino DIY | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Autônomo com display | Codificação manual | Sem suporte a TFT | UI touch embutida, ou LCD 16×2 |
| Ambientes regulados | Sem trilha de auditoria | Sem RBAC de usuários | 32 contas, PIN de painel por conta, log de eventos persistente e syslog remoto |
| Cadeia fria (sondas até −50 °C) | Leituras básicas | Monitoramento básico | Multissensor calibrado, janelas de manutenção |
| Operação offline | Sim | Frequentemente depende de nuvem | Web local completa + display |
| Atualização OTA | Regravação manual | OTA | OTA assinada + backup/restore |
| Segurança | Nenhuma | Básica | HMAC-SHA256, RBAC de 13 bits, bloqueios, HTTPS opcional |
| Home Assistant | Integração manual | Nativa | MQTT Discovery (opcional) |
| Métricas Prometheus | Nenhuma | Embutido | Rota `/metrics` |
| Log de auditoria remoto | Nenhum | Complemento | Syslog (RFC 5424 / UDP) |

**O SIMUT é para você se:** precisa de um sistema de monitoramento de temperatura autônomo, seguro e auditável, que funcione com ou sem internet — típico de laboratórios, farmácias, bancos de sangue, armazenamento de vacinas e cadeias frias de alimentos.

**ESPHome/Tasmota podem ser melhores se:** você não precisa de display local e prefere configuração YAML a uma interface web embutida. (Se o que te prendia lá era o Home Assistant: o SIMUT fala MQTT Discovery.)

## Arquitetura

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                   │
│  ┌──────────────────────┐  ┌────────────────────────────┐│
│  │      Core 0          │  │        Core 1              ││
│  │  (Main Loop)         │  │  (Display Loop)            ││
│  │                      │  │                            ││
│  │  ◆ AppManager ───────┼──┼─ state/snapshots ──────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ DMA canvas renderer     ││
│  │  ◆ CommandManager    │  │  ◆ Themes                  ││
│  │  ◆ StorageManager    │  │  ◆ i18n (EN/PT/ES packs)   ││
│  │  ◆ NetworkManager    │  │                            ││
│  └──────────┬───────────┘  └────────────────────────────┘│
│             │                                            │
│  ┌──────────┴──────────────────────────────────────────┐ │
│  │  Hardware Interfaces                                │ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch        │ │
│  │    (alpha: HD44780 16×2 LCD · Air: no display)      │ │
│  │  ◆ GP0–GP15 → 16 universal sensor slots:            │ │
│  │      DS18B20 (1-Wire) · DHT22 · BMP280/BME280 (I2C) │ │
│  │  ◆ USB CDC → serial console (+ Bluetooth: alpha/Air)│ │
│  │  ◆ WiFi (CYW43439) → HTTP(S) server + telemetry     │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │ Sensors │          │ Web UI │         │  Telemetry │
    │ DS18B20 │          │ Browser│         │ HTTP(S) /  │
    │  DHT22  │          │ (RBAC) │         │  MQTT(S)   │
    │ BMx280  │          └────────┘         └────────────┘
    └─────────┘
```

## Telas

| Dashboard TFT | Gráfico de histórico no TFT | Dashboard web | Alpha inicial |
|:---:|:---:|:---:|:---:|
| ![Dashboard TFT](docs/images/screens/dashboard.png) | ![Gráfico TFT](docs/images/screens/graph.png) | ![Dashboard web](docs/images/web-dashboard.png) | [![Vídeo do alpha](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> Todas as telas do display, capturadas do framebuffer do painel real: [docs/images/screens/screens.md](docs/images/screens/screens.md).
>
> O vídeo do **alpha inicial** mostra o primeiro protótipo TFT + touch — a interface foi redesenhada desde então.

## Hardware

| Componente | Especificação |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, dual-core) |
| Display | `release`: TFT ILI9341 320×240 (SPI, com DMA) · `alpha`: LCD de caracteres HD44780 16×2 (4 bits) · `air`: nenhum |
| Touch | Touchscreen resistivo XPT2046 (`release`) |
| Sensores | **16 slots universais em GP0–GP15** — qualquer mistura de DS18B20 (1-Wire), DHT22 e BMP280/BME280. O BMx280 é I²C e ocupa dois pinos; dois deles podem dividir um par (0x76/0x77) |
| Buzzer | Piezo passivo (via PIO) — não existe no Air |
| Armazenamento | Flash interna de 2 MB (slot de firmware de 1 MB + LittleFS de 1 MB) |

Veja o **[guia de fiação](docs/WIRING.md)** para a pinagem completa e os diagramas de ligação.

> **PCB do SIMUT — layout disponível para download** — o projeto da placa no KiCad (`.kicad_pcb`, `.kicad_sch`) está em [`PCB_test/`](PCB_test/), e o pacote de fabricação pronto para enviar à fábrica (Gerbers + furação PTH/NPTH, sem camadas de pasta) está publicado como release público: **[simut-pcb-v1.1 — `simut_pcb_fabrication.zip`](https://github.com/angeloINTJ/simut/releases/tag/simut-pcb-v1.1)**.

## Recursos

- **Sensores e alarmes** — 16 slots universais de sensor (DS18B20, DHT22, BMP280/BME280) definidos em tempo de execução, curvas de calibração, limites por canal, alarme de falha e janelas de manutenção.
- **Painel touch** (`release`) — tela inicial, gráficos do histórico e calendário, e identidade no vidro: a conta, depois o PIN dela.
- **LCD de caracteres** (`alpha`) — cada slot por vez, o ponto de acesso de configuração e um console Bluetooth.
- **Interface web** — 11 páginas, o espelho do painel ao vivo, gráficos do histórico e exportação CSV no navegador.
- **API HTTP** — 62 rotas. Cada uma é protegida por uma permissão ou pública por projeto, e o CI confere isso.
- **Telemetria** — HTTP, HTTPS, MQTT e MQTTS, em lotes por quantidade, e uma segunda linha, com confirmação, para os alarmes; Home Assistant, Prometheus e syslog.
- **Rede e horário** — Wi-Fi que se reconecta sozinho, ponto de acesso de configuração aberto a pedido, e NTP com um relógio provisório até sincronizar.
- **Armazenamento** — o histórico binário V5 (cerca de 116 dias em 1 MB), a configuração com CRC32 e `.bak`, e um log de eventos com 162 códigos de evento.
- **Segurança** — 32 contas, 13 bits de permissão, HMAC-SHA256 com salt, bloqueios por tentativa e HTTPS opcional.
- **Atualização** — atualização pelo ar assinada, pela página web, e backup e restauração do sistema de arquivos inteiro.
- **SIMUT Air** (experimental) — registrador a bateria que hiberna entre as leituras.
- **Idiomas** — inglês embutido; pt-BR e es-ES como packs de idioma.

Cada recurso em detalhe, e quais uma build própria pode deixar de fora: **[docs/FEATURES.pt-BR.md](docs/FEATURES.pt-BR.md)**.

## Início rápido

### Pré-requisitos
- [PlatformIO](https://platformio.org/) (Core 6.x ou superior)
- `pip install zopfli` — opcional; as páginas web comprimem 2.888 B a menos com ele, e os orçamentos de flash são medidos com ele
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

# Só na primeira gravação: subir os dados da LittleFS (packs de idioma, temas, favicon).
# Atenção: o uploadfs reformata a partição LittleFS — num dispositivo já em uso ele
# destrói histórico, config e calibração. Nunca repita depois que o
# dispositivo tiver dados; packs de idioma podem subir depois pelo
# gerenciador de arquivos da web. Ele copia os dois packs de idioma, e o
# aparelho carrega o primeiro em ordem alfabética (es-ES): apague o outro.
pio run -e pico_w_release -t uploadfs
```

Prefere não compilar? Todo [release](https://github.com/angeloINTJ/simut/releases/latest) traz `simut_vX.Y.Z_release.uf2`, `_alpha.uf2` e `_air.uf2` (arrastar e soltar com BOOTSEL segurado), o `.bin` correspondente para atualização pelo ar e os packs de idioma pt-BR e es-ES. Uma imagem com outro conjunto de recursos sai do [configurador de build](https://angelointj.github.io/simut/configurador/).

### Primeiro boot
1. **Anote a senha do admin.** Uma unidade recém-saída de fábrica imprime uma senha de admin aleatória de 8 caracteres **uma única vez no console serial USB** (115200 baud). Ela nunca é gravada em texto puro. Se você perder, `system admin reset confirm` pela USB imprime uma nova.
2. **Coloque o aparelho na sua rede.** O ponto de acesso de configuração abre quando você pede: digite `ap` no console (no Air, depois de `enable`), ou use Configurações → 8 no painel touch. Da v2.7.1 à v2.8.0, uma unidade sem rede configurada o abria sozinha; não abre mais.
   - Conecte-se a `<nome>_SETUP` (`simut_SETUP` de fábrica). É WPA2, e a chave por aparelho está na resposta do `ap`, no console USB e no terminal de boot do TFT.
   - O portal abre em `http://192.168.4.1`.
   - O aparelho continua medindo com o ponto de acesso no ar; só a telemetria e o syslog esperam a rede. Da v2.7.1 à v2.8.0, ele não lia sensores, não conferia alarmes e não gravava histórico enquanto o ponto de acesso estava no ar.

   Sem tela, dá para usar o console: `system ssid <nome>`, `system pass <senha>` e depois `reload confirm`. O console corta no primeiro espaço: rede ou senha com espaço só pela página web.

   Sem rede configurada, a unidade pede a data e a hora no fim do boot, porque sem rede não há NTP: o painel touch abre uma tela de data e hora (**PULAR** sai dela, e Configurações → 4 acerta o relógio depois), e toda imagem escreve no console `Sem rede, relogio provisorio: conf time AAAA-MM-DD HH:MM:SS`. Esse comando funciona no console de todas as imagens, pela USB ou pelo Bluetooth: no alpha e no Air, que não têm painel, é o jeito de responder à pergunta. A seção **Data e Hora** da página web também acerta o relógio.
3. **Abra a interface web** no endereço que o aparelho recebeu — na imagem `release`, também em `http://simut.local` — e entre como `admin` com a senha do passo 1. O aparelho vai pedir uma senha nova.
4. **Adicione sensores** em **Config → Sensors & GPIO**, ou deixe o *Scan for probes* encontrá-los.
5. **No painel touch**, Configurações pede uma conta e o PIN dela. O PIN de fábrica do admin é `1234`, e a troca é exigida no primeiro uso.

## Estrutura do projeto

```
simut/
├── src/                    # Código do firmware (C++17)
│   ├── main.cpp            # Ponto de entrada
│   ├── AppManager*         # Máquina de estados, boot, alarmes, ciclo do Air
│   ├── DisplayManager*     # Painel TFT e LCD do alpha (Core 1), touch, temas
│   ├── WebManager*         # Servidor web, API HTTP, sessões, OTA, TLS
│   ├── StorageManager*     # LittleFS, config, contas, arquivos de histórico
│   ├── SensorManager*      # Drivers DS18B20 / DHT22 / BMx280
│   ├── NetworkManager*     # Wi-Fi, escada de reconexão, AP de setup, mDNS, NTP
│   ├── TelemetryManager*   # Telemetria HTTP(S)/MQTT(S) e linha de alarmes
│   ├── CommandManager*     # Console serial e Bluetooth
│   ├── LogManager*         # Log de eventos e forense de travamentos
│   ├── HistoryV5.*         # Codec do histórico V5
│   ├── air/                # Configuração do SIMUT Air
│   ├── display/            # Teclados, fontes e rótulos comuns aos displays
│   ├── sensors/            # Tabela de canais, curvas de calibração
│   ├── ota/                # Preparo, validação e aplicação da atualização
│   └── SystemDefs*.h       # Constantes e limites do sistema
├── data/                   # Dados da LittleFS (packs de idioma, temas, favicon)
├── PCB_test/               # Projeto da PCB no KiCad + arquivos Gerber/DRL
├── test/                   # Testes unitários nativos (Unity), nove suítes
├── tools/                  # Portões de build, suítes de bancada, PicoHand, scripts de release, editor de temas
├── docs/                   # Documentação + site do GitHub Pages
├── WebUI.h                 # Fonte da interface web (vira src/WebUI_GZ.h no build)
├── AGENTS.md               # Manual da bancada: gravação, o Air, medições
└── platformio.ini          # Configuração de build
```

## Compilação

### Ambientes

| Ambiente | Propósito | Publicado |
|-------------|---------|:---:|
| `pico_w_release` | Imagem de produção para o painel TFT: console de emergência, servidor HTTPS, mDNS | `…_release` |
| `pico_w_alpha` | LCD de caracteres 16×2 (HD44780), sem touch; console de emergência + console Bluetooth | `…_alpha` |
| `pico_w_air` | **Experimental** — SIMUT Air: sem display, sem buzzer, ciclo de hibernação (M0 acordado / M1 acorda-lê-envia-dorme); console completo + Bluetooth. Ver o §17 do [Manual do Usuário](docs/MANUAL.pt-BR.md) | `…_air` |
| `pico_w_test` | Imagem de bancada: o console completo para as suítes de teste; sem HTTPS, sem mDNS | — |
| `pico_w_test_https` | `pico_w_test` mais o servidor HTTPS, para validar TLS; três das páginas vêm da LittleFS para caber | — |
| `pico_w_asserts` | Release + asserções de concorrência | — |
| `pico2_w_release` | A release compilada para o Pico 2 W (RP2350), para o CI vê-la compilar. Sobe na placa da bancada; recusa atualizar pelo ar até os slots A/B existirem | — |
| oito ambientes `native*` | Testes unitários no host — ver [Testes](#testes) | — |

> **Nota de segurança para `pico_w_alpha` e `pico_w_air`:** as duas compilam o
> console Bluetooth SPP (`SIMUT_BLUETOOTH=1`), então nessas duas imagens ele é
> superfície de ataque real. Ele é autenticado pela **senha do admin da web**,
> com bloqueio exponencial que sobrevive a uma reconexão, e janela de descoberta
> que fecha 5 minutos depois do boot. Os comandos de recuperação são restritos à USB.
>
> O ponto de acesso de configuração é WPA2 em todas as imagens, com chave por
> aparelho mostrada no console e, onde houver, no display. Ver [SECURITY.md](SECURITY.md) §2 e §8.

> Não há ambiente de depuração. O `pico_w_debug` foi removido na v2.4.1 depois de nunca ter linkado: em `-Og` a imagem estourava o slot de 1020 KB em ~100 KB. A flash é apertada. A imagem release usa 97,7 % do slot de programa de 1.044.480 B (o valor medido mora em `tools/flash_budget.json`), e o CI confere cada `.bin`, com os 241 B da assinatura, contra o teto de atualização pelo ar, de 1.040.384 B. Um alvo de GDB teria de ser montado cortando funcionalidades. Para o tripwire de concorrência no hardware, use `pico_w_asserts`.

### Flags de build
- `-Os` — otimização por tamanho
- `-Wall -Wextra`, e `-Werror` em `src/` (bibliotecas de terceiros não entram)
- `-specs=nano.specs` — newlib-nano para binário menor
- `-DNDEBUG` em todas as imagens
- LTO desabilitado (limitação do toolchain com o Arduino-Pico earlephilhower)
- O framework é fixado no arduino-pico 5.6.1 e recebe patches de `tools/arduino_pico_overrides/patch.sh`

## Configuração

### Console (CLI)
O console serial fica disponível pela USB (115200 baud) e, no alpha e no Air, por Bluetooth SPP.

- **O console de emergência** roda nas imagens `release` e `alpha`. Os 15 comandos dele:
  - `show net status`, `show system info`, `show system log`
  - `debug on|off`
  - `system admin reset`, `system format`, `system factory`, `system https off`
  - `system ssid <nome>`, `system pass <senha>`, `system cors <origem|off>`
  - `ap`, `time <data> <hora>`, `reload`, `help`

  Os comandos destrutivos pedem `confirm`, e as quatro recuperações (`system factory`, `system format`, `system admin reset`, `system https off`) são recusadas pelo Bluetooth.
- **O console completo estilo Cisco** (`enable` / `configure terminal`) roda na imagem `air` e nas imagens de bancada `pico_w_test` — veja o [manual do CLI](docs/CLI-Manual.md). O Air acrescenta `air status | hibernate | stop | idle <seg> | charger <gpio|off>`.

**Onde a configuração acontece:**
- **A interface web** é a ferramenta do dia a dia.
- **O painel touch** cobre o que o operador precisa no aparelho: temas, alarmes, sons, idioma, o próprio PIN, usuários, a política de PIN, calibração do toque, ajuste da tela, status, o ponto de acesso de configuração e a data e a hora.

### API Web
O aparelho expõe uma API REST em `http://<ip-do-dispositivo>/api/`:
- **62 rotas** — 52 protegidas por permissão, 10 públicas por projeto, nenhuma sem proteção, conferidas por `tools/check_authz.py` no CI;
- a tabela de rotas está no [manual do usuário](docs/MANUAL.pt-BR.md);
- o [docs/AUTHORIZATION.md](docs/AUTHORIZATION.md) liga cada rota à sua permissão;
- o [docs/API_POST.md](docs/API_POST.md) documenta os corpos dos POST.

## Testes

### Testes no host

```bash
pio test -e native             # validadores, cursor de telemetria, rótulos, parsers, pacotes de idioma, as telas Licença e de atualização, o menu de Configurações, a verificação de senha, a sessão web (283 casos)
pio test -e native_history_v5  # codec do histórico V5 (62)
pio test -e native_cli         # parser do CLI (33)
pio test -e native_logpolicy   # persistência de log por transição, faixas da autópsia, leitura do toque (57)
pio test -e native_alarmqueue  # fila da telemetria de alarmes (54)
pio test -e native_network     # máquina de estados da reconexão Wi-Fi (36)
pio test -e native_air         # config persistente do SIMUT Air (16)
pio test -e native_sensors     # tabela de tipos de sensor (13)
pio test -e native_otasig      # assinatura da imagem de OTA, a restauração do backup, e o gravador de slot e a atualização em teste do RP2350 (75)

# Checagens de referência do codec V5 (Python vs C++, 20 mil casos aleatórios)
python3 tools/check_history_v5_parity.py --cases 20000
python3 tools/history_v5.py --selftest --trials 200000
```

### Integração contínua

Todo push e pull request para a `main` roda quatro jobs:
- **gates** — as suítes do host mais estas checagens:
  - varredura de segredos, tabelas de códigos de log, matriz de autorização;
  - consistência da licença, guarda do sistema de arquivos, consistência do Air;
  - testes da fusão de dias do histórico.
- **firmware** — as sete imagens, compiladas com o cache frio:
  - cada uma é conferida contra o seu orçamento de flash e o teto de atualização pelo ar;
  - o próprio build aplica o `-Werror` e os portões da interface web, da ajuda do CLI, dos códigos de log, da tabela de canais e dos packs de idioma.
- **fuzz** — 60 s de libFuzzer contra os validadores da API web, com oráculos de contrato.
- **análise estática** — cppcheck, em versão fixa.

A `main` é protegida: nove dessas checagens precisam passar antes de qualquer merge, todos os jobs menos a imagem `pico_w_test_https`.

### Verificação no hardware

O comportamento é conferido numa bancada: um Pico W com o painel TFT e o touch, e um segundo Pico, a *PicoHand*, que aciona as linhas de RESET e BOOTSEL do alvo. Cada medição, com data e números: **[docs/VERIFICATION.pt-BR.md](docs/VERIFICATION.pt-BR.md)**.

## Documentação

| Documento | Descrição |
|----------|-------------|
| [Manual do usuário](docs/MANUAL.pt-BR.md) | Montagem, display/web/console, OTA, referência da API, solução de problemas — mantido atualizado |
| [User Manual (EN)](docs/MANUAL.md) | O mesmo manual, em inglês |
| [Manual completo](docs/MANUAL.pt-BR.html) | O manual do produto, atualizado para a v2.10.0: 31 capítulos sobre instalação, configuração, uso no dia a dia e integração com servidores. As telas estão sendo recapturadas; cada uma que falta está marcada no lugar dela |
| [Guia de fiação](docs/WIRING.md) | Pinagem completa e diagramas de ligação |
| [Atualização pelo ar](docs/OTA_USAGE.md) | Atualizar pela página web, e o que sobrevive |
| [Guia de recuperação](docs/RECOVERY.md) | Recuperação de brick — BOOTSEL, picotool, reset 1200 bps |
| [Manual do CLI](docs/CLI-Manual.md) | Referência completa do console, inclusive o do Air |
| [Matriz de autorização](docs/AUTHORIZATION.md) | Cada rota HTTP e a permissão que ela exige |
| [Política de segurança](SECURITY.md) | Modelo de ameaças, tratamento de credenciais, resposta a incidentes |
| [Recursos](docs/FEATURES.pt-BR.md) | Cada recurso em detalhe |
| [Verificação no hardware](docs/VERIFICATION.pt-BR.md) | O que foi medido na bancada, com data |
| [Índice da documentação](docs/README.md) | Quais documentos são mantidos atualizados e quais são retratos de uma época |
| [Changelog](CHANGELOG.pt-BR.md) | Histórico de versões e mudanças |

## Como o SIMUT é desenvolvido

A maior parte das mudanças é escrita por um agente de IA (Claude Code) em sessões dirigidas pelo mantenedor: de 1º de setembro a 2 de outubro de 2026, 275 dos 371 commits da `main` traziam uma linha `Co-Authored-By: Claude`. As instruções que essas sessões seguem são o [CLAUDE.md](CLAUDE.md) e o [AGENTS.md](AGENTS.md). As regras são as mesmas para toda mudança, venha de quem vier:
- ela só chega à `main` por pull request, depois de nove checks obrigatórios: as nove suítes de teste no host, também sob AddressSanitizer e UBSan, 60 s de fuzzing, análise estática e seis imagens de firmware compiladas com o cache frio;
- os testes vêm primeiro: um conserto traz uma reprodução que falha antes e passa depois, e uma refatoração mostra que o comportamento não mudou;
- uma afirmação sobre bytes, velocidade ou conserto traz a medição, e o que não foi conferido no hardware diz isso;
- as decisões de produto, e a de mergear, são do mantenedor.

## Contribuindo

Contribuições são bem-vindas. Leia o [CONTRIBUTING.pt-BR.md](CONTRIBUTING.pt-BR.md) para setup de desenvolvimento, convenções de código e o processo de pull request.

Todos os contribuidores devem seguir o [Código de Conduta](CODE_OF_CONDUCT.pt-BR.md).

## Suporte

- **Bugs:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- **Pedidos de recurso:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- **Vulnerabilidades de segurança:** veja [SECURITY.md](SECURITY.md) — não abra issue pública
- **Dúvidas:** abra uma discussão ou issue

## Contribuidores

Obrigado a todas as pessoas que contribuíram:

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

As imagens de firmware também levam software de terceiros. O [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
cita cada componente, o titular do copyright, a licença e as imagens que o levam, e a pasta
[LICENSES/](LICENSES) guarda os textos das licenças. O aparelho mostra a mesma lista na tela
Licença do painel e na página `/license`.

Copyright © 2026 Ângelo Moisés Alves
