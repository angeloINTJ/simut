# SIMUT Air {#cap-19}

O SIMUT Air é a imagem do SIMUT para funcionar com bateria, num lugar sem tomada e sem ninguém por perto: ele acorda pelo relógio, lê os sensores, grava a leitura e volta a dormir. Este capítulo explica os dois modos do Air, o ciclo de hibernação, a telemetria a bateria, os tempos medidos, o que não existe nele e como instalá-lo. É para quem vai instalar e operar um Air.

[air]{.img}

## O que é o Air, e por que ele é experimental {#cap-19-status}

O Air é o mesmo firmware das outras imagens, sem painel, sem LCD e sem buzzer, com um ciclo de hibernação acrescentado. Ele roda num Pico W comum e é publicado como `simut_v<versão>_air.uf2` e `simut_v<versão>_air.bin`.

::: atencao
**O Air é experimental.** O ciclo e os tempos foram medidos em bancada, não numa instalação de campo. O único teste longo do ciclo falhou: em 10/09/2026, depois de 119 ciclos normais, um sono nunca terminou, e o aparelho ficou mudo por 27,5 h até ser reiniciado à mão. Desde então, um vigia de hardware cobre o despertar e reinicia o aparelho se ele travar ali, mas a causa do travamento não foi confirmada. O consumo de bateria citado neste capítulo é uma conta, não uma medição de corrente. Não use o Air onde a perda de medições não é aceitável.
:::

## Os dois modos {#cap-19-modos}

| | M0, acordado | M1, o ciclo |
|---|---|---|
| Para que serve | Configurar, consultar, atualizar | Medir com o menor consumo |
| Sensores e histórico | Sim, no **Intervalo Histórico** | Sim, uma leitura por despertar |
| Rádio Wi-Fi | Ligado | Só nos despertares de telemetria |
| Interface web e API | Sim | Não |
| Bluetooth | Sim | Não |
| Console USB | Sim, o console completo | Só durante os poucos segundos de cada despertar |
| Alarmes e linha de alarmes | Sim | Não ([Alarmes no Air](#cap-19-alarmes)) |
| USB no computador | Presente | Some durante o sono e volta a cada despertar |

### Entrar em M1 {#cap-19-entrar}

O Air passa de M0 para M1 de dois jeitos:

- **Pelo comando** `air hibernate`, ou `air sleep`, no console. O console responde `Entrando em hibernacao (SIMUT Air)...`.
- **Pela ociosidade:** depois de `air idle` segundos sem atividade, 300 s de fábrica, e sem o carregador ligado ([O tempo de ociosidade](#cap-19-ociosidade)).

Os dois caminhos gravam na flash que o ciclo está armado, e o `air status` passa a mostrar `armed=1`. Antes de dormir, o Air grava o bloco aberto do histórico e o cursor da telemetria.

### Sair de M1 {#cap-19-sair}

| Como | O que acontece |
|---|---|
| `air stop`, ou `air wake`, no console USB durante um despertar | O Air volta a M0, liga o rádio e a interface web, e desarma o ciclo na flash. O console responde `Hibernacao cancelada. Voltando ao modo operacional (M0)...` |
| O carregador ligado, lido num despertar | Aquele boot sobe em M0 completo. O ciclo continua armado: ao desligar o carregador, o Air volta a dormir quando o tempo de ociosidade acaba |
| Um boot limpo: alimentação, pino RUN, `reload`, atualização | M0 pelo tempo de ociosidade inteiro, para quem está diante do aparelho. Com o ciclo armado, o Air volta a dormir sozinho depois |
| Um reinício pelo vigia (watchdog) com o ciclo armado | M0 por só 10 s, e volta ao ciclo |
| Três reinícios pelo vigia seguidos | O Air fica em M0 pelo tempo de ociosidade inteiro, para que alguém consiga entrar, e registra **Ciclo Air retido em M0** (411), com o número de reinícios no contexto |

Pelo Bluetooth, `air stop` não funciona em M1: durante um despertar, o Air só atende o console USB. Pelo Bluetooth, use o carregador para trazer o Air a M0.

::: {.figura #fig-19-modos tipo="diagrama" arquivo="19-modos.png" captura="máquina de estados com dois estados, M0 (acordado: web, console, Bluetooth, rádio) e M1 (ciclo: dormir, despertar, ler, gravar, dormir). Setas de M0 para M1: 'air hibernate' e 'ociosidade (air idle)'. Setas de M1 para M0: 'air stop pelo USB (desarma)', 'carregador no despertar (continua armado)', 'boot limpo (M0 pelo idle inteiro, depois volta)', 'watchdog (10 s de graça, depois volta)', '3 watchdogs seguidos (retido em M0, evento 411)'. Dentro de M1, o laço 'dormir → despertar → ler → gravar → [rádio se pendentes ≥ lote mínimo] → dormir'"}
Legenda: como o Air entra e sai do ciclo de hibernação.
:::

## O ciclo de hibernação {#cap-19-ciclo}

Em M1, cada despertar é um boot completo do processador. O Air dorme pelo alarme do relógio de tempo real do RP2040 e, ao acordar, percorre estas fases:

1. **Aquecimento:** espera 0,4 s com os sensores alimentados. O GP16, que alimenta os sensores, já sobe no começo do boot ([O GP16](#cap-19-gp16)).
2. **Amostragem:** lê os sensores sem intervalo entre as leituras, até cada sensor juntar as 10 leituras da média, ou até 30 s. Num despertar de telemetria, conecta o Wi-Fi ao mesmo tempo.
3. **Decisão:** grava o registro no histórico, sempre. É a razão de existir do despertar. Aproveita para aplicar a limpeza automática do histórico, com até 4 arquivos por despertar ([capítulo 15](#cap-15-capacidade)).
4. **Conexão:** só num despertar de telemetria com o Wi-Fi de pé. Espera o enlace até 30 s; não espera o NTP.
5. **Envio:** esvazia a fila da telemetria, lote atrás de lote, até acabar, até o coletor falhar, até o Wi-Fi cair ou até o tempo do despertar acabar.
6. **Sono:** desliga o rádio e a alimentação dos sensores, arma o alarme e dorme.

O Air dorme o **Intervalo Histórico** menos o tempo que ficou acordado, para que o período de despertar a despertar seja o intervalo configurado. O sono mínimo é de 5 s. Se a espera da telemetria depois de uma falha for maior, ele dorme pela espera.

::: {.figura #fig-19-despertar tipo="diagrama" arquivo="19-despertar.png" captura="linha do tempo de dois despertares num intervalo de 60 s: faixa GP16 alta durante cada despertar; despertar de leitura com boot, aquecimento 0,4 s, amostragem (a maior parte, com a conversão do DS18B20), decisão com 'grava o histórico', e sono até completar 60 s; despertar de telemetria com a amostragem e o Wi-Fi em paralelo, decisão, conexão e envio, faixa 'rádio ligado' só nele; marcas 9,3 s (leitura) e 12,7 s (telemetria)"}
Legenda: as fases de um despertar. O rádio só liga nos despertares de telemetria.
:::

### O limite de 24 h {#cap-19-24h}

O alarme do relógio do RP2040 é uma hora do dia. Um sono de 24 h inteiras cairia na hora atual, então o Air dorme no máximo 1 s a menos que 24 h. Um sono pedido maior que isso é encurtado, e o Air registra **Sono limitado a menos de 24 h (alarme é por hora do dia)** (413), com os minutos pedidos no contexto. Isso acontece com o **Intervalo Histórico** em 1440 min, o valor máximo, no primeiro sono depois de M0: nos despertares seguintes, o tempo acordado já é descontado do sono.

### O relógio através do sono {#cap-19-relogio}

Nos despertares sem rádio não há NTP: a hora que o histórico grava é a que o Air reconstrói. Desde a v2.7.2, o Air carrega o relógio através do sono, com erro de cerca de ±0,09 s por despertar (de −0,085 a +0,030 s em 10 despertares, medido em 23/09/2026). Na v2.7.1, a hora perde até cerca de 1 s por despertar até o próximo acerto por NTP. Os detalhes estão no [capítulo 10](#cap-10-air).

## O tempo de ociosidade {#cap-19-ociosidade}

Em M0, o Air conta o tempo sem atividade e hiberna quando ele passa de `air idle` segundos, 300 s de fábrica.

```text
SIMUT> air idle 600
OK: air idle set
```

A faixa é de 10 a 65535 s. Fora dela, o console responde `ERROR: air idle <10..65535> (seconds)` e não muda nada. O valor fica gravado em `/config/air.bin`.

**O que renova a contagem:**

| Atividade | Renova? |
|---|---|
| Qualquer comando no console, USB ou Bluetooth | Sim |
| Um pedido à interface web ou à API com uma sessão válida | Sim |
| Uma entrada bem-sucedida na interface web | Sim |
| Uma leitura de `/metrics` com conta e senha válidas ([capítulo 24](#cap-24)) | Sim |
| Uma leitura de `/api/status?quiet=1` | Não, de propósito: é para o gestor de frota consultar sem manter o Air acordado |
| A página de entrada, antes de entrar | Só três vezes por boot. Uma entrada bem-sucedida devolve as três |
| Qualquer pedido sem sessão válida | Não |
| O carregador ligado | Sim, continuamente |

::: atencao
**Uma aba aberta mantém o Air acordado.** O **Painel de Controle** consulta o aparelho a cada 3 s com a sua sessão, e cada consulta renova a contagem. Feche a aba quando terminar ([capítulo 13](#cap-13-air)).
:::

::: nota
**Por que só pedidos com sessão contam.** Se qualquer pedido renovasse a contagem, qualquer pessoa na rede manteria o Air acordado até a bateria acabar, só consultando um endereço. As três renovações antes da entrada dão a quem está diante do aparelho até 15 min, com o tempo de fábrica, para terminar de entrar.
:::

## O carregador {#cap-19-carregador}

O Air lê num pino se o carregador está ligado. Um divisor de tensão leva os 5 V do carregador ao nível lógico do pino, que fica em nível alto enquanto o carregador está ligado. De fábrica, o pino é o GP17.

Com o carregador ligado, não há bateria a poupar:

- em M0, a ociosidade não conta, e o Air fica acordado;
- um despertar que encontra o carregador sobe M0 completo, com interface web e Bluetooth;
- o ciclo continua armado: ao desligar o carregador, o Air volta a dormir quando o tempo de ociosidade acaba.

O Air só detecta a presença da fonte. Ele não mede a corrente de carga nem a tensão da bateria.

Para mudar o pino:

```text
SIMUT> air charger 17
OK: charger sense on GP17 (now on battery)
SIMUT> air charger off
OK: charger sense off
```

- Aceita GP0 a GP22 e GP26 a GP28. Os pinos GP23, GP24, GP25 e GP29 são do rádio do Pico W e são recusados com `ERROR: air charger <0..22|26..28|off>`.
- Recusa um pino já usado pela alimentação dos sensores ou por um sensor ativo, com `ERROR: GP em uso (alimentacao de sensor ou sensor ativo)`.
- `off` desliga a leitura: o Air passa a hibernar mesmo ligado a uma fonte.
- O valor fica gravado em `/config/air.bin`.

## O GP16: acordado e alimentação dos sensores {#cap-19-gp16}

O Air põe o GP16 em nível alto durante todo o tempo em que está acordado, do começo do boot até a entrada no sono, e em nível baixo enquanto dorme. Esse pino serve para duas coisas:

- **Alimentar os sensores só quando preciso:** ligue a alimentação dos sensores por uma chave do lado positivo, como um MOSFET de canal P ou o pino de habilitação de um regulador de 3,3 V, comandada pelo GP16. Não alimente os sensores direto do GP16: um pino fornece pouca corrente.
- **Saber se o Air está acordado:** um analisador lógico ou um LED com resistor no GP16 mostra cada despertar.

O LED da placa não serve para isso: no Pico W, ele é ligado pelo chip do rádio, e o Air só o acende quando o rádio está de pé.

O esquema de ligação está no [capítulo 2](#cap-02).

## A telemetria a bateria {#cap-19-telemetria}

O rádio é a coisa mais cara que um despertar pode fazer. Por isso, o Air decide no começo de cada despertar se liga o rádio, pela quantidade de registros esperando, e não pelo relógio:

- **O Lote mínimo** (`t_int`) conta registros, não tempo ([capítulo 21](#cap-21-lotes)). O rádio liga quando os registros pendentes, contando o que o despertar vai gravar, chegam ao **Lote mínimo**. Com o **Lote mínimo** em N e todos os envios bem-sucedidos, o rádio liga uma vez a cada N despertares.
- **Lote mínimo em 0** desliga a telemetria: o rádio nunca liga em M1.
- **O Lote máximo** (`t_bat`) é o teto de registros por envio. A memória livre pode reduzir o lote abaixo dele: num Air em M0, com `t_bat=250`, os lotes saíram com cerca de 190 registros (medido em 23/09/2026). Uma fila grande sai em vários lotes no mesmo despertar, até o tempo dele acabar ([capítulo 21](#cap-21-tamanho)).
- **Um envio que falha** reserva 5 despertares de silêncio: nos 5 seguintes, o rádio não liga, mesmo com registros esperando. Sem isso, com o coletor fora do ar, o rádio ligaria em todo despertar e gastaria a bateria sem ninguém para receber.
- **Uma rede que não aparece** não prende o despertar: depois de duas tentativas de conexão, o Air desiste naquele despertar, grava o registro e dorme. O despertar seguinte tenta de novo.

Os registros esperam na flash, e o histórico guarda meses deles ([capítulo 15](#cap-15-capacidade)). O que não sai num despertar sai num dos seguintes.

Desde a v2.7.2, cada lote leva só registros inteiros, e o cursor avança só até o último registro que foi no corpo ([capítulo 21](#cap-21-registro-inteiro)).

::: nota
**Por que contar registros e não horas.** Num despertar sem rádio, a hora é a reconstruída pelo próprio Air. Uma regra baseada no relógio mediria justamente o que não é confiável. A contagem de registros é exata.
:::

## Tempos medidos e consumo {#cap-19-tempos}

Medido em bancada em 08/09/2026, na v2.4.1-beta, com o **Intervalo Histórico** de 1 min e um DS18B20 a 12 bits:

| Medição | Resultado |
|---|---|
| Despertar de leitura, do boot até a decisão | 9,31 s, dos quais 6,83 s são a conversão do sensor |
| Despertar de telemetria | cerca de 12,7 s |
| Dormindo, por ciclo | cerca de 51 s |
| Período real | 60,5 a 60,6 s, para 60 s configurados |
| Fração do tempo acordado | cerca de 13 % |

**Para onde vai o tempo.** A maior parte de um despertar é a média de 10 leituras enchendo do zero, e o DS18B20 leva 750 ms por conversão. O firmware espera 750 ms por conversão qualquer que seja a **Resolução DS18B20**: baixar a resolução hoje reduz a precisão e não encurta o despertar ([capítulo 6](#cap-06-resolucao)).

**Consumo, como conta.** Com as correntes de referência de bancada de 25 mA acordado lendo, 80 mA transmitindo e 2 mA dormindo, com uma leitura por minuto e telemetria a cada quinto despertar, a conta dá cerca de 8,2 mA de média, ou 17 dias numa célula 18650 de 3400 mAh. É aritmética sobre os tempos medidos; a corrente do Air não foi medida com um instrumento. Meça na sua montagem antes de dimensionar a bateria.

::: nota
**A alavanca é o intervalo.** O sono é a menor parte da conta. Um **Intervalo Histórico** maior reduz o número de despertares, e é o que mais estende a bateria.
:::

## Alarmes no Air {#cap-19-alarmes}

O Air só confere limites de alarme em M0. Num despertar de M1, ele lê os sensores e grava o histórico, mas não compara os valores com os limites e não gera registros da linha de alarmes ([capítulo 22](#cap-22-fila)).

- Um valor fora da faixa durante o ciclo fica no histórico e sai pela telemetria, mas não dispara nada.
- A fila da linha de alarmes fica na RAM, e cada despertar começa com um reinício: o que não foi entregue antes de o Air dormir se perde.
- O Air não tem buzzer nem visor: em M0, um alarme aparece na interface web e na linha de alarmes.

Para vigiar limites com um Air, faça a conferência no coletor da telemetria.

## O que não existe no Air {#cap-19-nao-existe}

| Recurso | Situação no Air |
|---|---|
| Painel, LCD e buzzer | Não existem |
| Espelho, captura e tema do painel na interface web | Não existem |
| PIN do painel e bloqueios do painel | Não existem: não há painel |
| Rede de configuração automática | Não abre sozinha, nem sem rede configurada ([capítulo 9](#cap-09-ap)). Configure a rede pelo console |
| Nome `.local` (mDNS) | Não existe, nem em M0: saiu da imagem para caber no limite de tamanho da atualização. Use o IP |
| Interface web, API, Bluetooth e atualização | Só em M0 ([capítulo 17](#cap-17-variantes)) |
| Alarmes e linha de alarmes | Só em M0 ([Alarmes no Air](#cap-19-alarmes)) |
| `air stop` pelo Bluetooth | Não funciona em M1 |

O Air tem o console completo, com os modos e os comandos de configuração ([capítulo 14](#cap-14-completo)). É a única imagem publicada com ele. Os comandos `air` estão também no [capítulo 14](#cap-14-air).

## O comando air status {#cap-19-status-cmd}

```text
SIMUT> air status
Air: phase=0 wake=60s hist=60s backoff=0s idle=300s armed=1 dirty=0 tel=31/5 skip=0 radio=1 chg=0 bat=50 cyc=4038ms wip=1
```

| Campo | O que mostra |
|---|---|
| `phase` | A fase do ciclo: 0 é M0; em M1, 1 aquecimento, 2 amostragem, 3 decisão, 5 conexão, 6 envio e 7 sono |
| `wake` | O sono pedido entre despertares, em segundos: o **Intervalo Histórico**, ou a espera da telemetria se for maior |
| `hist` | O **Intervalo Histórico**, em segundos |
| `backoff` | A espera da telemetria depois de uma falha, em segundos |
| `idle` | O tempo de ociosidade em vigor, em segundos. Mostra 10 durante a graça depois de um reinício pelo vigia |
| `armed` | 1 quando o ciclo está armado na flash e volta sozinho depois de um reinício |
| `dirty` | Quantos reinícios pelo vigia seguidos o ciclo sofreu. Em 3, o Air fica retido em M0 |
| `tel` | Registros pendentes e o **Lote mínimo**, como `31/5`. `/0` quer dizer telemetria desligada |
| `skip` | Quantos despertares de silêncio ainda faltam depois de um envio que falhou |
| `radio` | 1 quando este boot ligou o rádio |
| `chg` | 1 quando o carregador está ligado |
| `bat` | O tamanho de lote que o ajuste automático da telemetria está usando. Não é a bateria |
| `cyc` | A duração, em ms, do último ciclo completo de envio de um lote |
| `wip` | Quantas vezes a cópia do bloco aberto do histórico foi gravada desde o boot. Num despertar, o esperado é 1 |

Dentro de um despertar, o console responde antes de o registro ser gravado, então `wip` aparece como 0. O número de cada despertar sai na última linha que o Air imprime antes de dormir:

```text
[AIR] alarm: 00:00:52 wakeSec=51 awake=9342ms target=60000ms wip=1
```

O sufixo `OVERRUN` nessa linha indica que o despertar não coube no intervalo: o Air dormiu o mínimo de 5 s. Aumente o **Intervalo Histórico** ou reduza o que o despertar faz.

No começo de cada despertar, o console mostra também se o rádio vai ligar, como `[AIR] wake: radio=off (pending=3 min=5 skip=0)`. Desde a v2.7.2, a linha `[AIR] clock=` mostra, antes de gravar o registro, a hora com milissegundos e se ela veio do NTP (`ntp`) ou do relógio carregado (`prov`).

## Receita de instalação {#cap-19-instalacao}

1. **Grave a imagem do Air** pelo BOOTSEL: `simut_v<versão>_air.uf2` ([capítulo 3](#cap-03)).
2. **Ligue os sensores** e, se for usar, a chave de alimentação dos sensores no GP16 e o divisor do carregador no GP17 ([capítulo 2](#cap-02)).
3. **Abra o console USB** e pegue a senha do administrador: a moldura `SEC-003` do primeiro boot, ou `enable` seguido de `system admin reset confirm` ([capítulo 18](#cap-18-fabrica)).
4. **Configure o Wi-Fi pelo console:**

   ```text
   SIMUT> enable
   SIMUT# configure terminal
   SIMUT(config)# wifi ssid MinhaRede
   SIMUT(config)# wifi pass <senha do Wi-Fi>
   SIMUT(config)# end
   SIMUT# reload confirm
   ```

5. **Ligue o carregador** durante a configuração, para o Air não hibernar no meio dela. Sem carregador, cada comando e cada pedido da página renovam os 300 s de ociosidade.
6. **Descubra o IP** com `show net status` e entre na interface web com `admin`. Troque a senha.
7. **Configure os sensores** e o **Intervalo Histórico**, que é o período entre despertares ([capítulo 6](#cap-06)). Com o DS18B20, um despertar leva cerca de 9 s: um intervalo de 1 min deixa o Air acordado cerca de 13 % do tempo.
8. **Configure a telemetria** ([capítulo 21](#cap-21)): o coletor, o **Lote mínimo**, que define de quantos em quantos despertares o rádio liga, e o **Lote máximo**. Confira o NTP ([capítulo 10](#cap-10)).
9. **Ajuste o Air,** se precisar: `air idle` e `air charger`.
10. **Faça um backup** pela página **Arquivos** ([capítulo 17](#cap-17-backup)).
11. **Feche a página**, desligue o carregador e rode `air hibernate`, ou deixe a ociosidade acabar.
12. **Confira o ciclo:** acompanhe no coletor as chegadas no ritmo esperado. No primeiro dia, confira no log de eventos se aparece **Boot frio, não veio da hibernação** (412), que num Air instalado indica falta de energia ou reinício.

::: {.figura #fig-19-montagem tipo="foto" arquivo="19-montagem.png" captura="um Air montado: Pico W com um DS18B20 e o resistor de 4,7 kΩ, a bateria e o carregador; os fios do GP16 e do GP17 identificados com etiquetas"}
Um Air montado. O GP16 mostra quando ele está acordado e pode alimentar os sensores; o GP17 detecta o carregador.
:::

::: {.figura #fig-19-painel-controle tipo="web" arquivo="19-painel-controle.png" captura="rota /; largura 1280; sessão admin; imagem air em M0; 1 sensor DS18B20; cartão Registros Pendentes com alguns registros; sem o bloco de espelho do painel"}
O Painel de Controle de um Air em M0: sem o espelho do painel, que não existe nesta imagem.
:::

## Limitações {#cap-19-limitacoes}

- **Experimental.** O único teste longo do ciclo falhou em 10/09/2026, com um sono que nunca terminou depois de 119 ciclos. O vigia no despertar reduz o problema a um reinício, mas a causa não foi confirmada ([O que é o Air](#cap-19-status)).
- **Consumo não medido.** Os 8,2 mA e 17 dias são uma conta ([Tempos medidos e consumo](#cap-19-tempos)).
- **Sem alarmes em M1** ([Alarmes no Air](#cap-19-alarmes)).
- **A hora dos despertares sem rádio é reconstruída.** Na v2.7.1, ela perde até cerca de 1 s por despertar até o próximo NTP ([capítulo 10](#cap-10-air)).
- **Atualização só em M0,** e o Air está perto do limite de tamanho da atualização.
- **Uma janela curta no console em M1:** alguns segundos por despertar, só pelo USB.
- **Baixar a resolução do DS18B20 não encurta o despertar** ([Tempos medidos e consumo](#cap-19-tempos)).
- **As opções do Air ficam fora da configuração principal,** em `/config/air.bin`. Uma atualização as apaga; o backup as guarda ([capítulo 17](#cap-17-sobrevive)).

## Códigos do log de eventos {#cap-19-eventos}

| Código | Evento | Quando aparece |
|---|---|---|
| 411 | **Ciclo Air retido em M0** | Três reinícios pelo vigia seguidos; o contexto é o número deles |
| 412 | **Boot frio, não veio da hibernação** | Um boot que não foi um despertar. Contexto 1: boot limpo (alimentação, RUN, `reload`, atualização); 0: o vigia chegou antes |
| 413 | **Sono limitado a menos de 24 h (alarme é por hora do dia)** | Um sono de 24 h pedido; o contexto são os minutos pedidos |
| 414 | **Wake anterior do Air não completou** | O despertar anterior travou antes do fim e foi reiniciado |
| 567 | **Snapshot de histórico gravado** | O registro de cada ciclo, com o número de registros do bloco no contexto |

Num despertar, a sequência de partida não vai para o log, e sobra um registro por ciclo ([capítulo 16](#cap-16-air)).

## Solução de problemas {#cap-19-problemas}

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O Air não hiberna | O carregador está ligado ou o pino lê nível alto (`chg=1`) | Confira o `air status`. Se não há carregador, confira o divisor, ou use `air charger off` |
| O Air não hiberna, com `chg=0` | Uma aba do Painel de Controle aberta, um gestor de frota consultando sem `quiet=1`, ou comandos no console | Feche a aba; use `/api/status?quiet=1` no gestor |
| O Air nunca liga o rádio | **Lote mínimo** em 0, pendentes abaixo do **Lote mínimo**, ou `skip` maior que 0 | Confira `tel=` e `skip=` no `air status` |
| O coletor recebe menos que o esperado depois de uma queda | Depois de um envio que falha, 5 despertares ficam sem rádio | Normal. Os registros esperam na flash e saem depois |
| A interface web do Air não abre | O Air está em M1 | Ligue o carregador ou desligue e ligue a alimentação |
| `air stop` não tem efeito | Foi digitado pelo Bluetooth, ou fora da janela do despertar | Use o USB durante um despertar, ou o carregador |
| A linha `[AIR] alarm:` termina com `OVERRUN` | O despertar não cabe no **Intervalo Histórico** | Aumente o intervalo |
| O log mostra **Ciclo Air retido em M0** (411) | Três reinícios pelo vigia seguidos | Entre, exporte o log e relate; o Air espera em M0 |
| O log mostra **Wake anterior do Air não completou** (414) | Um despertar travou e foi reiniciado | Guarde o log e relate ([Limitações](#cap-19-limitacoes)) |
| O log mostra **Boot frio, não veio da hibernação** (412) sem ninguém ter mexido | Falta de energia ou reinício | Confira a bateria e a alimentação |
| As horas dos registros derivam cerca de 1 s por despertar | v2.7.1, sem o relógio carregado através do sono | Garanta despertares de telemetria para o NTP corrigir ([capítulo 10](#cap-10-air)) |
| Depois de atualizar, o Air voltou a hibernar em 300 s e o carregador voltou ao GP17 | A atualização apagou o `/config/air.bin` | Restaure o backup, ou ajuste `air idle` e `air charger` de novo |
