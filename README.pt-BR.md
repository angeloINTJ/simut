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
| **Release atual** | **v2.7.2** (24/09/2026). A linha 2.7 saiu do beta com a v2.7.0, com base em medições: soak de 8,18 h sem nenhum reboot e 6 de 6 atualizações pelo ar sem perder nada. A v2.7.2 corrige o relógio do Air e os lotes longos da telemetria, e duas telas do painel, e toda imagem fica 16 kB menor. |
| **Imagens publicadas** | Três imagens, cada uma em `.uf2` e `.bin`: `release` (painel touch TFT), `alpha` (LCD 16×2 com console Bluetooth) e `air` (registrador a bateria sem display). Junto vêm os packs de idioma pt-BR e es-ES e um manifesto de OTA. |
| **Na `main`, ainda sem release** | <ul><li>O Air carrega o relógio através do sono: os carimbos ficam em ±0,09 s em vez de atrasar 0,8 s por wake.</li><li>A telemetria monta o payload um registro inteiro por vez, então uma fila longa não sai mais como JSON inválido nem pula registros.</li><li>Com um sensor, o LCD do alpha mostra a contagem de pendentes de telemetria, e o ícone de Wi-Fi cresce da esquerda para a direita.</li></ul> |
| **Maturidade** | <ul><li>`release`: **estável**.</li><li>`alpha`: publicado e testado na bancada, menos a saída do LCD, coberta por testes no host — a bancada não tem HD44780.</li><li>`air`: **experimental**. O único soak longo dele falhou: um sono no ciclo 119 nunca acordou (F28). Um watchdog ao longo do wake hoje mitiga o problema; a causa raiz não foi confirmada.</li></ul> |
| **Testes** | Todo pull request roda 408 casos de teste no host em 7 suítes, 60 s de fuzzing e análise estática, e compila as seis imagens de firmware com o cache frio. O comportamento no hardware real é verificado numa bancada — ver [Verificação no hardware](#verificação-no-hardware). |

**Limitações conhecidas.** Cada uma está documentada onde se aplica.
- **Atualização.** A atualização pelo ar reformata o sistema de arquivos:
  - Wi-Fi, contas e slots de sensor atravessam a atualização. Histórico, arquivos de calibração e packs de idioma não, por isso a página web baixa um backup antes de começar, e restaurá-lo traz tudo de volta.
  - Há um único slot de firmware e nenhum rollback. Uma gravação ruim se recupera com BOOTSEL e cabo USB.
- **Resets sem explicação.** Um reset de watchdog com trace vazio (`ctx=209`/`ctx=455`) apareceu três vezes na imagem de bancada em 20–21 de setembro, e não desde então. Os dois núcleos agora estão instrumentados para explicar o próximo.
- **Conexões ociosas.** No soak da v2.7.0, 7,1 % das respostas numa conexão keep-alive ociosa chegaram cortadas. O aparelho derruba um fluxo que não consegue enviar por 4 s.
- **Lista de usuários.** Cada gravação da lista de usuários reinicia o aparelho, cerca de 25 s por vez.
- **Cursor de telemetria.** O cursor é um único carimbo de tempo, então um registro gravado fora de ordem na flash é pulado: 6 de 75.778 registros numa medição.
- **Não é instrumento certificado.** O SIMUT não é um instrumento metrológico certificado. Valide-o contra a sua própria referência antes de confiar nele para armazenamento regulado.

## Por que SIMUT?

| Necessidade | Sketch Arduino DIY | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Autônomo com display | ⚠️ Codificação manual | ❌ Sem suporte a TFT | ✅ UI touch embutida, ou LCD 16×2 |
| Ambientes regulados | ❌ Sem trilha de auditoria | ❌ Sem RBAC de usuários | ✅ 32 contas, PIN de painel por conta, trilha de auditoria assinada |
| Cadeia fria (sondas até −50 °C) | ⚠️ Leituras básicas | ✅ Monitoramento básico | ✅ Multissensor calibrado, janelas de manutenção |
| Operação offline | ✅ Sim | ❌ Frequentemente depende de nuvem | ✅ Web local completa + display |
| Atualização OTA | ❌ Regravação manual | ✅ OTA | ✅ OTA + backup/restore |
| Segurança | ❌ Nenhuma | ⚠️ Básica | ✅ HMAC-SHA256, RBAC de 13 bits, bloqueios, HTTPS opcional |
| Home Assistant | ⚠️ Integração manual | ✅ Nativa | ✅ MQTT Discovery (opcional) |
| Métricas Prometheus | ❌ Nenhuma | ✅ Embutido | ✅ Rota `/metrics` |
| Log de auditoria remoto | ❌ Nenhum | ⚠️ Complemento | ✅ Syslog (RFC 5424 / UDP) |

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

Veja o **[Guia de Fiação](docs/WIRING.md)** para a pinagem completa e os diagramas de ligação.

> **PCB do SIMUT — layout disponível para download** — o projeto da placa no KiCad (`.kicad_pcb`, `.kicad_sch`) está em [`PCB_test/`](PCB_test/), e o pacote de fabricação pronto para enviar à fábrica (Gerbers + furação PTH/NPTH, sem camadas de pasta) está publicado como release público: **[simut-pcb-v1.1 — `simut_pcb_fabrication.zip`](https://github.com/angeloINTJ/simut/releases/tag/simut-pcb-v1.1)**.

## Recursos principais

### Sensoriamento e alarmes
- **16 slots universais de sensor** — GP0–GP15. Cada slot aceita DS18B20, DHT22 ou BMP280/BME280; o BMx280 é reclassificado sozinho pelo ID do chip. Tipo e pinos são definidos em tempo de execução, sem recompilar.
- **Temperatura, umidade e pressão** como canais de primeira classe.
- **Calibração** — offsets por sensor e curvas de até 5 pontos por canal, lineares ou suaves.
- **Pipeline de sensores zero-trust:**
  - verificação da ROM do DS18B20, com a sonda trocada em quarentena até a certa voltar;
  - histerese de erro: 3 falhas para entrar, 5 sucessos para sair;
  - leituras fora da faixa descartadas.
- **Alarmes em todo canal:**
  - limites mínimo e máximo por canal;
  - alarme de falha que dispara mesmo com os limites do sensor desligados;
  - silêncio de 120 s e mudo global;
  - melodias no buzzer e aviso visual no display.
- **Janelas de manutenção** — por sensor, até 30 dias, definidas pelo painel ou por um servidor. Com uma aberta, os alarmes ficam suprimidos, e o início e o fim da janela são avisados como `maint_on` / `maint_off`.

### Painel touch (`release`)
- **Painel touch ILI9341 320×240** — dashboard, gráficos de histórico com faixa de mín/máx, estatísticas, calendário, configurações.
- **Identidade no painel** — o operador escolhe a conta e depois digita o PIN dela:
  - 32 contas, cada uma com o seu PIN;
  - política de PIN configurável: tamanho mínimo, 1 a 3 glifos por tecla, dígitos ou 0-9A-Z;
  - teclado sorteado de novo a cada toque;
  - bloqueio por conta: a sexta falha bloqueia a conta, e 20 falhas no total bloqueiam o painel.
- **Administração na tela:**
  - o item Usuários cria contas e define os bits de permissão e os PINs;
  - as 12 linhas de Configurações são filtradas pelo que a conta pode fazer;
  - Configurações → 12 liga o ponto de acesso de configuração.
- **Gestos no painel superior** — um toque alterna mín/máx, segurar 3 s fixa a seleção.
- **Renderização rápida com DMA** — composição em canvas pelo SPI a 62,5 MHz.
- **Área segura de 4 px em toda tela** — o ajuste de alinhamento da tela (±4 px por eixo) nunca corta conteúdo.
- **Temas** — até 8 carregados da LittleFS (11 vêm em `data/themes/`); o editor em `tools/theme-editor/` mostra a prévia num aparelho de verdade.
- **Sistema de som** — classes Toque, Confirmação, Erro, Alarme e Atenção, 6 melodias cada, com volumes separados para sistema e alarme.

### LCD de caracteres (`alpha`)
- **Leituras** — alterna entre todos os slots e canais ativos a cada 3 s, com dígitos grandes para temperatura e umidade e uma etiqueta `S<n>` indicando o slot.
- **Ponto de acesso de configuração** — mostra o endereço, o SSID e a chave, rolando os valores longos.
- **Console Bluetooth** — veja a nota de segurança em [Ambientes](#ambientes).
- **Na `main`, ainda sem release** — com um único sensor, o canto inferior esquerdo mostra a contagem de pendentes de telemetria (`N`, ou `Nk` a partir de mil), e o ícone de Wi-Fi cresce da esquerda para a direita.

### Interface web
- **11 páginas** — comprimidas com gzip (zopfli) na flash, com temas claro e escuro que seguem a preferência do sistema, gerenciador de arquivos e sessões multiusuário que expiram após 15 minutos ociosas.
- **Espelho do painel ao vivo** (`release`) — o quadro atual do painel no navegador, 213 ms por quadro. Um clique nele é um toque na tela.
- **Cada mudança diz quanto custa** — três botões:
  - *Testar*: aplica sem salvar;
  - *Aplicar agora*: salva sem reiniciar;
  - *Salvar e reiniciar*.

  O próprio aparelho classifica cada mudança com um ensaio (dry run) antes de a página oferecer os botões.
- **Busca de redes Wi-Fi** — escolha a rede numa lista, inclusive de dentro do ponto de acesso de configuração.
- **Gráficos de histórico e exportação CSV no navegador** — a página baixa os arquivos binários brutos de cada dia e ela mesma decodifica, agrupa em baldes (mín/máx/média) e exporta. A hora recente, ainda não selada, vem de `/api/history/open`. O renderizador de gráficos é embutido — sem CDN.
- **API HTTP** — 61 rotas. Cada uma é protegida por uma permissão ou pública por projeto, e o CI confere isso.

### Telemetria e integrações
- **Quatro transportes** — HTTP, HTTPS, MQTT e MQTTS:
  - payload em JSON, CSV ou template customizado;
  - TLS 1.2 (ECDHE com AES-GCM), com o certificado do servidor conferido contra um `/cert.pem` enviado ao aparelho.
- **Lote por quantidade:**
  - `t_int` é o lote mínimo: o rádio fica desligado até essa quantidade de registros esperar (0 = desligado);
  - `t_bat` é o máximo por requisição, um teto que a memória livre pode baixar;
  - o tamanho do lote se adapta a sucessos e falhas, e o tempo de resposta do servidor dita o intervalo até o próximo.
- **Uma segunda linha para alarmes:**
  - eventos de alarme, falha e manutenção seguem numa fila própria (32 por padrão, até 64);
  - cada evento só sai da fila quando o servidor confirma (HTTP 2xx ou ack MQTT);
  - cada um leva o nome da conta que agiu.
- **Integrações** — MQTT Discovery do Home Assistant (opcional), `/metrics` do Prometheus (sessão ou HTTP Basic) e syslog remoto (RFC 5424 sobre UDP).
- **Ganchos de frota:**
  - cabeçalhos de identidade `X-SIMUT-*` nos envios;
  - na imagem `release`, um serviço mDNS `_simut._tcp` com id, versão, imagem e TLS no registro TXT;
  - tokens Bearer e origem CORS configurável.

### Rede e horário
- **Wi-Fi que se reconecta sozinho:**
  - escada de tentativas: 5 s, dobrando até 120 s, depois dormência e uma nova rodada;
  - SSIDs ocultos e checagem de qualidade do sinal;
  - IP estático, dois servidores DNS, servidor NTP próprio ou relógio manual, e porta web configurável.
- **Ponto de acesso de configuração:**
  - chama-se `<nome do aparelho>_SETUP` — `simut_SETUP` de fábrica;
  - WPA2, com chave por aparelho mostrada no console USB e na tela de boot do TFT;
  - portal cativo em `http://192.168.4.1`.

  Cinco formas de entrar:
  - uma unidade sem rede configurada abre sozinha (menos o Air);
  - um fallback automático abre quando a rede se perde (menos o Air);
  - Configurações → 12 no painel;
  - o comando `ap` no console (USB, ou Bluetooth no alpha e no Air);
  - segurar o painel por 3 s durante o boot.
- **NTP** — o intervalo entre tentativas cresce de 20 s a 15 min, com fallback para `pool.ntp.org`. Até o NTP sincronizar, um relógio provisório parte do registro mais novo gravado.

### Armazenamento e histórico
- **Histórico binário compacto (V5)** — codificação delta + âncora a 5,38 bytes/registro, cerca de 116 dias no sistema de arquivos de 1 MB (11 canais a cada minuto, medido em arquivos de bancada em 31/07/2026):
  - blocos de 60 registros, cada um com o seu CRC;
  - o bloco aberto é salvo a cada registro;
  - passando de 86 % de ocupação, o dia mais antigo é apagado.
- **Configuração** — com CRC32, gravada num arquivo temporário e renomeada, com um `.bak` de reserva. Os segredos ficam ofuscados em repouso.
- **Log de eventos** — 2 × 800 registros e 155 códigos de evento:
  - eventos de rotina são gravados nas mudanças de estado, com um pulso por hora e uma contagem do que foi suprimido;
  - registros de segurança, configuração e falha fatal nunca são filtrados.

### Segurança
- **Contas e permissões:**
  - 32 contas, 13 bits de permissão;
  - ninguém concede um bit que não tem;
  - backup, restauração, OTA e instalação de certificado exigem a máscara de admin completa.
- **Senhas:**
  - HMAC-SHA256, 5000 rodadas, salt aleatório de hardware de 8 bytes por usuário e um pepper preso à placa;
  - uma unidade recém-saída de fábrica gera uma senha de admin aleatória de 8 caracteres, imprime uma vez no console USB e exige a troca no primeiro login.
- **Limites contra força bruta:**
  - bloqueio de login de 2 s a 300 s por cliente, com `429` quando todos os slots de bloqueio estão ocupados;
  - limite de taxa por IP nas rotas pesadas;
  - o console Bluetooth tem bloqueio exponencial próprio e para de se anunciar 5 minutos depois do boot.
- **Sessões** — cookie `HttpOnly; SameSite=Strict` (`Secure` no HTTPS), ou token Bearer.
- **Uploads** — path traversal, percent-encoding, bytes de controle e nomes reservados são recusados, e `/config` fica fora do alcance do gerenciador de arquivos.
- **HTTPS opcional** (`release`) — o par de certificado é instalado com `POST /api/tls`. TLS 1.2, ECDHE com AES-GCM.
- **Auditorias** — as auditorias de 16/08/2026, da v2.3.6-beta e de 07/09/2026 estão fechadas. O último achado, V-09 (uma conta restrita podia criar outra com mais bits do que tinha), foi corrigido na v2.7.0, e tanto ele quanto as correções de 07/09 foram verificados no hardware. Veja **[SECURITY.md](SECURITY.md)**.

### Resiliência e forense
- **Autópsia de travamento a cada boot** — os registradores de scratch do watchdog dizem qual módulo travou em cada núcleo. Desde a v2.7.0, mais três registros também guardam o módulo do Core 1, o heap livre e o uptime no momento do travamento.
- **Disciplina de flash entre os núcleos** — o Core 1 é pausado em toda escrita na flash (medido, não presumido).
- **Disciplina de watchdog** — o watchdog é alimentado em toda operação de arquivo, então clientes HTTP lentos não travam o laço.

### Atualização, backup e recuperação
- **OTA pela página web:**
  - só admin;
  - a imagem é conferida antes de ser gravada (tamanho, CRC do boot2, variante da imagem) e de novo no boot seguinte;
  - Wi-Fi, contas e slots de sensor atravessam a atualização, e o resto do sistema de arquivos é reformatado, por isso a página baixa um backup antes;
  - o aparelho volta em menos de um minuto: 52–56 s na campanha da v2.7.0.
- **Backup e restauração** — o sistema de arquivos inteiro num arquivo, com CRC32 e preso ao chip.
- **[Guia de recuperação](docs/RECOVERY.md)** — caminhos por BOOTSEL, picotool e 1200 bps para todo modo de falha.

### SIMUT Air (experimental)
- **Dois modos, sem display e sem buzzer:**
  - **M0** é o acordado: web, console, Bluetooth e sensores;
  - **M1** é o ciclo: dorme pelo alarme do RTC, acorda, lê, grava o histórico e dorme de novo.
- **O rádio só quando compensa** — ele só liga quando há `t_int` registros esperando. Um wake de leitura leva 9,31 s com um DS18B20, e com intervalo de 60 s o aparelho fica acordado cerca de 13 % do tempo.
- **Pino do carregador** — um pino de detecção do carregador (GP17 por padrão) o mantém acordado enquanto está na tomada.
- **Console completo** — é a única imagem publicada com o console completo.

### Internacionalização
- **3 idiomas de interface** — inglês embutido; português (pt-BR) e espanhol (es-ES) vêm como packs `.lng` no sistema de arquivos. Um aparelho roda o inglês mais o pack instalado.

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

# SÓ na primeira gravação: subir os dados da LittleFS (packs de idioma, temas, favicon).
# ⚠️ uploadfs REFORMATA a partição LittleFS — num dispositivo já em uso ele
# destrói histórico, config e calibração. Nunca repita depois que o
# dispositivo tiver dados; packs de idioma podem subir depois pelo
# gerenciador de arquivos da web. Ele copia OS DOIS packs de idioma, e o
# aparelho carrega o primeiro em ordem alfabética (es-ES): apague o outro.
pio run -e pico_w_release -t uploadfs
```

Prefere não compilar? Todo [release](https://github.com/angeloINTJ/simut/releases/latest) traz `simut_vX.Y.Z_release.uf2`, `_alpha.uf2` e `_air.uf2` (arrastar e soltar com BOOTSEL segurado), o `.bin` correspondente para atualização pelo ar e os packs de idioma pt-BR e es-ES.

### Primeiro boot
1. **Anote a senha do admin.** Uma unidade recém-saída de fábrica imprime uma senha de admin aleatória de 8 caracteres **uma única vez no console serial USB** (115200 baud). Ela nunca é gravada em texto puro. Se você perder, `system admin reset confirm` pela USB imprime uma nova.
2. **Coloque o aparelho na sua rede.** Uma unidade sem rede configurada abre sozinha o ponto de acesso de configuração. O Air não abre: digite `ap` no console dele.
   - Conecte-se a `<nome>_SETUP` (`simut_SETUP` de fábrica). É WPA2, e a chave por aparelho é impressa no console USB e no terminal de boot do TFT — no boot e, desde a v2.7.2, também quando o AP abre em operação. No alpha, leia a chave no console USB ou na resposta do comando `ap`: pelo código da v2.7.2, o LCD não chega às páginas do AP.
   - O portal abre em `http://192.168.4.1`.
   - Enquanto o ponto de acesso está no ar, o aparelho não mede: na v2.7.2 ele não lê sensores, não confere alarmes e não grava histórico até entrar numa rede.

   Sem tela, dá para usar o console: `system ssid <nome>`, `system pass <senha>` e depois `reload confirm`. O console corta no primeiro espaço: rede ou senha com espaço só pela página web.
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
├── test/                   # Testes unitários nativos (Unity), sete suítes
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
| `pico_w_release` | Imagem de produção para o painel TFT: console de emergência, servidor HTTPS, mDNS | ✅ `…_release` |
| `pico_w_alpha` | LCD de caracteres 16×2 (HD44780), sem touch; console de emergência + console Bluetooth | ✅ `…_alpha` |
| `pico_w_air` | **Experimental** — SIMUT Air: sem display, sem buzzer, ciclo de hibernação (M0 acordado / M1 acorda-lê-envia-dorme); console completo + Bluetooth. Ver o §17 do [Manual do Usuário](docs/MANUAL.pt-BR.md) | ✅ `…_air` |
| `pico_w_test` | Imagem de bancada: o console completo para as suítes de teste; sem HTTPS, sem mDNS | — |
| `pico_w_test_https` | `pico_w_test` mais o servidor HTTPS, para validar TLS; três das páginas vêm da LittleFS para caber | — |
| `pico_w_asserts` | Release + asserções de concorrência | — |
| sete ambientes `native*` | Testes unitários no host — ver [Testes](#testes) | — |

> **Nota de segurança para `pico_w_alpha` e `pico_w_air`:** as duas compilam o
> console Bluetooth SPP (`SIMUT_BLUETOOTH=1`), então nessas duas imagens ele é
> superfície de ataque real. Ele é autenticado pela **senha do admin da web**,
> com bloqueio exponencial que sobrevive a uma reconexão, e janela de descoberta
> que fecha 5 minutos depois do boot. Os comandos de recuperação são restritos à USB.
>
> O ponto de acesso de configuração é WPA2 em todas as imagens, com chave por
> aparelho mostrada no console e, onde houver, no display. Ver [SECURITY.md](SECURITY.md) §2 e §8.

> Não há ambiente de depuração. O `pico_w_debug` foi removido na v2.4.1 depois de nunca ter linkado: em `-Og` a imagem estourava o slot de 1020 KB em ~100 KB. A flash é apertada. A imagem release usa 97,2 % do slot de programa de 1.044.480 B, e o `.bin` dela fica 13.196 B abaixo do teto de atualização pelo ar, de 1.040.384 B. Um alvo de GDB teria de ser montado cortando funcionalidades. Para o tripwire de concorrência no hardware, use `pico_w_asserts`.

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

- **O console de emergência** roda nas imagens `release` e `alpha`. Os 14 comandos dele:
  - `show net status`, `show system info`, `show system log`
  - `debug on|off`
  - `system admin reset`, `system format`, `system factory`, `system https off`
  - `system ssid <nome>`, `system pass <senha>`, `system cors <origem|off>`
  - `ap`, `reload`, `help`

  Os comandos destrutivos pedem `confirm`, e as quatro recuperações (`system factory`, `system format`, `system admin reset`, `system https off`) são recusadas pelo Bluetooth.
- **O console completo estilo Cisco** (`enable` / `configure terminal`) roda na imagem `air` e nas imagens de bancada `pico_w_test` — veja o [Manual do CLI](docs/CLI-Manual.md). O Air acrescenta `air status | hibernate | stop | idle <seg> | charger <gpio|off>`.

**Onde a configuração acontece:**
- **A interface web** é a ferramenta do dia a dia.
- **O painel touch** cobre o que o operador precisa no aparelho: temas, alarmes, sons, idioma, o próprio PIN, usuários, a política de PIN, calibração do toque, ajuste da tela, status e o ponto de acesso de configuração.

### API Web
O aparelho expõe uma API REST em `http://<ip-do-dispositivo>/api/`:
- **61 rotas** — 51 protegidas por permissão, 10 públicas por projeto, nenhuma sem proteção, conferidas por `tools/check_authz.py` no CI;
- a tabela de rotas está no [Manual do Usuário](docs/MANUAL.pt-BR.md);
- o [docs/AUTHORIZATION.md](docs/AUTHORIZATION.md) liga cada rota à sua permissão;
- o [docs/API_POST.md](docs/API_POST.md) documenta os corpos dos POST.

## Testes

### Testes no host

```bash
pio test -e native             # validadores, cursor de telemetria, rótulos, parsers (185 casos)
pio test -e native_history_v5  # codec do histórico V5 (63)
pio test -e native_cli         # parser do CLI (31)
pio test -e native_logpolicy   # persistência de log por transição (45)
pio test -e native_alarmqueue  # fila da telemetria de alarmes (39)
pio test -e native_network     # máquina de estados da reconexão Wi-Fi (29)
pio test -e native_air         # config persistente do SIMUT Air (16)

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
- **firmware** — as seis imagens, compiladas com o cache frio:
  - cada uma é conferida contra o seu orçamento de flash e o teto de atualização pelo ar;
  - o próprio build aplica o `-Werror` e os portões da interface web, da ajuda do CLI, dos códigos de log, da tabela de canais e dos packs de idioma.
- **fuzz** — 60 s de libFuzzer contra os validadores da API web, com oráculos de contrato.
- **análise estática** — cppcheck, em versão fixa.

A `main` é protegida: oito dessas checagens precisam passar antes de qualquer merge.

### Verificação no hardware

**A bancada:**
- um Pico W com o painel TFT e o touch;
- um segundo Pico, a *PicoHand*, que aciona as linhas de RESET e BOOTSEL do alvo, cronometra a linha de acordado/dormindo e finge um carregador (ver o [AGENTS.md](AGENTS.md));
- suítes de bancada em `tools/` para a API web, o painel, a telemetria, a OTA, quedas de Wi-Fi e o ciclo do Air.

O que foi medido no hardware real, do mais recente ao mais antigo:

| Data | O quê | Resultado |
|---|---|---|
| 24/09/2026 | Painel: Segurança do PIN e Modo de Configuração (v2.7.2) | As setas do rodapé ficam na tela (a v2.7.1 a fechava); um toque não salvo não muda mais a política gravada; o Confirmar mostra a rede, a chave e 192.168.4.1 (a v2.7.1 ficava na confirmação, com o AP já no ar) |
| 23/09/2026 | Relógio do Air através do sono (v2.7.2) | Carimbos entre −0,085 e +0,030 s em 10 wakes (a v2.7.1 perdia 0,8 s por wake); a correção do NTP caiu de 9–10 s para 0,08 s |
| 23/09/2026 | Filas longas de telemetria no Air (v2.7.2) | 0 corpos inválidos; 13.681 de 13.682 registros entregues acordado, 13.670 de 13.671 hibernando (v2.7.1: 68 de 69 corpos eram JSON inválido) |
| 22/09/2026 | Soak da v2.7.0 | 8,18 h, 0 reboots; o maior bloco livre do heap variou −42 B |
| 22/09/2026 | Atualizações pelo ar da v2.7.0 | 6 de 6 aplicadas; 57 arquivos restaurados, 0 registros faltando |
| 22/09/2026 | Ponto de acesso de configuração (v2.7.1) | Um cliente entra em 4,1 s, no `release` e no `alpha` com Bluetooth ligado, também com MAC aleatório. O fallback automático abre depois de 6–7 min sem rede |
| 22/09/2026 | Correção do V-09 | 10 de 10 vereditos, com controles positivos |
| 21/09/2026 | Coletor fora do ar por 3 h 58 min | 237 registros na fila, 0 reboots; drenados numa rodada com 0 faltando, mais 25 registros da linha de alarmes |
| 21/09/2026 | Suítes web | 67/67 como admin, 87/87 como conta restrita; 500 commits que gravam na flash, 0 reboots |
| 21/09/2026 | Busca de redes Wi-Fi | 18 de 18, 0,94 s por varredura, também de dentro do ponto de acesso |
| 20/09/2026 | Contas, PINs e política no painel | 32/32 |
| 19/09/2026 | Espelho do painel | 613 → 213 ms por quadro; idêntico ao framebuffer, pixel a pixel (0 de 76.800 diferentes) |
| 11/09/2026 | Queda de energia durante a atualização | Só a janela de aplicação, de ~25 s, deixa o aparelho precisando de BOOTSEL |
| 10/08/2026 | Histórico através de resets | 10 de 10 resets de hardware e 10 de 10 reboots perderam 0 registros |

O LCD 16×2 é a única saída não validada na tela de verdade. A bancada não tem HD44780, então os testes no host conferem o que ele recebe.

## Documentação

| Documento | Descrição |
|----------|-------------|
| [Manual do Usuário](docs/MANUAL.pt-BR.md) | Montagem, display/web/console, OTA, referência da API, solução de problemas — mantido atualizado |
| [User Manual (EN)](docs/MANUAL.md) | O mesmo manual, em inglês |
| [Manual completo](docs/MANUAL.pt-BR.html) | O manual do produto, atualizado para a v2.7.2: 31 capítulos sobre instalação, configuração, uso no dia a dia e integração com servidores. As telas estão sendo recapturadas; cada uma que falta está marcada no lugar dela |
| [Guia de Fiação](docs/WIRING.md) | Pinagem completa e diagramas de ligação |
| [Atualização pelo ar](docs/OTA_USAGE.md) | Atualizar pela página web, e o que sobrevive |
| [Guia de Recuperação](docs/RECOVERY.md) | Recuperação de brick — BOOTSEL, picotool, reset 1200 bps |
| [Manual do CLI](docs/CLI-Manual.md) | Referência completa do console, inclusive o do Air |
| [Matriz de autorização](docs/AUTHORIZATION.md) | Cada rota HTTP e a permissão que ela exige |
| [Política de Segurança](SECURITY.md) | Modelo de ameaças, tratamento de credenciais, resposta a incidentes |
| [Índice da documentação](docs/README.md) | Quais documentos são mantidos atualizados e quais são retratos de uma época |
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
