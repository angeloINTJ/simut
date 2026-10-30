# O painel Ângulo (Pico 2 W) {#cap-11a}

[release]{.img} [main]{.img} Este capítulo percorre o painel de toque da imagem `releasetwo`, a do Pico 2 W, que o desenha no padrão Ângulo: a tela inicial, os gestos, o gráfico e o menu de configurações. Esse painel está no código-fonte, mas ainda não numa versão publicada. O Pico W e um Pico 2 W montado com **Visual original** (*Original look*) usam o painel do [capítulo 11](#cap-11).

## O painel Ângulo em resumo {#cap-11a-visao}

O painel é uma tela de 320 × 240 pixels com toque resistivo, como a do painel original. Muda o desenho: os cartões, as cores, os ícones e as telas. Não muda o que um toque faz: as ações são as mesmas funções do núcleo, e quem decide é o núcleo 0. Cada toque que grava na configuração vira um pedido, e o núcleo 0 confere a permissão da conta antes de gravar, com quatro exceções registradas ([O menu de configurações](#cap-11a-menu)). Silenciar um alarme é outra coisa e não pede permissão, de propósito: quem está diante do painel pode parar o som, e a linha de alarmes registra o silêncio ([A tela de alarme](#cap-11a-alarme)). As leituras, os alarmes, as contas e as permissões vêm do núcleo 0, como no painel original: as regras estão no [capítulo 7](#cap-07) (alarmes) e no [capítulo 8](#cap-08) (contas e PIN), e valem para os dois painéis.

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
| Cartão de cima | Um sensor: o fixado, com um alfinete ao lado do nome, ou, sem fixação, o selecionado |
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

**Fixado.** O cartão de cima pode ficar fixado num sensor: o ícone de um alfinete aparece ao lado do nome, e o sensor fixado não ganha botão na barra de botões. Segurar o dedo no cartão por 3 s fixa o sensor selecionado ou desfaz a fixação ([Gestos e navegação](#cap-11a-gestos)). Sem fixação, o cartão de cima mostra o sensor selecionado, o mesmo do cartão de baixo; ao fixar o sensor que está selecionado, o cartão de baixo passa para o primeiro outro sensor ativo, se houver.

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

Tocar no cartão que pisca abre a [tela de alarme](#cap-11a-alarme) do sensor, em vez do mínimo e máximo. Durante o silêncio de 120 s, o painel para de piscar, deixa uma borda vermelha no cartão do sensor em alarme, e o toque no cartão volta a ser o normal. O alarme em si está no [capítulo 7](#cap-07-disparo).

### O aviso de web ocupada {#cap-11a-web-ocupada}

Algumas operações da interface web leem a memória por vários segundos: os gráficos do histórico, a exportação do histórico em CSV, a leitura e a exportação do log de eventos. Enquanto uma delas corre, um cartão de 52 px cobre o pé do cartão de baixo, com o nome da conta e a frase **lê registros pela web.** (*reads records on the web.*), e embaixo **Aguarde para tocar na tela.** (*Wait before touching the screen.*). O nome do sensor e o selo de situação, acima dele, e a barra de botões continuam à vista. Nada é gravado: o cartão só aparece enquanto a web lê. O texto segue o idioma do painel. Os controles continuam desenhados, mas não respondem: o painel recusa os toques na tela inicial até a web terminar. O nome da conta é cortado com `…` quando a frase não cabe.

### Claro e escuro {#cap-11a-modo}

O painel tem modo claro e modo escuro, que se escolhem em **Tema** (*Theme*), no [menu de configurações](#cap-11a-menu). A tela está em [Temas](#cap-11a-temas). No modo **Automático** (*Automatic*), o painel fica escuro das 19 h às 7 h, pelo relógio do aparelho, e claro enquanto não há relógio.

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
O gráfico da janela de 24 h de um sensor de temperatura, em valores de exemplo: o título, as estatísticas, a curva com a linha tracejada cinza da média (4,4), o eixo de tempo e, no rodapé, as setas e os cinco períodos, com **24 h** aceso.
:::

De cima para baixo:

- **O título** traz o nome do sensor e a janela. À direita ficam **Dia** e **Voltar**. O título é só leitura: tocar nele não faz nada.
- **As estatísticas** são da temperatura e da janela inteira: **máx** (*max*) e **mín** (*min*), cada um com a hora em que ocorreu (`dd/mm` na janela de 7 d), **média** (*avg*), **σ** (o desvio), **Δ** (a variação) e **agora** (*now*). Quando a janela termina no passado, **agora** vira **fim** (*end*).
- **A curva** usa até 200 intervalos de tempo, e nunca menos de 40. Dois tipos de linha tracejada podem aparecer: os limites de alarme do sensor, em vermelho, o inferior e o superior, só quando caem dentro da escala (eles nunca esticam a escala), e a média da janela, em cinza.
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

O botão **Dia** abre o calendário do sensor. O título traz o mês e o ano, como `Out 2026`, e a semana começa no domingo. Os dias com histórico têm um ponto, e só eles respondem ao toque. O dia de hoje fica marcado na cor de destaque e nunca leva o ponto: com histórico, ele responde ao toque do mesmo jeito. Embaixo, **Ponto = dia com histórico.** (*Dot = day with history.*) explica o ponto; num mês sem nenhum dia com histórico, o texto é **Nenhum dia com histórico.** (*No days with history.*).

::: {.figura #fig-11a-calendario tipo="tft" arquivo="11a-calendario.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra calendario_outubro do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
O calendário de outubro de 2026, em valores de exemplo: os dias com histórico com um ponto, o dia de hoje marcado e o botão **Hoje** no canto.
:::

As setas do título passam um mês por toque, e a da direita fica apagada no mês atual. Um toque num dia com ponto abre o gráfico daquele dia, depois de **Lendo o histórico…**. **Hoje** abre o gráfico no presente, na janela que ele tinha, e **Voltar** volta ao gráfico com os dados que ele já tinha.

## O menu de configurações {#cap-11a-menu}

Para entrar, toque na engrenagem, escolha a sua conta e digite o PIN ([capítulo 8](#cap-08-entrar)). A engrenagem encerra qualquer sessão e pede o PIN de novo: ela não concede nada. O menu abre com o título **Configurações** (*Settings*) e lista só as linhas que as permissões da conta alcançam. Cada linha tem um ícone, o nome e uma seta à direita. Sob a lista ficam as setas de página, o número da página, como `1 de 4`, e a conta, no formato **Conta** (*Account*) seguido do nome, como `Conta: admin`.

O menu tem até 13 linhas, quatro por página. Toque numa linha e a tela dela abre na hora: não há **ENTRAR** nem confirmação. **Voltar** leva à tela inicial. As setas de página param na primeira e na última página, e repetem a cada 300 ms enquanto você segura. O painel não numera as linhas.

As linhas seguem a frequência de uso, e cada página do menu completo é um grupo: a primeira tem o que se usa no dia a dia (alarmes, sons, status e relógio); a segunda, o acesso (o próprio PIN, as contas, a política de PIN e o AP de configuração); a terceira, os ajustes da tela, feitos uma vez; a quarta, a licença. Uma conta que só tem permissões de painel vê uma página: limites de alarme, status, o próprio PIN e a licença.

| Nº | Linha | Permissão | O que abre |
|---|---|---|---|
| 1 | **Limites de alarme** (*Alarm limits*) | Uma das três de painel | [Limites de alarme](#cap-11a-alarmes) |
| 2 | **Sons** (*Sounds*) | **Sistema** [PERM_SYS_CONFIG]{.perm} | [Sons](#cap-11a-sons) |
| 3 | **Status do sistema** (*System status*) | Nenhuma | [Status do sistema](#cap-11a-status) |
| 4 | **Data e hora** (*Date and time*) | **Sistema** | [Data e hora](#cap-11a-data-hora) |
| 5 | **Alterar meu PIN** (*Change my PIN*) | Nenhuma | [O PIN novo](#cap-11a-pin-novo) |
| 6 | **Usuários** (*Users*) | **Usuários** [PERM_USER_MGR]{.perm} | [Usuários](#cap-11a-usuarios) |
| 7 | **Segurança do PIN** (*PIN security*) | **Usuários** | [A segurança do PIN](#cap-11a-seguranca-pin) |
| 8 | **Modo de configuração** (*Setup mode*) | **Rede** [PERM_NET_CONFIG]{.perm} | [A rede de configuração](#cap-11a-ap) |
| 9 | **Idioma** (*Language*) | **Sistema** | [Idioma](#cap-11a-idioma) |
| 10 | **Tema** (*Theme*) | **Sistema** | [Temas](#cap-11a-temas) |
| 11 | **Calibrar o toque** (*Calibrate touch*) | **Sistema** | [A calibração do toque](#cap-11a-calibrar) |
| 12 | **Alinhar a tela** (*Align the screen*) | **Sistema** | [Alinhar a tela](#cap-11a-alinhar) |
| 13 | **Licença** (*License*) | Nenhuma | [Licença](#cap-11a-licenca) |

A coluna **Nº** é a posição no menu completo; o painel não a desenha. As três permissões de painel da linha 1 são **Limites (painel)** [PERM_ALARM_LIMITS]{.perm}, **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm} e **Manut. (painel)** [PERM_MAINT]{.perm}; basta uma delas. As 13 permissões estão no [capítulo 8](#cap-08-permissoes). Numa imagem com cabo, sem rádio, a linha **Modo de configuração** não existe, e as linhas seguintes sobem uma posição.

Esconder a linha é só cortesia: o núcleo 0 confere a permissão de novo quando a ação da tela chega, e `tools/check_authz.py` classifica cada pedido do painel (`docs/AUTHORIZATION.md`). **Quatro ações ainda não passam por essa conferência no núcleo 0**: salvar os sons, salvar a sensibilidade do toque, salvar a calibração do toque e aplicar o alinhamento da tela. Para elas, a única guarda é a permissão **Sistema** da linha do menu, que o painel só mostra a quem a tem, e quem perde a permissão com a tela aberta ainda consegue salvar. Estão registradas como desvios (decisão do mantenedor de 28/10/2026). Os pedidos são os mesmos nos dois painéis, então o desvio vale para os dois.

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

## Os ajustes {#cap-11a-ajustes}

Esta seção percorre as telas que o [menu](#cap-11a-menu) abre para ajustar o aparelho, na ordem do menu: os alarmes, os sons, o status, a hora, a rede de configuração e a tela. Para cada uma: os campos, os limites, as ações, a permissão e onde o ajuste fica gravado. As telas de conta e PIN estão em [Quem está no painel](#cap-11a-conta-pin), e o tema, o idioma e a licença têm uma seção cada ([Temas](#cap-11a-temas), [Idioma](#cap-11a-idioma) e [Licença](#cap-11a-licenca)).

Duas regras valem para quase todas as telas. O ajuste se faz numa cópia de trabalho, e nada é gravado antes do botão que salva ou aplica. E quem grava é o núcleo 0, depois de conferir a permissão, menos nas quatro ações que o [menu](#cap-11a-menu) lista.

### Limites de alarme e o menu do sensor {#cap-11a-alarmes}

O item **Limites de alarme** (*Alarm limits*) abre a lista dos sensores ativos, com duas linhas por sensor: o nome e a faixa de temperatura do alarme, como `2,0 a 8,0 °C`, com um selo à direita. O selo diz **Ligado** (*On*), em verde, quando os alarmes do sensor estão ligados; **Desligado** (*Off*), em cinza, quando estão desligados; e **Manutenção** (*Maintenance*), em âmbar, enquanto há uma janela de manutenção aberta. A manutenção vale mais que os outros dois, porque com a janela aberta os alarmes ficam suspensos, qualquer que seja a chave. Sem sensor ativo, a lista diz **Nenhum sensor ativo** e **Ative um sensor pela web.**. Um toque numa linha abre o menu do sensor.

::: {.figura #fig-11a-alarmes tipo="tft" arquivo="11a-alarmes.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra alarmes_3 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; lista de sensores com alarme, três sensores"}
A lista de sensores com alarme, em valores de exemplo: dois sensores com os alarmes ligados e um em manutenção.
:::

O menu do sensor tem três linhas, e cada uma pede a sua permissão de painel. Uma linha que a conta não pode usar toca o som de erro e não abre nada.

| Linha | O que faz | Permissão |
|---|---|---|
| **Limites** (*Limits*) | Abre o [editor de limites](#cap-11a-limites) | **Limites (painel)** [PERM_ALARM_LIMITS]{.perm} |
| **Alarmes** (*Alarms*) | É uma chave: liga ou desliga os alarmes do sensor na hora | **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm} |
| **Manutenção** | Abre a [janela de manutenção](#cap-11a-manutencao). Embaixo, o que falta, como `1 h 30 min restantes`, ou **Desligada** (*Inactive*) | **Manut. (painel)** [PERM_MAINT]{.perm} |

::: {.figura #fig-11a-sensor tipo="tft" arquivo="11a-sensor.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra sensor_liberado do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; menu de um sensor"}
O menu de um sensor, em valores de exemplo: **Limites**, a chave **Alarmes** ligada e **Manutenção** desligada, com a nota embaixo.
:::

Embaixo fica a nota **Ligar ou desligar alarmes vale na hora.** A chave **Alarmes** vira um pedido ao núcleo 0, que confere a permissão de novo, grava na configuração do sensor, registra a mudança e a põe na linha de alarmes. A linha **Limites de alarme** do menu vale para quem tem qualquer uma das três permissões de painel.

### O editor de limites {#cap-11a-limites}

O editor mostra uma grandeza por página, com o mínimo e o máximo dela: **Temperatura mínima** (*Minimum temperature*) e **Temperatura máxima** na primeira página e, conforme o sensor, **Umidade mínima** e **Umidade máxima**, ou **Pressão mínima** e **Pressão máxima**, nas seguintes. O número de páginas é o de grandezas que o sensor mede. Cada limite tem uma barra com os botões − e +, e apertar uma seta escolhe o limite dela.

::: {.figura #fig-11a-limites tipo="tft" arquivo="11a-limites.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra limites_temperatura do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; edição dos limites de temperatura"}
O editor de limites da temperatura, em valores de exemplo: o mínimo de 2,0 °C, o máximo de 8,0 °C, a página `1 de 2` e os botões **Cancelar** e **Salvar limites**.
:::

- **O passo.** Cada toque em − ou + muda o limite em 0,1. Segurando a mesma seta, o passo cresce: 0,5 depois de 2 s, 1 depois de 4 s e 10 depois de 6 s. A seta repete a cada 300 ms.
- **Os extremos.** O valor para na faixa plausível da grandeza. O mínimo e o máximo nunca se cruzam: o que você não está segurando é empurrado 0,1 para o lado.
- **Cancelar** joga fora a cópia e volta ao menu do sensor. **Salvar limites** (*Save limits*) pede ao núcleo 0 que compare a cópia com a configuração, grave, registre e emita o registro `alarm_lim`; depois volta ao menu do sensor.

O núcleo 0 confere **Limites (painel)** antes de gravar, e os limites ficam na configuração do sensor.

### A manutenção {#cap-11a-manutencao}

A tela **Manutenção** abre pelo menu do sensor, com o nome do sensor no título, e tem dois estados:

- **Sem janela aberta**, mostra dois seletores, **Horas** (*Hours*) e **Minutos** (*Minutes*), cada um com − e +. As horas vão de 0 a 720, de uma em uma e sem dar a volta (o teto são 30 dias). Os minutos andam de 5 em 5 e dão a volta entre 55 e 0. O botão **Iniciar manutenção** (*Start maintenance*) abre a janela com o tempo escolhido; com 0 h e 0 min, o painel toca o som de erro e nada acontece.
- **Com a janela aberta**, mostra **Tempo restante** (*Time left*), como `1 h 29 min`, numa moldura âmbar, e o botão **Encerrar manutenção** (*End maintenance*), em vermelho.

::: {.figura #fig-11a-manutencao tipo="tft" arquivo="11a-manutencao.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra manut_aberta do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; manutenção em andamento"}
Uma manutenção em andamento, em valores de exemplo: o tempo restante e o botão **Encerrar manutenção**.
:::

Embaixo, a nota **Os alarmes ficam suspensos até o fim.** aparece quando a conta tem a permissão **Manut. (painel)** ou a janela está aberta. Sem a permissão, a tela diz **Sua conta não pode abrir manutenção.**, sozinha, quando não há janela aberta, ou **Sua conta não pode encerrar.**, sob a nota dos alarmes, quando há; e o painel não envia o pedido. O núcleo 0 recebe os segundos (zero fecha a janela), confere a permissão de novo, grava, registra e reavalia os alarmes.

### Sons {#cap-11a-sons}

A tela **Sons** (*Sounds*) tem nove linhas em três páginas. Sete têm uma chave: **Toque na tela**, **Confirmação**, **Erro**, **Alarme**, **Atenção**, **Acessos pela web** e **Silenciar tudo**. As outras duas ajustam o **Volume do sistema** e o **Volume do alarme**.

::: {.figura #fig-11a-sons tipo="tft" arquivo="11a-sons.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra sons_p1 do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página 1"}
A primeira página da tela Sons, em valores de exemplo: quatro sons, cada um com a melodia escolhida e a chave, a página `1 de 3` e o botão **Salvar sons**.
:::

- **A chave** liga ou desliga o som na cópia. Ligar um som, ou o acesso pela web, desliga o **Silenciar tudo**, como na página web.
- **O nome da melodia e a seta**, nos cinco primeiros sons (de **Toque na tela** a **Atenção**) que estão ligados, abrem a escolha da melodia.
- **Silenciar tudo**: ligar pede confirmação, numa tela própria; desligar é direto.
- **Os volumes** andam de 10 em 10, entre 0 e 100, e a seta repete a cada 300 ms. O painel toca uma prévia no volume novo.
- **Salvar sons** (*Save sounds*) manda a cópia ao núcleo 0, que aplica os sons, grava na configuração do aparelho, toca o som de confirmação e volta ao menu.

A confirmação do mudo pergunta **Silenciar todos os sons?** e avisa que nenhum som toca, nem o do alarme, até alguém ligar os sons de novo, e que o mudo só vale depois de **Salvar sons**. **Silenciar tudo** aplica o mudo à cópia e desliga todos os sons dela; **Voltar** cancela. Nos dois casos o painel volta à tela Sons.

O nome de um som ligado abre a escolha da melodia: seis opções por som, como **Bipe duplo**, **Sirene**, **Rápido**, **Pulso**, **Crescente** e **Staccato** para o alarme. Um toque numa opção a escolhe e toca uma prévia; deslizar o dedo para outra faz o mesmo.

::: {.figura #fig-11a-melodia tipo="tft" arquivo="11a-melodia.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra melodia_alarme do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; escolha da melodia do alarme"}
A escolha da melodia do alarme, em valores de exemplo: seis opções, com **Sirene** escolhida.
:::

**Usar esta melodia** (*Use this melody*) liga o som com a melodia escolhida, só na cópia, e **Cancelar** volta à lista sem levar nada. A escolha só vale depois de **Salvar sons**.

A linha **Sons** do menu pede **Sistema** [PERM_SYS_CONFIG]{.perm}. Salvar os sons é uma das quatro ações que o núcleo 0 não confere ([O menu de configurações](#cap-11a-menu)).

### Status do sistema {#cap-11a-status}

A tela **Status** (*Status*) é só de leitura: mostra o estado do aparelho em quatro páginas, que as setas do rodapé viram. O núcleo 0 atualiza os dados uma vez por segundo. O título leva o nome da página, como `Status · Sistema`.

| Página | Linhas |
|---|---|
| **Sistema** (*System*) | **Aparelho** (*Device*; **Sem nome** se não tem nome), **Firmware**, **Tempo ligado** (*Uptime*), **Memória livre**, **Flash** (usado e total) e **Placa** (a temperatura da placa e o fuso horário) |
| **Rede** (*Network*) | **Wi-Fi** (o nome da rede, ou **Conectado** ou **Desconectado**), **Sinal** (só com o Wi-Fi conectado), **Endereço IP**, **MAC**, **NTP** (**Sincronizado** ou **Não sincronizado**) e **Servidor NTP**. Numa imagem com cabo, **Cabo** (**Conectado** ou **Sem cabo**) e nenhuma linha de sinal |
| **Sensores** (*Sensors*) | **Ativos**, como `3 de 16` (os sensores ativos e o total de slots), e uma linha por sensor com a situação: **Na faixa**, **Em alarme**, **Erro**, **Manutenção** ou **Sem alarme** (alarmes desligados). Até 5 sensores; os outros viram a linha `e mais N sensores` |
| **Telemetria** (*Telemetry*) | **Transporte** (**MQTT** ou **HTTP**), **Servidor**, **Pendentes** e **Lote mínimo**; com MQTT, uma linha **MQTT** diz **Conectado** ou **Desconectado** |

::: {.figura #fig-11a-status tipo="tft" arquivo="11a-status.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra status_sistema do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página Sistema; os valores, a versão do firmware (2.11.1) entre eles, são dados da amostra"}
A página **Sistema** do status, em valores de exemplo: o nome do aparelho, a versão do firmware (2.11.1), o tempo ligado, a memória livre, o uso da flash e a temperatura da placa com o fuso.
:::

Algumas linhas têm cor. A **Memória livre** fica vermelha abaixo de 20.000 bytes e verde de 20.000 para cima. O **Sinal** fica verde acima de −60 dBm, cinza até −80 dBm e âmbar de −80 dBm para baixo. **Pendentes** fica verde até 50 e âmbar acima de 50. O **NTP** é verde quando sincronizado e âmbar quando não. **Conectado** é verde, e **Desconectado** e **Sem cabo** são vermelhos.

A linha **Status do sistema** do menu não pede permissão, a tela não envia nenhum pedido, e nada é gravado.

### Data e hora {#cap-11a-data-hora}

A tela **Data e hora** (*Date and time*) tem cinco colunas, dia, mês, ano, hora e minuto, cada uma com uma seta para cima e outra para baixo; as setas repetem a cada 300 ms. Os valores dão a volta: o dia dentro do mês, o mês de 1 a 12, a hora de 0 a 23 e o minuto de 0 a 59. O ano para em 2026 e em 2099, e trocar o mês ou o ano corrige o dia para caber no mês.

::: {.figura #fig-11a-data-hora tipo="tft" arquivo="11a-data-hora.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra relogio_menu do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela Data e hora, em valores de exemplo: dia, mês e ano à esquerda, hora e minuto à direita, e os botões **Voltar** e **Salvar hora**.
:::

**Salvar hora** (*Save time*) entrega os campos ao núcleo 0, que os confere de novo e decide se a sessão pode acertar o relógio; o painel não desenha nada até a resposta. A ação pede **Sistema** [PERM_SYS_CONFIG]{.perm}. No boot, sem sessão, o núcleo 0 aceita só a pergunta do próprio boot. Quando a tela abre no boot, o botão da esquerda é **Pular** (*Skip*) no lugar de **Voltar** (*Back*), e uma nota diz **Sem rede: os registros usam esta hora.**. **Pular** segue para a tela inicial; **Voltar**, fora do boot, volta ao menu.

### A rede de configuração {#cap-11a-ap}

A linha **Modo de configuração** (*Setup mode*) do menu abre a pergunta **Abrir a rede de configuração?** A tela avisa que o aparelho sai da rede Wi-Fi, abre a própria rede de configuração e que a telemetria para até ele voltar, 15 minutos depois. O prazo é o `AP_MODE_TIMEOUT_MS`, de 900.000 ms, e só vale com uma rede Wi-Fi configurada: é a ela que o aparelho volta, reiniciando. Sem rede configurada, o aparelho não reinicia no fim do prazo, e a frase da tela não vale.

::: {.figura #fig-11a-ap-confirmar tipo="tft" arquivo="11a-ap-confirmar.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra dialogo_ap do grupo base, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; confirmação para abrir a rede de configuração"}
A pergunta que abre a rede de configuração, com os botões **Cancelar** e **Abrir rede**.
:::

**Abrir rede** (*Start setup*) pede ao núcleo 0 que abra o ponto de acesso, e o núcleo 0 confere **Rede** [PERM_NET_CONFIG]{.perm} antes. O painel passa a mostrar o terminal de boot, até a rede ficar pronta. **Cancelar** volta ao menu. Não há ajuste a gravar. Numa imagem com cabo, sem rádio, nem a linha nem a pergunta existem. O ponto de acesso está no [capítulo 9](#cap-09).

### A calibração do toque {#cap-11a-calibrar}

A linha **Calibrar o toque** (*Calibrate touch*) do menu abre duas etapas em sequência: primeiro a sensibilidade, depois a posição. A passagem de uma para a outra é automática. As duas etapas são do núcleo 0; o Ângulo só desenha, e o núcleo toma os toques delas antes de qualquer outra tela.

**A sensibilidade.** A tela pede **Toque na tela várias vezes, como faria no dia a dia.** e mostra **Leituras** (o número de leituras do toque, uma a cada 15 ms, e não o de toques), **Estabilidade** (em %) e **Limiar**. Ao terminar, diz **Pronto.** e **Agora a posição do toque.**. **Cancelar** sai: no boot da primeira vez, adota a calibração padrão e segue para a tela inicial; nos outros casos, volta ao menu.

::: {.figura #fig-11a-sensibilidade tipo="tft" arquivo="11a-sensibilidade.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra sensibilidade_metade do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A etapa de sensibilidade, em valores de exemplo: as leituras, a estabilidade e o limiar, e o botão **Cancelar**.
:::

**A posição.** A tela mostra uma mira e pede **Toque no centro da mira e segure.**, com `Ponto 1 de 4 · volta 1 de 2`: são quatro pontos em duas voltas. A mira fica verde quando o toque passa de 400 ms. Ao fim, a tela diz **Calibração concluída.** com o botão **Entendi**, ou **Toques imprecisos.** com **Toque de novo, devagar, no centro de cada mira.** e o botão **Refazer a calibração**.

::: {.figura #fig-11a-calibrar tipo="tft" arquivo="11a-calibrar.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra calibrar_ponto_1 do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; ponto 1 de 4"}
A etapa de posição, no primeiro ponto da primeira volta, em valores de exemplo: a mira e o texto `Ponto 1 de 4 · volta 1 de 2`.
:::

A etapa de sensibilidade grava o limiar de pressão e a de posição grava a calibração, as duas na configuração do aparelho, com o som de confirmação. A linha do menu pede **Sistema**. Os dois pedidos ao núcleo 0 estão entre as quatro ações que ele não confere ([O menu de configurações](#cap-11a-menu)).

### Alinhar a tela {#cap-11a-alinhar}

A tela **Alinhar a tela** (*Align the screen*) corrige o alinhamento da imagem com o vidro. Ela diz **A moldura deve tocar a borda.** e tem quatro setas, o centro, as coordenadas `x` e `y` e os botões **Redefinir** (*Reset*) e **Salvar** (*Save*). Cada seta move a imagem 1 px, ao vivo, entre −4 e +4 px nos dois eixos, e repete a cada 300 ms. **Redefinir** zera os dois eixos.

::: {.figura #fig-11a-alinhar tipo="tft" arquivo="11a-alinhar.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra alinhar_ajuste do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela de alinhamento, em valores de exemplo: a moldura, as setas, `x +1` e `y 0`, e a nota **Salvar refaz a calibração do toque.**
:::

**Salvar** manda o alinhamento ao núcleo 0, que o grava na configuração, zera a calibração do toque e manda calibrá-lo de novo, e por isso a tela avisa **Salvar refaz a calibração do toque.**. **Voltar** devolve a imagem ao alinhamento gravado. A linha do menu pede **Sistema**, e o pedido ao núcleo 0 é uma das quatro ações que ele não confere ([O menu de configurações](#cap-11a-menu)).

## Quem está no painel: conta e PIN {#cap-11a-conta-pin}

Para entrar no menu, o painel pergunta quem está na frente dele e pede o PIN da conta. Esta seção mostra o que o Ângulo desenha. As regras (a política de PIN, os bloqueios, o PIN de fábrica) são as do [capítulo 8](#cap-08-identidade) e valem para os dois painéis: o núcleo 0 embaralha os cartões, confere o PIN e conta as tentativas.

### A escolha da conta {#cap-11a-quem}

A engrenagem da tela inicial abre **Quem está no painel?** (*Who is at the panel?*): uma lista com o nome das contas, quatro por página, e **Voltar**. As setas de página param nas pontas e repetem a cada 300 ms. Um toque numa conta abre o teclado do PIN dela. A linha de uma conta bloqueada mostra o selo **Bloqueada** (*Locked*), em vermelho, com o nome apagado e sem a seta; um toque nela só toca o som de erro. **Voltar** leva à tela inicial. Se nenhuma conta tem PIN, a tela diz **Nenhuma conta tem PIN.** e **Defina um PIN pela web.**.

::: {.figura #fig-11a-quem tipo="tft" arquivo="11a-quem.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra quem_3_contas do grupo identidade, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A escolha da conta, em valores de exemplo: três contas.
:::

### O teclado do PIN {#cap-11a-teclado}

O teclado mostra o título **PIN de** (*PIN for*) e o nome da conta, como `PIN de maria`, uma linha de pontos, uma dica e as teclas, com **Cancelar** no canto, e **Apagar** e **Entrar** embaixo. O teclado depende da política de PIN ([O teclado](#cap-08-teclado)): cartões embaralhados, em que um toque no cartão vale por qualquer um dos caracteres dele, ou, com um caractere por tecla, um teclado ordenado, numérico ou em grupos. A dica diz o que tocar: **Toque nos dígitos do PIN.**, **Toque no grupo e depois no caractere.** ou `Toque no cartão com cada dígito.`. A linha de pontos tem no mínimo os lugares que a política exige e vai até o teto do teclado.

::: {.figura #fig-11a-teclado tipo="tft" arquivo="11a-teclado.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra pin_cartoes6_vazio do grupo identidade, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; teclado de cartões, seis cartões, nenhum dígito digitado"}
O teclado de cartões da conta maria, em valores de exemplo: seis cartões, nenhum dígito digitado, e **Apagar** e **Entrar** embaixo.
:::

**Entrar** (*Sign in*) envia o PIN, e enquanto o núcleo 0 o compara a tela diz **Conferindo…**. Um PIN errado diz **PIN incorreto. Tente de novo.**. Depois de várias tentativas, a tela diz `Muitas tentativas. Aguarde N s.`, e, esgotadas as tentativas, **Conta bloqueada.** ou `Painel bloqueado. Reinicie o aparelho para liberar.`. A escada de tentativas e esperas é a do núcleo 0 ([PIN errado e bloqueios](#cap-08-bloqueios)). Durante a espera ou o bloqueio, só **Cancelar** funciona.

### O PIN novo e a troca obrigatória {#cap-11a-pin-novo}

Para definir um PIN, o painel usa o teclado numérico ordenado, em duas fases: **Novo PIN** (*New PIN*), com a dica `Escolha de 6 a 8 dígitos.` e o botão **Seguir** (*Continue*), e **Confirme o novo PIN**, com **Digite o mesmo PIN de novo.** e o botão **Salvar PIN** (*Save PIN*). Um PIN curto diz `PIN curto: use 6 dígitos ou mais.`, e dois PINs diferentes dizem **Os PINs não batem. Comece de novo.**, e a tela pede para recomeçar. Para o PIN de outra conta, a dica leva o nome dela, como `joao.silva: digite o mesmo PIN.`.

::: {.figura #fig-11a-pin-novo tipo="tft" arquivo="11a-pin-novo.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra pin_definir_fase1 do grupo identidade, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; primeira fase da troca de PIN"}
A primeira fase de um PIN novo, em valores de exemplo: seis lugares, quatro deles preenchidos, o teclado numérico, com a tecla **5** apertada, e **Seguir**.
:::

Quem escolhe o próprio PIN usa **Alterar meu PIN**, no menu. Outro caminho é a troca obrigatória: o núcleo 0 leva a conta a esta tela, sem escolha, quando o PIN dela não cumpre mais a política (curto demais, ou com caracteres que o alfabeto novo não tem), quando é um PIN que a web definiu e é mais curto que a política, e no PIN de fábrica do administrador. Se a pessoa toca em **Cancelar** nessa tela, o painel volta à tela inicial, e a engrenagem pede o PIN de novo e leva a conta de volta à troca, para o menu não abrir com o PIN antigo. Na troca do próprio PIN pelo menu, **Cancelar** volta ao menu; na de outra conta, volta ao editor dela. O núcleo 0 grava o PIN: o de outra conta pede **Usuários** [PERM_USER_MGR]{.perm}, e o próprio pede só a sessão ([O próprio PIN](#cap-08-proprio-pin)).

### A segurança do PIN {#cap-11a-seguranca-pin}

A tela **Segurança do PIN** (*PIN security*) edita a política que vale para todas as contas, em três linhas: **Tamanho mínimo** (*Minimum length*), com − e +, e embaixo o intervalo, como `4 a 12 dígitos`; **Teclado** (*Keypad*): **Ordenado**, **6 cartões** ou **4 cartões** (com `0-9 e A-Z`, **18 cartões** e **12 cartões**); e **Caracteres** (*Characters*): `0-9` ou **0-9 e A-Z**. Os botões são **Cancelar** e **Salvar**. O painel ajusta os três valores para formar um par que ele sabe executar: o tamanho não passa do teto do teclado escolhido, e o teclado precisa ter um layout para o alfabeto.

::: {.figura #fig-11a-seguranca-pin tipo="tft" arquivo="11a-seguranca-pin.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra pin_6_cartoes do grupo ajustes, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A política de PIN, em valores de exemplo: tamanho mínimo 6, teclado de 6 cartões e caracteres 0-9.
:::

**Salvar** manda a política ao núcleo 0, que confere **Usuários** [PERM_USER_MGR]{.perm}, ajusta os valores, marca as contas cujo PIN deixou de cumprir a política, e grava. As contas marcadas trocam o PIN no próximo acesso. A linha **Segurança do PIN** do menu pede **Usuários**. A política está em [A política de PIN](#cap-08-politica).

### Usuários {#cap-11a-usuarios}

A tela **Usuários** (*Users*) lista as contas e tem, no alto, o botão **Nova conta** (*Add account*). Cada linha mostra o nome e as permissões de painel da conta: **Limites**, **Alarmes** e **Manut.**, ou **Só leitura** (*Read only*), e o selo **Sem PIN** (*No PIN*), na conta que não tem PIN. O selo **Bloqueada** não aparece aqui, só na [escolha da conta](#cap-11a-quem). Sem outras contas, a tela diz **Só o admin existe.** e **Toque em Nova conta para criar outra.**; sem a permissão, **Sua conta não pode criar contas.**.

::: {.figura #fig-11a-usuarios tipo="tft" arquivo="11a-usuarios.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra usuarios_3_contas do grupo identidade, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A lista de usuários, em valores de exemplo: três contas, com as permissões de painel de cada uma e o selo **Sem PIN** na terceira.
:::

- **Uma conta** abre o editor dela, com três chaves, uma por permissão de painel (**Editar limites**, **Bloquear alarmes** e **Manutenção**), e as linhas **Definir PIN** (*Set PIN*), que abre o teclado do PIN dela, e **Excluir a conta** (*Delete account*). Na conta do administrador as chaves ficam travadas. **Salvar** pede ao núcleo 0 que grave as permissões; o núcleo 0 confere de novo o que a conta pode conceder e recusa o administrador.
- **Nova conta** pede primeiro o nome, no teclado de texto (**Nome da conta**). O teclado confere o nome antes de seguir e recusa um nome vazio, com aspas ou barra invertida, o nome `admin` e um nome que já existe. Com o nome aceito e as permissões escolhidas, **Seguir** leva ao PIN da conta nova.
- **Excluir a conta** pergunta antes: a conta perde o acesso pelo painel e pela web, e o PIN e a senha dela são apagados. O núcleo 0 recusa o administrador, uma posição vazia e a conta que está em uso.

Tudo isso pede **Usuários** [PERM_USER_MGR]{.perm}, e o núcleo 0 confere de novo a cada pedido. O painel só mexe nas três permissões de painel; o resto fica na página web ([Usuários no painel](#cap-08-usuarios-painel)).

### A recusa {#cap-11a-recusa}

Quando o núcleo 0 recusa uma ação por falta de permissão, o painel mostra **Sem permissão.** (*No permission.*) com **Peça a quem administra o aparelho para liberar esta função na sua conta.** e o botão **Entendi**, e toca o som de erro. O menu já esconde as linhas que a conta não alcança; a recusa vem do núcleo 0, quando um pedido chega sem a permissão da sessão.

::: {.figura #fig-11a-sem-permissao tipo="tft" arquivo="11a-sem-permissao.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra mensagem_erro do grupo base, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; é o mesmo quadro de 11a-mensagem-erro: a recusa é essa mensagem"}
A recusa por falta de permissão. É o mesmo quadro da mensagem de erro, em [As mensagens do painel](#cap-11a-mensagens).
:::

## A tela de alarme {#cap-11a-alarme}

Um toque no cartão de um sensor em alarme, fora do silêncio, abre a tela de alarme desse sensor. Isso vale para qualquer slot de 0 a 15, mesmo os que não têm gráfico. A tela não tem botão de voltar: ela sai por um dos três botões ou, sem toque por 30 s, pela [volta à tela inicial](#cap-11a-ocioso). A barra de cima é a da tela inicial.

::: {.figura #fig-11a-alarme tipo="tft" arquivo="11a-alarme.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra acao_acima do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; alarme de valor acima do limite"}
A tela de alarme, em valores de exemplo: o sensor Vacinas acima do limite, 8,7 °C contra o máximo de 8,0 °C, e os três botões.
:::

A faixa vermelha diz o que aconteceu: o nome do sensor e a causa, **em alarme**, **acima do limite**, **abaixo do limite** ou **com erro no sensor**, e uma linha com os números, como `8,7 °C agora · máx 8,0 °C`, ou **Sem leitura. Confira o cabo.** ou **Um valor passou do limite.**. O painel decide a causa assim: erro, se o slot está em alarme de erro ou a leitura não vale; acima, se a temperatura passou do máximo; abaixo, se passou do mínimo. Se a temperatura está na faixa, o alarme é de outra grandeza e a linha diz **Um valor passou do limite.**. Sem a leitura do slot à mão, só o alarme de erro tem causa própria. O nome é o do sensor em que você tocou, seja qual for o do cartão de baixo.

| Botão | O que faz | Permissão |
|---|---|---|
| **Silenciar por** e os segundos, como `Silenciar por 120 s` (*Silence for*) | Silencia o painel inteiro por 120 s, não só o sensor desta tela, e volta à tela inicial | Nenhuma: qualquer pessoa no painel pode parar o som, e a linha de alarmes registra |
| **Ver mínimo e máximo** (*View minimum and maximum*) | Volta à tela inicial com um cartão em mínimo e máximo | Nenhuma |
| **Desativar os alarmes (pede o PIN)** (*Deactivate alarms (asks for PIN)*) | Abre o teclado do PIN; com o PIN, o núcleo 0 desativa os alarmes do sensor | **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm}, conferida depois do PIN |

**Ver mínimo e máximo** põe sempre o cartão de baixo em mínimo e máximo, mesmo quando a tela foi aberta pelo cartão de cima: é um defeito do painel original, mantido.

**O silêncio** dura 120 s e vale para o painel inteiro, não para um sensor só: é uma marca única, que todos os cartões leem. Enquanto dura, os cartões param de piscar e ficam com a borda vermelha, o toque neles volta ao normal, e a barra de cima mostra **Silenciado** e os segundos que faltam, como `Silenciado · 87 s`. O núcleo 0 para o som do alarme e registra o silêncio na linha de alarmes.

::: {.figura #fig-11a-silenciado tipo="tft" arquivo="11a-silenciado.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra painel_silenciado do grupo painel, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela inicial durante o silêncio, em valores de exemplo: o selo `Silenciado · 87 s` no lugar da hora, o cartão do sensor em alarme com borda vermelha e o ponto vermelho no botão S1.
:::

**A rotação.** Com dois ou mais sensores em alarme e sem silêncio, o cartão de baixo passa de um sensor em alarme para o seguinte a cada 3 s, e a barra de botões vai para a página dele. A rotação só olha os slots de 0 a 9 e não conta o sensor fixado no cartão de cima, que já está à vista: com dois sensores em alarme, um deles fixado, o cartão de baixo não gira. A tela de alarme em si não gira: ela mostra o sensor em que você tocou. Os alarmes estão no [capítulo 7](#cap-07-tela-alarme).

## Temas: a cor de destaque e o modo {#cap-11a-temas}

O painel tem um tema por modo e por cor de destaque. O modo é **Claro** (*Light*), **Escuro** (*Dark*) ou **Automático** (*Automatic*), e a cor de destaque é uma entre seis. A cor de destaque é a única cor que o tema escolhe: o resto das cores segue o modo. Cada cor de destaque tem uma tabela para o modo claro e outra para o escuro, e o gerador dos tokens do painel mede o contraste dos pares de cores de que ele depende. No modo **Automático**, o painel fica escuro das 19 h às 7 h ([Claro e escuro](#cap-11a-modo)).

| Cor de destaque | Em inglês |
|---|---|
| **verde-azulado**, a de fábrica | *teal* |
| **petróleo** | *petrol blue* |
| **azul** | *blue* |
| **índigo** | *indigo* |
| **violeta** | *violet* |
| **magenta** | *magenta* |

::: {.figura #fig-11a-temas tipo="tft" arquivo="11a-temas.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra temas_claro do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela Tema, em valores de exemplo: os três modos, com **Claro** escolhido, as seis cores de destaque, a primeira marcada, a linha `Cor de destaque: verde-azulado` e o botão **Aplicar tema** apagado, porque nada mudou.
:::

Embaixo das cores, a linha **Cor de destaque** (*Accent color*) diz o nome da escolhida. Um toque num modo ou numa cor só mostra a prévia, e só nesta tela: as outras telas seguem com o tema gravado. **Aplicar tema** (*Apply theme*) manda o par ao núcleo 0, que o grava na configuração, como um único índice de tema, e leva o painel à tela inicial; o botão fica apagado enquanto não há o que aplicar. **Voltar** joga a prévia fora e volta ao menu. A [figura da tela inicial no modo escuro](#fig-11a-principal-escuro) mostra o painel em **Escuro**.

Os temas de fábrica são 18, os 3 modos vezes as 6 cores. O de cor de fábrica leva só o nome do modo, como **Claro**; os outros levam o modo e a cor, como `Claro, petróleo` ou `Escuro, azul`. A página web mostra os mesmos 18 nomes, traduzidos pelo pacote de idioma.

Os arquivos `.thm` da pasta `/themes` não fazem nada neste painel: a busca de temas personalizados não encontra nada, e o carregamento de um tema ignora o arquivo. O painel original os lê ([Temas](#cap-11-temas)).

A linha **Tema** do menu pede **Sistema** [PERM_SYS_CONFIG]{.perm}, e o núcleo 0 confere a mesma permissão ao aplicar.

## Idioma {#cap-11a-idioma}

A tela **Idioma** (*Language*) lista **English** e, se houver, o pacote de idioma que está em `/lang`, com o nome dele, como **Português (Brasil)**. O nome **English** não é traduzido. A lista tem no máximo duas linhas. Uma marca fica no idioma gravado. Embaixo, a nota **Vale na próxima reinicialização.** (*Takes effect at the next restart.*): o painel lê os textos do pacote uma vez, na partida, então um idioma novo só aparece no boot seguinte.

::: {.figura #fig-11a-idioma tipo="tft" arquivo="11a-idioma.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra idioma_pacote do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A tela Idioma: **English** e **Português (Brasil)**, com a marca no segundo, e a nota **Vale na próxima reinicialização.**
:::

Um toque numa linha pede o idioma ao núcleo 0, que confere **Sistema** [PERM_SYS_CONFIG]{.perm} e a linha de novo. Há dois caminhos:

- **O pacote já está na memória** (o aparelho iniciou nesse idioma): o núcleo 0 aplica na hora, grava, toca o som de confirmação e leva o painel à tela inicial.
- **O pacote só está listado** (o aparelho iniciou em inglês e achou o pacote em `/lang`): o núcleo 0 grava o idioma e mostra a mensagem `Reboot required`, e o painel volta à tela inicial ao fechar a mensagem. Se a gravação falha, o idioma volta ao que era, e a mensagem é de erro.

A mensagem aparece em inglês porque é do núcleo 0 e só sai quando o pacote não está na memória, isto é, quando o aparelho está em inglês. O botão `Got it` vem em inglês pelo mesmo motivo: o painel lê o pacote só no boot.

::: {.figura #fig-11a-idioma-reiniciar tipo="tft" arquivo="11a-idioma-reiniciar.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra mensagem_reiniciar do grupo base, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; a mensagem do núcleo 0 depois de escolher um pacote que só será carregado na próxima partida: o aparelho iniciou em inglês, por isso a mensagem e o botão vêm em inglês"}
A mensagem do núcleo 0 depois de escolher um pacote que só será carregado na próxima partida: o aparelho iniciou em inglês, e por isso a mensagem e o botão vêm em inglês.
:::

Um texto que o pacote não traz aparece em inglês. O idioma gravado fica na configuração do aparelho. Os pacotes de idioma estão no [capítulo 3](#cap-03-idioma).

## Licença {#cap-11a-licenca}

A tela **Licença MIT** (*MIT license*) mostra o texto legal em inglês, sob um título em português, e a nota **O texto legal fica no original (inglês).**. Cada página tem 6 linhas, e há uma barra de rolagem à direita e as setas de página embaixo. O texto vem em inglês, na ordem: a abertura (`Free software. Below, what that means in practice.`), o texto da licença MIT, a lista dos componentes de terceiros e, no fim, o bloco `--- Fontes e ícones deste painel ---`, que o painel escreve no idioma do aparelho.

::: {.figura #fig-11a-licenca tipo="tft" arquivo="11a-licenca.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra licenca_p1 do grupo menus, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; página 1"}
A primeira página da licença, em valores de exemplo: o texto em inglês, a barra de rolagem, a nota e o paginador `1 de 19`.
:::

O bloco do fim lista o que é só deste painel:

| Componente | Onde o painel o usa | Licença |
|---|---|---|
| Bricolage Grotesque | Nos títulos | SIL Open Font License 1.1 |
| GNU FreeFont (FreeSans, FreeMono Bold) | No texto e nos números | GPL-3.0-or-later, com a exceção de fontes |
| Lucide | Nos ícones | ISC |

As palavras em volta dos nomes seguem o idioma do aparelho; os nomes, os titulares e as licenças não se traduzem. A página `/license` da interface web ([A página Licença](#cap-13-licenca)) traz o mesmo bloco, que só as imagens com o painel Ângulo têm. A linha **Licença** do menu não pede permissão, a tela não envia pedidos, e nada é gravado.

## As mensagens do painel {#cap-11a-mensagens}

Depois de uma ação, o núcleo 0 responde com uma mensagem: um ícone, o texto e o botão **Entendi** (*Got it*). O ícone é um visto verde quando deu certo e um X vermelho quando a ação foi recusada, e a recusa toca o som de erro. Um toque em **Entendi** volta à tela de onde a ação veio, ou à tela inicial. A mensagem não tem prazo próprio: fica até o toque ou até a [volta à tela inicial](#cap-11a-ocioso), depois de 30 s sem toque.

::: {.figura #fig-11a-mensagem-ok tipo="tft" arquivo="11a-mensagem-ok.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra mensagem_ok do grupo base, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
Uma mensagem de sucesso: **Alteração salva.**, com o botão **Entendi**.
:::

::: {.figura #fig-11a-mensagem-erro tipo="tft" arquivo="11a-mensagem-erro.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra mensagem_erro do grupo base, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; é a mensagem de recusa, Sem permissão."}
Uma mensagem de recusa: **Sem permissão.** e a linha que diz o que fazer. É o mesmo quadro de [A recusa](#fig-11a-sem-permissao).
:::

As mensagens que o painel sabe dizer, cada uma com a linha que diz o que fazer, quando há:

| Mensagem | A linha de ajuda |
|---|---|
| **Conta salva.** (*Account saved.*) | Não tem |
| **Conta excluída.** (*Account deleted.*) | Não tem |
| **Alteração salva.** (*Change saved.*) | Não tem. Vale para um PIN salvo e para a política de PIN salva |
| **Sem permissão.** (*No permission.*) | Peça a quem administra o aparelho para liberar esta função na sua conta. |
| **Não foi possível salvar a conta.** | O nome pode estar em uso ou ser inválido. Uma conta também não pode excluir a si mesma. |
| **Não há vaga para outra conta.** | Diz quantas contas o aparelho guarda e manda excluir uma para criar outra. |
| **Este PIN já é de outra conta.** | Escolha outro PIN. |
| **PIN inválido.** | Confira o tamanho mínimo e os caracteres em Segurança do PIN. |
| **Não deu certo.** | Confira os valores e tente de novo. |

O painel também diz `Esta tela (modo N) ainda não existe nesta build.`, com **Volte ao painel e use a web para esta função.** e o botão **Voltar ao painel**, quando o núcleo 0 liga um modo que nenhum desenho cobre. Isso hoje só acontece com a pergunta do ponto de acesso numa imagem com cabo, que o menu dessa imagem nem abre. A mensagem `Reboot required` é do núcleo 0 e está em [Idioma](#cap-11a-idioma).

## A atualização e o boot {#cap-11a-atualizacao}

### A tela de atualização {#cap-11a-ota}

Quando uma atualização do firmware começa, o núcleo 0 toma o painel, de qualquer tela, e mostra **Atualização do firmware** (*Firmware update*). O Ângulo desenha as mesmas seis etapas do painel original ([A tela de atualização](#cap-11-atualizacao)), com as mesmas duas esperas no fim e os mesmos sete motivos de recusa:

| Etapa | O que a tela diz | Quanto fica |
|---|---|---|
| Recebendo | **Recebendo a nova versão** (*Receiving the new version*), a barra com a porcentagem e **Não desligue o aparelho.** | Até a etapa seguinte |
| Conferindo | **Conferindo a assinatura** (*Checking the signature*) | Até a etapa seguinte |
| Conferida | **Imagem conferida** (*Image verified*) | Até a etapa seguinte |
| Instalando | **Instalando a nova versão** (*Installing the new version*), **O aparelho reinicia sozinho em cerca de 30 s.** e **Não desligue o aparelho.** | Até o reinício |
| Recusada | **Atualização recusada** (*Update refused*), o motivo, o que fazer, **Continua na versão atual.** e **Toque na tela para voltar.** | 12 s, ou até um toque |
| Interrompida | **Atualização interrompida** (*Update interrupted*), **O envio parou antes do fim.**, **Envie o arquivo de novo.**, **Continua na versão atual.** e **Toque na tela para voltar.** | 8 s, ou até um toque |

Os tempos de cada etapa são do núcleo 0 e estão em [A tela de atualização](#cap-11-atualizacao). Enquanto a atualização recebe, confere, está pronta ou instala, nada leva o painel de volta à tela inicial, nem a volta por inatividade. Só uma atualização recusada ou interrompida sai por um toque. A tela não envia pedidos e não pede permissão.

::: {.figura #fig-11a-ota-recebendo tipo="tft" arquivo="11a-ota-recebendo.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra ota_recebendo_42 do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; barra em 42 %"}
A etapa Recebendo, em valores de exemplo: a barra em 42 %.
:::

::: {.figura #fig-11a-ota-instalando tipo="tft" arquivo="11a-ota-instalando.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra ota_instalando do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
A etapa Instalando, com o aviso para não desligar o aparelho.
:::

Os sete motivos de recusa, com o que fazer em cada um:

| O que a tela diz | O que fazer |
|---|---|
| **A imagem é de outro modelo.** | Use o arquivo feito para este aparelho. |
| **A imagem não é assinada.** | Use um arquivo de release ou do configurador. |
| **A assinatura não confere.** | Baixe o arquivo de novo. |
| **Assinada com uma chave aposentada.** | Use uma release mais nova. |
| **Abaixo do nível de segurança atual.** | Use a versão atual ou uma mais nova. |
| **Assinada com a chave de bancada.** | Use um arquivo de release. |
| **Não é uma imagem de firmware válida.** | Envie o arquivo de novo. |

::: {.figura #fig-11a-ota-recusada tipo="tft" arquivo="11a-ota-recusada.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra ota_recusada_sem_assinatura do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write; recusa de uma imagem sem assinatura"}
Uma atualização recusada, pelo motivo de uma imagem sem assinatura.
:::

::: {.figura #fig-11a-ota-interrompida tipo="tft" arquivo="11a-ota-interrompida.png" captura="desenhada pelo código do painel (tools/tft_ui), amostra ota_interrompida do grupo graficos, modo claro, pacote pt-BR, ampliada 2x; refazer com python3 tools/tft_ui/manual_frames.py --write"}
Uma atualização interrompida: o envio parou antes do fim.
:::

### As telas de boot {#cap-11a-boot}

O Ângulo desenha o boot numa tela só: a marca `SIMUT`, **Monitor de temperatura e umidade** (*Temperature and humidity monitor*), as últimas linhas do registro de boot, com a etapa atual em letra maior, e, embaixo, a versão e o endereço MAC. As linhas, a ordem, o tempo de cada etapa e o gesto do ponto de acesso são do núcleo 0 e iguais nos dois painéis ([As telas de boot](#cap-11-boot)); o Ângulo só os desenha.

Enquanto o aparelho sobe, só um botão responde ao toque: **Pular a espera** (*Skip the wait*), que aparece quando o núcleo 0 oferece e faz o boot seguir. A volta por inatividade não vale no boot.

O gesto do ponto de acesso é do núcleo 0: ele abre uma janela de 3,5 s e pede que você segure a tela por 3 s. Durante a janela, o Ângulo troca as linhas do boot por **Segure para o modo de configuração** (*Hold to enter configuration mode*) e uma barra que enche com o tempo segurado.

### O que a captura e o espelho mostram {#cap-11a-captura}

`GET /api/screenshot` devolve um BMP de 320 × 240 pixels e 24 bits do que está no vidro: o aparelho lê a memória da própria tela, por faixas de linhas, com o desenho pausado, e monta o arquivo. Como lê o vidro, mostra o painel Ângulo do mesmo modo que mostraria o original, sem saber qual dos dois desenhou. O pedido exige **Sistema** [PERM_SYS_CONFIG]{.perm} e responde 503, pedindo para tentar de novo, quando o painel está em uso. No Ângulo, a leitura só roda com o núcleo 1, que é o que desenha, parado, para não cruzar com o envio de uma faixa. O espelho do painel na web ([O Painel de Controle](#cap-13-painel-de-controle)) faz a mesma leitura e vale para o Ângulo.

## A volta à tela inicial {#cap-11a-ocioso}

Depois de 30 s sem nenhum toque, o painel volta à tela inicial, a partir de qualquer tela, menos três: a própria tela inicial, a de atualização do firmware e o boot. A volta só troca a tela; nada é gravado. Os cartões em mínimo e máximo também voltam ao valor atual depois de 30 s sem toque.

**O que uma escolha ainda não aplicada perde.** Ela se perde. O painel só grava quando você toca no botão que salva ou aplica, e até lá o que está na tela é uma cópia: **Salvar limites**, **Salvar sons**, **Salvar hora**, **Salvar** na política de PIN e no alinhamento, **Aplicar tema**, **Iniciar manutenção** e a escolha de uma linha em **Idioma**. A prévia do tema só é desenhada na tela Tema, e a tela inicial usa sempre o tema gravado. O alinhamento é o caso a ter em mente: a prévia move a imagem na hora, antes de salvar, e **Voltar** devolve o alinhamento gravado. A volta por inatividade não devolve: ela só troca a tela, e a imagem fica deslocada, sem ter sido salva. Se você entra de novo no alinhamento e toca em **Voltar**, o deslocamento fica; uma reinicialização aplica de novo o alinhamento gravado.

## Referência das telas {#cap-11a-referencia}

O painel Ângulo tem 31 modos de tela, os mesmos do painel original. Desenha todos, menos `MODE_CONFIRM_AP` na imagem com cabo: sem rádio, essa imagem não tem a linha do menu nem a pergunta. Dois deles têm mais de uma tela: `MODE_DASHBOARD`, que também desenha o boot, e `MODE_SETTINGS_SOUNDS`, que também desenha a escolha da melodia. A tabela traz o modo interno, o título em português, como chegar, a etiqueta do comando `screen` do console ([O painel pelo console](#cap-14-painel)) e a seção.

| Modo | Título na tela | Como chegar | Etiqueta | Seção |
|---|---|---|---|---|
| `MODE_DASHBOARD` | Sem título: barra de cima | Ao fim do boot; **Voltar** do menu; 30 s sem toque | `dash` | [A tela inicial](#cap-11a-principal) |
| `MODE_GRAPH_LOADING` | O sensor e a janela, e **Lendo o histórico…** | Ao abrir um gráfico ou um dia do calendário | — | [O gráfico](#cap-11a-grafico) |
| `MODE_GRAPH_VIEW` | O sensor e a janela | Botão **Gráfico** de um cartão em mínimo e máximo | `gra` | [A tela do gráfico](#cap-11a-grafico-tela) |
| `MODE_GRAPH_DETAIL` | **Detalhe** e a janela | Toque na área do gráfico | — | [O detalhe](#cap-11a-detalhe) |
| `MODE_AUTH` | **PIN de** e a conta, **Novo PIN** ou **Confirme o novo PIN** | Escolha da conta; **Alterar meu PIN**; **Definir PIN** | — | [O teclado do PIN](#cap-11a-teclado) |
| `MODE_SETTINGS_MAIN` | **Configurações** | Engrenagem, a conta e o PIN | `set` | [O menu de configurações](#cap-11a-menu) |
| `MODE_SETTINGS_THEMES` | **Tema** | Menu, **Tema** | `thm` | [Temas](#cap-11a-temas) |
| `MODE_SETTINGS_ALARMS` | **Limites de alarme** | Menu, **Limites de alarme** | `alm` | [Limites de alarme](#cap-11a-alarmes) |
| `MODE_SETTINGS_ALARM_EDIT` | O nome do sensor | Menu do sensor, **Limites** | — | [O editor de limites](#cap-11a-limites) |
| `MODE_SETTINGS_LANG` | **Idioma** | Menu, **Idioma** | `lng` | [Idioma](#cap-11a-idioma) |
| `MODE_SETTINGS_PASSWORD` | **Nome da conta** e, no PIN antigo, **Nova senha** e **Confirmar senha** | **Usuários**, **Nova conta**; `screen pwd` abre o PIN antigo | `pwd` | [Usuários](#cap-11a-usuarios) |
| `MODE_SETTINGS_TOUCH_CAL` | Sem título: a mira | Depois da sensibilidade | — | [A calibração do toque](#cap-11a-calibrar) |
| `MODE_SETTINGS_TOUCH_SENS` | **Sensibilidade do toque** | Menu, **Calibrar o toque** | `touchsens`, `touchcal` | [A calibração do toque](#cap-11a-calibrar) |
| `MODE_SETTINGS_SOUNDS` | **Sons**, ou **Melodia** e o som | Menu, **Sons** | — | [Sons](#cap-11a-sons) |
| `MODE_SETTINGS_LICENSE` | **Licença MIT** | Menu, **Licença** | `lic` | [Licença](#cap-11a-licenca) |
| `MODE_SETTINGS_STATUS` | **Status** e a página | Menu, **Status do sistema** | `sts` | [Status do sistema](#cap-11a-status) |
| `MODE_SETTINGS_DISPLAY_OFFSET` | **Alinhar a tela** | Menu, **Alinhar a tela** | `offset` | [Alinhar a tela](#cap-11a-alinhar) |
| `MODE_ALARM_ACTION` | O sensor e a causa | Toque num cartão em alarme | — | [A tela de alarme](#cap-11a-alarme) |
| `MODE_CALENDAR` | O mês e o ano | **Dia**, no gráfico | — | [O calendário](#cap-11a-calendario) |
| `MODE_CONFIRM_MUTE_ALL` | **Silenciar todos os sons?** | **Sons**, ligando **Silenciar tudo** | — | [Sons](#cap-11a-sons) |
| `MODE_SETTINGS_ALARM_SENSOR` | O nome do sensor | Lista de **Limites de alarme**, um sensor | — | [Limites de alarme](#cap-11a-alarmes) |
| `MODE_SETTINGS_MAINT` | **Manutenção** e o sensor | Menu do sensor, **Manutenção** | — | [A manutenção](#cap-11a-manutencao) |
| `MODE_SETTINGS_USERS` | **Usuários** | Menu, **Usuários** | `usr` | [Usuários](#cap-11a-usuarios) |
| `MODE_SETTINGS_USER_EDIT` | O nome da conta, ou **Nova conta** | **Usuários**, uma conta | — | [Usuários](#cap-11a-usuarios) |
| `MODE_SETTINGS_USER_CONFIRM_DEL` | **Excluir a conta** | Editor da conta, **Excluir a conta** | — | [Usuários](#cap-11a-usuarios) |
| `MODE_PANEL_MESSAGE` | Sem título: ícone e mensagem | O resultado de uma ação | — | [As mensagens do painel](#cap-11a-mensagens) |
| `MODE_AUTH_USER` | **Quem está no painel?** | Engrenagem | `pin` | [A escolha da conta](#cap-11a-quem) |
| `MODE_SETTINGS_PIN_POLICY` | **Segurança do PIN** | Menu, **Segurança do PIN** | — | [A segurança do PIN](#cap-11a-seguranca-pin) |
| `MODE_CONFIRM_AP` | **Abrir a rede de configuração?** | Menu, **Modo de configuração** | — | [A rede de configuração](#cap-11a-ap) |
| `MODE_SET_CLOCK` | **Data e hora** | Menu, **Data e hora**; no boot, quando o núcleo 0 pede a hora | `clk` | [Data e hora](#cap-11a-data-hora) |
| `MODE_OTA_UPDATE` | **Atualização do firmware** | Uma atualização do firmware | `otarx`, `otachk`, `otardy`, `otainst`, `otaref`, `otacut` | [A tela de atualização](#cap-11a-ota) |

As etiquetas `gra` e `pin` só existem nas imagens que têm o gráfico e o PIN no painel. As da atualização mostram a etapa sem atualizar nada.

**O comando `screen`.** `screen <etiqueta>`, no console, abre a tela direto, sem passar pelo toque, e a coluna **Etiqueta** diz qual etiqueta abre qual modo. O comando só existe no console completo, e nenhuma imagem com o painel Ângulo o tem: a `pico2_w_release` e a de bancada `pico_wiretft_angulo` levam o console de emergência. Das imagens com painel, só as de teste, `pico_w_test` e `pico_w_test_https`, têm o comando, e o painel delas é o original ([O painel pelo console](#cap-14-painel)). Fora `dash`, `gra` e `pin`, o comando entra como administrador, sem sessão de verdade, para o menu aparecer inteiro, e zera a contagem dos 30 s.

**O espelho do painel na web** ([O Painel de Controle](#cap-13-painel-de-controle)) e a captura valem para o Ângulo: leem o vidro, não o desenho ([O que a captura e o espelho mostram](#cap-11a-captura)).
