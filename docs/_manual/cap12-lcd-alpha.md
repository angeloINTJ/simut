# O LCD do alpha {#cap-12}

[alpha]{.img} A imagem `alpha` troca o painel de toque por um LCD de caracteres de 16 colunas e 2 linhas. Este capítulo mostra tudo o que esse LCD exibe, do boot às leituras e ao ponto de acesso de configuração, para quem instala e opera um SIMUT alpha.

## O que o LCD faz e o que não faz {#cap-12-visao}

O LCD só mostra; ele não tem botões nem toque. Tudo o que se configura num alpha passa pela interface web ([capítulo 13](#cap-13)), pelo console serial ou pelo Bluetooth ([capítulo 14](#cap-14)).

| O LCD mostra | O LCD não mostra |
|---|---|
| O boot, com uma barra de progresso | A data e a hora ([capítulo 10](#cap-10-onde)) |
| O resultado da conexão Wi-Fi e o endereço IP | Os alarmes de limite ([capítulo 7](#cap-07-lcd)) |
| As leituras, uma grandeza por vez | O mínimo, o máximo e o histórico |
| `ERRO` para sensor em falha | O estado do Bluetooth |
| O sinal do Wi-Fi | Menus |
| A contagem de pendentes da telemetria | |
| O endereço, o nome e a chave do AP de configuração, com uma ressalva na v2.7.1 ([O ponto de acesso de configuração](#cap-12-ap)) | |

O LCD fala só português, seja qual for o idioma da interface web ou do console.

## O layout {#cap-12-layout}

O LCD tem 16 colunas (0 a 15) e 2 linhas (0 e 1). Nas leituras, ele se divide assim:

| Área | Linha | Colunas | O que mostra |
|---|---|---|---|
| Canto de cima | 0 | 0 e 1 | `W` e o ícone do sinal do Wi-Fi |
| Canto de baixo | 1 | 0 a 2 | O slot na tela, como `S2`, ou a contagem de pendentes |
| Valor | 0 e 1 | 3 a 13 | O número em algarismos grandes, de duas linhas |
| Unidade | 0 e 1 | 13 a 15 | `o` e `C`, ou `%UR` |

Os algarismos grandes são desenhados com caracteres especiais do LCD: cada um ocupa 3 colunas e as 2 linhas.

## O boot {#cap-12-boot}

Ao ligar, o LCD mostra por 1,2 s:

```text
SIMUT 2.7.1
  Inicializando
```

Em seguida, a linha de baixo vira uma barra de 14 posições entre colchetes. Ela avança uma posição a cada etapa nova do boot, ou a cada 0,8 s se nenhuma etapa nova chegar, e se completa no fim do boot:

```text
SIMUT 2.7.1
[#######       ]
```

::: {.figura #fig-12-splash tipo="lcd" arquivo="12-splash.png" captura="logo depois de ligar; linha de cima SIMUT 2.7.1, linha de baixo Inicializando"}
A abertura do LCD, com a versão.
:::

::: {.figura #fig-12-boot tipo="lcd" arquivo="12-boot.png" captura="durante o boot, com a barra pela metade: SIMUT 2.7.1 e [#######       ]"}
A barra de progresso do boot.
:::

### O resultado da rede {#cap-12-rede}

No fim do boot, o LCD mostra o resultado da conexão:

| Linha de cima | Linha de baixo | Situação | Por quanto tempo |
|---|---|---|---|
| `Conectado!` | O endereço IP, como `192.0.2.10` | Na rede, com IP | 3 s depois de o IP aparecer |
| `Conectado!` | `Obtendo IP...` | Associado à rede, esperando o endereço | Até 10 s |
| `Sem WiFi` | `Offline` | Sem rede | 3 s |

Depois disso, o LCD passa às leituras. Um boot que abre o AP de configuração não chega a esta etapa ([O ponto de acesso de configuração](#cap-12-ap)).

```text
 Conectado!
192.0.2.10
```

::: {.figura #fig-12-conectado tipo="lcd" arquivo="12-conectado.png" captura="fim do boot, na rede, com IP; Conectado! e o endereço IP na linha de baixo (use um endereço de documentação na legenda publicada)"}
O fim do boot com a rede: Conectado! e o endereço IP.
:::

::: {.figura #fig-12-obtendo-ip tipo="lcd" arquivo="12-obtendo-ip.png" captura="fim do boot, associado ao roteador sem endereço ainda; Conectado! e Obtendo IP..."}
Associado à rede, esperando o endereço.
:::

::: {.figura #fig-12-offline tipo="lcd" arquivo="12-offline.png" captura="fim do boot sem rede alcançável; Sem WiFi e Offline"}
O fim do boot sem rede.
:::

## As leituras {#cap-12-leituras}

O LCD mostra uma grandeza de um sensor por vez e passa para a próxima a cada 3 s. A ordem segue os slots ativos, do menor para o maior, e, dentro de cada sensor, as grandezas dele:

| Tipo | Grandezas, na ordem |
|---|---|
| DS18B20 | Temperatura |
| DHT22 | Temperatura, umidade |
| BME280 | Temperatura, umidade, pressão |
| BMP280 | Temperatura, pressão |

Com um DHT22 no slot 0 e um BME280 no slot 2, o ciclo é: temperatura do slot 0, umidade do slot 0, temperatura do slot 2, umidade do slot 2, pressão do slot 2, e de novo. A tela atualiza a cada 0,5 s.

Como cada grandeza aparece:

| Grandeza | Formato |
|---|---|
| Temperatura | Algarismos grandes com uma casa decimal e vírgula, como `23,4`, e `o` e `C` à direita. Um valor negativo leva um traço antes dos algarismos |
| Umidade | Algarismos grandes, inteiros, como `58`, com `%UR` embaixo, à direita |
| Pressão | Caracteres normais, na linha de cima, com uma casa decimal e ponto, como `1013.2 hPa` |

A dezena de uma temperatura abaixo de 10 °C fica em branco: 5,3 °C aparece como `5,3`.

**Qual sensor está na tela.** Com mais de um sensor ativo, o canto de baixo à esquerda mostra `S` e o número do slot, como `S2`. Com um só, o canto fica livre para a contagem de pendentes. O LCD não mostra o nome do sensor.

::: {.figura #fig-12-temperatura tipo="lcd" arquivo="12-temperatura.png" captura="2 sensores ativos; na vez da temperatura do slot 0, com 23,4 °C; W e o ícone de sinal no canto de cima, S0 no canto de baixo"}
Uma temperatura em algarismos grandes, com o slot no canto de baixo.
:::

::: {.figura #fig-12-umidade tipo="lcd" arquivo="12-umidade.png" captura="DHT22 no slot 1, na vez da umidade, com 58 %; S1 no canto de baixo"}
A umidade em algarismos grandes, com %UR.
:::

::: {.figura #fig-12-pressao tipo="lcd" arquivo="12-pressao.png" captura="BME280 no slot 2, na vez da pressão, com 1013,2 hPa; S2 no canto de baixo"}
A pressão, em caracteres normais na linha de cima.
:::

::: {.figura #fig-12-negativa tipo="lcd" arquivo="12-negativa.png" captura="DS18B20 num congelador, com -18,5 °C; um só sensor ativo e nenhum pendente"}
Uma temperatura negativa: o traço antes dos algarismos.
:::

## Erro e ausência de sensor {#cap-12-erro}

O LCD mostra `ERRO` em letras grandes, nas colunas 3 a 14, em dois casos:

- **Sensor em falha:** quando chega a vez de um sensor que não está lendo ([capítulo 6](#cap-06-erro-aparece)). O ciclo continua: os outros sensores aparecem normalmente na vez deles.
- **Nenhum sensor ativo:** sem nenhum slot ativo, `ERRO` fica fixo, com o ícone de Wi-Fi no canto.

Com mais de um sensor, o canto de baixo diz qual está em falha. Uma grandeza sem valor, como a umidade de um sensor que ainda não fez a primeira leitura, também aparece como `ERRO` na vez dela.

A figura do sensor em falha está no [capítulo 6](#fig-06-lcd-erro).

::: {.figura #fig-12-sem-sensor tipo="lcd" arquivo="12-sem-sensor.png" captura="imagem alpha sem nenhum slot ativo, conectada ao Wi-Fi; ERRO em letras grandes e W com o ícone de sinal"}
ERRO fixo: nenhum sensor ativo.
:::

## Alarmes {#cap-12-alarmes}

O LCD não muda durante um alarme de limite: ele segue o ciclo, mostrando os valores normalmente. A cigarra do alpha toca, e não há como silenciar pelo aparelho ([capítulo 7](#cap-07-lcd)). Um alarme de falha aparece só como o `ERRO` do sensor na vez dele.

Para acompanhar alarmes num alpha, use a linha de alarmes ([capítulo 22](#cap-22)).

## O sinal do Wi-Fi {#cap-12-wifi}

O canto de cima à esquerda mostra `W` seguido de um ícone de sinal com seis estados:

| Ícone | RSSI |
|---|---|
| 5 barras | −50 dBm ou mais |
| 4 barras | de −60 a −51 dBm |
| 3 barras | de −70 a −61 dBm |
| 2 barras | de −80 a −71 dBm |
| 1 barra | de −90 a −81 dBm |
| Um `X` | abaixo de −90 dBm, ou sem conexão |

Desde a v2.7.2, as barras são uma escada de cinco degraus, cada um um pixel mais alto que o da esquerda, e acendem da esquerda para a direita, como no painel da imagem `release` e no celular.

Na v2.7.1, cada nível acende uma linha a mais, de baixo para cima: com sinal fraco, o ícone é uma linha reta na largura toda, e ele cresce para cima.

::: {.figura #fig-12-wifi-niveis tipo="diagrama" arquivo="12-wifi-niveis.png" captura="os seis desenhos do ícone, em grade de 5 x 8 pixels, lado a lado: X, 1, 2, 3, 4 e 5 barras; em cima, a escada da v2.7.2 (colunas acesas da esquerda para a direita); embaixo, os desenhos da v2.7.1 (linhas acesas de baixo para cima); bitmaps em src/display/BigFont_HD44780.h"}
Os seis estados do ícone de sinal: a escada da v2.7.2 e as linhas da v2.7.1.
:::

## Os pendentes da telemetria {#cap-12-pendentes}

Desde a v2.7.2, com um único sensor ativo, o canto de baixo à esquerda mostra quantos registros esperam envio pela telemetria, do mesmo jeito que o painel da imagem `release`:

- nada, quando não há pendentes;
- o número, até 999, como `37`;
- os milhares com `k`, de 1.000 em diante: 2.500 registros aparecem como `2k`.

O aparelho atualiza a contagem a cada 10 s. Com mais de um sensor, o canto mostra o slot, e a contagem não aparece no LCD; ela continua no cartão **Registros Pendentes** do **Painel de Controle** da interface web ([capítulo 13](#cap-13-cartoes-estado)).

Na v2.7.1, o LCD não mostra os pendentes.

::: {.figura #fig-12-pendentes tipo="lcd" arquivo="12-pendentes.png" captura="imagem v2.7.2; 1 sensor ativo; telemetria ligada com o coletor fora do ar, 37 registros pendentes; 37 no canto de baixo à esquerda"}
A contagem de pendentes no canto de baixo, com um único sensor.
:::

::: {.figura #fig-12-pendentes-k tipo="lcd" arquivo="12-pendentes-k.png" captura="imagem v2.7.2; 1 sensor ativo; 2.500 registros pendentes; 2k no canto de baixo"}
Mais de mil pendentes: a contagem em milhares.
:::

## O ponto de acesso de configuração {#cap-12-ap}

O alpha tem três páginas para o ponto de acesso de configuração ([capítulo 9](#cap-09-ap)), que se alternam:

| Página | Linha de cima | Linha de baixo |
|---|---|---|
| 1 | `Modo AP  1 de 3` | `192.168.4.1` |
| 2 | `Rede:    2 de 3` | O nome da rede, como `simut_SETUP` |
| 3 | `Senha:   3 de 3` | A chave, de 10 caracteres, ou `(aberta)` numa rede sem senha |

Cada página fica 3 s. Um valor com mais de 16 caracteres rola para a esquerda, uma coluna a cada 300 ms, com dois espaços entre o fim e o recomeço; nesse caso a página dura uma volta inteira. O nome de rede mais longo, com 37 caracteres, leva cerca de 12 s para passar.

::: atencao
**Na v2.7.1, essas páginas quase nunca aparecem.** Pelo código da v2.7.1:

- **Num alpha que liga sem rede configurada**, o boot abre o AP e não encerra a tela de boot. O LCD fica em `SIMUT 2.7.1` com a barra cheia, sem o nome da rede e sem a chave.
- **Num AP aberto com o aparelho em operação**, pela escada de reconexão ou pelo comando `ap`, o LCD continua nas leituras, paradas no último valor. As páginas do AP só aparecem depois que alguém abre uma página da interface web pelo AP.

Isso vem da leitura do código. O LCD não pôde ser conferido no aparelho, porque a bancada de testes não tem LCD.
:::

Para usar o AP num alpha:

1. Pegue o nome da rede e a chave:
   - no console USB, que mostra as linhas `[AP] SSID`, `[AP] PSK` e `[AP] URL` quando o AP abre; ou
   - na resposta do comando `ap`, pelo console USB ou pelo Bluetooth ([capítulo 14](#cap-14)). Ela traz a chave; o nome da rede é o nome do aparelho seguido de `_SETUP`.
2. No celular ou no computador, conecte-se a essa rede com essa chave.
3. Abra `http://192.168.4.1`.

O restante está no [capítulo 9](#cap-09-ap-aberto).

```text
Senha:   3 de 3
K7PX4MRW2A
```

::: {.figura #fig-12-ap-1 tipo="lcd" arquivo="12-ap-1.png" captura="AP aberto em operação pelo comando ap, depois de abrir uma página da interface web pelo AP; página Modo AP 1 de 3 com 192.168.4.1"}
A página 1 do AP: o endereço.
:::

::: {.figura #fig-12-ap-2 tipo="lcd" arquivo="12-ap-2.png" captura="mesmo estado; página Rede: 2 de 3 com simut_SETUP"}
A página 2 do AP: o nome da rede.
:::

::: {.figura #fig-12-ap-3 tipo="lcd" arquivo="12-ap-3.png" captura="mesmo estado; página Senha: 3 de 3 com a chave; borre a chave na foto publicada ou use um aparelho de demonstração"}
A página 3 do AP: a chave.
:::


## Referência dos estados {#cap-12-referencia}

| Estado | Linha de cima | Linha de baixo | Figura |
|---|---|---|---|
| Abertura | `SIMUT 2.7.1` | `Inicializando` | [12-splash](#fig-12-splash) |
| Boot | `SIMUT 2.7.1` | Barra `[##...]` | [12-boot](#fig-12-boot) |
| Rede com IP | `Conectado!` | O IP | [12-conectado](#fig-12-conectado) |
| Esperando IP | `Conectado!` | `Obtendo IP...` | [12-obtendo-ip](#fig-12-obtendo-ip) |
| Sem rede | `Sem WiFi` | `Offline` | [12-offline](#fig-12-offline) |
| Temperatura | `W`, ícone e algarismos | Slot ou pendentes, algarismos, `C` | [12-temperatura](#fig-12-temperatura) |
| Umidade | `W`, ícone e algarismos | Slot ou pendentes, algarismos, `%UR` | [12-umidade](#fig-12-umidade) |
| Pressão | `W`, ícone e `1013.2 hPa` | Slot ou pendentes | [12-pressao](#fig-12-pressao) |
| Sensor em falha | `W`, ícone e `ERRO` | Slot ou pendentes, `ERRO` | [06-lcd-erro](#fig-06-lcd-erro) |
| Nenhum sensor | `W`, ícone e `ERRO` | `ERRO` | [12-sem-sensor](#fig-12-sem-sensor) |
| AP, página 1 | `Modo AP  1 de 3` | `192.168.4.1` | [12-ap-1](#fig-12-ap-1) |
| AP, página 2 | `Rede:    2 de 3` | O nome da rede | [12-ap-2](#fig-12-ap-2) |
| AP, página 3 | `Senha:   3 de 3` | A chave | [12-ap-3](#fig-12-ap-3) |
