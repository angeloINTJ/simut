# Histórico {#cap-15}

Este capítulo explica o que o aparelho grava no histórico, como guarda, quanto cabe e o que acontece quando a memória enche. Mostra também a página **Histórico e Logs** de ponta a ponta, com o gráfico, o calendário e a exportação em CSV. É para quem opera o aparelho e para quem precisa tirar os dados dele.

## O que o aparelho grava {#cap-15-o-que}

A cada **Intervalo Histórico**, o aparelho grava um registro. O intervalo vai de 1 min a 1440 min, e o de fábrica é 1 min ([capítulo 6](#cap-06-intervalo-historico)).

Cada registro tem:

- a hora, em segundos;
- um valor para cada grandeza de cada sensor ativo: temperatura, umidade ou pressão, conforme o tipo do sensor.

O valor gravado é a média das últimas leituras, já com a correção de calibração. É o mesmo valor que o painel e a página mostram naquele instante ([Leitura e histórico](#cap-06-intervalos)).

| Grandeza | Resolução gravada |
|---|---|
| Temperatura | 0,01 °C |
| Umidade | 0,1 % |
| Pressão | 0,1 hPa |

Regras do registro:

- **Sensor em erro ou ausente:** as grandezas dele entram no registro sem valor, e as dos outros sensores são gravadas normalmente. No gráfico, uma falta mais longa que 3,5 intervalos aparece como um trecho interrompido ([O gráfico](#cap-15-grafico)).
- **Nenhum sensor com valor:** o aparelho não grava o registro. O log de eventos registra **Histórico pulado: schema não cobre sensor ativo** (515) uma vez, e o mesmo código de novo quando a gravação volta.
- **Nenhum sensor ativo:** o aparelho não grava e registra **Histórico pulado: schema V4 vazio** (514).
- **A hora:** o aparelho sempre carimba o registro com a hora que tem, seja a do NTP, a de um acerto manual ou a do relógio provisório ([capítulo 10](#cap-10-origem)).
- **O primeiro registro depois de um reinício** não espera o intervalo inteiro, desde que o relógio já esteja acertado ([Leitura e histórico](#cap-06-intervalos)).

[air]{.img} No SIMUT Air, o aparelho grava um registro a cada despertar ([capítulo 19](#cap-19)).

## Como o histórico é guardado {#cap-15-guardado}

O histórico fica no sistema de arquivos do próprio aparelho, na pasta `/history`:

| Arquivo | O que é |
|---|---|
| `AAAAMMDD.h5` | Um arquivo por dia, como `20260923.h5`. A data é a do fuso do aparelho ([capítulo 10](#cap-10-fuso)) |
| `.wip` | Uma cópia do bloco aberto, que ainda está na RAM |

O formato se chama V5. Ele é compacto e se descreve sozinho: cada arquivo começa com uma descrição dos canais gravados, com a grandeza e a escala de cada um. Quem tem o arquivo lê os valores sem precisar saber qual firmware o escreveu.

### Blocos {#cap-15-blocos}

Os registros são agrupados em blocos de até 60. Cada bloco leva o próprio código de verificação (CRC) e o mínimo e o máximo de cada canal. Um bloco corrompido custa só aquele bloco, e não o dia inteiro: com o intervalo de fábrica de 1 min, é 1 h de dados.

O caminho de um registro é este:

1. O registro entra no **bloco aberto**, na RAM.
2. Logo depois, o aparelho copia o bloco aberto inteiro para `/history/.wip`. Se o painel está sendo tocado ou uma operação pesada está em andamento, a cópia espera e é tentada de novo 2 s depois.
3. Quando o bloco chega a 60 registros, o aparelho o **sela**: acrescenta o bloco ao arquivo do dia e apaga o `.wip`. O log registra **Bloco de histórico selado** (566), com o número de registros.

O bloco também é selado antes de completar 60 registros quando o dia muda, porque um bloco nunca atravessa dois arquivos, e quando o NTP corrige o relógio ([capítulo 10](#cap-10-correcao)).

Com o intervalo de fábrica, um bloco cobre 1 h. Com intervalos maiores, cobre mais: 60 registros de 10 min são 10 h. A página do histórico busca o bloco aberto à parte, então o gráfico e o CSV chegam sempre ao registro mais recente ([De onde vêm os dados](#cap-15-dados)).

::: {.figura #fig-15-caminho tipo="diagrama" arquivo="15-caminho.png" captura="fluxo da esquerda para a direita: sensores → 'registro a cada Intervalo Histórico' → caixa 'bloco aberto (RAM), até 60 registros'; seta tracejada 'cópia a cada registro' para '/history/.wip'; seta cheia 'selar (60 registros, virada do dia, correção do NTP)' para '/history/AAAAMMDD.h5', desenhado como uma pilha de blocos, cada um com 'CRC' e 'mín/máx'; ao lado, uma fila de arquivos diários com o mais antigo marcado 'apagado acima de 86 %'"}
Legenda: o registro nasce na RAM, é copiado para o `.wip` na hora e vai para o arquivo do dia quando o bloco sela.
:::

### Falta de energia e reinício {#cap-15-energia}

Como o bloco aberto é copiado a cada registro, uma falta de energia perde no máximo o último registro. No boot seguinte, o aparelho confere o `.wip`:

- **Válido e com espaço:** o aparelho retoma o bloco de onde parou, e os registros novos continuam nele.
- **Válido e cheio:** o aparelho o acrescenta ao arquivo do dia dele.
- **Corrompido:** o aparelho o descarta e registra um aviso **Snapshot de histórico gravado** (567) com contexto −1.

Um reinício pedido, como **Salvar e reiniciar**, grava o bloco aberto antes de reiniciar. A exceção é a restauração de um backup, que substitui o sistema de arquivos inteiro ([O histórico e a atualização de firmware](#cap-15-ota)).

### Quando o conjunto de sensores muda {#cap-15-schema}

Quando você liga, desliga ou troca um sensor e o aparelho reinicia, o arquivo do dia recebe uma descrição nova dos canais, e os registros seguem nele. O que foi gravado antes continua legível com a descrição antiga. O log registra **Schema de histórico divergente** (568) com o número de canais novo.

Cada canal é identificado pelo slot e pela grandeza, e não pelo ID de hardware do sensor. Veja o efeito disso na página em [Nomes dos sensores](#cap-15-nomes). Para refazer o arquivo do dia pela configuração atual, use **Recompor o histórico agora** ([capítulo 6](#cap-06-recompor)).

::: atencao
**Não guarde outros arquivos em `/history`.** A cada boot, o aparelho apaga dessa pasta tudo o que não for um arquivo `.h5`, o `.wip` ou o `README.txt`, e registra **Histórico legado apagado** (569) com o número de arquivos apagados.
:::

## Capacidade e limpeza automática {#cap-15-capacidade}

O sistema de arquivos tem 1 MiB. Ele guarda o histórico e também a configuração, o log de eventos, os pacotes de idioma, os temas, a calibração e o par de certificados do HTTPS.

**Quanto cabe.** Medido em 31/07/2026, em arquivos de bancada: 5,38 bytes por registro com 11 canais a um registro por minuto. Isso dá cerca de 7,6 KiB por dia, e 116 dias em 86 % de 1 MiB. O número real depende do seu caso:

- mais canais ocupam mais espaço por registro;
- valores que mudam pouco comprimem melhor que valores que oscilam;
- um intervalo maior grava menos registros por dia e estica a retenção.

**Limpeza automática.** O aparelho confere a ocupação uma vez por minuto e toda vez que cria o arquivo de um dia novo. Acima de 86 %:

1. Apaga o arquivo de dia mais antigo, pela data do nome.
2. Apaga no máximo dois arquivos por vez, para não prender o rádio. Se ainda estiver acima do limite, registra **Budget de limite de storage excedido** (562) e continua apagando um arquivo a cada 15 s.
3. Para quando a ocupação volta a 86 % ou menos.

O arquivo do dia corrente nunca é apagado. Se ele for o mais antigo que restou, o aparelho para e registra **Pulando arquivo de log ativo** (563). O nome do evento fala em log, mas o arquivo em questão é o do histórico.

::: atencao
**Os 86 % contam o sistema de arquivos inteiro.** Tudo o que você envia pela página **Arquivos**, como temas, pacotes de idioma e páginas em `/web`, ocupa o espaço do histórico. Acima do limite, o aparelho apaga dias antigos para abrir espaço.
:::

::: nota
**Um dia apagado não volta.** A telemetria envia a partir do histórico ([capítulo 21](#cap-21)). Se você precisa guardar mais do que cabe no aparelho, mantenha um coletor de telemetria ou exporte o histórico periodicamente. A ocupação aparece no cartão **Armazenamento** do **Painel de Controle** ([capítulo 13](#cap-13-cartoes-estado)).
:::

## A página Histórico e Logs {#cap-15-pagina}

Abra a gaveta de navegação e toque em **Histórico e Logs** (*History & Logs*). O endereço é `/history`, por exemplo `http://192.0.2.10/history`.

A página tem duas partes:

- em cima, os gráficos do histórico, sob o título **Telemetria** (*Sensor Telemetry*), com o seletor de sensores, o calendário, o gráfico e a exportação;
- embaixo, **Eventos do Sistema** (*System Event Logs*), o log de eventos, descrito no [capítulo 16](#cap-16-pagina).

**Permissões.** A página abre com **Histórico** [PERM_HISTORY]{.perm} ou com **Logs** [PERM_LOGS]{.perm}. Os gráficos exigem **Histórico**. Como a página baixa os arquivos de cada dia, ver os dias anteriores exige também **Leitura** [PERM_FILE_READ]{.perm} ([capítulo 8](#cap-08-permissoes)).

::: atencao
**Sem a permissão Leitura, o gráfico só mostra o bloco aberto.** A página não consegue listar os arquivos do dia, e o gráfico e o CSV ficam só com os registros que ainda estão na RAM, no máximo 60. O calendário marca os dias, mas eles aparecem sem dados. Dê à conta **Histórico** e **Leitura** juntas.
:::

::: {.figura #fig-15-pagina tipo="web" arquivo="15-pagina.png" captura="rota /history; largura 1280; sessão admin; imagem release; 3 sensores ativos (DS18B20, DHT22, BME280); período 24h; um sensor selecionado; histórico com 20 dias; seção de eventos ainda não carregada"}
A página Histórico e Logs: à esquerda, o seletor de sensores e o calendário; à direita, os mínimos e máximos, o gráfico e os controles de período; embaixo, os eventos do sistema.
:::

### Escolher os sensores {#cap-15-sensores}

O botão **Origem:** (*Sensors*) abre a lista dos sensores configurados. Cada linha tem uma caixa de marcar, um ponto com a cor da série no gráfico, o ID de hardware e o nome.

- Ao abrir a página, só o primeiro sensor da lista vem marcado.
- Marque outros para compará-los no mesmo gráfico. Cada mudança recarrega o gráfico.
- Pelo menos um sensor fica marcado: desmarcar o último marca o primeiro de novo.
- O botão mostra o ID e o nome do sensor quando só um está marcado, e **N selecionados** quando há mais de um.
- A escolha não é lembrada: ao recarregar a página, volta só o primeiro sensor.

::: {.figura #fig-15-sensores tipo="web" arquivo="15-sensores.png" captura="rota /history; largura 1280; sessão admin; lista Origem aberta com 3 sensores, dois marcados; recorte do cartão da esquerda"}
A lista de sensores aberta. O ponto colorido é a cor da série no gráfico.
:::

### O período {#cap-15-periodo}

O seletor abaixo do gráfico escolhe o tamanho da janela:

| Opção | Janela |
|---|---|
| `1h` | 1 hora |
| `6h` | 6 horas |
| `24h` | 24 horas, a opção de entrada |
| `7d` | 7 dias |
| `1M` | 30 dias |
| `1A` | 365 dias |
| `MAX` | Todo o histórico guardado no aparelho |

A janela termina na hora atual do aparelho, e não na do computador. As setas dos dois lados do seletor movem a janela um período para trás ou para a frente. A seta para a frente não passa da hora atual.

Acima do gráfico, o título mostra a janela: `dd/mm  hh:mm - hh:mm` quando ela começa e termina no mesmo dia, e `dd/mm - dd/mm` quando não.

### O calendário {#cap-15-calendario}

O **Calendário** (*Monthly Calendar*) mostra um mês. As setas ao lado do nome do mês trocam de mês.

- Os dias que têm arquivo no aparelho aparecem destacados. Só eles respondem ao toque.
- Toque num dia destacado para ver aquele dia inteiro, da 0h às 24h, em 24h.
- Um dia destacado pode ter só parte das horas: o destaque diz que o arquivo existe, não que o dia está completo.

::: {.figura #fig-15-calendario tipo="web" arquivo="15-calendario.png" captura="rota /history; largura 1280; sessão admin; mês de setembro de 2026 com os dias 1 a 23 destacados; dia 22 selecionado; recorte do calendário"}
O calendário com os dias que têm dados destacados. O dia tocado fica selecionado, e o gráfico mostra as 24 horas dele.
:::

### O gráfico {#cap-15-grafico}

**Eixos.** A temperatura usa o eixo da esquerda, em °C. A umidade usa um eixo à direita, em %RH, e a pressão, um segundo eixo à direita, em hPa. Os eixos da direita só aparecem quando há série daquela grandeza.

**Cores.** Cada sensor tem uma cor quente para a temperatura. A umidade é sempre azul, e a pressão, roxa. Com mais de um sensor, a umidade e a pressão de cada um ganham um tracejado diferente, para que a série não se identifique só pela cor.

**Legenda.** Com mais de uma série, a legenda aparece no topo do gráfico. Toque num item da legenda para esconder ou mostrar aquela série.

**Leitura de um ponto.** Passe o mouse sobre o gráfico, ou toque nele no celular. Aparece a data e a hora, `dd/mm/aaaa hh:mm:ss`, e o valor de cada série naquele instante. Quando o ponto resume vários registros, o valor vem seguido da faixa `[mínimo … máximo]` daquele trecho.

**Lacunas.** Um trecho sem registros aparece interrompido, e não ligado por uma reta. O gráfico interrompe a linha quando dois registros seguidos estão separados por mais de 3,5 vezes o **Intervalo Histórico**, com o mínimo de 90 s.

**Mínimos e máximos.** Com um só sensor marcado, a faixa acima do gráfico mostra o máximo e o mínimo de cada grandeza na janela, como **Max Temp:** e **Min Temp:**, com uma casa decimal. Eles são calculados sobre todos os registros da janela, e não só sobre os pontos desenhados. Com mais de um sensor marcado, a faixa some.

::: {.figura #fig-15-grafico-dois tipo="web" arquivo="15-grafico-dois.png" captura="rota /history; largura 1280; sessão admin; dois sensores marcados, um DS18B20 e um DHT22; período 24h; legenda visível; tooltip aberto sobre um ponto; eixos °C e %RH"}
Dois sensores no mesmo gráfico: temperatura no eixo da esquerda, umidade tracejada no da direita, e a legenda no topo.
:::

### Baldes: como meses cabem na tela {#cap-15-baldes}

O gráfico desenha no máximo um ponto por pixel de largura. Para isso, divide a janela em baldes: tantos quantos são os pixels de largura do gráfico, com o mínimo de 300 e o máximo de 1200.

- **Poucos registros:** até 1,6 registro por balde, o gráfico desenha cada registro como ele é.
- **Muitos registros:** cada balde guarda o mínimo, o máximo e a média dos registros que caíram nele. A linha é a média, e uma faixa sombreada vai do mínimo ao máximo.

Assim, um pico de um minuto num gráfico de um mês continua visível na faixa. O registro mais recente é sempre desenhado com a própria hora.

::: {.figura #fig-15-baldes tipo="web" arquivo="15-baldes.png" captura="rota /history; largura 1280; sessão admin; um sensor DS18B20 de geladeira; período 7d; faixa mínimo-máximo visível em torno da linha média, com os ciclos de degelo; tooltip mostrando o valor e a faixa entre colchetes"}
Uma semana em baldes: a linha é a média de cada balde, e a faixa sombreada vai do mínimo ao máximo. O pico do degelo aparece mesmo resumido.
:::

### De onde vêm os dados {#cap-15-dados}

A página não pede ao aparelho um gráfico pronto. Ela baixa os arquivos `.h5` dos dias da janela e decodifica tudo no navegador. Depois, se a janela chega até os últimos 75 min, ela busca o bloco aberto, que ainda não está em arquivo nenhum.

- **Ordem:** do dia mais novo para o mais antigo. Uma barra mostra **Arquivos: N/M** e os kilobytes recebidos.
- **Cancelar:** o botão **Cancelar** (*Cancel*), dentro da barra, interrompe a carga. O gráfico mostra o que já chegou, que é a parte mais recente.
- **Falhas:** cada arquivo tem até 3 tentativas. Se algum não chega, ou se você cancela, aparece o aviso **Parcial: N perdidas**, em que N é o número de arquivos que faltaram.
- **Memória da página:** um dia fechado não muda, então a página guarda os arquivos que já baixou. Trocar o período ou os sensores não baixa de novo o que ela já tem. O arquivo do dia corrente e o bloco aberto são buscados a cada vez.

Mensagens no lugar do gráfico:

| Mensagem | Quando aparece |
|---|---|
| **Carregando...** | Durante a carga |
| **Sem dados.** | Nenhum registro dos sensores marcados na janela. Escolha outro período ou outro dia no calendário |
| **Erro.** | A página não conseguiu ler o estado do aparelho antes de começar. Recarregue a página |
| **Conexão perdida.** | A carga falhou no meio. Confira a rede e toque de novo no período |

::: {.figura #fig-15-carregando tipo="web" arquivo="15-carregando.png" captura="rota /history; largura 1280; sessão admin; período MAX com 60 dias no aparelho; carga em andamento na metade, barra mostrando Arquivos: 31/60 e os KB recebidos, botão Cancelar visível"}
A carga de um período longo: a barra conta os arquivos baixados, e Cancelar deixa na tela a parte mais recente.
:::

::: nota
**Por que a página baixa os arquivos.** O aparelho só entrega bytes que já estão na flash, e o trabalho de decodificar e resumir fica com o navegador. Por isso um período longo custa pouco ao aparelho e não atrapalha as medições.
:::

### Nomes dos sensores {#cap-15-nomes}

Os arquivos do histórico não guardam o nome nem o ID de hardware dos sensores. Eles guardam o slot e a grandeza de cada canal. A página rotula os dados de cada slot com o ID e o nome que o slot tem agora.

Consequências:

- Se você renomeia um sensor ou troca o ID de hardware dele, os dias anteriores aparecem com o nome e o ID novos.
- Se você troca o sensor físico de um slot, os dados dos dois sensores aparecem como uma série só. Para ver só um deles, escolha a janela pelo período ou pelo calendário.
- Os dados de um slot que não está mais configurado não aparecem na página, nem no gráfico, nem no CSV. Eles continuam nos arquivos `.h5` e podem ser lidos no computador ([Ferramentas para o computador](#cap-15-ferramentas)).

### Fuso do computador {#cap-15-fuso}

A página mostra as horas no fuso do computador, e o aparelho nomeia os arquivos pelo fuso dele. Use um computador no mesmo fuso do aparelho: com fusos diferentes, o dia do calendário e as horas do gráfico e do CSV não coincidem com os do painel ([capítulo 10](#cap-10-onde)).

### No celular {#cap-15-celular}

Na largura de um celular, os dois cartões ficam um sobre o outro: primeiro o seletor e o calendário, depois o gráfico. Toque no gráfico para ler um ponto. O toque não impede a rolagem da página.

::: {.figura #fig-15-celular tipo="web" arquivo="15-celular.png" captura="rota /history; largura 390; sessão admin; um sensor; período 24h; tooltip aberto por toque"}
A página no celular, com o gráfico abaixo do calendário e a leitura de um ponto aberta pelo toque.
:::

## Exportar o histórico em CSV {#cap-15-csv}

A exportação leva para um arquivo CSV exatamente a janela e os sensores que estão no gráfico.

1. Escolha os sensores em **Origem:**.
2. Escolha o período, ou toque num dia do calendário, e espere o gráfico carregar.
3. Toque em **⤓ CSV**, abaixo do gráfico.
4. Espere a caixa **Exporting...** terminar. Ela mostra a porcentagem, o tempo estimado e quantos dias chegaram (`OK`) ou falharam (`err`). O botão **Cancelar** interrompe a exportação e salva o que já chegou.
5. O navegador salva o arquivo, e um aviso mostra o número de linhas.

Se você tocar em **⤓ CSV** antes de carregar o gráfico, a página avisa **Carregue um intervalo no gráfico**. Se a janela não tem registros, o aviso é **Sem dados neste período**.

Enquanto a exportação roda, os controles do gráfico ficam travados. A exportação aproveita os arquivos que o gráfico já baixou e só vai ao aparelho pelo que falta, pelo arquivo do dia corrente e pelo bloco aberto.

::: {.figura #fig-15-exportando tipo="web" arquivo="15-exportando.png" captura="rota /history; largura 1280; sessão admin; exportação em andamento de um período 1M, caixa Exporting... com 45 %, tempo estimado, contagem OK e err, e o botão Cancelar"}
A caixa da exportação em andamento. Cancelar salva o CSV com o que já chegou, com _partial no nome.
:::

### O arquivo {#cap-15-csv-arquivo}

| Situação | Nome do arquivo |
|---|---|
| Um sensor | `simut_history_s<slot>_<AAAAMMDD>.csv` |
| Vários sensores | `simut_history_n<quantidade>_<AAAAMMDD>.csv` |
| Exportação cancelada | `_partial` antes da data, como `simut_history_s2_partial_20260916.csv` |

A data no nome é a do começo da janela.

O aviso final diz `N linhas`. Se você cancelou, diz `cancelado: N linhas`. Se algum dia falhou, diz `com falha: N linhas (K chunks falhos)`, em que K é o número de dias que não chegaram.

### As colunas {#cap-15-csv-colunas}

O arquivo tem uma linha por grandeza por registro: um DHT22 gera duas linhas por registro, uma de temperatura e uma de umidade.

| Coluna | Conteúdo |
|---|---|
| `timestamp_iso` | Data e hora no fuso do computador, em ISO 8601 com o deslocamento, como `2026-09-23T14:05:00-03:00` |
| `sensor_id` | O ID de hardware atual do slot |
| `sensor_name` | O nome atual do slot, entre aspas duplas. Aspas duplas no nome viram aspas simples |
| `value` | O valor, com ponto decimal: duas casas para temperatura e umidade, uma para pressão |
| `unit` | `°C`, `%RH` ou `hPa` |

Exemplo, com um DHT22 chamado Câmara fria e um intervalo de 1 min:

```text
timestamp_iso,sensor_id,sensor_name,value,unit
2026-09-23T14:05:00-03:00,camara1,"Câmara fria",4.25,°C
2026-09-23T14:05:00-03:00,camara1,"Câmara fria",81.30,%RH
2026-09-23T14:06:00-03:00,camara1,"Câmara fria",4.31,°C
2026-09-23T14:06:00-03:00,camara1,"Câmara fria",81.20,%RH
```

Detalhes do formato:

- As linhas saem em ordem cronológica.
- Um canal sem valor naquele registro não gera linha.
- O separador é a vírgula e o decimal é o ponto. Numa planilha em português, importe o arquivo indicando esses dois caracteres.
- O arquivo começa com a marca de UTF-8 (BOM), para que as planilhas leiam os acentos dos nomes.

## Baixar os arquivos .h5 {#cap-15-h5}

Os arquivos do dia também podem ser baixados um a um, como estão na flash:

1. Abra a página **Arquivos** ([capítulo 17](#cap-17)).
2. Toque na pasta `history`.
3. Toque no nome do arquivo, como `20260923.h5`.

Baixar da pasta `/history` exige **Leitura** e **Histórico**.

O arquivo do dia corrente não tem o bloco aberto. Para ter também os últimos minutos, use o CSV. O `.wip` pode ser baixado, mas é só a cópia do bloco aberto.

## Ferramentas para o computador {#cap-15-ferramentas}

O repositório do SIMUT traz, em `tools/`, dois programas em Python 3 que leem os arquivos `.h5` sem o aparelho. Os dois usam só a biblioteca padrão do Python.

### history_v5.py: de .h5 para CSV {#cap-15-history-v5}

```bash
python3 tools/history_v5.py --dump-csv 20260923.h5 --out 20260923.csv
python3 tools/history_v5.py --dump-csv 20260923.h5 --interval 300 > 20260923.csv
```

- **`--interval`** é o **Intervalo Histórico** do aparelho em segundos. O padrão, 60, serve para o intervalo de fábrica de 1 min. Com o valor errado, as horas de cada registro saem erradas.
- **A primeira coluna** é `epoch`: a hora em segundos Unix, em UTC.
- **As outras colunas** são uma por canal, com a grandeza, um número e a unidade, como `temp0 [°C]`, `hum1 [%]` e `temp8 [°C]`. O número é o slot vezes 8 mais o canal: 0 para temperatura, 1 para umidade e 2 para pressão. `temp8` é a temperatura do slot 1.
- **Célula vazia** quer dizer canal sem valor naquele registro.
- **Bloco corrompido:** o programa pula o bloco e avisa na saída de erro, com `# skipped chunk at <posição>: <motivo>`. A última linha da saída de erro diz quantos registros foram lidos, como `# 1440 records`.

::: atencao
**O cabeçalho vem da primeira descrição do arquivo.** Num dia em que o conjunto de sensores mudou, as linhas depois da mudança seguem a descrição nova, e o cabeçalho não acompanha. Nesses dias, prefira o CSV da página, que tem uma linha por grandeza e o nome de cada sensor.
:::

### h5_day_merge.py: juntar duas cópias do mesmo dia {#cap-15-merge}

```bash
python3 tools/h5_day_merge.py 20260923.h5 copia-do-backup/20260923.h5 copia-do-aparelho/20260923.h5
```

Junta duas ou mais versões do arquivo do mesmo dia num arquivo só, no computador. Serve, por exemplo, para juntar a cópia do dia que está num backup com a cópia baixada do aparelho depois de uma atualização ([O histórico e a atualização de firmware](#cap-15-ota)).

- Copia cada bloco sem alterá-lo e ordena os blocos pela hora.
- Blocos idênticos entram uma vez só.
- Recusa arquivos de dias diferentes, arquivos com descrições de canais diferentes e arquivos com mais de uma descrição.
- Recusa um bloco corrompido, a não ser com `--skip-bad`.
- Recusa dois blocos diferentes com a mesma hora de início, a não ser com `--on-conflict keep-longer`, que fica com o de mais registros.

## O gráfico no painel {#cap-15-painel}

[release]{.img}

O painel também desenha o histórico de cada sensor, com os mínimos e máximos. Como chegar e o que ele mostra está no [capítulo 11](#cap-11-grafico). O LCD do alpha não tem gráfico: no alpha e no Air, o histórico se vê pela interface web.

## As horas do histórico {#cap-15-horas}

O histórico é carimbado com a hora do aparelho, qualquer que seja a fonte dela. Quando o primeiro acerto por NTP depois de um boot encontra o relógio provisório errado, o aparelho corrige a hora dos blocos gravados desde aquele boot ([capítulo 10](#cap-10-correcao)).

::: atencao
**Mudar o Intervalo Histórico afeta a leitura dos dias anteriores.** Dentro de um bloco, a hora de cada registro é guardada como diferença em relação ao intervalo em vigor. A página decodifica todos os arquivos com o intervalo atual. Depois de uma mudança, os blocos gravados com o intervalo antigo aparecem com o primeiro registro na hora certa e os seguintes espalhados. Exporte o que precisa antes de mudar o intervalo, ou leia os arquivos antigos com `history_v5.py --interval` igual ao intervalo da época.
:::

## O histórico e a atualização de firmware {#cap-15-ota}

Uma atualização de firmware reformata o sistema de arquivos, e o histórico inteiro vai junto. Só a configuração principal atravessa a atualização ([capítulo 17](#cap-17)).

Para não perder o histórico:

1. Atualize pela página **Arquivos**. Ela baixa um backup `.bkp` para o seu computador antes de começar, com todos os arquivos do histórico e o bloco aberto.
2. Depois da atualização, restaure esse backup logo, pela mesma página.

::: atencao
**As medições feitas entre a atualização e a restauração se perdem.** A restauração grava cada arquivo do backup por cima do arquivo de mesmo nome. O arquivo do dia corrente, que o aparelho recriou depois da atualização, é substituído pela cópia do backup, e o bloco aberto na RAM é descartado. Restaure logo depois de atualizar. Se precisar desse intervalo, baixe o arquivo do dia pela página **Arquivos** antes de restaurar e junte as duas cópias no computador com o `h5_day_merge.py`.
:::

## Códigos do log de eventos {#cap-15-eventos}

Os eventos do histórico no log ([capítulo 16](#cap-16)):

| Código | Evento | Quando aparece |
|---|---|---|
| 510 | **Registro de histórico salvo** | Um registro foi gravado. É um evento de rotina: vai para o log só em transições e uma vez por hora ([capítulo 16](#cap-16-transicao)) |
| 514 | **Histórico pulado: schema V4 vazio** | Nenhum sensor ativo |
| 515 | **Histórico pulado: schema não cobre sensor ativo** | Nenhum sensor deu valor para o registro. O mesmo código, como informação, marca a volta |
| 560 | **Falha em escrever histórico** | Uma gravação falhou. Se a falha é ao selar um bloco, o aparelho recusa o registro seguinte e tenta de novo; na quinta falha seguida, desiste do bloco e recomeça num bloco novo |
| 562 | **Budget de limite de storage excedido** | A limpeza automática não terminou de uma vez e continua aos poucos |
| 563 | **Pulando arquivo de log ativo** | O arquivo mais antigo é o do dia corrente, que não é apagado |
| 566 | **Bloco de histórico selado** | Um bloco foi para o arquivo do dia. O contexto é o número de registros. Evento de rotina |
| 567 | **Snapshot de histórico gravado** | A cópia do bloco aberto foi gravada; o contexto é o número de registros no bloco. No boot, diz o que foi feito com o `.wip`: descartado vem como aviso, com contexto −1 |
| 568 | **Schema de histórico divergente** | O conjunto de sensores mudou e o arquivo do dia ganhou uma descrição nova |
| 569 | **Histórico legado apagado** | O boot apagou de `/history` arquivos que não são do histórico |
| 408, 409, 410 | Correção de horas pelo NTP | [Capítulo 10](#cap-10-correcao) |

A lista completa está no [apêndice B](#ap-b).
