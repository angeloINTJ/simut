# O painel Ângulo (Pico 2 W) {#cap-11a}

[release]{.img} [main]{.img} Este capítulo percorre o painel de toque da imagem `releasetwo`, a do Pico 2 W, que o desenha no padrão Ângulo: a tela inicial, os gestos, o gráfico e o menu de configurações. Esse painel está no código-fonte, mas ainda não numa versão publicada. O Pico W e um Pico 2 W montado com **Visual original** (*Original look*) usam o painel do [capítulo 11](#cap-11).

## O painel Ângulo em resumo {#cap-11a-visao}

O painel é uma tela de 320 × 240 pixels com toque resistivo, como a do painel original. Muda o desenho: os cartões, as cores, os ícones e as telas. Não muda o que um toque faz: as ações são as mesmas funções do núcleo, e quem decide é o núcleo 0. Cada toque que altera algo vira um pedido, e o núcleo 0 confere a permissão da conta antes de gravar. As leituras, os alarmes, as contas e as permissões vêm do núcleo 0, como no painel original: as regras estão no [capítulo 7](#cap-07) (alarmes) e no [capítulo 8](#cap-08) (contas e PIN), e valem para os dois painéis.

Três regras valem para todas as telas:

- **Um toque age quando o dedo encosta.** Para repetir, levante o dedo e toque de novo. A exceção é o toque curto no cartão de cima, que age ao soltar, porque o mesmo dedo pode estar começando o gesto de 3 s. As setas do menu e do gráfico repetem a cada 300 ms enquanto você segura.
- **Um toque pertence à tela onde começou.** Se a tela muda com o dedo ainda encostado, o resto do toque é ignorado até você levantar o dedo.
- **Sem toque, o painel volta à tela inicial.** Depois de 30 s sem nenhum toque, qualquer tela volta à tela inicial, menos a própria tela inicial, a da atualização do firmware e a do boot.

Um toque que cai a até 12 px de um controle vale para ele, porque a calibração de uma tela resistiva erra alguns pixels. Um toque a mais de 12 px de todos os controles não faz nada. Um controle desenhado apagado, como a seta da direita do gráfico quando a janela termina agora, não responde e não passa o toque ao controle vizinho.

Quem já conhece o [painel original](#cap-11) encontra quatro diferenças logo de início:

- a hora tem só horas e minutos, sem data e sem segundos ([A barra de cima](#cap-11a-barra));
- o gráfico tem cinco janelas, em botões no rodapé, e não os botões de ampliar e reduzir ([As janelas](#cap-11a-janelas));
- um toque numa linha do menu abre a tela dela, sem **ENTRAR** ([O menu de configurações](#cap-11a-menu));
- no menu, no gráfico, no detalhe e no calendário, o botão que sai da tela se chama **Voltar** (*Back*), e não **SAIR** nem **X**.

## A tela inicial {#cap-11a-principal}

A tela inicial tem quatro faixas, de cima para baixo:

| Faixa | O que mostra |
|---|---|
| Barra de cima | `SIMUT`, a hora, o que a telemetria ainda tem a enviar e a rede |
| Cartão de cima | Um sensor; com um alfinete ao lado do nome quando o cartão está fixado nele |
| Cartão de baixo | O sensor selecionado na barra de botões |
| Barra de botões | Um botão por sensor, a engrenagem das configurações e, se preciso, o botão de página |

::: {.figura #fig-11a-principal tipo="tft" arquivo="11a-principal.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_dht22 do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela inicial, em valores de exemplo: a barra de cima com a hora e o sinal do Wi-Fi, o cartão de cima com temperatura e umidade, o cartão de baixo com a situação **Na faixa** e a barra de botões com S0 a S3 e a engrenagem. O S1 está selecionado.
:::

### A barra de cima {#cap-11a-barra}

Da esquerda para a direita:

| Elemento | O que é |
|---|---|
| `SIMUT` | O nome do produto, fixo |
| Hora | `hh:mm`, sem data e sem segundos. Fica em âmbar enquanto o relógio é provisório ([capítulo 10](#cap-10-onde)). Sem relógio, não aparece |
| Pendentes | Um ícone de envio (uma seta para cima) e o número de registros que esperam envio pela telemetria. Só aparece quando há algum |
| Rede | Um ícone: o sinal do Wi-Fi em quatro níveis, ou o cabo |

**Pendentes.** Até 999, a contagem mostra o número; de 1.000 em diante, os milhares com um `k`: 1.500 registros aparecem como `1k`. O número e a seta ficam em cinza, e só ficam em vermelho quando o último envio falhou. A seta não pisca quando um envio dá certo. A telemetria está no [capítulo 21](#cap-21).

**A hora e os avisos.** Dois avisos tomam o lugar da hora:

- durante o silêncio de um alarme, o selo **Silenciado** (*Silenced*) com os segundos que faltam, como `Silenciado · 87 s`, em âmbar. Se o número não couber, o selo diz só **Silenciado**;
- por 5 s depois de um login pela web, o selo **Web** (*Web*) com o nome da conta, como `Web: admin`, em cinza. O nome é cortado com `…` quando não cabe.

Se os dois acontecem juntos, vale o silêncio.

**A rede.** O número de barras segue a intensidade do sinal (RSSI), nos mesmos quatro níveis do painel original ([capítulo 11](#cap-11-barra)):

| Ícone | RSSI |
|---|---|
| Quatro barras | acima de −55 dBm |
| Três barras | de −64 a −55 dBm |
| Duas barras | de −74 a −65 dBm |
| Uma barra, em âmbar | de −99 a −75 dBm |
| Wi-Fi riscado | −100 dBm ou menos: sem sinal |

A barra única vem em âmbar porque o limite de −78 dBm, de onde para baixo o aparelho segura a telemetria ([capítulo 20](#cap-20-rede)), cai dentro desse nível. Os outros estados são cinza, e nenhum estado da rede é vermelho: o sinal ausente também aparece em cinza, porque um enlace caído não é um erro.

Numa imagem montada com o cabo (módulo ENC28J60) no lugar do Wi-Fi, pelo [configurador](#cap-03-sob-medida), o ícone é um conector de rede quando o enlace está ligado e um plugue solto quando não está, os dois em cinza.

### Os cartões {#cap-11a-cartoes}

O cartão de cima mostra o nome do sensor e, lado a lado, a temperatura e o segundo valor do sensor, cada um com o seu ícone. O cartão de baixo mostra o nome, a situação do sensor, a temperatura em número grande e, à direita, o segundo valor e a faixa do alarme, como `2 a 8 °C`. O segundo valor depende do sensor:

| Sensor | Segundo valor |
|---|---|
| DHT22 | Umidade, em `%UR` |
| BMP280 e BME280 | Pressão, em `hPa`, sempre em número inteiro |
| DS18B20 | Nenhum: só a temperatura |

**A situação.** O selo no cartão de baixo diz onde a temperatura está em relação à faixa do alarme. O painel escolhe o primeiro da tabela que vale:

| Selo | Quando | Cor |
|---|---|---|
| **Erro no sensor** (*Sensor error*) | O sensor falhou ou não deu leitura | Vermelho |
| **Acima** (*Above*) | A temperatura passou do limite alto | Vermelho |
| **Abaixo** (*Below*) | A temperatura passou do limite baixo | Vermelho |
| **Em alarme** (*In alarm*) | A temperatura está na faixa, mas o slot está em alarme: outra grandeza, como a umidade ou a pressão, passou do limite | Vermelho |
| **No limite** (*At the limit*) | A temperatura está na faixa, a menos de um décimo da largura dela de um dos extremos | Âmbar |
| **Na faixa** (*In range*) | Nenhum dos casos acima | Verde |

O número grande do cartão de baixo tem a cor da situação. Sem leitura, o cartão mostra `--,-` no lugar da temperatura, e no cartão de cima o valor fica vermelho. O separador é o do idioma: `--.-` em inglês ([O gráfico, o detalhe e o calendário](#cap-11a-grafico)).

Um cartão sem sensor diz **Nenhum sensor ativo** (*No active sensor*) e **Ative um sensor pela web.** (*Activate a sensor on the web.*). Sem sensor em nenhum dos dois cartões, a mensagem ocupa a área dos dois.

**Fixado.** O cartão de cima pode ficar fixado num sensor: o ícone de um alfinete aparece ao lado do nome, e o sensor fixado não ganha botão na barra de botões. Segurar o dedo no cartão por 3 s fixa o sensor selecionado ou desfaz a fixação ([Gestos e navegação](#cap-11a-gestos)).

### Mínimo e máximo {#cap-11a-minmax}

Um toque no cartão troca o valor atual pelo mínimo e o máximo que o painel guarda do sensor: duas linhas, **mín** (*min*) e **máx** (*max*), com a temperatura e, nos sensores que a têm, a umidade. O DHT22 e o BME280 mostram a umidade aqui; o BMP280 e o DS18B20 mostram só a temperatura. O cartão em mínimo e máximo ganha o botão **Gráfico** (*Chart*), na cor de destaque. Outro toque no cartão, ou 30 s sem toque, devolve o valor atual.

::: {.figura #fig-11a-minmax tipo="tft" arquivo="11a-minmax.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_minmax_cima do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
O cartão de cima, fixado, em mínimo e máximo, em valores de exemplo: o alfinete ao lado do nome, as linhas **mín** e **máx** e o botão **Gráfico**. O sensor fixado não tem botão na barra.
:::

### A barra de botões {#cap-11a-botoes}

A barra tem cinco posições, da esquerda para a direita:

| Botão | O que faz |
|---|---|
| **S** e o número do slot, como **S1** | Seleciona o sensor para o cartão de baixo. O selecionado fica na cor de destaque |
| Engrenagem | Abre as configurações: primeiro a escolha da conta, depois o PIN ([capítulo 8](#cap-08-entrar)) |
| Página, como `1/3` | Aparece quando os botões não cabem numa linha; mostra a próxima página |

Um sensor ativo ganha um botão, menos o fixado no cartão de cima. A engrenagem vem logo depois do último sensor, então a posição dela muda com o número de sensores. Com mais de cinco botões, a barra se divide em páginas de quatro, e o botão de página fica sempre na quinta posição, à direita. Numa página incompleta, as posições vazias ficam em branco e o botão de página não sai do lugar. Depois da última página, o botão de página volta à primeira.

::: {.figura #fig-11a-paginas tipo="tft" arquivo="11a-paginas.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_9_sensores_p1 do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; nove sensores, a primeira das três páginas de botões"}
Nove sensores, em valores de exemplo: a primeira das três páginas de botões, com S0 a S3 e o botão `1/3` na quinta posição.
:::

::: {.figura #fig-11a-paginas-3 tipo="tft" arquivo="11a-paginas-3.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_9_sensores_p3 do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; nove sensores, a terceira das três páginas de botões; não há amostra da segunda"}
A terceira página dos mesmos nove sensores, em valores de exemplo: o S8, a engrenagem, duas posições vazias e o botão `3/3`.
:::

### Alarme nos cartões e nos botões {#cap-11a-alarme-cartoes}

Um sensor em alarme pisca, trocando de fase a cada 600 ms. O cartão alterna entre o normal e o cheio de vermelho. O botão do sensor alterna entre o cheio de vermelho e o normal com um ponto vermelho no canto. Se um sensor em alarme não tem botão na página que está na tela, o botão de página também fica vermelho.

Tocar no cartão que pisca abre a tela de alarme do sensor, em vez do mínimo e máximo. Durante o silêncio de 120 s, o painel para de piscar, deixa uma borda vermelha no cartão do sensor em alarme, e o toque no cartão volta a ser o normal. O alarme em si está no [capítulo 7](#cap-07-disparo).

### O aviso de web ocupada {#cap-11a-web-ocupada}

Enquanto a web grava no aparelho, um cartão cobre a parte de baixo da tela inicial com o nome da conta e a frase **está gravando pela web.** (*is saving through the web.*), e embaixo **Aguarde para tocar na tela.** (*Wait before touching the screen.*). O texto segue o idioma do painel. Os controles continuam desenhados, mas não respondem: o painel recusa os toques na tela inicial até a web terminar. O nome da conta é cortado com `…` quando a frase não cabe.

### Claro e escuro {#cap-11a-modo}

O painel tem modo claro e modo escuro, que se escolhem em **Tema** (*Theme*), no [menu de configurações](#cap-11a-menu). No modo **Automático** (*Automatic*), o painel fica escuro das 19 h às 7 h, pelo relógio do aparelho, e claro enquanto não há relógio.

::: {.figura #fig-11a-principal-escuro tipo="tft" arquivo="11a-principal-escuro.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_dht22 do grupo painel, modo escuro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; o mesmo quadro de 11a-principal, no modo escuro"}
A mesma tela inicial da primeira figura, em valores de exemplo, no modo escuro.
:::

## Gestos e navegação {#cap-11a-gestos}

Na tela inicial:

| Onde | Gesto | Resultado |
|---|---|---|
| Cartão de cima | Toque curto | Alterna entre o valor atual e o mínimo e o máximo. Age ao soltar o dedo |
| Cartão de cima | Segurar 3 s | Fixa o sensor selecionado no cartão, ou desfaz a fixação. O painel toca o som de toque |
| Cartão de cima em mínimo e máximo | Toque no botão **Gráfico** | Abre o gráfico do sensor, na janela de 1 h |
| Cartão de baixo | Toque | Alterna entre o valor atual e o mínimo e o máximo |
| Cartão de baixo em mínimo e máximo | Toque no botão **Gráfico** | Abre o gráfico do sensor, na janela de 1 h |
| Cartão em alarme, sem silêncio | Toque | Abre a tela de alarme do sensor |
| Botão **S** | Toque, ou deslizar o dedo até ele | Seleciona o sensor para o cartão de baixo |
| Engrenagem | Toque, ou deslizar o dedo até ela | Encerra qualquer sessão e abre a escolha da conta |
| Botão de página | Toque | Mostra a próxima página de botões |

Para o gesto de 3 s, o painel conta só o tempo em que viu o dedo: uma pausa de mais de 120 ms entre duas leituras do toque recomeça a contagem. Depois do gesto, o cartão de cima ignora toques por 600 ms, enquanto o dedo sobe. Enquanto o sensor do cartão de cima está em alarme sem silêncio, o toque abre a tela de alarme e o gesto de 3 s não começa.

Nas outras telas:

| Tela | Onde | Gesto | Resultado |
|---|---|---|---|
| Menu | Uma linha | Toque | Abre a tela da linha, sem pedir confirmação |
| Menu | Setas de página | Toque, ou segurar | Muda de página; repete a cada 300 ms; para na primeira e na última página |
| Menu | **Voltar** | Toque | Volta à tela inicial |
| Lendo o histórico | Qualquer ponto | Toque | Nada |
| Gráfico | **Voltar** | Toque | Fecha o gráfico e volta à tela inicial |
| Gráfico | Setas do rodapé | Toque, ou segurar | Recua ou avança a janela; repete a cada 300 ms |
| Gráfico | Um dos cinco períodos | Toque | Troca a janela. O período aceso não responde |
| Gráfico | **Dia** (*Day*) | Toque | Abre o calendário |
| Gráfico | A área do gráfico | Toque | Abre o detalhe |
| Detalhe | Os cartões | Toque | Passa à grandeza seguinte; depois da última, volta ao gráfico |
| Detalhe | Setas de página | Toque | Muda de página, quando há mais de uma |
| Detalhe | **Voltar** | Toque | Volta ao gráfico |
| Calendário | Setas do mês | Toque | Um mês por toque |
| Calendário | Um dia com ponto | Toque | Abre o gráfico daquele dia |
| Calendário | **Hoje** (*Today*) | Toque | Abre o gráfico no presente, na janela que ele tinha |
| Calendário | **Voltar** | Toque | Volta ao gráfico |

## O gráfico, o detalhe e o calendário {#cap-11a-grafico}

O gráfico mostra o histórico de um sensor numa janela de tempo. Para abri-lo, ponha o cartão em mínimo e máximo e toque em **Gráfico**. Ele abre na janela de 1 h que termina agora.

O botão **Gráfico** aparece em qualquer sensor, mas o histórico abre só para os slots de 0 a 10. Nos slots de 11 a 15, o toque no botão só devolve o cartão ao valor atual. O gráfico também depende de uma chave da imagem: o configurador exige a chave **Gráfico e calendário na tela touch** ligada para oferecer o visual Ângulo ([Uma imagem sob medida](#cap-03-sob-medida)).

Ao abrir, o painel mostra **Lendo o histórico…** (*Reading the history…*), com o título do gráfico e uma barra em que um trecho anda a cada 250 ms. A barra não mostra fração, porque ninguém sabe quanto falta, e essa tela não responde a toque. Dentro do gráfico, trocar de janela não apaga a tela: uma linha fina sob a barra de título avisa que a leitura está em curso, e o gráfico novo substitui o antigo. A linha some quando os dados chegam ou, no máximo, depois de 8 s.

::: {.figura #fig-11a-carregando tipo="tft" arquivo="11a-carregando.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra carregando_24h do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela **Lendo o histórico…**, em valores de exemplo: o título com o sensor e a janela de 24 h, e a barra com o trecho que anda.
:::

Os números do painel usam o separador decimal do idioma, que o pacote de idioma dá: vírgula com o pacote pt-BR e com o es-ES, ponto em inglês e sem pacote. O mesmo vale para a tela inicial: `4,6` e `--,-` em português, `4.6` e `--.-` em inglês.

### A tela do gráfico {#cap-11a-grafico-tela}

::: {.figura #fig-11a-grafico tipo="tft" arquivo="11a-grafico.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra grafico_24h do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
O gráfico da janela de 24 h de um sensor de temperatura, em valores de exemplo: o título, as estatísticas, a curva, o eixo de tempo e, no rodapé, as setas e os cinco períodos, com **24 h** aceso.
:::

De cima para baixo:

- **O título** traz o nome do sensor e a janela. À direita ficam **Dia** e **Voltar**. O título é só leitura: tocar nele não faz nada.
- **As estatísticas** são da temperatura e da janela inteira: **máx** (*max*) e **mín** (*min*), cada um com a hora em que ocorreu (`dd/mm` na janela de 7 d), **média** (*avg*), **σ** (o desvio), **Δ** (a variação) e **agora** (*now*). Quando a janela termina no passado, **agora** vira **fim** (*end*).
- **A curva** usa até 200 intervalos de tempo, e nunca menos de 40. Linhas tracejadas marcam os limites de alarme do sensor, quando caem dentro da escala; elas nunca esticam a escala.
- **O eixo de tempo** tem três marcas: começo, meio e fim da janela, em `hh:mm` até 24 h e em `dd/mm` na janela de 7 d. A data aparece também na primeira marca quando a janela cruza a meia-noite ou está no passado.
- **O rodapé** tem a seta de recuar, os cinco períodos e a seta de avançar.

Sem leitura na janela, o painel diz **Sem dados neste período.** (*No data in this period.*) e **Escolha outro período ou outro dia.** (*Choose another period or another day.*).

### As janelas {#cap-11a-janelas}

O rodapé tem cinco janelas: **1 h**, **6 h**, **12 h**, **24 h** e **7 d**. A janela atual fica acesa na cor de destaque e não responde ao toque; tocar noutra troca a janela. As setas nas pontas recuam e avançam a janela no tempo. A da direita fica apagada, e não responde, enquanto a janela termina agora: não há futuro para mostrar.

### O detalhe {#cap-11a-detalhe}

Um toque na área do gráfico abre o detalhe, com os números da janela em seis cartões: **Máximo**, **Mínimo**, **Média** (com o número de leituras), **Desvio σ** (a dispersão), **Variação** (do início ao fim) e **Agora**, que vira **Fim** quando a janela termina no passado.

::: {.figura #fig-11a-detalhe tipo="tft" arquivo="11a-detalhe.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra detalhe_temperatura do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
O detalhe da temperatura, em valores de exemplo: os seis cartões e, no rodapé, o paginador `1 de 3`.
:::

O detalhe tem uma página para cada grandeza que o sensor tem, nesta ordem: **Temperatura**, **Umidade** e **Pressão**. Um paginador no rodapé mostra a página, e as setas ficam apagadas nas pontas. Um toque nos cartões também passa à página seguinte e, depois da última, volta ao gráfico. Com menos de duas leituras na janela, só a página da temperatura existe. **Voltar** volta ao gráfico.

### O calendário {#cap-11a-calendario}

O botão **Dia** abre o calendário do sensor. O título traz o mês e o ano, como `Out 2026`, e a semana começa no domingo. Os dias com histórico têm um ponto, e só eles respondem ao toque. O dia de hoje fica marcado na cor de destaque. Embaixo, **Ponto = dia com histórico.** (*Dot = day with history.*) explica o ponto; num mês sem nenhum dia com histórico, o texto é **Nenhum dia com histórico.** (*No days with history.*).

::: {.figura #fig-11a-calendario tipo="tft" arquivo="11a-calendario.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra calendario_outubro do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
O calendário de outubro de 2026, em valores de exemplo: os dias com histórico com um ponto, o dia de hoje marcado e o botão **Hoje** no canto.
:::

As setas do título passam um mês por toque, e a da direita fica apagada no mês atual. Um toque num dia com ponto abre o gráfico daquele dia, depois de **Lendo o histórico…**. **Hoje** abre o gráfico no presente, na janela que ele tinha, e **Voltar** volta ao gráfico com os dados que ele já tinha.

## O menu de configurações {#cap-11a-menu}

Para entrar, toque na engrenagem, escolha a sua conta e digite o PIN ([capítulo 8](#cap-08-entrar)). A engrenagem encerra qualquer sessão e pede o PIN de novo: ela não concede nada. O menu abre com o título **Configurações** (*Settings*) e lista só as linhas que as permissões da conta alcançam. Cada linha tem um ícone, o nome e uma seta à direita. Sob a lista ficam as setas de página, o número da página, como `1 de 4`, e a conta, no formato **Conta** (*Account*) seguido do nome, como `Conta: admin`.

O menu tem até 13 linhas, quatro por página. Toque numa linha e a tela dela abre na hora: não há **ENTRAR** nem confirmação. **Voltar** leva à tela inicial. As setas de página param na primeira e na última página, e repetem a cada 300 ms enquanto você segura. O painel não numera as linhas.

As linhas seguem a frequência de uso, e cada página do menu completo é um grupo: a primeira tem o que se usa no dia a dia (alarmes, sons, status e relógio); a segunda, o acesso (o próprio PIN, as contas, a política de PIN e o AP de configuração); a terceira, os ajustes da tela, feitos uma vez; a quarta, a licença. Uma conta que só tem permissões de painel vê uma página: limites de alarme, status, o próprio PIN e a licença.

| Nº | Linha | Permissão |
|---|---|---|
| 1 | **Limites de alarme** (*Alarm limits*) | Uma das três de painel |
| 2 | **Sons** (*Sounds*) | **Sistema** [PERM_SYS_CONFIG]{.perm} |
| 3 | **Status do sistema** (*System status*) | Nenhuma |
| 4 | **Data e hora** (*Date and time*) | **Sistema** |
| 5 | **Alterar meu PIN** (*Change my PIN*) | Nenhuma |
| 6 | **Usuários** (*Users*) | **Usuários** [PERM_USER_MGR]{.perm} |
| 7 | **Segurança do PIN** (*PIN security*) | **Usuários** |
| 8 | **Modo de configuração** (*Setup mode*) | **Rede** [PERM_NET_CONFIG]{.perm} |
| 9 | **Idioma** (*Language*) | **Sistema** |
| 10 | **Tema** (*Theme*) | **Sistema** |
| 11 | **Calibrar o toque** (*Calibrate touch*) | **Sistema** |
| 12 | **Alinhar a tela** (*Align the screen*) | **Sistema** |
| 13 | **Licença** (*License*) | Nenhuma |

A coluna **Nº** é a posição no menu completo; o painel não a desenha. As três permissões de painel da linha 1 são **Limites (painel)** [PERM_ALARM_LIMITS]{.perm}, **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm} e **Manut. (painel)** [PERM_MAINT]{.perm}; basta uma delas. As 13 permissões estão no [capítulo 8](#cap-08-permissoes). Numa imagem com cabo, sem rádio, a linha **Modo de configuração** não existe, e as linhas seguintes sobem uma posição.

Esconder a linha é só cortesia: o núcleo 0 confere a permissão de novo quando a ação da tela chega. Quatro ações do painel ainda não passam por essa conferência no núcleo 0 e dependem só da permissão da linha: salvar os sons, aplicar e salvar a calibração do toque e aplicar o alinhamento da tela. Elas estão registradas como desvios em `tools/check_authz.py`, e valem do mesmo modo no painel original.

::: {.figura #fig-11a-menu tipo="tft" arquivo="11a-menu.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra menu_admin_p1 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; menu da conta administradora, página 1 de 4"}
A primeira página do menu da conta administradora, em valores de exemplo: **Limites de alarme**, **Sons**, **Status do sistema** e **Data e hora**, com `1 de 4` e a conta no rodapé.
:::

::: {.figura #fig-11a-menu-p2 tipo="tft" arquivo="11a-menu-p2.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra menu_admin_p2 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página 2 de 4"}
A segunda página, em valores de exemplo: **Alterar meu PIN**, **Usuários**, **Segurança do PIN** e **Modo de configuração**.
:::

::: {.figura #fig-11a-menu-p3 tipo="tft" arquivo="11a-menu-p3.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra menu_admin_p3 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página 3 de 4"}
A terceira página, em valores de exemplo: **Idioma**, **Tema**, **Calibrar o toque** e **Alinhar a tela**.
:::

::: {.figura #fig-11a-menu-p4 tipo="tft" arquivo="11a-menu-p4.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra menu_admin_p4 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página 4 de 4"}
A quarta página, em valores de exemplo: só **Licença**, com a seta de avançar apagada.
:::
