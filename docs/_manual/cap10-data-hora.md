# Data e hora {#cap-10}

Este capítulo explica de onde vem a hora do aparelho: o NTP, o fuso horário, o acerto manual, pela web, pelo painel ou pelo console, e o relógio provisório que vale até o primeiro acerto. Mostra também o que acontece com o histórico quando o relógio é corrigido. É para quem instala o aparelho, sobretudo numa rede sem acesso à internet ou num lugar sem Wi-Fi.

## De onde vem a hora {#cap-10-origem}

O aparelho não guarda a hora quando é desligado ou reiniciado; o Air é uma exceção parcial ([O relógio do Air através do sono](#cap-10-air)). Toda vez que liga, ele precisa descobrir que horas são. Há três fontes, nesta ordem de preferência:

1. **NTP:** a hora certa, pela rede. É o padrão de fábrica ([NTP](#cap-10-ntp)).
2. **Acerto manual:** a hora que alguém digitou, pela interface web, pelo painel ou pelo console ([Acerto manual](#cap-10-manual)).
3. **Relógio provisório:** uma estimativa feita a partir do histórico, usada até uma das duas primeiras chegar ([O relógio provisório](#cap-10-provisorio)).

Internamente, o aparelho conta o tempo em UTC. O fuso horário só entra na hora de mostrar a hora e de decidir em qual arquivo diário do histórico cada registro cai ([Fuso horário](#cap-10-fuso)).

## NTP {#cap-10-ntp}

O NTP vem ligado de fábrica. Ele se liga e desliga na página **Configurações**, seção **Data e Hora**, pelo interruptor **Sincronizar automaticamente via NTP**, e o servidor se escolhe na página **Rede**, seção **Servidor de Hora (NTP)**, campo **Endereço do Servidor**. As duas alterações exigem **Salvar e reiniciar** ([capítulo 5](#cap-05), [capítulo 9](#cap-09-ntp)).

### Os servidores {#cap-10-servidores}

O aparelho consulta dois servidores:

| Posição | Servidor |
|---|---|
| Primeiro | O **Endereço do Servidor** da página **Rede**; vazio, `pool.ntp.org` |
| Segundo | `time.nist.gov`, sempre |

O servidor pode ser um nome ou um IP, com até 31 caracteres. Um nome exige DNS funcionando ([capítulo 9](#cap-09-dns)). O NTP usa a porta UDP 123: numa rede com firewall, libere essa porta até o servidor escolhido.

### Tentativas e troca de servidor {#cap-10-tentativas}

O aparelho pede a hora assim que entra na rede. Se ela não chega, ele pede de novo, e a espera entre os pedidos triplica até o teto de 15 min:

| Tentativa | Espera antes dela |
|---|---|
| 1ª | — (ao entrar na rede) |
| 2ª | 20 s |
| 3ª | 1 min |
| 4ª | 3 min |
| 5ª | 9 min |
| 6ª em diante | 15 min |

Depois de três esperas sem resposta, cerca de 4 min 20 s depois do primeiro pedido, o aparelho troca o primeiro servidor por `pool.ntp.org`, se ele já não for esse. A troca vale até o próximo reinício. O log de eventos registra `NTP fallback: <servidor> -> pool.ntp.org`.

Uma queda da rede Wi-Fi zera a contagem: na volta, as esperas recomeçam de 20 s.

### Sem servidor NTP alcançável {#cap-10-sem-ntp}

::: atencao
**Com o NTP ligado, o aparelho só termina de entrar na rede depois do primeiro acerto do relógio.** Até lá, ele tem IP e a interface web funciona, mas:

- a página **Rede** mostra **Desconectado**;
- o cartão **Sinal Wi-Fi** do **Painel de Controle** mostra −100 dBm;
- a telemetria e a linha de alarmes esperam;
- o nome `.local` não responde ([capítulo 9](#cap-09-mdns));
- no boot, o aparelho espera até 30 s pela rede antes de seguir; no painel, a mensagem é **Sincronizando Relógio Global** (até a v2.8.0, **Aguardando roteador**), seguida de **Timeout de rede. Iniciando Offline...**.

Numa rede sem acesso à internet, escolha uma destas saídas:

- aponte o **Endereço do Servidor** para um servidor NTP da própria rede;
- desligue o NTP e acerte o relógio à mão ([Acerto manual](#cap-10-manual));
- acerte o relógio à mão mesmo com o NTP ligado: isso também encerra a espera, e o log registra `NTP OK`.
:::

### O que o log mostra {#cap-10-log}

Os eventos do relógio, na página **Histórico e Logs** ([capítulo 16](#cap-16)):

| Código | Texto do evento | Quando |
|---|---|---|
| 524 | **Hora provisória do flash**: `Provisional: <data> <hora>` | No boot, ao ligar o relógio provisório |
| 13 | **NTP sincronizado**: `NTP OK: <data> <hora>` | O relógio está acertado e o aparelho terminou de entrar na rede. Aparece também a cada reconexão |
| 13 | `NTP fallback: <servidor> -> pool.ntp.org` | Troca de servidor; o contexto é o número de falhas |
| 13 | `NTP disabled — manual RTC mode` | O aparelho entrou na rede com o NTP desligado |
| 13 | `RTC set manually` | Acerto manual. O contexto diz por onde: 1 interface web (`POST /api/set_time`), 2 console (`time`), 3 painel, item 13 do menu, 4 painel, pergunta do boot |
| 408 | **NTP corrigindo timestamps**: `NTP correction: <n>s` | O primeiro acerto, pelo NTP ou à mão, corrigiu o relógio provisório em mais de 5 s; o contexto é a correção, em segundos |
| 409 | **Timestamps corrigidos** | O histórico foi corrigido; o contexto é o número de blocos |
| 410 | **Caches de gráfico invalidados** | Os gráficos serão refeitos com as horas corrigidas |

O log grava só o código e o contexto, não o texto ([capítulo 16](#cap-16)). No evento 13, o contexto separa as origens: 0 é `NTP OK` ou o NTP desligado; de 1 a 4, um acerto à mão; a troca de servidor é um aviso (`WRN`), com o número de falhas. Até a v2.8.0, o acerto à mão também gravava o contexto 0.

Uma correção maior que 1 h aparece como aviso, e não como informação. Depois de uma falta de energia, uma correção grande é normal: ela mede quanto tempo o aparelho ficou desligado ([O relógio provisório](#cap-10-provisorio)). Uma correção grande com o aparelho sempre ligado indica um relógio provisório mal semeado. O contexto do evento 408 vai até 32.767 s, cerca de 9 h; um valor travado nesse número indica uma correção ainda maior.

::: {.figura #fig-10-log-ntp tipo="web" arquivo="10-log-ntp.png" captura="rota /history; largura 1280; sessão admin; aba de eventos carregada logo depois de um boot com NTP; linhas 524 Hora provisória do flash, 14 IP obtido, 13 NTP sincronizado, 408 NTP corrigindo timestamps com contexto de alguns segundos, 409 e 410 visíveis"}
Os eventos de um boot com NTP: a hora provisória, o acerto e a correção do histórico.
:::

## Fuso horário {#cap-10-fuso}

O fuso se configura na página **Configurações**, seção **Identidade**, campo **Fuso Horário** ([capítulo 5](#cap-05-identidade)).

| Item | Valor |
|---|---|
| Formato | Diferença para o UTC em horas inteiras, com sinal: `-3` para Brasília, `9` para Tóquio |
| Faixa | −12 a +14 |
| Fábrica | −3 |
| Aplicação | Exige reinício (grupo `time`) |

O fuso é um número fixo. Não há horário de verão nem fusos com meia hora ou 45 min, como os da Índia (+5:30) ou do Nepal (+5:45). Onde há horário de verão, mude o **Fuso Horário** nas datas de troca. Desde a v2.4.9-beta, o aparelho guarda o fuso só como esse número; nada no firmware lê a variável de ambiente `TZ`.

O fuso decide:

- a hora mostrada no painel e no console;
- o arquivo diário do histórico em que cada registro entra, pela data local ([capítulo 15](#cap-15));
- a conversão da data e da hora digitadas no acerto manual pela web.

Os registros do histórico guardam o instante em UTC. Mudar o fuso não altera os registros já gravados.

::: atencao
**Digitar no campo Fuso Horário já muda o fuso em uso.** Cerca de 0,6 s depois de você digitar, a página pede ao aparelho o ensaio da alteração ([capítulo 5](#cap-05-ensaio)), e esse ensaio aplica o fuso novo à hora em uso sem gravá-lo. O painel e o histórico passam a usar o fuso digitado até o próximo reinício, ou até a próxima reconexão à rede com o NTP ligado, mesmo que você desista da alteração. Se isso acontecer, reinicie o aparelho para voltar ao fuso gravado. No Air isso não acontece, porque ele recusa o ensaio.
:::

## Acerto manual {#cap-10-manual}

Com o NTP desligado, ou sem rede, a hora do aparelho vem do acerto manual. Há três caminhos: a interface web, o painel da release e o console, em todas as imagens. Os três valem na hora, sem reinício, e o primeiro acerto depois do boot corrige a hora dos blocos do histórico que o boot começou sob o relógio provisório ([Quando o primeiro acerto corrige o relógio](#cap-10-correcao)).

::: atencao
**O acerto manual não sobrevive a um reinício.** Depois de qualquer reinício ou falta de energia, o aparelho volta ao relógio provisório, que parte do registro mais recente do histórico e perde o tempo em que o aparelho ficou desligado. Com o NTP desligado, acerte o relógio de novo depois de cada reinício.
:::

Com o NTP ligado, um acerto manual vale até a próxima sincronização.

### Pela interface web

1. Abra **Configurações** e vá até **Data e Hora**.
2. Preencha **Data** e **Hora**, na hora local do fuso configurado.
3. Toque em **Aplicar Agora**.

O aviso **Hora aplicada.** aparece abaixo do botão. O acerto vale na hora, sem reinício e sem passar pelos botões da barra de topo ([capítulo 5](#cap-05-data-hora)). O seletor de data começa em 01/01/2026.

::: {.figura #fig-10-acerto-manual tipo="web" arquivo="10-acerto-manual.png" captura="rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos Data e Hora preenchidos e a mensagem Hora aplicada."}
O acerto manual com o NTP desligado: data, hora e o botão Aplicar Agora da seção.
:::

Para integradores, a rota é `POST /api/set_time` com o corpo `{"epoch": <segundos UTC>}`, com a permissão **Sistema** ([capítulo 26](#cap-26)).

### No painel {#cap-10-painel}

[release]{.img} A tela **Data e hora** abre de dois jeitos:

- **No fim do boot,** sozinha, num aparelho sem rede configurada e com o relógio provisório. Sem rede não há NTP, e o histórico leva a hora do relógio provisório, que perde o tempo em que o aparelho ficou desligado ([O relógio provisório](#cap-10-provisorio)).
- **Pelo menu,** no item 13, **Data e hora**, a qualquer momento, com a permissão **Sistema** [PERM_SYS_CONFIG]{.perm} no painel, a mesma da rota `POST /api/set_time` ([capítulo 11](#cap-11-data-hora)).

A tela tem cinco colunas, `dd / mm / aaaa  hh : mm`, que começam na data e na hora do relógio em uso. Acima de cada valor há uma seta que soma 1; abaixo, uma seta que subtrai 1. Segurada, a seta repete a cada 300 ms. O dia fica dentro do mês: mudar o mês ou o ano ajusta o dia, e 31/03 menos um mês dá 28/02, ou 29/02 num ano bissexto. O dia, o mês, a hora e o minuto dão a volta; o ano para em 2026 e em 2099. Os segundos ficam em zero.

| Botão | Na pergunta do boot | Pelo menu |
|---|---|---|
| Esquerdo | **PULAR**: vai à tela inicial e não muda o relógio | **SAIR**: volta ao menu e não muda o relógio |
| Direito | **SALVAR**: acerta o relógio e vai à tela inicial | **SALVAR**: acerta o relógio e volta ao menu |

Na pergunta do boot, a linha **Sem Wi-Fi, os dados levam esta hora.** aparece em âmbar acima dos botões. A tela está na [figura do capítulo 4](#fig-04-painel-data-hora).

- **Quem pode salvar.** Pelo menu, o aparelho confere a permissão **Sistema** da conta identificada no painel. A pergunta do boot não pede conta nem PIN: o aparelho aceita **SALVAR** sem conta enquanto não houver rede configurada nem relógio acertado. O primeiro acerto, por qualquer caminho, encerra essa exceção; depois dele, um **SALVAR** sem conta toca o som de erro e não muda nada.
- **Ninguém responde.** Depois de 30 s sem toque, o painel volta à tela inicial, como em qualquer tela ([capítulo 11](#cap-11-ocioso)), e o relógio continua o provisório. Um aparelho que volta de uma falta de energia sem ninguém por perto mostra as leituras, não uma pergunta.
- **A pergunta volta a cada boot.** O acerto à mão não sobrevive a um reinício, então um aparelho sem rede pergunta de novo sempre que liga.

No mesmo boot, toda imagem escreve no console `Sem rede, relogio provisorio: conf time AAAA-MM-DD HH:MM:SS` ou, com o aparelho em inglês, `No network, provisional clock: conf time YYYY-MM-DD HH:MM:SS`. O Air escreve a linha num boot acordado (M0), não num despertar do ciclo. O comando que a linha cita funciona em todas as imagens ([No console](#cap-10-console)); no alpha e no Air, que não têm painel, é o jeito de responder à pergunta.

### No console {#cap-10-console}

O console de todas as imagens acerta o relógio, pelo USB ou pelo Bluetooth, com `time <AAAA-MM-DD> <HH:MM:SS>` na hora local; `conf time` é o mesmo comando ([capítulo 14](#cap-14-time)). No console completo, ele vale no modo EXEC e no privilegiado. O comando confere a data como um calendário: o ano vai de 2026 a 2099, e o dia tem de existir no mês. Uma data que o calendário não tem é recusada, e o relógio não muda:

```text
SIMUT> time 2026-02-31 12:00:00
ERROR: Data ou hora invalida (ano >= 2026)
```

Até a v2.8.0, o comando existia só no console completo, o do Air e das imagens de teste, e conferia cada campo só pela faixa: aceitava qualquer dia de 1 a 31 e levava `2026-02-31` a 03/03/2026, sem aviso. Um campo fora da faixa respondia `Valores fora de range (ano >= 2026)`.

## O relógio provisório {#cap-10-provisorio}

Até o primeiro acerto, o aparelho precisa de uma hora para carimbar as medições. Ele usa um relógio provisório:

- **No boot:** o relógio parte do carimbo do registro mais recente do histórico. O log registra **Hora provisória do flash** (524) com a data e a hora escolhidas.
- **Sem nenhum registro no histórico** (aparelho novo ou histórico apagado): o relógio parte de uma data fixa gravada no firmware, 30/07/2026 às 00:00, hora de Brasília.
- **Depois:** o relógio provisório anda com o oscilador do próprio aparelho.

O relógio provisório fica atrasado pelo tempo em que o aparelho ficou desligado, mais o intervalo entre o último registro gravado e o desligamento. Para um reinício de poucos segundos, o erro é pequeno. Para uma falta de energia de horas, é de horas, e o primeiro acerto corrige ([Quando o primeiro acerto corrige o relógio](#cap-10-correcao)).

Na cópia em flash do bloco aberto, o arquivo `/history/.wip`, o aparelho guarda se o relógio que carimbou aquele bloco estava acertado ou era provisório. Os blocos selados nos arquivos do dia não guardam essa marca, e a página de histórico, o CSV e os gráficos não distinguem as duas horas. No boot seguinte, um bloco aberto carimbado com o relógio acertado é aceito como semente. Um carimbado com o relógio provisório só é aceito se o carimbo couber no dia do arquivo dele. Isso impede que um relógio provisório errado semeie o próximo e o erro se acumule de um boot para outro.

[release]{.img} Enquanto o relógio em uso é o provisório, o painel marca a data e a hora da barra de cima com `?` e as pinta de âmbar ([Onde a hora aparece](#cap-10-onde)).

## Quando o primeiro acerto corrige o relógio {#cap-10-correcao}

No primeiro acerto depois de um boot, pelo NTP ou à mão (pela interface web, pelo painel ou pelo console), o aparelho compara a hora certa com a do relógio provisório. Se a diferença passa de 5 s:

1. Registra **NTP corrigindo timestamps** (408), com a diferença em segundos. O texto diz NTP também num acerto à mão.
2. Corrige a hora de cada bloco do histórico que este boot começou, no arquivo do dia e, se um bloco atravessou a meia-noite, no do dia anterior.
3. Registra **Timestamps corrigidos** (409), com o número de blocos corrigidos. Um número negativo indica falha, e aparece como aviso.
4. Registra **Caches de gráfico invalidados** (410): gráficos e mínimos e máximos são refeitos com as horas corrigidas.

O resultado é um histórico sem buraco e sem salto: as medições feitas antes do acerto aparecem na hora em que realmente aconteceram.

Detalhes:

- Blocos gravados em sessões anteriores não são tocados: o relógio que os carimbou já tinha sido acertado naquela sessão.
- O bloco aberto que o boot retomou da sessão anterior, a partir do `.wip`, é selado antes da correção e não é deslocado, nem nos registros que este boot acrescentou a ele: deslocar só uma parte do bloco poderia desordená-lo. A correção vale para os blocos que este boot começou.
- Se alguém está usando o painel, a correção espera 5 s sem toques para começar.
- Se a correção acontece ainda durante o boot, o painel mostra **Corrigindo timestamps (NTP)...**.
- Só o primeiro acerto depois do boot corrige. Um acerto seguinte, à mão ou pelo NTP, muda o relógio dali em diante e não mexe no que já foi gravado.
- Até a v2.8.0, o acerto à mão não corrigia o histórico: os registros gravados antes dele ficavam com as horas do relógio provisório, e só o NTP corrigia.

Com o NTP ligado, a telemetria só começa depois do primeiro acerto, porque o aparelho só termina de entrar na rede depois dele ([Sem servidor NTP alcançável](#cap-10-sem-ntp)).

::: {.figura #fig-10-correcao tipo="diagrama" arquivo="10-correcao.png" captura="linha do tempo: aparelho desligado por 2 h; boot com relógio provisório a partir do último registro, 2 h atrasado; medições carimbadas com a hora provisória; acerto por NTP com correção de +7200 s; os blocos gravados desde o boot deslocados 2 h para a frente; eventos 524, 13, 408, 409 e 410 marcados na linha"}
O que o primeiro acerto por NTP faz com as medições gravadas sob o relógio provisório.
:::

## Onde a hora aparece {#cap-10-onde}

| Lugar | Formato | Fuso |
|---|---|---|
| Painel, no topo do painel principal | `dd/mm/aa - hh:mm:ss`; com o relógio provisório, `dd/mm/aa ? hh:mm:ss`, em âmbar | O do aparelho |
| LCD do alpha | O LCD não mostra a hora | — |
| Interface web, cartão **Data e Hora** do **Painel de Controle** | `dd/mm/aaaa hh:mm:ss` | O do computador ([capítulo 13](#cap-13-cartoes-estado)) |
| Interface web, exportação CSV da página **Histórico e Logs** | ISO 8601 com o deslocamento, como `2026-09-23T14:05:00-03:00` | O do computador ([capítulo 15](#cap-15)) |

O painel marca o relógio provisório. Enquanto nem o NTP nem uma pessoa acertaram o relógio desde o boot, o separador entre a data e a hora passa de ` - ` a ` ? `, e a data e a hora ficam em âmbar, como `01/10/26 ? 09:58:12`. A marca some no primeiro acerto. Até a v2.8.0, o painel mostrava o relógio provisório do mesmo jeito que o acertado.

Se o fuso do computador é diferente do fuso do aparelho, a hora da interface web difere da do painel. Para comparar, use um computador no mesmo fuso do aparelho.

::: {.figura #fig-10-painel-hora tipo="tft" arquivo="10-painel-hora.png" captura="screen dash; 2 sensores ativos; relógio acertado por NTP; recorte da faixa de topo com a data e a hora"}
A data e a hora no topo do painel principal, no fuso do aparelho.
:::

::: {.figura #fig-10-painel-hora-provisoria tipo="tft" arquivo="10-painel-hora-provisoria.png" captura="screen dash; rede configurada e NTP desligado e gravado, reiniciado e sem acerto à mão desde o boot; recorte da faixa de topo com a data e a hora em âmbar e o ? no lugar do -"}
O relógio provisório no topo do painel: a data e a hora em âmbar, separadas por `?`.
:::

## O relógio do Air através do sono {#cap-10-air}

[air]{.img}

O Air passa a maior parte do tempo hibernando, e o rádio só liga em alguns despertares ([capítulo 19](#cap-19)). Nos despertares sem rádio, não há NTP: a hora que o histórico grava é a que o próprio Air reconstrói.

Desde a v2.7.2, o Air carrega o relógio através do sono. Ao dormir, ele guarda o instante em que armou o alarme, com milissegundos. Ao acordar, soma a esse instante a duração do sono, medida pelo relógio de hardware, e o tempo de boot já decorrido, incluindo cerca de 140 ms antes de o firmware começar a contar. A hora sai com erro de cerca de ±0,09 s por despertar (medido em bancada em 23/09/2026). Isso vale também no boot com o carregador conectado.

Na v2.7.1, e quando o sono não trouxe uma hora plausível, o Air reconstrói a hora a partir do registro mais recente mais a duração do sono. Esse método perde o fim do despertar anterior e até cerca de 1 s por despertar, e o erro se acumula até o próximo acerto por NTP.

Nos despertares com rádio, o NTP acerta o relógio e corrige o histórico como em qualquer imagem ([Quando o primeiro acerto corrige o relógio](#cap-10-correcao)).
