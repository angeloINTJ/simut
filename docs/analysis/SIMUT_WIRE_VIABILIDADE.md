# SIMUT Wire — viabilidade (Pico sem rádio, rede por ENC28J60)

> **Estado:** estudo; nenhuma linha de produto escrita. Branch `study/simut-wire`,
> base `main` 900eb0d (v2.7.4). Builds medidas em 26/09/2026 numa worktree
> descartável. **Nada foi gravado na bancada**: a gravação do protótipo no alvo
> foi negada pela permissão do ambiente e ficou como passo proposto (§11).
> **Pedido:** o SIMUT release num Raspberry Pi Pico sem Wi-Fi, com rede por um
> módulo ENC28J60, e o que aproveitar do fork `angeloINTJ/pico-enc28j60`.
> **Idioma:** pt-BR.

---

## 1. Veredito

**Viável.** Tudo o que o release faz por IP continua: servidor web HTTP e HTTPS,
telemetria nos quatro transportes, syslog, `/metrics`, descoberta do Home
Assistant, mDNS, NTP e OTA pela web; e, fora da rede, painel TFT, sensores,
histórico, alarmes e CLI. A rede do SIMUT já é a lwIP do arduino-pico, e o
arduino-pico já liga um ENC28J60 a ela como mais uma interface
(`LwipIntfDev<ENC28J60>`). Sai o que só existe com rádio: ponto de acesso, busca
de redes e RSSI.

Medido contra a v2.7.4, com a mesma toolchain:

| | release (Pico W) | Wire (protótipo) |
|---|---:|---:|
| `firmware.bin` | 1.010.012 B | **770.744 B** (−239.268 B) |
| folga até o teto da OTA (1.040.384 B) | 30.372 B | **269.640 B** |
| RAM estática (linha do PlatformIO) | 116.920 B | 113.556 B (−3.364 B) |

A release inteira compila e linka para `board = rpipico` mudando **uma linha**: a
chamada `cyw43_arch_enable_sta_mode( )` da busca de redes.

Quatro condições decidem o projeto:

1. **O driver do ENC28J60 tem de ser nosso.** O do arduino-pico, igual no upstream
   6.1.1, ignora o SPI que recebe e fala sempre no spi0, que é o barramento do TFT.
   Também não trata duas erratas do silício atual e espera sem prazo em três laços.
   O código do fork não encaixa no SIMUT (§6, §7).
2. **O código de Wi-Fi tem de sair da imagem, não só ficar inerte.** Sem rádio, o
   stub do framework leva o boot ao modo AP, e com o AP "aberto" o laço principal
   pula telemetria, syslog e histórico (§5).
3. **Pinos:** quatro dos 16 slots de sensor (GP12–GP15) e o GP21.
4. **Energia:** o ENC28J60 consome 120 mA parado e até 180 mA transmitindo, e o
   pino 3V3 do Pico deve ficar abaixo de 300 mA no total. O módulo precisa de
   regulador próprio.

Do fork aproveita-se o conhecimento, não o código (§6).

---

## 2. As builds

Mesma árvore (900eb0d), arduino-pico 5.6.1 com os overrides de
`tools/arduino_pico_overrides/`, `-Os`, `-Werror` em `src/`:

| imagem | `.bin` | `.text` | `.rodata` | `.bss` | RAM (PlatformIO) |
|---|---:|---:|---:|---:|---:|
| `pico_w_release` v2.7.4 | 1.010.012 | 585.032 | 405.048 | 109.276 | 116.920 |
| Pico, sem placa de rede | 766.152 | — | — | — | 113.356 |
| Pico + ENC28J60 | 770.744 | 572.576 | 178.348 | 106.020 | 113.556 |
| Pico + W5500 (comparação) | 770.216 | — | — | — | 113.540 |

- Do que sai, **225.240 B são o firmware do rádio** (`w43439A0_7_95_49_00_combined`,
  em `.rodata`). Os 175 símbolos com `cyw43` ou `w43439` no nome somam 240.230 B de
  flash e 2.605 B de RAM, dos quais 2.504 B são o `cyw43_state`.
- O driver ENC28J60, o `LwipIntfDev` e o contexto assíncrono da Ethernet custam
  **4.592 B**; o W5500 custa 528 B a menos. A reconstrução do ENC28J60 depois do
  W5500 deu de novo 770.744 B.
- O protótipo ainda carrega o código de Wi-Fi (máquina de estados, AP, busca),
  compilado contra o stub. A Wire de verdade tira esse código, o que devolve flash,
  e ganha a superfície cabeada, o que custa flash. O número final fica perto deste,
  não igual.

Para reproduzir, use uma worktree: um ambiente novo no projeto muda o checksum e
apaga o `.pio/build` de todos os ambientes. O bloco usado, que é o
`pico_w_release` sem `-DPICO_CYW43_SUPPORTED=1`:

```ini
[env:pico_wire_proto]
extends = pico_base
board = rpipico
build_type = release
build_flags =
    -Os
    -fmerge-all-constants
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-format-truncation
    -Wno-implicit-fallthrough
    -I src
    -specs=nano.specs
    -Wl,-u,_printf_float
    -DNDEBUG
    -DSIMUT_CLI_FULL=0
    -DSIMUT_WEB_HTTPS=1
build_src_filter = ${pico_base.build_src_filter}
    -<DisplayManager_Alpha.cpp>
    -<ConcurrencyAsserts.cpp>
    -<BluetoothManager.cpp>
lib_ignore =
    SerialBT
```

Além dele:

- `#if defined(PICO_CYW43_SUPPORTED)` em volta de `NetworkManager.cpp:266`. Sem a
  guarda, o link falha: a chamada puxa o `cyw43_ctrl.c.o` da `liblwip.a`, que
  exige os `__wrap_cyw43_cb_*` que só a placa W fornece.
- Uma unidade de sondagem que instancia `ENC28J60lwIP eth(13, SPI1, 21)` e chama
  `begin( )` a partir do `NetworkManager::begin( )`, para o linker não descartar o
  driver.

---

## 3. O que funciona igual sobre Ethernet

A lwIP não sabe qual é a interface. Continua valendo, sem mudança:

- o servidor web (`WebServer`/`WebServerSecure`), inclusive upload e OTA, que
  passam por ele (`WebManager_Core.cpp:188-206`, `:263-293`);
- a telemetria: `HTTPClient` sobre `WiFiClient`/`WiFiClientSecure` (BearSSL) e
  `PubSubClient`; o socket UDP do syslog;
- o mDNS (o LEAmDNS acompanha todas as interfaces) e o NTP (SNTP da lwIP);
- os onze patches de `tools/arduino_pico_overrides/`, nenhum ligado à interface;
- a identidade de segurança: uid, serial, chave do AP, chave XOR da config e o id
  do backup vêm de `pico_get_unique_board_id`. O MAC só entra no client id do MQTT
  e no id do nó do HA (§4);
- a senha inicial do admin pelo console USB e a troca forçada no primeiro login;
- painel, sensores, histórico, alarmes e buzzer, que não tocam na rede.

---

## 4. O que precisa de um equivalente cabeado

| onde | hoje | na Wire |
|---|---|---|
| estado da rede (`NetworkManager`) | a metade IP/NTP é genérica (`NetworkManager.cpp:452-523`); as transições são chamadas de Wi-Fi | enlace pelo PHY (`linkStatus( )`): o `status( )` do `LwipIntfDev` só diz "tem IPv4" e não vê cabo desligado. IP fixo configurado antes do `begin( )` |
| saúde da rede | `isNetworkHealthy( )` exige RSSI > −78 dBm (`NetworkManager.cpp:822-824`); a telemetria depende dele, e o syslog depende de `WiFi.status( )` | enlace + IP. Sem isso, o RSSI 0 do stub vira −100 e **a telemetria nunca envia** |
| cadência da telemetria | RSSI fraco alarga o intervalo em 1,5× (`TelemetryManager.cpp:533-541`) | sai |
| MAC (6 consumidores) | `WiFi.macAddress( )`: painel, `/api/network`, `/api/status`, client id do MQTT (`simut_` + 6 hex, que vira o id do nó do HA), tokens `{MAC}` | ler da interface cabeada. Com o stub, `WiFi.macAddress( )` formata um vetor não inicializado (`WiFiClass.h:259-264`): client id e nó do HA mudariam a cada boot |
| nome no DHCP | nunca definido; sai "PicoW", o default do framework (`LwipIntf.cpp:51`) | o `deviceName` |
| RSSI na superfície | `/api/status` (`rssi`, `metr.wf`), `/metrics` (`simut_wifi_*`), ícone de 4 barras e página de status do painel, bloco "WiFi Signal" do painel web, `show net status` | estado do enlace |
| variante da OTA | `SIMUT_ENV_NAME` é "release" para todo build TFT (`simut_config.h:388-396`), e a OTA só recusa por variante (`ota/validation.cpp:92-126`) | variante própria. Hoje uma imagem de Pico W passaria num Wire, e o contrário; nos dois casos o aparelho sobe **sem rede** |
| IP fixo sem AP | `ip` e `dns` só existem na CLI completa (`CommandParser.cpp:201-237`); o release traz o console de emergência | decisão (§12): numa LAN sem DHCP, hoje não haveria como entrar |
| espera do boot | até 30 s esperando rede e hora (`AppManager_Boot.cpp:1118-1183`) | esperar pelo enlace; sem cabo, seguir |
| manifesto e CI | `board` e `PICO_CYW43_SUPPORTED` fixos no `pico_base`; `check_features.py:238-242` só reconhece `env:pico_w_*`; o CI lista os seis; `custom_web_omit` só conhece `tft` | escolha nova no `features.toml` (placa e rede), com portões e orçamento de flash |
| códigos de log | só de Wi-Fi: 10, 11, 12, 15, 403, 405, 522, 523, 525, 526 | enlace ligado e desligado |
| packs de idioma | strings de Wi-Fi em `@DICT`, `@LOGCODES`, `@TRL`, `@HELP` e `@WEBDICT` | strings novas custam espaço; o es-ES tem ~110 B de folga nas seções residentes (`NetworkManager.cpp:279`) |
| testes | `native_network`: 29 casos, todos da escada de reconexão, da busca e do AP, sobre o `FakeWiFiClass`; o estado de NTP sem caso nativo | stub cabeado e casos de enlace (§11) |

---

## 5. O que sai, e por que tirar da imagem

Só existe com rádio:

- o modo AP inteiro: `beginAP`, portal cativo com `DNSServer`, chave WPA2 derivada
  do id da placa, SSID `<nome>_SETUP`, reboot aos 15 min, `SIMUT_AP_OPEN`;
- as cinco portas de entrada nele: boot sem SSID, gesto no painel, item "Modo de
  configuração", comando `ap` e a queda da escada de reconexão;
- a busca de redes: `/api/wifi/scan`, o bloco da página `/network` e 432 B de RAM de
  resultados;
- a escada de reconexão (varredura, backoff, dormência) e o detector de RSSI
  implausível;
- o ciclo de energia do CYW43 pelo GP23 no boot (600 ms);
- o `WiFi.end( )` e a marca de ciclo do rádio na OTA;
- `system ssid|pass` e `wifi ssid|pass` na CLI.

**Deixar isso compilado contra o stub não é inofensivo.** O stub do framework
(`lwIP_nodriver.h`) responde `WL_NO_MODULE`, RSSI 0 e busca vazia; o `softAP( )`
devolve falso, e o stub não escreve o MAC. Na prática:

- a config de fábrica tem SSID vazio (`StorageManager.cpp:494`), e SSID vazio
  força o modo AP (`AppManager_Boot.cpp:1063-1069`);
- o `beginAP` falha, o retorno é ignorado e `_isApMode` fica verdadeiro;
- com `_isApMode`, o laço principal retorna antes do syslog, da telemetria, do
  autorreparo dos sensores e do histórico (`AppManager_Loop.cpp:258-261`). É o
  defeito conhecido do AP aberto, só que permanente;
- o painel mostra um SSID e uma chave de AP que não estão no ar;
- no Pico comum o GP23 é o pino de modo do regulador, e o boot o deixa como
  entrada sem pull.

---

## 6. O fork `angeloINTJ/pico-enc28j60`

É um fork de `ljanyst/pico-enc28j60` (MIT) **idêntico ao original**: 0 commits à
frente, 0 atrás, e o último é de 22/06/2025. É C para o pico-sdk, com CMake, em duas
camadas: um driver base (SPI por PIO, buffer de comandos, DMA) e uma cola para a
pilha **FreeRTOS+TCP**.

### Por que o código não encaixa

1. **Pilha.** A cola é para FreeRTOS+TCP. O SIMUT é arduino-pico sem RTOS, com
   lwIP, e o papel da cola aqui já é do `LwipIntfDev`.
2. **PIO.** O programa SPI tem 17 instruções e usa uma state machine. Na Wire
   sobram 5 instruções no pio0 (o 1-Wire do DS18B20 usa 27) e 11 no pio1 (buzzer 4
   + DHT22 17); o rádio só devolve as 6 do programa dele. **Não cabe** sem tirar uma
   família de sensores.
3. **Relógio.** O autor fixou o SPI em 3,5 MHz, com 10 ciclos de PIO por bit, porque
   acima disso os acessos a MAC, MII e PHY falhavam na placa dele. A errata 1 (MAC
   instável abaixo de 8 MHz) só vale para as revisões B1 e B4, então a B7 aceita,
   mas o barramento fica ~4× mais lento que o SPI de hardware.
4. **O PIO não compra nada aqui.** O spi1 está livre na Wire e não gasta PIO. Os
   pinos que ele aceita fora do TFT estão em GP8–GP15, dentro dos slots de sensor,
   e qualquer arranjo, com PIO ou sem, custa quatro slots mais o GP21.
5. **Erratas sem tratamento:**
   - 6: decide a recepção pelo PKTIF;
   - 12: não reinicia a lógica de transmissão;
   - 13: não olha o TXERIF, então um quadro abortado fica "em transmissão" para
     sempre e a interface para de transmitir;
   - 14: escreve ERXRDPT par na recepção e no descarte;
   - 16: nunca programa `PHCON1.PDPXMD`.
6. **Defeitos:**
   - `enc28j60_frame_rx_info_blk` sobrescreve o resultado e perde o ponteiro do
     próximo quadro, então a recepção bloqueante não funciona;
   - `gpio_set_irq_enabled_with_callback` troca o callback global de GPIO, o mesmo
     que o `attachInterrupt` do arduino-pico usa. Conforme a ordem, perde-se o IRQ
     do toque (GP20) ou o da placa de rede;
   - o MAC padrão é fixo (`a2:32:15:cc:72:0a`);
   - `enc28j60_deinit( )` é vazio;
   - os laços de CLKRDY e de MISTAT.BUSY não têm prazo;
   - o objeto do driver é alocado com `sizeof(enc28j60_config)`.

### O que aproveitar: conhecimento

- **A inicialização em half duplex** (MACON3 sem FULDPX, MACON4.DEFER, MABBIPG
  0x12, MAIPGL/MAIPGH 0x12/0x0C, PHCON2.HDLDIS) e o motivo dela: o chip não negocia,
  e um switch com autonegociação diante dele assume 10 Mb/s half duplex.
- **A divisão RX/TX dos 8 KB do chip como parâmetro.** A Wire precisa de mais de
  4 KB de RX (§9).
- O anel de TX com mais de um quadro na área de transmissão, como otimização
  opcional.
- O buffer de comandos com DMA em lista. Serve de ideia para tirar o SPI da CPU,
  mas depende do PIO controlar o CS; com SPI de hardware, o DMA vale só para as
  rajadas de RBM e WBM, como no espelho do painel (`ESPELHO_DELTA.md` §11.4).
- O mapa de registradores, sob MIT.

---

## 7. O driver do arduino-pico

`lwIP_enc28j60` (EtherSia/Contiki via esp8266, licença BSD de 3 cláusulas) liga o
chip ao `LwipIntfDev`, que faz o resto: netif, ARP, DHCP, DNS, SNTP e IGMP. Está
**igual ao `master` do upstream** (release 6.1.1, de 21/09/2026). A última mudança
de comportamento no driver é de abril de 2024; a de abril de 2026 só trocou a
assinatura do `sendFrame`.

Acerta nisto:

- usa o EPKTCNT (errata 6), ERXRDPT ímpar (14) e RX começando em 0000h (5);
- espera 1 ms depois do reset (2) e roda o SPI acima de 8 MHz;
- o `lwipopts.h` do framework tem `LWIP_NETIF_TX_SINGLE_PBUF 1`, então enviar só o
  primeiro pbuf da cadeia é seguro;
- o MAC vem do id da placa, com o bit de administração local.

Erra nisto, em ordem de gravidade:

1. **Ignora o SPI que recebe.** O construtor guarda o `SPIClass&`, e todo acesso
   usa o `SPI` global, que é o spi0 do TFT. O `DisplayManager_Auth.cpp:138-141`
   registra que a leitura da GRAM por DMA só é legal porque "a lwIP nunca chega ao
   spi0". O driver do W5500, no mesmo framework, usa o objeto recebido.
2. **Laços sem prazo.** São três: `while ((readreg(ESTAT) & ESTAT_CLKRDY) == 0)` no
   reset, `while (readreg(ECON1) & ECON1_TXRTS)` a cada quadro enviado e
   `while (readreg(MISTAT) & MISTAT_BUSY)` a cada leitura do PHY, que o
   `isLinked( )` faz.
   - O terceiro lê o banco errado. O MISTAT é do banco 3, e o `phyread( )` está no
     banco 2, onde o endereço 0x0A é o `MAMXFLL` (0xEE depois do reset do driver).
     Com o chip presente, a espera não espera, e o estado do enlace pode vir da
     leitura anterior. Sem o chip e com o MISO em 0xFF, ela não termina.
   - Sem o módulo, os pads do RP2040 nascem com pull-down
     (`PADS_BANK0_GPIOx_PDE_RESET = 1`), e o `SPI.begin( )` só troca a função do
     pino. O MISO lê 0x00 e o primeiro laço nunca termina.
   - Isso roda no `setup( )`, e o watchdog só é armado no primeiro `loop( )`
     (`main.cpp`). **Um cabo solto trava o boot para sempre.** A conclusão vem do
     código e do reset dos pads, sem medição (§11).
3. **Erratas 12 e 13; a 13 afeta as revisões B5 e B7.** Não há TXRST antes do
   TXRTS nem leitura de TXERIF. Segundo a errata 13, um pulso de enlace pode passar
   por colisão tardia e deixar o TXRTS ligado para sempre. Nesse caso, o segundo
   laço acima prende o Core 0 dentro da lwIP até o watchdog.
4. **Duplex.** O driver liga `MACON3.FULDPX`, pausa e `MABBIPG 0x15`, que são
   valores de full duplex, e nunca programa `PHCON1.PDPXMD`.
   - O datasheet (§2.6) tira o PDPXMD da fiação do LED B: com o LED ligado ao
     3,3 V (o pino drena), full; ligado ao terra (o pino fornece), half; sem LED,
     indeterminado.
   - A errata 16 acrescenta que essa detecção pode errar e deixar o PDPXMD no
     estado errado.
   - Um switch com autonegociação diante de uma porta 10BASE-T que não negocia
     fica em half.
   - O provável é descasar o duplex, dentro do chip ou com o switch, e o tráfego
     nos dois sentidos colidir.
5. **Buffer de RX de 4 KB** (0000h–0FFFh), onde cabem dois quadros cheios (§9).
6. **SPI de um byte por chamada.** No espelho do painel essa chamada custou ~4,3 µs
   de software por pixel de 3 bytes (`ESPELHO_DELTA.md` §9.2). Estimativa: ~3 ms de
   Core 0 por quadro cheio, contra ~1,2 ms com `spi_read_blocking` e
   `spi_write_blocking`.
7. Não trata erro de recepção (EIR.RXERIF, ESTAT.BUFER).

### As erratas nos dois drivers

Da errata do silício (Microchip DS80349C), as que pesam:

| errata | revisões | arduino-pico 5.6.1 | fork |
|---|---|---|---|
| 1 — MAC instável com SPI < 8 MHz | B1, B4 | atende (≥ 8 MHz) | 3,5 MHz: só serve em B5 e B7 |
| 2 — CLKRDY cedo: esperar 1 ms | todas | atende | atende |
| 5 — RX tem de começar em 0000h | todas | atende | atende |
| 6 — PKTIF não é confiável | todas | atende (EPKTCNT) | **não** |
| 12 — aborto de TX trava a lógica de TX | todas | **não** | **não** |
| 13 — pulso de enlace vira colisão tardia | B5, B7 | **não** (laço sem prazo) | **não** (TX para) |
| 14 — ERXRDPT par corrompe o RX | todas | atende | **não** |
| 16 — polaridade do LED e duplex detectados errado | todas | **não** (PDPXMD solto, MAC em full) | **não** (PDPXMD solto) |

---

## 8. Hardware

### Pinos

Na release TFT, o único GPIO livre fora dos slots de sensor é o GP21
(`simut_config.h:23-34`). O spi0 é do TFT, e dividir o barramento não serve: o
Core 1 desenha por DMA, e a leitura da GRAM depende de exclusividade (§7, item 1).
O spi1 aceita SCK em GP10/14/26, TX em GP11/15/27 e RX em GP8/12/28, e GP26–GP28
são do TFT. Proposta:

| sinal | pino |
|---|---|
| RX (MISO) | GP12 |
| CS | GP13 |
| SCK | GP14 |
| TX (MOSI) | GP15 |
| INT | GP21 |
| RESET | sem pino livre: reset por comando, na sequência da errata 19 |

- Os slots de sensor caem de 16 para 12.
- A busca de sondas (GP0–GP16) e a validação de slot têm de pular esses pinos,
  como o `airPinValid( )` já recusa GP23/24/25/29.
- Sem o INT, a lwIP consulta o chip a cada 20 ms (`lwipPollingPeriod`, default em
  `LwipEthernet.cpp:208`): o GP21 fica livre, com mais latência.
- Na bancada, GP8 é o UART de `[BOOT step]` e GP10 tem um DHT22; GP12–GP15 e GP21
  estão livres.

### Energia

O ENC28J60 consome 120 mA ativo sem transmitir, e 160 mA típico, 180 mA máximo
transmitindo (DS39662E, características DC). O datasheet do Pico recomenda manter
a carga do pino 3V3 abaixo de 300 mA, e o RP2040, o backlight do TFT e os sensores
já estão nela. Alimente o módulo de VSYS/5 V pelo regulador dele (os módulos com
AMS1117 fazem isso), ou meça a corrente na placa antes de decidir.

### Enlace

10BASE-T, sem autonegociação. Half duplex programado explicitamente no MAC e no
PHY (§7, item 4).

### Alternativa: W5500

O encaixe é o mesmo: `LwipIntfDev`, com a biblioteca `lwIP_w5500` do arduino-pico
5.6.1. O driver dele já usa o `SPIClass` recebido, então vai no spi1 sem patch. Traz
100BASE-TX com autonegociação e 32 KB de buffer. No protótipo, 770.216 B (528 B a
menos). Não avaliei as erratas nem o consumo dele.

---

## 9. Memória da pilha

**O pool de pbufs muda de dono.**

- O CYW43 recebe em `PBUF_POOL` (24 entradas, 36.771 B de `.bss`), e a correção do
  D14 (`TCP_WND` 4×MSS) foi feita contra esse pool.
- O `LwipIntfDev` recebe cada quadro em `PBUF_RAM`, do heap da lwIP (`MEM_SIZE`,
  16.403 B). É o mesmo heap das cópias do TX (`TCP_SND_BUF` = 8×MSS = 11.680 B por
  conexão).
- Na Wire o pool fica ocioso e a pressão vai para o heap, então a conta do D14 não
  vale mais.
- Há duas saídas: o nosso driver receber em `PBUF_POOL`, ou redimensionar
  `MEM_SIZE` e `PBUF_POOL_SIZE` sob uma macro da Wire no `lwipopts.h` (a lwIP é
  compilada por ambiente; ver `patch.sh`). Escolher pelo `lwip_stats` medido.

**O buffer do chip contra as janelas sem IRQ.**

- Com 4 KB de RX cabem dois quadros cheios, e a janela de 4×MSS deixa o outro lado
  pôr quatro no ar.
- O SIMUT passa até ~60 ms com IRQ desligada a cada setor apagado (**59.724 µs**
  medidos, `SIMUT-Plano-Estabilidade-Concorrencia.md`).
- Nesse intervalo o CYW43 guarda os quadros; o ENC28J60 descarta o que não cabe, e
  o TCP só recupera por retransmissão. Isso acontece justamente durante histórico,
  log e o stage da OTA.
- Mitigação: RX de 0000h a 19FFh (6.656 B, quatro quadros), deixando 1.536 B para um
  quadro de TX, e/ou `TCP_WND` de 2×MSS na Wire. Medir o stage da OTA contra a
  Pico W.

---

## 10. Riscos

| risco | efeito | defesa |
|---|---|---|
| módulo ausente ou cabo solto no boot | boot travado para sempre (driver do framework) | laços com prazo; a rede é opcional e o boot segue, com log e aviso no painel |
| erratas 12 e 13 | Core 0 preso no envio, reboot pelo watchdog | TXRST antes de cada envio, espera limitada de TXIF/TXERIF, reenvio em colisão tardia |
| duplex descasado | a vazão despenca com tráfego nos dois sentidos | half duplex programado no MAC e no PHY |
| imagem da variante errada pela OTA | o aparelho sobe sem rede, só com USB | variante própria no `SIMUT_ENV_NAME` |
| código de Wi-Fi contra o stub | aparelho cego (modo AP permanente) | tirar da imagem, não só desligar |
| janelas sem IRQ | quadros perdidos, retransmissões na OTA | RX de 6,5 KB e/ou janela TCP menor; medir |
| orçamento do 3V3 | brown-out, reboots "sem causa" | regulador próprio do módulo |
| driver do framework usado como está | tráfego de rede no barramento do TFT | driver nosso, no spi1 |

---

## 11. Plano, com os testes antes

### P0 — a Wire no manifesto

- Escolha nova no `features.toml` (placa e rede) e perfil `pico_wire`.
- `check_features.py` e `gen_features.py` aceitando a placa; CI e orçamento de
  flash com a Wire.
- `SIMUT_ENV_NAME` próprio.
- O código de Wi-Fi fora da imagem (§5).

*Prova:* as seis imagens de hoje byte-idênticas (sha256), porque nada muda para a
Pico W; e `pico_wire` compila sem nenhuma chamada ao objeto `WiFi`.

### P1 — o driver

Um `RawDev` para o `LwipIntfDev`, em `src/net/`. Os casos ficam numa suíte nativa
nova, com um chip falso que decodifica os comandos SPI sobre um banco de
registradores:

- `begin( )` com o MISO lendo 0x00 ou 0xFF devolve falso dentro do limite, sem
  travar;
- depois do `begin( )`, `PHCON1.PDPXMD = 0` e `MACON3.FULDPX = 0`, mesmo com o chip
  falso "nascido" em full;
- todo envio liga e desliga o TXRST e limpa TXIF e TXERIF antes do TXRTS; um chip que
  nunca conclui dá erro dentro do limite;
- colisão tardia (TXERIF + bit 29 do vetor de status) leva a até 16 reenvios;
- o ERXRDPT é sempre ímpar, para todo ponteiro de próximo quadro, inclusive na
  volta do anel;
- a recepção é guiada pelo EPKTCNT, nunca pelo PKTIF; RXERIF drena e segue;
- o RX comporta quatro quadros cheios.

### P2 — rede cabeada e superfície

`NetworkManager` guiado pelo enlace; status, painel, `/network`, `/metrics`, HA,
códigos de log e packs. Na `native_network`, um stub cabeado ao lado do
`FakeWiFiClass`, com estes casos:

- enlace caído deixa a rede não saudável;
- enlace + DHCP chega a pronto;
- cabo puxado: telemetria e syslog param e voltam sem reboot;
- IP fixo;
- o estado de NTP, que hoje não tem caso.

### P3 — bancada

O alvo da bancada pode rodar a imagem Wire: é o mesmo RP2040, o rádio fica
desligado, e GP12–GP15 e GP21 estão livres. O único passo físico é ligar o módulo e
a alimentação dele. A conferir:

- boot sem módulo: sobe, registra, o painel avisa e a CLI responde;
- com módulo, contra a Pico W: tempo até o DHCP, ping, carga da UI web e do
  `/api/screenshot` (a maior resposta do aparelho), stage e apply da OTA;
- os quatro transportes de telemetria, syslog, mDNS, NTP e o handshake HTTPS;
- cabo puxado e devolvido: o enlace fica registrado e a rede volta sem reboot;
- `lwip_stats` (heap e pool) e a contagem de RXERIF durante a OTA e a gravação de
  histórico;
- carga de broadcast (~1.000 pacotes/s): o aparelho responde, sem watchdog;
- duplex: contadores da porta do switch, se for gerenciável, ou vazão nos dois
  sentidos ao mesmo tempo;
- 24 h de soak com zero reboots, e a corrente no 3V3 e no 5 V.

---

## 12. Decisões do mantenedor

1. **ENC28J60 ou W5500.** O encaixe é o mesmo; o W5500 traz 100 Mb/s com
   autonegociação e um driver que já usa o spi1.
2. **Doze slots de sensor:** GP12–GP15 para a rede, GP21 para o INT.
3. **Como se põe, e se desfaz, um IP fixo sem AP.** As opções:
   - o gesto do boot volta ao DHCP;
   - `ip` e `dns` entram no console de emergência;
   - a Wire leva a CLI completa, que cabe: 48.232 B (`tools/feature_savings.json`)
     contra 269.640 B de folga.
4. **O que fazer com os ~240 kB.** CLI completa, as páginas que hoje moram no
   LittleFS, ou guardar para uma OTA com dois slots, que disputa espaço com o
   LittleFS.
5. **Onde mora o driver.** Uma cópia nossa em `src/net/`, como os overrides do
   framework, e depois um PR no arduino-pico com as correções.
6. **O nome da variante**, que é o que a OTA compara.

---

## 13. O que este estudo não fez

- Não gravou nada no ferro: a gravação do protótipo na bancada foi negada pela
  permissão do ambiente. O travamento sem módulo (§7, item 2) é conclusão pelo
  código.
- Não ligou um ENC28J60. Vazão, CPU por quadro e consumo são estimativas.
- Não escreveu o driver nem mexeu no firmware da `main`.
