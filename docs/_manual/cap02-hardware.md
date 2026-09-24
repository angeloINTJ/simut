# Hardware e ligações {#cap-02}

Este capítulo descreve o Pico W e a alimentação, o mapa completo de pinos de cada imagem, os pinos que servem para sensores, a ligação de cada tipo de sensor e a placa de circuito do projeto. É para quem monta o aparelho. A configuração dos sensores depois de ligados está no [capítulo 6](#cap-06).

## O Pico W e a alimentação {#cap-02-pico}

O SIMUT roda num **Raspberry Pi Pico W**: o processador RP2040, 2 MB de memória flash e o chip de rádio CYW43, que faz o Wi-Fi e o Bluetooth. O firmware é compilado para o Pico W e depende do rádio dele: o Pico sem W não serve.

A alimentação vem pelo conector USB do Pico W, em 5 V. Dentro do aparelho:

| Ponto do Pico W | Pino | Alimenta |
|---|---|---|
| VBUS (5 V do USB) | 40 | O LCD da alpha |
| 3V3 (saída do regulador) | 36 | O TFT e o toque da release, e os sensores |
| GND | 3, 8, 13, 18, 23, 28, 38 | Terra comum de tudo |

Regras de montagem:

- **Terra comum.** Ligue o GND do Pico W, da tela e de todos os sensores entre si.
- **Sensores e TFT no 3V3.** Os sensores, os resistores de pull-up e o TFT vão no 3V3, o pino 36.
- **LCD no 5 V.** O LCD 16×2 da alpha vai no 5 V (VBUS, pino 40), não no 3V3.
- **Cabo USB de qualidade.** O guia de ligações do projeto pede cabo de pelo menos 22 AWG: queda de tensão no cabo causa reinícios.

Os GPIOs 23, 24, 25 e 29 não saem na borda da placa: o Pico W os usa internamente para falar com o chip de rádio.

## O mapa de pinos {#cap-02-mapa}

Cada imagem usa os pinos de GP16 em diante para a tela, o toque, a cigarra e, no Air, para o controle de energia. Os pinos de GP0 a GP15 ficam para os sensores nas três imagens.

| GPIO | Pino | release | alpha | Air |
|---|---|---|---|---|
| GP0 a GP15 | 1, 2, 4–7, 9–12, 14–17, 19, 20 | Slots de sensor | Slots de sensor | Slots de sensor |
| GP16 | 21 | MISO do SPI0 (TFT e toque) | RS do LCD | Energia dos sensores e sinal de acordado (saída) |
| GP17 | 22 | CS do toque | EN do LCD | Carregador (entrada) |
| GP18 | 24 | SCK do SPI0 (TFT e toque) | D4 do LCD | Sem uso |
| GP19 | 25 | MOSI do SPI0 (TFT e toque) | D5 do LCD | Sem uso |
| GP20 | 26 | IRQ do toque | D6 do LCD | Sem uso |
| GP21 | 27 | Sem uso | D7 do LCD | Sem uso |
| GP22 | 29 | Cigarra | Cigarra | Sem uso |
| GP26 | 31 | RST do TFT | Sem uso | Sem uso |
| GP27 | 32 | DC do TFT | Sem uso | Sem uso |
| GP28 | 34 | CS do TFT | Sem uso | Sem uso |
| GP23, GP24, GP25, GP29 | — | Rádio | Rádio | Rádio |

"Sem uso" quer dizer que o firmware não usa o pino. Ele também não serve para sensor: um slot só aceita GP0 a GP15 ([Onde ligar os sensores](#cap-02-livres)).

A numeração física segue a do Pico W: o pino 1 é o GP0, no canto do conector USB, e os pinos seguem em U até o 40, o VBUS, do outro lado do mesmo canto.

::: {.figura #fig-02-pinos-release tipo="diagrama" arquivo="02-pinos-release.png" captura="vista de cima do Pico W com os 40 pinos numerados; GP0–GP15 marcados como SLOT 0–15; GP16 MISO, GP17 T_CS, GP18 SCK, GP19 MOSI, GP20 T_IRQ, GP22 cigarra, GP26 RST, GP27 DC, GP28 CS; 3V3 (36) para TFT e sensores, VBUS (40), GNDs destacados; GP23/24/25/29 em cinza como internos do rádio"}
O mapa de pinos da release: sensores à esquerda, tela, toque e cigarra à direita.
:::

::: {.figura #fig-02-pinos-alpha tipo="diagrama" arquivo="02-pinos-alpha.png" captura="vista de cima do Pico W; GP0–GP15 como SLOT 0–15; GP16 RS, GP17 EN, GP18 D4, GP19 D5, GP20 D6, GP21 D7 do LCD; GP22 cigarra; VBUS (40) para o LCD, 3V3 (36) para os sensores; GP26–GP28 sem uso"}
O mapa de pinos da alpha: o LCD em GP16 a GP21, a cigarra em GP22.
:::

::: {.figura #fig-02-pinos-air tipo="diagrama" arquivo="02-pinos-air.png" captura="vista de cima do Pico W; GP0–GP15 como SLOT 0–15; GP16 saída de energia dos sensores e sinal de acordado; GP17 entrada do carregador; demais pinos de GP18 em diante sem uso"}
O mapa de pinos do Air: só GP16 e GP17 além dos sensores.
:::

### A release: TFT e toque {#cap-02-tft}

[release]{.img} O painel é um TFT de 320×240 com controlador ILI9341 e toque resistivo com controlador XPT2046, no mesmo barramento SPI0. Os dois compartilham MISO, MOSI e SCK e têm cada um o seu CS.

| Pino do módulo | Função | Liga em |
|---|---|---|
| VCC | Alimentação | 3V3 (pino 36) |
| GND | Terra | GND |
| CS | Seleção do TFT | GP28 (pino 34) |
| RESET | Reinício do TFT | GP26 (pino 31) |
| DC | Dado ou comando | GP27 (pino 32) |
| SDI (MOSI) | Dados para o TFT | GP19 (pino 25) |
| SCK | Relógio | GP18 (pino 24) |
| LED | Luz de fundo | 3V3 (pino 36) |
| SDO (MISO) | Dados do TFT | GP16 (pino 21) |
| T_CLK | Relógio do toque | GP18 (pino 24) |
| T_CS | Seleção do toque | GP17 (pino 22) |
| T_DIN | Dados para o toque | GP19 (pino 25) |
| T_DO | Dados do toque | GP16 (pino 21) |
| T_IRQ | Aviso de toque | GP20 (pino 26) |

O firmware não controla a luz de fundo: com o pino LED no 3V3, ela fica sempre acesa.

::: atencao
**Fios curtos no TFT.** A imagem publicada escreve no TFT a 62,5 MHz e lê a tela de volta, para o espelho da web, a 12 MHz. Fios longos ou ruins aparecem como pontos soltos em áreas lisas da tela ou do espelho. Encurte a fiação. Se não resolver, a saída é compilar o firmware com uma frequência menor ([capítulo 3](#cap-03-fonte)).
:::

### A alpha: LCD 16×2 {#cap-02-lcd}

[alpha]{.img} A alpha usa um LCD de 16×2 caracteres com controlador HD44780, ligado em modo paralelo de 4 bits.

| Pino do LCD | Símbolo | Liga em |
|---|---|---|
| 1 | VSS | GND |
| 2 | VDD | 5 V (VBUS, pino 40) |
| 3 | VO | Cursor de um potenciômetro de 10 kΩ entre 5 V e GND (contraste) |
| 4 | RS | GP16 (pino 21) |
| 5 | R/W | GND |
| 6 | E | GP17 (pino 22) |
| 7 a 10 | D0 a D3 | Não ligados |
| 11 | D4 | GP18 (pino 24) |
| 12 | D5 | GP19 (pino 25) |
| 13 | D6 | GP20 (pino 26) |
| 14 | D7 | GP21 (pino 27) |
| 15 | A (LED+) | 5 V, por um resistor de 100 Ω |
| 16 | K (LED−) | GND |

O aparelho só escreve no LCD, por isso o R/W vai direto ao GND. Com o contraste errado, o LCD parece apagado ou mostra blocos cheios: ajuste o potenciômetro com o aparelho ligado.

::: nota
**LCD com adaptador I²C.** O código também aceita um LCD com adaptador PCF8574 no endereço `0x27`, em GP26 (SDA) e GP27 (SCL). Esse modo só existe numa imagem compilada da fonte; a alpha publicada usa o modo paralelo desta tabela.
:::

### A cigarra {#cap-02-cigarra}

[release]{.img} [alpha]{.img} A cigarra fica entre o GP22 (pino 29) e o GND. Use uma cigarra **passiva**, que toca a frequência que recebe: o aparelho gera cada nota pelo PIO do RP2040. O Air não tem cigarra. Os sons estão no [capítulo 7](#cap-07-sons).

### O Air: energia dos sensores e carregador {#cap-02-air}

[air]{.img} O Air usa dois pinos além dos sensores. O do carregador se troca ou se desliga pelo console, com o comando `air charger` ([capítulo 19](#cap-19)); o de energia não tem comando e fica no GP16. Os pinos de fábrica:

| Pino | Direção | O que faz |
|---|---|---|
| GP16 (pino 21) | Saída | Fica em nível alto enquanto o aparelho está acordado e em nível baixo enquanto ele dorme |
| GP17 (pino 22) | Entrada, com pull-down | Nível alto quer dizer carregador ligado: o aparelho para de hibernar e fica acordado |

O GP16 serve para dois fins, e você pode usar um, os dois ou nenhum:

- **Cortar a energia dos sensores durante o sono.** Ligue o GP16 à entrada de controle de uma chave do lado positivo, como um MOSFET de canal P com o circuito de acionamento adequado ou o pino de habilitação de um regulador de 3,3 V, e alimente os sensores pela saída dessa chave. Nunca alimente os sensores direto do GP16: um GPIO fornece pouca corrente e o DHT22 consome mais que isso durante a medição.
- **Medir o ciclo.** O nível do GP16 mostra, num analisador lógico, quando o aparelho está acordado.

O GP17 espera um divisor resistivo a partir dos 5 V do carregador, que o leve a nível alto (3,3 V) enquanto o carregador está ligado. Com o carregador, o Air fica acordado e alcançável; sem ele, volta a hibernar no fim do tempo de ociosidade ([capítulo 19](#cap-19)).

::: atencao
**Sensores com energia cortada.** O DHT22 precisa de pelo menos 1 s depois de receber energia antes da primeira leitura; o DS18B20 e o BME280 ficam prontos em poucos milissegundos. O aparelho liga o GP16 logo no início do boot, antes de iniciar os sensores. A chave que você usar não deve atrasar a energia além disso.
:::

::: {.figura #fig-02-air-energia tipo="diagrama" arquivo="02-air-energia.png" captura="GP16 acionando uma chave do lado positivo (MOSFET canal P com acionamento, ou pino EN de um regulador 3,3 V) que alimenta o barramento de 3,3 V dos sensores; à parte, o carregador de 5 V com um divisor resistivo levando ao GP17; legenda 'GP16 alto = acordado'"}
O Air: o GP16 corta a energia dos sensores durante o sono, e o GP17 percebe o carregador.
:::

## Onde ligar os sensores {#cap-02-livres}

Nas três imagens, os sensores vão nos pinos GP0 a GP15, que são os 16 slots. A página **Configurações** e o console recusam qualquer pino de GP16 em diante. Um pino atende um só slot ativo: dois slots ativos no mesmo GPIO são recusados.

| Tipo | GPIOs por sensor | Máximo de sensores |
|---|---|---|
| DS18B20 | 1 | 16, um por GPIO |
| DHT22 | 1 | 16, um por GPIO |
| BME280 ou BMP280 | 2 | 8, cada um no seu par ([as regras do I²C](#cap-02-i2c)) |

Os tipos podem se misturar: 10 DS18B20 e 2 BME280 ocupam 14 GPIOs e deixam 2 livres.

Cuidados por imagem:

- [release]{.img} Durante o boot, o GP8 e o GP9 transmitem e recebem o diagnóstico de boot pela UART1, a 115200 baud, até o sensor configurado neles assumir o pino. Com um adaptador USB-serial de 3,3 V no GP8 (pino 11), você lê em que etapa um boot parou.
- **A busca de sondas mexe no GP16.** O botão **Procurar sondas** testa os pinos de GP0 a GP16 ([capítulo 6](#cap-06-procurar)). O GP16 é o MISO do TFT na release, o RS do LCD na alpha e, no Air, a saída que liga a energia dos sensores. A busca não devolve esses pinos ao estado de antes: reinicie o aparelho depois de uma busca.
- **A placa do projeto só leva GP0 a GP10** aos conectores de sensor ([A placa do projeto](#cap-02-placa)).

## Os sensores {#cap-02-sensores}

| Tipo | Mede | Alimentação | Pull-up externo | O aparelho aceita |
|---|---|---|---|---|
| DS18B20 | Temperatura | 3V3 | 4,7 kΩ do dado ao 3V3 | −50 a 150 °C |
| DHT22 | Temperatura e umidade | 3V3 | O que o fabricante do módulo indica | Leitura com soma de verificação correta |
| BME280 | Temperatura, umidade e pressão | 3V3 | 4,7 kΩ em SDA e SCL, se o módulo não tiver | −40 a 85 °C; 0 a 100 %; 300 a 1.100 hPa |
| BMP280 | Temperatura e pressão | 3V3 | Igual ao BME280 | −40 a 85 °C; 300 a 1.100 hPa |

O aparelho liga o pull-up interno do RP2040 em todo pino de sensor. Ele é fraco demais para substituir os resistores externos da tabela. O que acontece com uma leitura fora da faixa está no [capítulo 6](#cap-06-validacao).

### DS18B20 {#cap-02-ds18b20}

O DS18B20 é um sensor de temperatura de 1 fio (1-Wire), em geral numa sonda à prova d'água com três fios.

| Fio do sensor | Liga em |
|---|---|
| GND | GND |
| DQ (dado) | O GPIO do slot |
| VDD | 3V3 (pino 36) |

Ponha um resistor de 4,7 kΩ entre o DQ e o 3V3.

Ligue **uma sonda por GPIO**. O aparelho identifica a sonda pelo número de série gravado nela (a ROM) e lê esse número com um comando que só funciona com uma sonda no fio. Com a ROM gravada no slot, o aparelho percebe a troca de sonda ([capítulo 6](#cap-06-adotar)).

::: {.figura #fig-02-ds18b20 tipo="diagrama" arquivo="02-ds18b20.png" captura="sonda DS18B20 de três fios ligada ao Pico W: VDD no 3V3 (pino 36), GND no GND, DQ num GPIO de slot (exemplo GP2, pino 4); resistor de 4,7 kΩ entre DQ e 3V3"}
Um DS18B20: três fios e um resistor de 4,7 kΩ entre o dado e o 3V3.
:::

### DHT22 {#cap-02-dht22}

O DHT22 mede temperatura e umidade por um único fio de dados.

| Ligação | Liga em |
|---|---|
| Alimentação (+) | 3V3 (pino 36) |
| Dados | O GPIO do slot |
| Terra (−) | GND |

Ligue **um DHT22 por GPIO**. O aparelho lê cada DHT22 a cada 2 s, o intervalo mínimo do sensor.

::: {.figura #fig-02-dht22 tipo="diagrama" arquivo="02-dht22.png" captura="módulo DHT22 de três terminais ligado ao Pico W: + no 3V3 (pino 36), dados num GPIO de slot (exemplo GP3, pino 5), − no GND; nota 'pull-up conforme o módulo'"}
Um DHT22: alimentação, dados e terra.
:::

### BME280 e BMP280 {#cap-02-bmx280}

O BME280 e o BMP280 são sensores I²C: usam dois GPIOs, SDA e SCL. Por fora os módulos são iguais; o BMP280 não mede umidade.

| Pino do módulo | Liga em |
|---|---|
| VCC (ou VIN) | 3V3 (pino 36) |
| GND | GND |
| SDA | O GPIO do pino SDA do slot |
| SCL | O GPIO do pino SCL do slot |
| CSB | 3V3 (seleciona o modo I²C) |
| SDO | GND para o endereço `0x76`, ou 3V3 para `0x77` |

- **O endereço.** Com um sensor por par, qualquer um dos dois endereços funciona: o aparelho procura em `0x76` e, sem resposta, em `0x77`.
- **O tipo.** Se você escolheu BME280 e o chip é um BMP280, ou o contrário, o aparelho corrige o tipo pelo identificador do chip e grava a correção ([capítulo 6](#cap-06-tipos)).
- **A busca de sondas** só encontra um BME280 ou BMP280 em GP4 (SDA) e GP5 (SCL). Em outros pinos, configure o slot à mão.

::: {.figura #fig-02-bmx280 tipo="diagrama" arquivo="02-bmx280.png" captura="módulo BME280 de seis pinos ligado ao Pico W: VCC no 3V3 (pino 36), GND, SDA no GP4 (pino 6), SCL no GP5 (pino 7), CSB no 3V3, SDO no GND (endereço 0x76); nota 'pull-ups de 4,7 kΩ em SDA e SCL se o módulo não tiver'"}
Um BME280 no par GP4/GP5, o par que a busca de sondas encontra.
:::

### As regras do I²C {#cap-02-i2c}

O RP2040 tem dois periféricos I²C. Cada um só aceita SDA e SCL em certos pinos:

| Periférico | Pinos de SDA | Pinos de SCL | Pares vizinhos |
|---|---|---|---|
| I2C0 | GP0, GP4, GP8, GP12 | GP1, GP5, GP9, GP13 | GP0/GP1, GP4/GP5, GP8/GP9, GP12/GP13 |
| I2C1 | GP2, GP6, GP10, GP14 | GP3, GP7, GP11, GP15 | GP2/GP3, GP6/GP7, GP10/GP11, GP14/GP15 |

Com o SDA num pino de SDA e o SCL num pino de SCL **do mesmo periférico**, o aparelho usa o periférico I²C. Com o SDA num periférico e o SCL no outro, como SDA em GP8 e SCL em GP11, ele faz o I²C pelo PIO do RP2040, mais devagar, e registra no log de eventos o aviso `BME in bit-bang (pins have no hardware I2C)`.

Para ligar vários BME280 ou BMP280, siga estas regras, tiradas do código da v2.7.1:

1. **Um sensor por par.** A leitura do aparelho aceita dois sensores no mesmo par, em `0x76` e `0x77`, mas a página **Configurações** e o console recusam dois slots ativos no mesmo GPIO. Dê a cada sensor o seu par.
2. **Um sensor por periférico.** O aparelho prende cada periférico aos pinos do primeiro sensor que o usa, pela ordem dos slots. Um segundo sensor em outro par do mesmo periférico é lido nos pinos do primeiro. Por isso o segundo slot mostra a leitura do primeiro sensor. Use no máximo um sensor em pares do I2C0 e um em pares do I2C1, e ligue os demais em pares cruzados, que vão pelo PIO.
3. **Nunca inverta SDA e SCL num mesmo periférico.** SDA e SCL trocados (SDA em GP5 e SCL em GP4) ou dois pinos do mesmo papel (SDA em GP0 e SCL em GP4) passam na gravação.

::: perigo
**SDA e SCL trocados travam o boot.** Pelo código da v2.7.1, um BME280 ou BMP280 com SDA e SCL em pinos do mesmo periférico, mas fora dos papéis da tabela, faz o firmware parar na partida dos sensores, a cada boot, antes de o console e a interface web ficarem prontos. O aparelho confere só se os dois pinos são do mesmo periférico, e a biblioteca de I²C para o processador quando recebe um SDA num pino de SCL. Confira a tabela antes de tocar em **Salvar e reiniciar**. Um aparelho nesse estado só volta apagando a flash inteira pelo BOOTSEL e gravando o firmware de novo ([capítulo 18](#cap-18-bootsel-apagar)); a configuração se perde, e um backup feito com os pinos trocados traz o defeito de volta.
:::

## A placa do projeto {#cap-02-placa}

O projeto publica uma placa de circuito para a release e a alpha, desenhada no KiCad. Os arquivos de projeto e de fabricação (Gerber e furação) estão na pasta `PCB_test/` do repositório.

| Revisão | Etiqueta no repositório | Diferença |
|---|---|---|
| v1.0 | `simut-pcb-v1.0` | O conector do LCD tem a pinagem errada |
| v1.1 | `simut-pcb-v1.1` | Pinagem do LCD corrigida e placa com nomes: pinos do TFT e do toque, bloco `Alphanumeric`, coluna de sensores `S0` a `S10`, aviso de rede elétrica e endereço do projeto |

Use a revisão v1.1, que é a da pasta `PCB_test/`. As duas revisões estão publicadas como releases do repositório, cada uma com o arquivo `simut_pcb_fabrication.zip`; a v1.0 fica só como registro do defeito.

O que a placa v1.1 tem:

- **Soquete do Pico W.**
- **Entrada de rede elétrica**, marcada `90-240Vac`, com um conversor HLK-PM01 que fornece os 5 V. Os 5 V chegam ao pino VBUS do Pico W e ao LCD.
- **Conector `TFT-Touch`**, de 14 pinos, na ordem da tabela da [release](#cap-02-tft). O VCC e o LED do módulo vêm do 3V3 do Pico W.
- **Conector `Alphanumeric`**, de 12 pinos, na ordem VSS, VDD, VO, RS, RW, E, D4, D5, D6, D7, A e K, com um trimpot de contraste na placa.
- **Cigarra na placa**, acionada pelo GP22 por meio de um transistor.
- **Coluna de sensores `S0` a `S10`**, ligada a GP0 a GP10, com redes de resistores de pull-up para o 3V3. Os pinos GP11 a GP15 não chegam a nenhum conector: nesta placa, o aparelho tem 11 posições de sensor.
- **Quatro furos de fixação** para parafusos M2.5.

Os arquivos da placa e do esquema não definem o valor de nenhum resistor. Para as redes de pull-up dos sensores, use 4,7 kΩ, o valor da [tabela dos sensores](#cap-02-sensores).

::: perigo
**Rede elétrica na placa.** A entrada `90-240Vac` recebe a tensão da tomada. Monte a placa numa caixa fechada, faça as ligações com a placa desligada da tomada e não toque na placa ligada.
:::

::: atencao
**USB com a placa ligada na tomada.** Na placa, os 5 V do conversor vão ao mesmo pino VBUS que o conector USB do Pico W. Com a placa ligada na tomada e um cabo USB ligado a um computador, as duas fontes de 5 V ficam em paralelo, e uma pode alimentar a outra. Para usar o console USB, desligue a placa da tomada e deixe o USB alimentar o aparelho, ou use um adaptador USB que corta a linha de 5 V.
:::

::: {.figura #fig-02-placa tipo="foto" arquivo="02-placa.png" captura="placa SIMUT v1.1 montada, vista de cima: Pico W no soquete, conector TFT-Touch com o painel ligado, bloco Alphanumeric vazio, coluna S0–S10 com uma sonda DS18B20 em S2, cigarra, conversor HLK-PM01 e a marcação 90-240Vac visível"}
A placa v1.1 montada para a release: o Pico W, o conector do painel, a coluna de sensores e a entrada de rede elétrica.
:::

## Lista de conferência da montagem {#cap-02-conferencia}

Antes de ligar o aparelho pela primeira vez:

1. O GND do Pico W, da tela e de todos os sensores está ligado entre si.
2. Os sensores e os pull-ups vão no 3V3 (pino 36), não no 5 V.
3. [alpha]{.img} O LCD vai no 5 V (pino 40), com o R/W no GND e o potenciômetro de contraste no VO.
4. Cada DS18B20 tem o resistor de 4,7 kΩ do dado ao 3V3, e há uma sonda por GPIO.
5. Cada BME280 ou BMP280 tem o seu par, com SDA e SCL nos papéis da [tabela do I²C](#cap-02-i2c).
6. Nenhum sensor está em GP16 ou acima.
7. [release]{.img} O TFT e o toque compartilham MISO, MOSI e SCK, com CS separados: GP28 para o TFT e GP17 para o toque.
8. Anote qual sensor está em qual GPIO: a configuração dos slots pede essa informação ([capítulo 6](#cap-06-adicionar)).
