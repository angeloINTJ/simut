# O painel {#cap-11}

[release]{.img} Este capítulo percorre o painel de toque da imagem `release`, tela por tela: a tela inicial, os gestos, o gráfico, o menu de configurações, as telas de boot e as mensagens. É para quem opera o aparelho na bancada ou no laboratório.

## O painel em resumo {#cap-11-visao}

O painel é uma tela de 320 × 240 pixels com toque resistivo. Ele mostra as leituras, os alarmes e o histórico sem exigir conta nem PIN. O menu de configurações pede a identificação de quem está no painel ([capítulo 8](#cap-08-identidade)).

Três regras valem para todas as telas:

- **Um toque é uma ação.** Quase tudo responde ao encostar do dedo; a exceção é o toque curto no cartão de cima, que age ao soltar, porque o mesmo dedo pode estar começando o gesto de 3 s. Para repetir, levante o dedo e toque de novo. As setas dos rodapés e do gráfico repetem a cada 300 ms enquanto você segura.
- **Um toque pertence à tela onde começou.** Se a tela muda com o dedo ainda encostado, o resto do toque é ignorado até você levantar o dedo.
- **Sem toque, o painel volta à tela inicial.** Depois de 30 s sem nenhum toque, qualquer tela volta à tela inicial ([A volta à tela inicial](#cap-11-ocioso)).

O painel não existe nas imagens `alpha`, que têm um LCD de 16 × 2 ([capítulo 12](#cap-12)), nem no Air, que não tem tela ([capítulo 19](#cap-19)).

## A tela inicial {#cap-11-principal}

A tela inicial tem quatro faixas, de cima para baixo:

| Faixa | Altura | O que mostra |
|---|---|---|
| Barra de cima | 29 px | `SIMUT`, data e hora, pendentes da telemetria e sinal do Wi-Fi |
| Cartão de cima | 75 px | Um sensor: o fixado, ou o selecionado no modo de seleção |
| Cartão de baixo | 75 px | O sensor selecionado na barra de botões |
| Barra de botões | 41 px | Um botão por sensor, **CFG** e, se preciso, o botão de página |

::: {.figura #fig-11-principal tipo="tft" arquivo="11-principal.png" captura="screen dash; 3 sensores ativos: DS18B20 no slot 0 (fixado no cartão de cima), DHT22 no slot 1 (selecionado) e BME280 no slot 2; Wi-Fi conectado com sinal forte; 12 registros pendentes; relógio acertado; nenhum alarme"}
A tela inicial: a barra de cima, os dois cartões e a barra de botões com S1, S2 e CFG.
:::

### A barra de cima {#cap-11-barra}

Da esquerda para a direita:

| Elemento | O que é |
|---|---|
| `SIMUT` | O nome do produto, fixo |
| Data e hora | `dd/mm/aa - hh:mm:ss`, no fuso do aparelho ([capítulo 10](#cap-10-onde)) |
| Pendentes | Quantos registros esperam envio pela telemetria, seguidos de uma seta |
| Sinal do Wi-Fi | Quatro barras, que acendem da esquerda para a direita |

**Pendentes.** A contagem aparece só quando há registros esperando envio. Até 999 ela mostra o número; de 1.000 em diante, os milhares com um `k`: 1.500 registros aparecem como `1k`. O aparelho atualiza a contagem a cada 10 s. A cor diz como foi o último envio:

| Cor do número e da seta | Significado |
|---|---|
| Azul | O último envio deu certo, ou nenhum envio foi tentado desde que o aparelho ligou |
| Seta piscando entre azul e branco por 1 s | Um envio acabou de dar certo |
| Vermelho | O último envio falhou |

A telemetria está no [capítulo 21](#cap-21).

**Sinal do Wi-Fi.** O número de barras acesas segue a intensidade do sinal (RSSI):

| Barras acesas | RSSI |
|---|---|
| 4 | acima de −55 dBm |
| 3 | de −65 a −55 dBm |
| 2 | de −75 a −65 dBm |
| 1 | abaixo de −75 dBm |
| Nenhuma | Sem conexão, ou leitura de sinal impossível |

**O que substitui a data e a hora.** Dois avisos ocupam o lugar do relógio enquanto duram:

- **Silenciado:** e os segundos que faltam, como `Silenciado: 97s`, durante os 120 s de silêncio de um alarme ([capítulo 7](#cap-07-silenciar)).
- `Web:` e o nome da conta, como `Web: admin`, por 5 s depois de uma entrada bem-sucedida na interface web. O silêncio tem prioridade sobre esse aviso.

::: {.figura #fig-11-pendentes-falha tipo="tft" arquivo="11-pendentes-falha.png" captura="screen dash; telemetria ligada com o coletor fora do ar há alguns minutos; recorte da barra de cima com a contagem de pendentes em vermelho"}
A contagem de pendentes em vermelho: o último envio falhou.
:::

::: {.figura #fig-11-aviso-web tipo="tft" arquivo="11-aviso-web.png" captura="screen dash; até 5 s depois de entrar na interface web com a conta admin; recorte da barra de cima com Web: admin no lugar do relógio"}
O aviso de entrada pela web, no lugar da data e da hora.
:::

### Os cartões {#cap-11-cartoes}

Cada cartão mostra um sensor. O nome dele fica no alto, centralizado; um slot sem nome aparece como `Sensor` seguido do número do slot. O conteúdo depende do tipo do sensor:

| Tipo | O cartão mostra |
|---|---|
| DS18B20 | Temperatura, com uma casa decimal, em °C |
| DHT22 | Temperatura e umidade relativa, esta em inteiro com `%UR` |
| BME280 | Temperatura e pressão, esta em inteiro com `hPa` |
| BMP280 | Temperatura e pressão |

O BME280 mede umidade, mas o cartão mostra a pressão no lugar dela; a umidade dele aparece no gráfico, no detalhe e na interface web. Enquanto não há leitura, o valor aparece como `--.-`. Um sensor em erro mostra **Erro** (*Error*) em letras grandes ([capítulo 6](#cap-06-erro-aparece)).

**Mínimo e máximo.** Um toque no cartão troca o valor atual pelo mínimo e o máximo do dia: **MIN** e **MAX** da temperatura e, no DHT22 e no BME280, também da umidade. No DS18B20 e no BMP280, só da temperatura. À direita aparece o botão de gráfico, com o desenho de três barras, que abre o histórico do sensor ([O gráfico](#cap-11-grafico)). Outro toque fora do botão volta ao valor atual, e o cartão volta sozinho depois de 30 s sem toque.

Os valores de mínimo e máximo vêm do histórico do dia, lido quando o aparelho liga, e das leituras seguintes. Eles não zeram à meia-noite: continuam contando desde o começo do dia em que o aparelho ligou, até o próximo reinício.

::: atencao
**Sensor num GPIO de número diferente do slot.** Pelo código da v2.7.1, as leituras novas entram no mínimo e no máximo do slot cujo número é igual ao GPIO do sensor, e não no slot do próprio sensor. Um sensor no slot 3 ligado ao GP3 funciona como esperado. Um sensor no slot 0 ligado ao GP2 fica com o mínimo e o máximo lidos quando o aparelho ligou, e as leituras dele vão para o cartão do slot 2, se esse slot estiver em uso. Um reinício relê do histórico os valores certos de cada slot.
:::

::: {.figura #fig-11-minmax tipo="tft" arquivo="11-minmax.png" captura="screen dash -> tap(160,150); DHT22 selecionado no cartão de baixo, com leituras desde a meia-noite"}
O cartão de baixo em mínimo e máximo: temperatura e umidade, e o botão de gráfico à direita.
:::

**O cartão de cima: fixado ou em seleção.** O cartão de cima tem dois modos:

- **Fixado:** mostra sempre o mesmo sensor, que some da barra de botões para não aparecer duas vezes. De fábrica, o cartão de cima fica fixado no slot 0.
- **Em seleção:** fica cinza, com texto e borda brancos, e mostra o mesmo sensor do cartão de baixo. Serve para escolher qual sensor fixar.

Para fixar outro sensor:

1. Segure o dedo no cartão de cima por 3 s. O painel toca o som de toque e o cartão fica cinza.
2. Toque no botão do sensor na barra de botões.
3. Segure o dedo no cartão de cima por 3 s de novo. O cartão volta à cor normal com o sensor escolhido, e o cartão de baixo passa para outro sensor.

Depois do gesto de 3 s, o cartão de cima ignora toques por 600 ms, enquanto o dedo sobe. O aparelho grava o sensor fixado e o selecionado alguns segundos depois da mudança, e os dois voltam depois de um reinício.

::: {.figura #fig-11-selecao tipo="tft" arquivo="11-selecao.png" captura="screen dash; 3 sensores ativos; segure o dedo no cartão de cima por 3 s e solte; a ferramenta de mapa de telas não faz o gesto de segurar"}
O cartão de cima em seleção: cinza, espelhando o sensor do cartão de baixo.
:::

**Cartões em alarme.** Um sensor em alarme pisca: em vermelho para limite e em âmbar para falha, com a borda alternando. Tocar no cartão que pisca abre a tela de alarme em vez do mínimo e máximo ([A tela de alarme](#cap-11-alarme)). Durante o silêncio de 120 s, os cartões param de piscar e o toque volta ao normal. Com dois ou mais sensores em alarme, o cartão de baixo passa de um para outro a cada 3 s, sem contar o sensor fixado no cartão de cima; essa rotação percorre os slots de 0 a 9. As figuras estão no [capítulo 7](#cap-07-disparo).

### A barra de botões {#cap-11-rodape}

A barra de botões tem até cinco posições, da esquerda para a direita:

| Botão | O que faz |
|---|---|
| `S` e o número do slot, como `S1` | Seleciona o sensor para o cartão de baixo. O selecionado fica na cor de destaque |
| **CFG** | Abre as configurações: primeiro a escolha da conta, depois o PIN ([capítulo 8](#cap-08-entrar)) |
| Página, como `1/2` | Aparece quando os botões não cabem numa linha; mostra a próxima página |

Um sensor ativo ganha um botão, menos o fixado no cartão de cima. **CFG** vem logo depois do último sensor, então a posição dele muda com o número de sensores. Com mais de cinco botões, a barra se divide em páginas de quatro, e o botão de página fica sempre na quinta posição, à direita.

Nos alarmes, o botão do sensor pisca junto com o cartão. Se um sensor em alarme está noutra página, o botão de página pisca em vermelho; durante o silêncio, ele fica parado, com a borda vermelha.

::: {.figura #fig-11-paginas tipo="tft" arquivo="11-paginas.png" captura="screen dash; 7 sensores ativos, nenhum fixado fora da primeira página; barra de botões na primeira página, com S e o botão 1/2"}
A barra de botões com páginas: quatro botões e o botão de página à direita.
:::

### Quando a web ocupa o aparelho {#cap-11-web-ocupado}

Algumas operações da interface web leem a memória por vários segundos: os gráficos do histórico, a exportação em CSV, a leitura e a exportação do log de eventos. Enquanto uma delas corre, a barra de cima mostra, em âmbar, `WEB '` e o nome da conta, seguidos de `' - toque bloqueado`, e a tela inicial ignora os toques. As leituras continuam atualizando. O aviso some sozinho, até um segundo depois de a operação terminar.

O texto desse aviso está sempre em português, seja qual for o idioma do painel.

::: {.figura #fig-11-web-ocupado tipo="tft" arquivo="11-web-ocupado.png" captura="screen dash; durante a exportação em CSV de um dia de histórico pela conta admin, na página Histórico e Logs"}
O aviso de web ocupada: WEB 'admin' - toque bloqueado.
:::

## Gestos e navegação {#cap-11-gestos}

Na tela inicial:

| Onde | Gesto | Resultado |
|---|---|---|
| Cartão de cima | Toque | Mínimo e máximo, ou volta ao valor |
| Cartão de cima | Segurar 3 s | Fixa o sensor selecionado, ou entra em seleção |
| Cartão de cima ou de baixo, em mínimo e máximo | Toque no botão de gráfico | Abre o gráfico do sensor, em 1H |
| Cartão de baixo | Toque | Mínimo e máximo, ou volta ao valor |
| Cartão em alarme, sem silêncio | Toque | Abre a tela de alarme do sensor |
| Botão `S` | Toque | Seleciona o sensor |
| **CFG** | Toque | Abre a escolha da conta |
| Botão de página | Toque | Mostra a próxima página de botões |

O toque curto no cartão de cima vale no cartão inteiro e nos dois modos. Enquanto o sensor do cartão de cima está em alarme sem silêncio, o toque abre a tela de alarme e o gesto de 3 s não começa.

O botão de gráfico abre o histórico dos slots 0 a 10. Nos slots 11 a 15, ele só volta o cartão ao valor atual.

Nas outras telas, a navegação segue o mesmo padrão:

- **Listas** (menu, temas, idiomas): toque numa linha para selecioná-la. As setas do rodapé movem a seleção e dão a volta no fim da lista.
- **Rodapé padrão:** seta para cima, seta para baixo, **SAIR** e o botão de ação à direita (**ENTRAR**, **APLICAR** ou **SALVAR**).
- **Telas de consulta** (gráfico, detalhe, calendário): o **X** no canto de cima, à direita, fecha.

## O gráfico {#cap-11-grafico}

O gráfico mostra o histórico de um sensor numa janela de tempo. Para abri-lo, ponha o cartão em mínimo e máximo e toque no botão de gráfico. Ele abre na janela de 1 hora que termina agora.

Ao abrir, o painel mostra **Carregando...** (*Loading...*) enquanto lê o histórico. Dentro do gráfico, trocar de janela não apaga a tela: uma linha fina na cor de destaque, no alto, avisa que a leitura está em curso, e o gráfico novo substitui o antigo.

::: {.figura #fig-11-carregando tipo="tft" arquivo="11-carregando.png" captura="tela MODE_GRAPH_LOADING; aparece por instantes ao abrir um gráfico a partir da tela inicial; transitória, pode exigir várias tentativas de captura"}
A tela Carregando..., ao abrir um gráfico.
:::

::: {.figura #fig-11-grafico tipo="tft" arquivo="11-grafico.png" captura="screen dash -> tap(160,150) -> tap(290,150) -> tap(286,215) -> tap(286,215) -> tap(286,215); DHT22 selecionado no cartão de baixo; o gráfico abre em 1H e os três toques na lupa de menos levam a 24H; histórico de pelo menos um dia"}
O gráfico de 24H de um DHT22: faixa de mínimo e máximo, linha de temperatura, curva de umidade e marcadores.
:::

### O cabeçalho {#cap-11-grafico-cabecalho}

| Elemento | O que mostra |
|---|---|
| Etiqueta à esquerda | A janela: `1H`, `6H`, `12H`, `24H` ou `7D` |
| Centro | O começo e o fim da janela, como `23/09/26 14:00 - 15:00`, ou `22/09/26 15:00 - 23/09 15:00` quando a janela atravessa a meia-noite |
| **X**, à direita | Fecha o gráfico e volta à tela inicial |

Um toque no centro do cabeçalho mostra o nome do sensor por 3 s. Sem dados na janela, o texto do centro fica apagado.

### A área do gráfico {#cap-11-grafico-area}

O aparelho divide a janela em intervalos iguais de tempo, até 200 por janela e nunca menos de 40. Com o histórico gravado a cada minuto, a janela de 1H tem 60 intervalos. Em cada intervalo, ele calcula a média, o mínimo e o máximo das leituras. Na tela:

- **Faixa sombreada:** vai do mínimo ao máximo de cada intervalo. É a variação real da temperatura dentro dele.
- **Linha grossa:** a média da temperatura.
- **Linha fina:** a segunda grandeza, com a escala à direita. É a umidade, no DHT22 e no BME280, ou a pressão, no BMP280.
- **Lacunas:** um intervalo sem leitura fica em branco, com a largura real do tempo sem dados. Uma leitura isolada entre duas lacunas aparece como um ponto.
- **Losangos:** um marca o máximo da janela, no alto da faixa, e outro o mínimo, embaixo.
- **Círculo no fim:** o último valor da temperatura.
- **Eixo da esquerda:** o máximo, no alto, e o mínimo, embaixo.
- **Eixo de baixo:** três horários (começo, meio e fim) em `hh:mm`, ou datas em `dd/mm` na janela de 7D.

Sem nenhuma leitura na janela, o centro mostra **Sem Dados** (*No Data*).

### A barra de botões do gráfico {#cap-11-grafico-botoes}

| Posição | Botão | O que faz |
|---|---|---|
| 1 | Setas duplas para a esquerda | Recua uma janela inteira no tempo |
| 2 | Setas duplas para a direita | Avança uma janela; apagado quando a janela já termina agora |
| 3 | Calendário | Abre o calendário ([O calendário](#cap-11-calendario)) |
| 4 | Lupa com `+` e a janela seguinte | Encurta a janela: 7D, 24H, 12H, 6H, 1H. Apagado em 1H |
| 5 | Lupa com `−` e a janela seguinte | Alonga a janela. Apagado em 7D |

Os botões de lupa mostram a janela para onde levam: em 24H, a lupa de `+` mostra `12H` e a de `−` mostra `7D`. As setas e as lupas repetem se você segurar. O aparelho não mostra o futuro: avançar para além de agora para no agora.

Um toque na área do gráfico abre o detalhe numérico.

::: atencao
**A janela pode ficar presa no passado.** Pelo código da v2.7.1, depois que você recua ou avança no tempo, ou escolhe um dia no calendário, os gráficos abertos em seguida continuam terminando naquele mesmo instante, inclusive pelo botão **Hoje** do calendário, até o aparelho reiniciar. Para trazer a janela ao presente, toque na seta para a esquerda e depois na seta para a direita até ela apagar: a janela passa a terminar no instante desse toque. Um gráfico aberto horas depois continua terminando nesse instante; só um reinício solta a janela.
:::

## O detalhe numérico {#cap-11-detalhe}

O detalhe mostra os números da mesma janela do gráfico, uma grandeza por página. O cabeçalho e a barra de botões são os do gráfico, e funcionam do mesmo jeito.

Cada página tem um título com a grandeza e a unidade, como **Temperatura** (°C), e quatro linhas:

| Linha | Valor | À direita |
|---|---|---|
| **MAX** | O máximo da janela | Data e hora do máximo, em `dd/mm/aa hh:mm` |
| **MIN** | O mínimo da janela | Data e hora do mínimo |
| **MÉDIA** (*AVG*) | A média da janela | A diferença entre o último e o primeiro valor da janela, com sinal, como `+0.3`; `0.0` quando é menor que 0,05 |
| **DESVIO** (*STD DEV*) | O desvio padrão, com duas casas | `n=` e o número de leituras |

As páginas são temperatura, umidade (DHT22 e BME280) e pressão (BME280 e BMP280). Pontos no título mostram em qual página você está. Um toque no centro passa para a próxima página; na última, volta ao gráfico. A umidade aparece em inteiros.

::: {.figura #fig-11-detalhe-temp tipo="tft" arquivo="11-detalhe-temp.png" captura="screen gra -> tap(160,120); gráfico de um BME280 aberto antes, em 24H"}
O detalhe numérico, página de temperatura: máximo, mínimo, média e desvio.
:::

::: {.figura #fig-11-detalhe-umid tipo="tft" arquivo="11-detalhe-umid.png" captura="screen gra -> tap(160,120) -> tap(160,120); mesmo BME280"}
A página de umidade do detalhe.
:::

::: {.figura #fig-11-detalhe-pressao tipo="tft" arquivo="11-detalhe-pressao.png" captura="screen gra -> tap(160,120) -> tap(160,120) -> tap(160,120); mesmo BME280"}
A página de pressão do detalhe.
:::

## O calendário {#cap-11-calendario}

O calendário mostra um mês e marca os dias que têm histórico. Abra pelo terceiro botão da barra do gráfico.

- **Título:** o mês abreviado e o ano, como `Set 2026`.
- **Grade:** as colunas `D S T Q Q S S`, de domingo a sábado.
- **Dia com histórico:** número normal com um ponto embaixo.
- **Dia sem histórico:** número apagado; tocar nele não faz nada.
- **Hoje:** número sobre um fundo na cor de destaque.

Toque num dia com histórico para abrir o gráfico daquele dia, em 24H, da meia-noite à meia-noite. O rodapé tem **< Mês**, **Hoje** e **Mês >**. **Hoje** volta ao gráfico, na janela que estava aberta. O **X** do título volta ao gráfico.

O calendário está sempre em português, seja qual for o idioma do painel.

::: {.figura #fig-11-calendario tipo="tft" arquivo="11-calendario.png" captura="screen gra -> tap(160,215); mês corrente com histórico em pelo menos dez dias"}
O calendário: os dias com histórico têm um ponto, e o dia de hoje fica destacado.
:::

::: nota
**A tela de estatísticas.** O código da v2.7.1 tem uma tela de estatísticas, com os valores atuais e o botão **GERAR GRÁFICO** (*PLOT CHART*), mas nenhum toque chega a ela. O detalhe numérico faz esse papel.
:::

## O menu de configurações {#cap-11-menu}

Para entrar, toque em **CFG**, escolha a sua conta e digite o PIN ([capítulo 8](#cap-08-entrar)). O menu abre com o título **Configurações >** seguido do nome da conta e lista só os itens que as permissões dela alcançam.

O menu tem até 12 itens, quatro por página, com uma barra de rolagem à direita. Toque num item para selecioná-lo, ou mova a seleção com as setas do rodapé, e toque em **ENTRAR**. **SAIR** volta à tela inicial.

Quando a conta vê os 12 itens, eles aparecem numerados, de `1.` a `12.`. Com o menu filtrado, os números somem, para a lista não começar por um número salteado.

| Nº | Item | Permissão | O que abre |
|---|---|---|---|
| 1 | **Temas Visuais** (*Visual Themes*) | **Sistema** [PERM_SYS_CONFIG]{.perm} | [Temas](#cap-11-temas) |
| 2 | **Limites de Alarme** (*Alarm Limits*) | Uma das três de painel | [Alarmes](#cap-11-alarmes) |
| 3 | **Sons de Alarme** (*Alarm Sounds*) | **Sistema** | [Sons](#cap-11-sons) |
| 4 | **Idioma do Sistema** (*System Language*) | **Sistema** | [Idioma](#cap-11-idioma) |
| 5 | **Alterar Senha** (*Change Password*) | Nenhuma | O próprio PIN ([capítulo 8](#cap-08-proprio-pin)) |
| 6 | **Calibrar Touch** (*Touch Calibration*) | **Sistema** | [Calibração do toque](#cap-11-calibrar) |
| 7 | **Licença** (*License*) | Nenhuma | [Licença](#cap-11-licenca) |
| 8 | **Status do Sistema** (*System Status*) | Nenhuma | [Status](#cap-11-status) |
| 9 | **Alinhamento da Tela** (*Display Alignment*) | **Sistema** | [Alinhamento](#cap-11-alinhamento) |
| 10 | **Usuários** (*Users*) | **Usuários** [PERM_USER_MGR]{.perm} | [Usuários](#cap-11-usuarios) |
| 11 | **Segurança do PIN** (*PIN security*) | **Usuários** | A política de PIN ([capítulo 8](#cap-08-politica)) |
| 12 | **Modo de Configuração** (*Configuration Mode*) | **Rede** [PERM_NET_CONFIG]{.perm} | [O AP de configuração](#cap-11-ap) |

As três permissões de painel do item 2 são **Limites (painel)** [PERM_ALARM_LIMITS]{.perm}, **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm} e **Manut. (painel)** [PERM_MAINT]{.perm}. O aparelho confere a permissão de novo quando recebe o pedido do painel: esconder o item é só conforto.

::: {.figura #fig-11-menu tipo="tft" arquivo="11-menu.png" captura="screen set; a ferramenta entra como administrador; primeira página, item 1 selecionado"}
O menu completo, primeira página: itens numerados de 1 a 4.
:::

::: {.figura #fig-11-menu-p3 tipo="tft" arquivo="11-menu-p3.png" captura="screen set -> tap(35,215); a seta para cima dá a volta e seleciona o item 12; terceira página"}
A terceira página do menu completo, com Alinhamento da Tela, Usuários, Segurança do PIN e Modo de Configuração.
:::

O menu filtrado de uma conta só com permissões de painel está na [figura do capítulo 8](#fig-08-menu-operador).

### Temas Visuais {#cap-11-temas}

A tela **Configurações > Temas** (*Settings > Themes*) lista os temas instalados, quatro por página. Cada linha tem o nome do tema e três amostras de cor: o fundo, o cartão e o destaque. O tema de fábrica é o **Simut Default**; os outros vêm de arquivos `.thm` na pasta `/themes` do aparelho.

1. Toque no tema, ou mova a seleção com as setas.
2. Toque em **APLICAR**.

O painel mostra **Aplicando Tema...** e volta à tela inicial com as cores novas. O aparelho grava na hora, sem reiniciar, e o log registra o código 440, **Tema alterado via UI**. **SAIR** volta ao menu sem mudar nada.

O tema do painel também muda pela interface web ([capítulo 13](#cap-13-tema-painel)).

::: {.figura #fig-11-temas tipo="tft" arquivo="11-temas.png" captura="screen thm; tema de fábrica aplicado; pelo menos quatro temas instalados"}
A escolha de tema, com as amostras de cor de cada um.
:::

### Limites de Alarme {#cap-11-alarmes}

O item abre a lista de sensores, com o estado dos alarmes de cada um. Da lista, um sensor abre o menu dele, com três linhas: **Limites de alarme**, **Alarmes** e **Manutenção**. Cada linha exige a sua permissão de painel.

Tudo isso, com as telas e as figuras, está no [capítulo 7](#cap-07-painel): a lista, o menu do sensor, o editor de limites e a janela de manutenção.

### Sons de Alarme {#cap-11-sons}

A tela **Config. de Sons** (*Sound Settings*) tem nove linhas em três páginas: os dois volumes, os sons de toque, confirmação, erro, alarme e atenção, os sons da web e o **Mudo Global**. A escolha de melodia e a confirmação do **Mudo Global** são telas próprias. Tudo está no [capítulo 7](#cap-07-sons-painel).

### Idioma do Sistema {#cap-11-idioma}

A tela **Configurações > Idioma** (*Settings > Language*) tem até duas linhas:

| Linha | Código | Quando aparece |
|---|---|---|
| **English** | `EN` | Sempre |
| O nome do pacote de idioma instalado, como **Portugues** | O código do pacote, como `pt-BR` | Só com um pacote `.lng` carregado |

1. Toque no idioma.
2. Toque em **APLICAR**.

O painel toca o som de confirmação e volta à tela inicial no idioma novo. O aparelho grava na hora, e o log registra o código 441, **Idioma alterado via UI**. O console serial passa a usar o mesmo idioma a partir do próximo reinício. De fábrica, o idioma é o do pacote; sem pacote carregado, o painel fica em inglês.

Algumas telas não seguem o idioma escolhido: o aviso de web ocupada, o calendário e o LCD do alpha estão sempre em português, e as linhas da tela [Status do Sistema](#cap-11-status) estão sempre em inglês.

::: {.figura #fig-11-idioma tipo="tft" arquivo="11-idioma.png" captura="screen lng; pacote pt-BR carregado e em uso"}
A escolha de idioma: English e o pacote instalado.
:::

### Calibrar Touch {#cap-11-calibrar}

A calibração acerta o toque em duas etapas seguidas: primeiro a pressão, depois a posição. Faça com o dedo ou com a caneta que vai usar no dia a dia.

**Etapa 1, sensibilidade.** A tela **Sensibilidade do Toque** (*Touch Sensitivity*) mostra uma mira no centro, a frase **Toque na mira** (*Touch the crosshair*), uma barra vertical à direita e, embaixo dela, o limite de pressão encontrado.

1. Encoste o dedo na mira e mantenha, com pressão leve e constante.
2. Espere a barra encher. Ela sobe enquanto a pressão fica estável e desce devagar quando oscila. Com pressão estável, enche em cerca de 2 s.

Quando a barra enche, a mira fica verde e a tela mostra **Calibração Concluída!**. O aparelho grava o limite de pressão (código 444, **Sensibilidade do touch salva**) e, 1,5 s depois, passa sozinho à etapa 2. **CANCELAR**, embaixo à esquerda, volta ao menu.

**Etapa 2, posição.** A tela **Calibração do Touch** (*Touch Calibration*) mostra uma mira por vez, nos quatro cantos: em cima à esquerda, em cima à direita, embaixo à esquerda e embaixo à direita. A frase diz qual é a mira, como **Toque na mira (1/4)**, e `[ 1 / 2 ]` diz a volta.

1. Segure o dedo na mira até ela ficar verde, cerca de 0,4 s.
2. Solte. A próxima mira aparece.
3. Repita nos quatro cantos, duas voltas: oito toques.

O aparelho compara as duas voltas. Se algum canto diferir demais entre elas, a tela mostra **Toques imprecisos! Tente novamente.**; **ENTENDI** recomeça as oito miras. Se as voltas concordarem, a tela mostra **Calibração Concluída!**, o aparelho grava na hora (código 443, **Calibração do touch salva**) e **ENTENDI** volta ao menu. A etapa 2 não tem botão para cancelar.

A calibração também começa pela interface web, pela página **Configurações** ([capítulo 5](#cap-05-touch)), e pelo console. Esse caminho existe porque um toque descalibrado pode não alcançar o menu.

::: {.figura #fig-11-sensibilidade tipo="tft" arquivo="11-sensibilidade.png" captura="screen touchsens; dedo ainda não encostado; barra vazia"}
A etapa de sensibilidade: a mira, a barra de estabilidade e o limite de pressão.
:::

::: {.figura #fig-11-mira tipo="tft" arquivo="11-mira.png" captura="screen touchcal e complete a etapa de sensibilidade com o dedo; capture a primeira mira, no canto de cima à esquerda; o toque simulado não completa a sensibilidade, porque não tem pressão"}
A etapa de posição: a primeira mira, Toque na mira (1/4) e a volta [ 1 / 2 ].
:::

::: {.figura #fig-11-calib-ok tipo="tft" arquivo="11-calib-ok.png" captura="depois do oitavo toque de uma calibração com as duas voltas coerentes"}
O fim de uma calibração aceita: Calibração Concluída! e ENTENDI.
:::

::: {.figura #fig-11-calib-recusada tipo="tft" arquivo="11-calib-recusada.png" captura="depois do oitavo toque, tendo tocado longe da mira num dos cantos da segunda volta"}
Uma calibração recusada: Toques imprecisos! Tente novamente.
:::

### Licença {#cap-11-licenca}

A tela **Licença MIT** (*MIT License*) mostra o texto da licença do software, 17 linhas por página, no idioma do pacote instalado. Pontos no título mostram a página. Um toque na metade de cima do texto volta uma página; na metade de baixo, avança. O rodapé tem seta para cima, seta para baixo e **SAIR**, que volta ao menu.

::: {.figura #fig-11-licenca tipo="tft" arquivo="11-licenca.png" captura="screen lic; primeira página"}
A primeira página da licença.
:::

### Status do Sistema {#cap-11-status}

A tela **Status do Sistema** (*System Status*) mostra o estado do aparelho em quatro páginas, atualizadas a cada segundo. As setas do rodapé trocam a página e **SAIR** volta ao menu. Os rótulos das linhas estão em inglês.

| Página | Linha | O que mostra |
|---|---|---|
| 1 | `Device` | O nome do aparelho |
| 1 | `Firmware` | A versão |
| 1 | `Serial` | O número de série da placa, em 16 dígitos hexadecimais |
| 1 | `Uptime` | Tempo desde o último reinício, em `Nd hh:mm:ss` |
| 1 | `Heap Free` | Memória livre, em bytes; vermelho abaixo de 20.000 |
| 1 | `Flash Used` | Bytes ocupados no sistema de arquivos |
| 1 | `Board Temp` | Temperatura do chip, em °C |
| 1 | `Timezone` | O fuso, como `GMT-3` |
| 2 | `WiFi` | `Connected` em verde ou `Disconnected` em vermelho |
| 2 | `SSID` | A rede configurada |
| 2 | `IP` e `MAC` | Os endereços do aparelho |
| 2 | `RSSI` | O sinal, em dBm: verde acima de −60, na cor de destaque até −80, vermelho abaixo |
| 2 | `NTP` | `Synced` em verde ou `Not synced` em vermelho |
| 2 | `NTP Server` | O servidor de hora configurado |
| 3 | `Active` | Quantos slots estão ativos |
| 4 | `Transport` | `HTTP` ou `MQTT` |
| 4 | `Server` | O coletor da telemetria |
| 4 | `Pending` | Registros esperando envio; vermelho acima de 50 |
| 4 | `Fails` | Sempre `0` na v2.7.1: o aparelho não preenche esse campo |
| 4 | `Min batch` | O lote mínimo da telemetria, como `10 rec`, ou `off` |
| 4 | `MQTT` | Só com transporte MQTT: `Connected` ou `Disconnected` |

O número de série é a chave da linha do sensor no arquivo `calib.csv` para sensores sem ROM ([capítulo 6](#cap-06-calib-csv)).

::: {.figura #fig-11-status-1 tipo="tft" arquivo="11-status-1.png" captura="screen sts; página 1"}
Status do Sistema, página 1: o aparelho.
:::

::: {.figura #fig-11-status-2 tipo="tft" arquivo="11-status-2.png" captura="screen sts -> tap(105,215); página 2; Wi-Fi conectado e NTP sincronizado"}
Status do Sistema, página 2: rede e hora.
:::

::: {.figura #fig-11-status-3 tipo="tft" arquivo="11-status-3.png" captura="screen sts -> tap(105,215) -> tap(105,215); página 3"}
Status do Sistema, página 3: sensores.
:::

::: {.figura #fig-11-status-4 tipo="tft" arquivo="11-status-4.png" captura="screen sts -> tap(35,215); a seta para cima dá a volta até a página 4; telemetria MQTT ligada, com registros pendentes"}
Status do Sistema, página 4: telemetria.
:::

### Alinhamento da Tela {#cap-11-alinhamento}

Alguns painéis mostram a imagem deslocada alguns pixels, cortando a borda de um lado. A tela **Alinhamento da Tela** (*Display Alignment*) move a imagem inteira até 4 px em cada direção.

| Controle | O que faz |
|---|---|
| Setas para cima e para baixo | Movem a imagem 1 px na vertical, de −4 a +4 |
| Setas para a esquerda e para a direita | Movem 1 px na horizontal, de −4 a +4 |
| Quadrado no centro | Volta a `X +0` e `Y +0` |
| **SAIR** | Volta ao menu e desfaz o que não foi aplicado |
| **APLICAR** | Grava o deslocamento |

A imagem se move enquanto você ajusta, e os valores aparecem à direita, como `X +2` e `Y -1`. Ao tocar em **APLICAR**, o aparelho grava, descarta a calibração do toque, porque ela dependia da posição antiga da imagem, e abre a [calibração do toque](#cap-11-calibrar) na hora. Faça a calibração até o fim.

::: {.figura #fig-11-alinhamento tipo="tft" arquivo="11-alinhamento.png" captura="screen offset; deslocamento X +2 e Y -1 ajustado, antes de APLICAR"}
O alinhamento da tela, com as quatro setas, o quadrado de zerar e os valores X e Y.
:::

### Usuários e Segurança do PIN {#cap-11-usuarios}

O item **Usuários** abre a lista de contas do painel, com **NOVO** para criar uma conta e o editor de cada conta: as três permissões de painel, **Definir PIN** e **Excluir usuário**. O nome de uma conta nova se digita no teclado de texto, que tem as letras em oito grupos (`abc` a `wxyz`), uma tecla de algarismos, uma de símbolos, espaço e apagar; tocar num grupo abre as letras dele em minúsculas e maiúsculas.

O item **Segurança do PIN** define o tamanho mínimo, o teclado e os caracteres do PIN.

Tudo isso está no [capítulo 8](#cap-08-usuarios-painel) e na [política de PIN](#cap-08-politica).

::: {.figura #fig-11-teclado-texto tipo="tft" arquivo="11-teclado-texto.png" captura="screen usr -> tap(270,215) -> tap(121,151); NOVO e depois a tecla do grupo pqrs, segunda linha, segunda coluna"}
O teclado de texto com o grupo pqrs aberto, em minúsculas e maiúsculas.
:::

### Modo de Configuração {#cap-11-ap}

O item **Modo de Configuração** abre o ponto de acesso de configuração com o aparelho em operação. Use quando precisar trocar a rede Wi-Fi e não tiver outro caminho. O AP está no [capítulo 9](#cap-09-ap).

1. Toque em **Modo de Configuração** e em **ENTRAR**.
2. A tela de confirmação diz: **O aparelho sai da rede e abre a rede de setup. Tem certeza?**
3. Toque em **Confirmar** para abrir o AP, ou em **SAIR** para voltar ao menu.

O aparelho confere a permissão **Rede** antes de abrir o AP e registra no log o código 403, **AP iniciado**, com o contexto 0.

Depois de **Confirmar**, o painel passa ao terminal da tela de boot, com **Iniciando Ponto de Acesso (AP)...**, e em poucos segundos mostra o que é preciso para entrar no AP: o nome da rede (o nome do aparelho seguido de `_SETUP`), a chave, numa linha que começa com `PSK`, **Acesse no celular: 192.168.4.1** e **AP Ativo! Reinicie a placa para sair.** O comando `ap` do console também responde com a chave ([capítulo 14](#cap-14)). Se o AP não abrir, o painel mostra **Erro** e o aparelho continua medindo.

::: atencao
**O painel com o AP aberto.** Enquanto o AP está aberto, o aparelho não lê os sensores, não confere os alarmes e não grava o histórico ([capítulo 9](#cap-09-ap-aberto)). O painel fica na tela do AP e não responde ao toque. O AP aberto em operação fecha sozinho em 15 min, com um reinício.
:::

::: {.figura #fig-11-ap-confirmar tipo="tft" arquivo="11-ap-confirmar.png" captura="screen set -> tap(35,215) -> tap(270,215); a seta para cima dá a volta até o item 12"}
A confirmação do Modo de Configuração.
:::

## A tela de alarme {#cap-11-alarme}

Um toque no cartão de um sensor em alarme, sem silêncio, abre a tela de alarme daquele sensor. O título, sobre fundo de alarme, é `!` seguido do nome do sensor, ou `! Slot` e o número quando o slot não tem nome. Os três botões:

| Botão | O que faz |
|---|---|
| **Silenciar 120s** | Cala a cigarra e a piscada de todos os alarmes por 120 s, e volta à tela inicial |
| **Desativar** | Pede a conta e o PIN; desliga o alarme daquele sensor |
| **Min/Max** | Volta à tela inicial com o cartão em mínimo e máximo |

A tela não tem **SAIR**: para voltar sem agir, toque em **Min/Max** ou espere 30 s. O que cada botão faz, o registro no log e a permissão de **Desativar** estão no [capítulo 7](#cap-07-tela-alarme), com a [figura da tela](#fig-07-tela-alarme).

A confirmação do **Mudo Global**, que parte da tela de sons, está no [capítulo 7](#cap-07-sons-painel), com a [figura](#fig-07-mudo-confirmar).

## As mensagens do painel {#cap-11-mensagens}

Depois de uma ação que o aparelho precisa validar, o painel mostra o resultado numa tela própria: um visto verde para sucesso ou um X âmbar para recusa, a mensagem e o botão **ENTENDI**, que volta à tela de onde a ação partiu. Na recusa, o painel toca o som de erro.

| Mensagem | Tipo | Quando |
|---|---|---|
| **PIN salvo!** | Sucesso | O próprio PIN ou o PIN de outra conta foi gravado |
| **Usuário salvo!** | Sucesso | Uma conta foi criada ou alterada no painel |
| **Usuário excluído** | Sucesso | Uma conta foi excluída |
| **Sem permissão** | Recusa | A conta identificada não tem a permissão da ação |
| **PIN inválido!** | Recusa | O PIN não segue a política |
| **PIN já em uso!** | Recusa | Outra conta já tem esse PIN |
| **Nome inválido ou repetido** | Recusa | O nome de uma conta nova não serve |
| **Sem vaga para conta nova** | Recusa | As 32 contas estão ocupadas |

As causas e o que fazer estão no [capítulo 8](#cap-08-proprio-pin) e em [Usuários no painel](#cap-08-usuarios-painel). As telas de calibração do toque têm mensagens próprias, no mesmo formato ([Calibrar Touch](#cap-11-calibrar)).

::: {.figura #fig-11-mensagem-ok tipo="tft" arquivo="11-mensagem-ok.png" captura="menu > Alterar Senha; digite duas vezes um PIN novo e válido, com ENTRAR"}
Uma mensagem de sucesso: PIN salvo! e ENTENDI.
:::

A figura de uma recusa está no [capítulo 8](#fig-08-pin-em-uso).

## As telas de boot {#cap-11-boot}

Ao ligar, o painel mostra `SIMUT` em letras grandes, a versão e uma caixa com `> system_init( )` e as cinco últimas etapas do boot, em letras pequenas e sem acentos. Cada etapa nova empurra as antigas para cima. As etapas, na ordem:

| Etapa | Quando aparece |
|---|---|
| **Montando Sistema de Arquivos...** | Sempre, primeiro |
| **Mantenha a tela pressionada: Modo AP...** | Sempre; abre a janela do gesto do AP ([O gesto do AP no boot](#cap-11-boot-ap)) |
| **Iniciando Gerenciador de Log...**, **Iniciando Interface de Comando...**, **Carregando Tema & Idioma...**, **Carregando Periféricos & Sensores...** | Sempre |
| **Iniciando Ponto de Acesso (AP)...** e as linhas do AP | Só quando o aparelho abre o AP no boot |
| **Iniciando Interface Wi-Fi...** | Quando o aparelho não abre o AP |
| **Aguardando roteador** e pontos | Até entrar na rede, com o botão **PULAR** |
| **Sincronizando Relógio Global** e pontos | Até acertar o relógio, com o botão **PULAR** |
| **Rede Conectada & Sincronizada!** | Rede e hora prontas |
| **Conexão Ignorada pelo Usuário.** | Depois de **PULAR** |
| **Timeout de rede. Iniciando Offline...** | Depois de 30 s sem rede ou sem hora |
| **Iniciando Servidor de Telemetria...**, **Iniciando Servidor Web...**, **Registrando Callbacks...** | Sempre |
| **Carregando cache diário Min/Max...**, **Aquecendo sensores...** | Quando o aparelho não abre o AP |
| **Corrigindo timestamps (NTP)...**, **Recarregando cache Min/Max...** | Quando o relógio foi acertado durante o boot |
| **Preparando dados do painel...**, **Todos os subsistemas iniciados.** | Sempre, no fim |
| **Sistema Pronto! Entrando no Painel.** | 0,8 s antes da tela inicial |

**PULAR** (*SKIP*) aparece enquanto o aparelho espera o roteador ou o relógio. Um toque nele segue o boot sem rede; o aparelho continua tentando a rede depois ([capítulo 9](#cap-09-reconexao)).

::: {.figura #fig-11-boot tipo="foto" arquivo="11-boot.png" captura="boot com rede configurada e alcançável; capture durante Aguardando roteador; a captura por GET /api/screenshot não alcança o boot, então use foto da tela"}
A tela de boot com as últimas etapas e o botão PULAR.
:::

### O gesto do AP no boot {#cap-11-boot-ap}

O gesto abre o AP de configuração no boot, para quando o aparelho não consegue entrar na rede e você não tem outro acesso.

1. Ligue o aparelho.
2. Espere aparecer **Mantenha a tela pressionada: Modo AP...**. A janela do gesto só abre depois que essa linha está na tela.
3. Em até 3,5 s, encoste o dedo na tela e mantenha.
4. A tela passa a **Modo de Configuração** com uma barra de progresso. Mantenha o dedo até a barra encher, 3 s.

Se você levantar o dedo antes, a tela mostra **Modo AP Cancelado.** e o boot segue normal. Um dedo encostado desde antes de ligar também vale: a janela o encontra no primeiro instante.

Com o AP aberto no boot, a caixa mostra **Iniciando Ponto de Acesso (AP)...**, **Conecte à rede SIMUT_SETUP** seguido de `PSK` e a chave, **Acesse no celular: 192.168.4.1** e, no fim, o nome real da rede, `PSK` com a chave e **AP Ativo! Reinicie a placa para sair.** O log registra o código 403 com o contexto 1 para o gesto e 2 para aparelho sem rede configurada.

::: atencao
**O nome da rede na tela de boot.** A linha **Conecte à rede SIMUT_SETUP** tem o nome fixo. A rede de verdade é o nome do aparelho seguido de `_SETUP`, como `simut_SETUP` ([capítulo 9](#cap-09-ap)). Procure no celular o nome que aparece sozinho numa das últimas linhas da caixa.
:::

::: nota
**A chave fica na tela.** Com o AP aberto no boot, o painel não passa à tela inicial: a caixa do boot fica parada nessas últimas linhas, com o nome da rede e a chave, até o aparelho reiniciar. Anote a chave na primeira configuração; ela não muda.
:::

::: {.figura #fig-11-boot-gesto tipo="foto" arquivo="11-boot-gesto.png" captura="boot; linha Mantenha a tela pressionada: Modo AP... no fim da caixa; foto da tela"}
O convite do gesto do AP no boot.
:::

::: {.figura #fig-11-boot-progresso tipo="foto" arquivo="11-boot-progresso.png" captura="boot; dedo mantido na tela há cerca de 1,5 s dentro da janela; foto da tela"}
O gesto em curso: Modo de Configuração e a barra enchendo.
:::

::: {.figura #fig-11-boot-cancelado tipo="foto" arquivo="11-boot-cancelado.png" captura="boot; dedo levantado antes de a barra encher; foto da tela"}
O gesto cancelado: Modo AP Cancelado.
:::

::: {.figura #fig-11-boot-ap tipo="foto" arquivo="11-boot-ap.png" captura="boot pelo gesto completo; caixa com o nome da rede, PSK e AP Ativo!; foto da tela; a chave da foto deve ser borrada antes de publicar"}
O boot no AP: o nome da rede, a chave e AP Ativo!.
:::

## A volta à tela inicial {#cap-11-ocioso}

Depois de 30 s sem nenhum toque, qualquer tela volta à tela inicial, inclusive o menu, o gráfico e a tela de alarme. No mesmo prazo, um cartão em mínimo e máximo volta ao valor atual. As telas de boot não contam.

Uma alteração ainda não aplicada numa tela de lista, como um tema selecionado sem **APLICAR**, se perde na volta. O que já foi gravado continua gravado. Para voltar ao menu depois disso, toque em **CFG** e se identifique de novo.

## Referência das telas {#cap-11-referencia}

As 30 telas do painel, com o modo interno que o console e as ferramentas usam, o título em português, como chegar e a figura. A coluna **Tag** é o argumento do comando `screen` do console completo ([capítulo 14](#cap-14)), que existe só nas imagens de teste; `screen` entra como administrador.

| Modo | Título na tela | Como chegar | Tag | Figura |
|---|---|---|---|---|
| `MODE_DASHBOARD` | Sem título: barra de cima | Ao fim do boot; **SAIR** do menu; 30 s sem toque | `dash` | [11-principal](#fig-11-principal) |
| `MODE_STATS_VIEW` | O nome do sensor | Nenhum toque chega a ela | — | — |
| `MODE_GRAPH_LOADING` | **Carregando...** | Ao abrir um gráfico | — | [11-carregando](#fig-11-carregando) |
| `MODE_GRAPH_VIEW` | A janela e o período | Cartão em mínimo e máximo, botão de gráfico | `gra` | [11-grafico](#fig-11-grafico) |
| `MODE_GRAPH_DETAIL` | A janela e o período | Toque na área do gráfico | — | [11-detalhe-temp](#fig-11-detalhe-temp) |
| `MODE_AUTH` | **Autenticação de Segurança**, **Novo PIN** ou **Confirme o PIN** | Escolha da conta; **Alterar Senha**; **Definir PIN** | — | [08-teclado-3](#fig-08-teclado-3) |
| `MODE_SETTINGS_MAIN` | **Configurações >** e a conta | PIN certo depois de **CFG** | `set` | [11-menu](#fig-11-menu) |
| `MODE_SETTINGS_THEMES` | **Configurações > Temas** | Menu, item 1 | `thm` | [11-temas](#fig-11-temas) |
| `MODE_SETTINGS_ALARMS` | **Limites de Alarme** | Menu, item 2 | `alm` | [07-lista-sensores](#fig-07-lista-sensores) |
| `MODE_SETTINGS_ALARM_EDIT` | O nome do sensor | Menu do sensor, **Limites de alarme** | — | [07-editor-limites](#fig-07-editor-limites) |
| `MODE_SETTINGS_LANG` | **Configurações > Idioma** | Menu, item 4 | `lng` | [11-idioma](#fig-11-idioma) |
| `MODE_SETTINGS_PASSWORD` | **Nome do usuário** | **Usuários**, **NOVO**; `screen pwd` abre a variante antiga, **Nova Senha** | `pwd` | [08-novo-nome](#fig-08-novo-nome) |
| `MODE_SETTINGS_TOUCH_CAL` | **Calibração do Touch** | Fim da etapa de sensibilidade | — | [11-mira](#fig-11-mira) |
| `MODE_SETTINGS_TOUCH_SENS` | **Sensibilidade do Toque** | Menu, item 6; **APLICAR** do alinhamento | `touchsens`, `touchcal` | [11-sensibilidade](#fig-11-sensibilidade) |
| `MODE_SETTINGS_SOUNDS` | **Config. de Sons** | Menu, item 3 | — | [07-sons-painel](#fig-07-sons-painel) |
| `MODE_SETTINGS_LICENSE` | **Licença MIT** | Menu, item 7 | `lic` | [11-licenca](#fig-11-licenca) |
| `MODE_SETTINGS_STATUS` | **Status do Sistema** | Menu, item 8 | `sts` | [11-status-1](#fig-11-status-1) |
| `MODE_SETTINGS_DISPLAY_OFFSET` | **Alinhamento da Tela** | Menu, item 9 | `offset` | [11-alinhamento](#fig-11-alinhamento) |
| `MODE_ALARM_ACTION` | `!` e o nome do sensor | Toque no cartão em alarme | — | [07-tela-alarme](#fig-07-tela-alarme) |
| `MODE_CALENDAR` | O mês e o ano | Terceiro botão do gráfico | — | [11-calendario](#fig-11-calendario) |
| `MODE_CONFIRM_MUTE_ALL` | **Mudo Global** | **Config. de Sons**, **Mudo Global** desligado | — | [07-mudo-confirmar](#fig-07-mudo-confirmar) |
| `MODE_SETTINGS_ALARM_SENSOR` | O nome do sensor | **Limites de Alarme**, sensor | — | [07-menu-sensor](#fig-07-menu-sensor) |
| `MODE_SETTINGS_MAINT` | **Manutenção** | Menu do sensor, **Manutenção** | — | [07-manutencao-entrada](#fig-07-manutencao-entrada) |
| `MODE_SETTINGS_USERS` | **Usuários** | Menu, item 10 | `usr` | [08-usuarios-lista](#fig-08-usuarios-lista) |
| `MODE_SETTINGS_USER_EDIT` | O nome da conta | **Usuários**, conta; ou o nome de uma conta nova | — | [08-novo-permissoes](#fig-08-novo-permissoes) |
| `MODE_SETTINGS_USER_CONFIRM_DEL` | **Excluir usuário** | Editor da conta, **Excluir usuário** | — | [08-excluir](#fig-08-excluir) |
| `MODE_PANEL_MESSAGE` | Sem título: ícone e mensagem | Resultado de uma ação | — | [11-mensagem-ok](#fig-11-mensagem-ok) |
| `MODE_AUTH_USER` | **Quem está usando o painel?** | **CFG**; **Desativar** na tela de alarme | `pin` | [08-quem](#fig-08-quem) |
| `MODE_SETTINGS_PIN_POLICY` | **Segurança do PIN** | Menu, item 11 | — | [08-politica](#fig-08-politica) |
| `MODE_CONFIRM_AP` | **Modo de Configuração** | Menu, item 12 | — | [11-ap-confirmar](#fig-11-ap-confirmar) |

As tags `touchcal` e `touchsens` abrem as duas a etapa de sensibilidade; a de posição vem depois dela. A tela de boot e a de progresso do gesto do AP não são modos: o painel as desenha antes da tela inicial, e `GET /api/screenshot` não as captura.
