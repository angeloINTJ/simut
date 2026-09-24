# Log de eventos {#cap-16}

O log de eventos é o registro do que o aparelho fez: entradas, mudanças de configuração, falhas de rede e de telemetria, reinícios e travamentos. Este capítulo explica o que entra no log, o que fica de fora, onde lê-lo e como interpretar o registro de um travamento. É para quem opera o aparelho e para quem investiga um problema.

## O que é o log de eventos {#cap-16-o-que}

Cada evento é um registro de 12 bytes gravado na flash, com estes campos:

| Campo | O que guarda |
|---|---|
| Data | A hora do aparelho no momento do evento, em segundos. Fica vazia quando o relógio ainda não tinha hora |
| Tempo | Há quanto tempo o aparelho estava ligado, em segundos, até cerca de 194 dias |
| Código | O número do evento, como 300 para uma entrada bem-sucedida. São 155 códigos |
| Contexto | Um número que complementa o código, como o número da conta ou uma contagem |
| Nível | A gravidade do evento |
| Módulo | A parte do firmware que registrou o evento |
| Núcleo | Qual dos dois núcleos do processador registrou o evento, 0 ou 1 |

O registro na flash guarda o código e o contexto, e não um texto. A descrição que você lê na página vem de uma tabela no navegador, pelo código. O texto completo de cada evento, com detalhes como o nome de um arquivo, só existe no console, no momento em que o evento acontece, e no syslog ([Onde ler o log](#cap-16-onde)).

A lista completa dos 155 códigos está no [apêndice B](#ap-b).

### Níveis {#cap-16-niveis}

| Nível | Sigla | O que indica | Vai para a flash |
|---|---|---|---|
| Depuração | `DBG` | Detalhe para desenvolvimento | Nunca |
| Informação | `INF` | Funcionamento normal | Sim, com o filtro de rotina ([Persistência por transição](#cap-16-transicao)) |
| Aviso | `WRN` | Algo fora do normal que o aparelho contornou | Sim |
| Erro | `ERR` | Uma operação falhou | Sim |
| Fatal | `FTL` | O aparelho travou e reiniciou | Sempre |

### Módulos {#cap-16-modulos}

| Módulo | Parte do firmware |
|---|---|
| `APP` | Aplicação: boot, alarmes, gráficos, SIMUT Air |
| `NET` | Rede: Wi-Fi, IP, NTP, mDNS |
| `TEL` | Telemetria e linha de alarmes |
| `STO` | Sistema de arquivos e histórico |
| `WEB` | Servidor web |
| `CFG` | Configuração |
| `CLI` | Console serial e Bluetooth |
| `SENSOR` | Sensores |
| `HIST` | Gravação do histórico |
| `SYS` | Sistema: boot, reinício, memória, autópsia de travamento |
| `DSP` | Painel e LCD |
| `SEC` | Segurança: entradas, permissões, mudanças de configuração |
| `OTA` | Atualização, backup e restauração |

## O que o log guarda {#cap-16-guarda}

O log guarda dois arquivos, `/system.blog` e `/system.old.blog`, de 800 registros cada. Quando o arquivo atual chega a 800 registros, o aparelho apaga o antigo, renomeia o atual para antigo e começa um novo. O primeiro registro do arquivo novo é **Storage rotacionado** (22), com contexto 800.

Assim, o log tem sempre entre 800 e 1.600 registros: os mais recentes. O que é mais antigo que isso se perde. Para guardar o log por mais tempo, envie-o a um coletor syslog ([capítulo 25](#cap-25)).

### Persistência por transição {#cap-16-transicao}

Um aparelho que envia telemetria a cada poucos segundos gastaria o log inteiro em uma hora dizendo que tudo continua bem. Para evitar isso, os eventos de rotina só vão para a flash quando o estado muda.

Os eventos de rotina são agrupados em famílias:

| Família | Eventos de sucesso | Eventos de falha |
|---|---|---|
| Telemetria | **Telemetria enviada** (30), **MQTT publicado** (37), **MQTT conectado** (35) | 31, 32, 33, 36, 547, 543, 544 |
| Linha de alarmes | 550, 552 | 551, 553 |
| Histórico | **Registro de histórico salvo** (510), 566, 567, 513 | 20, 512, 514, 515, 560, 568 |
| Rede | **Conectando WiFi** (10), **IP obtido** (14) | 11, 525, 528, 523 |
| Busca de redes | **Varredura WiFi** (12) | — |

As regras:

1. **O primeiro sucesso de cada família depois do boot** vai para a flash. É a prova de que aquela parte funciona.
2. **A primeira falha** de uma família saudável vai para a flash. As falhas seguintes da mesma família não vão, mesmo que sejam de outro código da família.
3. **O primeiro sucesso depois de uma falha** vai para a flash. É o registro da recuperação.
4. **Uma vez por hora,** cada família grava mais um registro, de sucesso ou de falha, conforme o estado dela. Um log quieto continua mostrando que a parte está viva.
5. **Uma vez por hora,** se algo foi deixado de fora, o aparelho grava **Registros de rotina suprimidos** (5), com o número de registros suprimidos no contexto.

Tudo o que está fora da tabela vai sempre para a flash, com nível de informação ou acima.

::: nota
**O que nunca é filtrado.** Os eventos de segurança, como entradas, recusas e bloqueios, e as mudanças de configuração nunca passam pelo filtro: o log é o único registro de quem fez o quê. Um evento `FTL` também nunca é filtrado, qualquer que seja o código. Avisos e erros que não pertencem a uma família também vão sempre para a flash.
:::

O filtro vale só para a flash. O console, com `debug on`, e o syslog recebem todos os eventos, inclusive os que ficaram de fora ([Onde ler o log](#cap-16-onde)).

::: {.figura #fig-16-transicao tipo="diagrama" arquivo="16-transicao.png" captura="linha do tempo de 3 horas da família Telemetria: pontos cinza pequenos a cada envio (console), pontos grandes onde o registro vai para a flash; flash no 1º envio depois do boot, na 1ª falha (coletor cai), nenhuma nas falhas seguintes, na 1ª volta, e um por hora nos trechos estáveis; marcas 'Registros de rotina suprimidos (5), ctx = N' a cada hora"}
Legenda: o console vê todos os eventos; a flash guarda as transições e um registro por hora de cada família.
:::

### No SIMUT Air {#cap-16-air}

[air]{.img}

No Air, todo despertar é um boot completo, e a sequência de partida gravaria os mesmos oito registros a cada minuto. Por isso, num boot que veio da hibernação, essa sequência não vai para a flash. Sobra um registro por ciclo: **Snapshot de histórico gravado** (567), com o número de registros do bloco no contexto.

Um boot que não veio da hibernação grava a sequência inteira e o evento **Boot frio, não veio da hibernação** (412). Num aparelho que deveria estar dormindo, ele indica uma falta de energia ou um reinício. O ciclo do Air está no [capítulo 19](#cap-19).

### Registros na RAM {#cap-16-ram}

Enquanto o painel está sendo tocado ou uma operação pesada está em andamento, o aparelho segura os registros na RAM, até 32, e os grava logo depois. Um reinício nesse intervalo perde esses registros. Se chegarem mais de 32, os excedentes se perdem, e só o console avisa.

## Onde ler o log {#cap-16-onde}

| Onde | O que mostra | Permissão |
|---|---|---|
| Página **Histórico e Logs**, seção **Eventos do Sistema** | O log inteiro, com filtros e exportação em CSV | **Logs** [PERM_LOGS]{.perm} |
| Console serial, `show system log` | O log inteiro, em texto | Acesso ao console ([capítulo 14](#cap-14-show-log)) |
| Console serial, `debug on` | Cada evento no momento em que acontece, com o texto completo, inclusive os filtrados | Acesso ao console ([capítulo 14](#cap-14-debug)) |
| Coletor syslog | Cada evento com o texto completo, a partir do nível mínimo configurado, inclusive os filtrados | Configuração em [capítulo 5](#cap-05-syslog) e [capítulo 25](#cap-25) |

### Na interface web {#cap-16-pagina}

A seção **Eventos do Sistema** (*System Event Logs*) fica embaixo da página **Histórico e Logs** ([capítulo 15](#cap-15-pagina)). Ela não carrega sozinha: até você pedir, mostra **Clique em 'Carregar' para exibir os logs.**

1. Toque em **Carregar** (*Load*). Uma barra mostra os kilobytes recebidos.
2. Marque os níveis que quer ver. De entrada, só **ERR** vem marcado.
3. Para procurar, digite em **Filtrar...** (*Filter events...*). A tabela mostra só as linhas que contêm o texto.
4. Para ler de novo, toque em **Atualizar** (*Refresh*), que substitui **Carregar** depois da primeira carga.

As caixas de nível:

| Caixa | Mostra |
|---|---|
| **INF** | Informação e depuração |
| **WRN** | Avisos |
| **ERR** | Erros e registros `FTL` |

::: atencao
**De entrada, a tabela esconde tudo o que não é erro.** Marque **INF** e **WRN** para ver o log inteiro, inclusive as entradas, as mudanças de configuração e as atualizações.
:::

A tabela mostra o registro mais recente primeiro, com estas colunas:

| Coluna | Conteúdo |
|---|---|
| **Data** (*Date & Time*) | `dd/mm/aaaa hh:mm:ss`, no fuso do computador. Sem hora gravada, mostra `Boot +` e o tempo desde a partida |
| **Tempo** (*Uptime*) | Há quanto tempo o aparelho estava ligado: `hh:mm:ss`, ou `Nd hh:mm:ss` a partir de um dia |
| **Nível** (*Level*) | `INF`, `WRN`, `ERR` ou `FTL` |
| **Módulo** (*Module*) | O módulo, como `SEC` ou `TEL` |
| **Descrição** (*Event Description*) | O nome do evento, seguido de `[ctx: N]` quando o contexto não é zero |

Os nomes dos eventos aparecem em português, sem acentos, quando o idioma da página é **PT**, e em inglês quando é **EN**. Um código que a página não conhece aparece como `Evento #` e o número.

Se a leitura falha, a tabela mostra `Error fetching logs`. Isso acontece, por exemplo, quando alguém está usando o painel naquele momento. Espere alguns segundos e toque de novo.

::: {.figura #fig-16-eventos tipo="web" arquivo="16-eventos.png" captura="rota /history; largura 1280; sessão admin; seção Eventos do Sistema carregada; caixas INF, WRN e ERR marcadas; cerca de 15 linhas visíveis, incluindo uma entrada (300), uma mudança de configuração (303) e uma falha de telemetria (31)"}
A seção Eventos do Sistema com os três níveis marcados. O registro mais recente fica no topo.
:::

::: {.figura #fig-16-eventos-celular tipo="web" arquivo="16-eventos-celular.png" captura="rota /history; largura 390; sessão admin; seção Eventos do Sistema carregada, rolada até a tabela; só ERR marcado"}
A tabela de eventos no celular, com o filtro de entrada: só erros.
:::

### Exportar o log em CSV {#cap-16-csv}

1. Carregue o log com **Carregar**.
2. Ajuste as caixas de nível e o **Filtrar...**: a exportação leva só as linhas que a tabela mostra.
3. Toque em **Exportar CSV** (*Export CSV*).

O navegador salva `simut_logs_<AAAAMMDD>.csv`, com a data de hoje, e um aviso mostra o número de linhas. Se nenhuma linha passa pelos filtros, o aviso é **Sem dados neste período**. Sem carregar antes, o aviso é **Clique em 'Carregar' primeiro**.

As colunas:

| Coluna | Conteúdo |
|---|---|
| `timestamp_iso` | Data e hora em ISO 8601 com o deslocamento, no fuso do computador. Sem hora gravada, o mesmo `Boot +` da tabela |
| `level` | `INF`, `WRN`, `ERR` ou `FTL` |
| `module` | O módulo |
| `code` | O código do evento |
| `message` | O nome do evento, entre aspas, no idioma da página |
| `context` | O contexto |
| `uptime_sec` | Deveria trazer o tempo ligado em segundos |

As linhas saem em ordem cronológica, do mais antigo para o mais recente, ao contrário da tabela.

::: atencao
**A coluna uptime_sec sai com o texto `undefined` em todas as linhas.** A página preenche essa coluna com um campo que não existe. Para o tempo ligado, use a coluna **Tempo** da tabela ou o `show system log` do console.
:::

### Limpar o log {#cap-16-limpar}

O botão **Limpar** (*Clear*) apaga os dois arquivos do log. Ele exige as permissões **Logs** e **Sistema** [PERM_SYS_CONFIG]{.perm}.

1. Toque em **Limpar**.
2. Confirme a pergunta **Limpar logs?**.
3. A página mostra **Logs limpos.** e recarrega a tabela.

O log novo começa com um registro de segurança: **Config alterada** (303), nível `WRN`, módulo `SEC`, com o número da conta que limpou no contexto. Limpar o log deixa rastro de quem limpou.

A página mostra **Falha ao limpar logs.** quando a conta não tem as duas permissões ou tem uma troca de senha pendente. Com o painel em uso, mostra **Display em uso. Tente novamente em alguns segundos.**

### No console {#cap-16-console}

O comando `show system log` imprime o log inteiro, do registro mais antigo para o mais recente, entre as linhas `--- SYSTEM LOG START ---` e `--- SYSTEM LOG END ---`. Cada linha tem este formato:

```text
1790412345 up3h04m   C0 [INF][SEC   ] code=300 ctx=0
```

| Parte | Significado |
|---|---|
| `1790412345` | A data, em segundos Unix (UTC). `0` quando o relógio não tinha hora |
| `up3h04m` | O tempo ligado: `Ns`, `NmNNs`, `NhNNm` ou `NdNNh` |
| `C0` | O núcleo |
| `[INF]` | O nível |
| `[SEC   ]` | O módulo |
| `code=300 ctx=0` | O código e o contexto |

O `show system log` não traduz os códigos: use o [apêndice B](#ap-b).

Com `debug on`, o console mostra cada evento no momento em que acontece, inclusive os que o filtro deixa fora da flash, neste formato:

```text
[hh:mm:ss][UP hh:mm:ss][C0][INF][SEC] <nome do evento>: <texto> (<contexto>)
```

O nome do evento sai no idioma do pacote instalado. O `: <texto>` só aparece quando o evento tem texto, e o `(<contexto>)` só quando o contexto não é zero. Antes de o relógio ter hora, o primeiro campo é `[BOOT+Ns]`. O console está no [capítulo 14](#cap-14).

## Como ler um travamento {#cap-16-travamento}

Quando o aparelho trava, um vigia de hardware (watchdog) o reinicia. No boot seguinte, o aparelho faz uma autópsia: lê os vestígios que o travamento deixou em registradores que sobrevivem ao reinício e grava o veredito no log como **Boot do sistema** (1) com nível `FTL`. O contexto diz qual foi o travamento.

### O contexto do veredito {#cap-16-veredito}

| Contexto | Nível | Veredito |
|---|---|---|
| `0` | `INF` | Reinício forçado de fora do firmware, como a gravação de um firmware pelo `picotool`. Não é travamento |
| `100` | `FTL` | O núcleo 0 parou de dar sinal de vida, e o núcleo 1 reiniciou o aparelho |
| `200` a `229` | `FTL` | O núcleo 0 parou dentro de um módulo, e o watchdog de hardware reiniciou o aparelho. O módulo é o contexto menos 200 |
| `455` | `FTL` | O mesmo, sem o registro do módulo |
| `300` a `322` | `FTL` | O núcleo 1 congelou numa fase, e o núcleo 0 reiniciou o aparelho. A fase é o contexto menos 300 |
| `400` | `FTL` | O núcleo 1 parou por uma exceção de hardware |

Uma falta de energia ou o botão de reinício não deixam registro `FTL`: o log mostra só a sequência normal de boot.

### Os registros irmãos {#cap-16-irmaos}

Desde a v2.7.0, o veredito do watchdog de hardware (contexto de 200 a 229, ou 455) vem seguido de mais três registros do mesmo código, gravados em sequência. Eles se distinguem pela faixa do contexto:

| Faixa | O que diz | Como ler |
|---|---|---|
| `1000` a `1255` | O módulo em que o núcleo 1 estava | Contexto menos 1000. `1255` quer dizer sem registro |
| `2000` a `2999` | A memória livre no travamento | Contexto menos 2000, em KB |
| `4000` a `32000` | Há quanto tempo o aparelho estava ligado quando travou | Contexto menos 4000, em minutos |

Os vereditos de contexto 100, de 300 a 322 e 400 não têm registros irmãos.

### Os módulos {#cap-16-modulos-travamento}

Os números de módulo das faixas `200` e `1000`:

| Nº | Módulo | Nº | Módulo | Nº | Módulo |
|---|---|---|---|---|---|
| 0 | `BOOT` | 10 | `SAVE_CFG` | 20 | `WEB_HIST` |
| 1 | `IDLE` | 11 | `LOG_FLASH` | 21 | `WEB_SEND` |
| 2 | `WIFI` | 12 | `HIST_FLASH` | 22 | `WEB_HSCAN` |
| 3 | `WEB_SERVER` | 13 | `CORE1_LOCK` | 23 | `TEL_COLLECT` |
| 4 | `STORAGE_RD` | 14 | `C1_ENDLOCK` | 24 | `TEL_BUILD` |
| 5 | `STORAGE_WR` | 15 | `C1_RESET` | 25 | `TEL_SEND` |
| 6 | `SENSOR` | 16 | `C1_LAUNCH` | 26 | `STO_MAINT` |
| 7 | `TELEMETRY` | 17 | `C1_KILLED` | 27 | `HIST_SAMPLE` |
| 8 | `DISPLAY` | 18 | `LOOP` | 28 | `HIST_WIP` |
| 9 | `CLI` | 19 | `WEB_POLL` | 29 | `HIST_SEAL` |

As fases do núcleo 1, da faixa `300`: 0 `INIT`, 1 `RESUME_MTX`, 2 `LOOP_TOP`, 3 `PARK`, 4 `TOUCH_RD`, 5 `TOUCH_HDL`, 6 `THEME_MTX`, 7 `DASH_MTX`, 8 `SNAPSHOT`, 9 `RENDER`, 10 `ALARM_FLASH`, 11 `R_BOOT`, 12 `R_FULL`, 13 `R_TOPBAR`, 14 `R_TOP_PANEL`, 15 `R_MINMAX`, 16 `R_BOT_PANEL`, 17 `R_ALARM`, 18 `LOOP_TAIL`, 19 `LOOP_DELAY`, 20 `W_WFE`, 21 `UI_GRAPH`, 22 `UI_SETTINGS`.

### Um exemplo {#cap-16-exemplo}

O `show system log` de um aparelho que travou atendendo a interface web:

```text
1790412281 up3h04m   C0 [INF][WEB   ] code=575 ctx=0
         0 up2s      C0 [FTL][SYS   ] code=1 ctx=219
         0 up2s      C0 [FTL][SYS   ] code=1 ctx=1008
         0 up2s      C0 [FTL][SYS   ] code=1 ctx=4184
         0 up2s      C0 [FTL][SYS   ] code=1 ctx=2031
```

A leitura:

1. `ctx=219`: o watchdog de hardware reiniciou o aparelho, com o núcleo 0 parado no módulo 19, `WEB_POLL`, que atende a interface web.
2. `ctx=1008`: o núcleo 1 estava no módulo 8, `DISPLAY`, desenhando o painel.
3. `ctx=4184`: o aparelho estava ligado havia 184 min, 3 h 04 min, quando travou. Isso confere com o `up3h04m` do último registro antes do travamento.
4. `ctx=2031`: havia 31 KB de memória livre.

Na página, os mesmos quatro registros aparecem no topo da tabela, em ordem inversa, como **Boot do sistema** com `[ctx: 2031]`, `[ctx: 4184]`, `[ctx: 1008]` e `[ctx: 219]`, nível `FTL` e módulo `SYS`. Marque **ERR** para vê-los.

::: nota
**Por que a data fica vazia.** A autópsia roda no começo do boot, antes de o relógio ser acertado, então os registros dela costumam sair com a data `0`, que a página mostra como `Boot +`. O travamento aconteceu segundos antes desse boot: use a data dos registros vizinhos para situá-lo.
:::

::: nota
**O resto da frase.** No boot, o console imprime o veredito por extenso, como `HW WATCHDOG: Core 0 loop stalled (no feed in WDT window). C0=[WEB_POLL] C1=[DISPLAY] at up=...`. Esse texto não vai para a flash. Os registros irmãos guardam os números que importam dele, para que o veredito possa ser lido dias depois.
:::

Se o log mostra travamentos repetidos, anote os contextos, exporte o log e siga o [capítulo 30](#cap-30).

## Eventos que valem acompanhar {#cap-16-acompanhar}

| Evento | O que indica | Onde saber mais |
|---|---|---|
| **Boot do sistema** (1) com `FTL` | Um travamento | [Como ler um travamento](#cap-16-travamento) |
| **Login bem-sucedido** (300) | Uma entrada na web, com o número da conta no contexto. O mesmo código, com contexto 0, marca uma saída por **Sair** | [Capítulo 13](#cap-13-sessoes) |
| **Falha de login** (301) e **Acesso não autorizado** (302) | Tentativas de entrada e acessos recusados | [Capítulo 8](#cap-08-auditoria), [capítulo 29](#cap-29-auditoria) |
| **Config alterada** (303) | Uma mudança de configuração, um backup, uma restauração ou uma atualização, com o número da conta no contexto | [Capítulo 17](#cap-17) |
| **Falha de telemetria** (31) | O coletor não recebeu | [Capítulo 21](#cap-21-eventos) |
| **WiFi desconectado** (11) | A rede caiu | [Capítulo 9](#cap-09-log) |
| **NTP corrigindo timestamps** (408) | O relógio foi corrigido e o histórico, ajustado | [Capítulo 10](#cap-10-correcao) |
| **Registros de rotina suprimidos** (5) | Quantos eventos de rotina ficaram fora da flash na última hora | [Persistência por transição](#cap-16-transicao) |
| **Boot frio, não veio da hibernação** (412) | [air]{.img} Uma falta de energia ou reinício num Air | [Capítulo 19](#cap-19) |
