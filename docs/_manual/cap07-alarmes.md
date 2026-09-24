# Alarmes, sons e janelas de manutenção {#cap-07}

Este capítulo mostra quando o aparelho dispara um alarme, o que acontece no painel, na cigarra e na interface web, e como silenciar, desligar e suspender alarmes com uma janela de manutenção. É para quem opera o aparelho no dia a dia e para quem define os limites; o envio dos alarmes a um servidor está no [capítulo 22](#cap-22).

## Os dois tipos de alarme {#cap-07-tipos}

O aparelho tem dois alarmes por sensor, independentes um do outro:

| Alarme | Quando dispara | Pode ser desligado |
|---|---|---|
| **Limite** | Uma grandeza do sensor sai da faixa entre o mínimo e o máximo configurados | Sim, por sensor ([Ligar e desligar](#cap-07-ligar)) |
| **Falha** | O sensor entra em estado de erro: 3 leituras seguidas falharam, ou outra sonda apareceu no pino de um DS18B20 ([capítulo 6](#cap-06-validacao)) | Não. Pode ser suspenso por uma janela de manutenção ou calado até o sensor voltar |

Nenhum dos dois dispara num slot inativo, nem num slot com a janela de manutenção aberta ([Janelas de manutenção](#cap-07-manutencao)).

[air]{.img} O Air não tem painel nem cigarra. Nele, os alarmes só saem pela linha de alarmes ([capítulo 22](#cap-22)).

## Limites por grandeza {#cap-07-limites}

Cada grandeza de cada sensor tem um mínimo e um máximo. O alarme de limite dispara quando qualquer grandeza do sensor fica abaixo do mínimo ou acima do máximo. Um BME280, por exemplo, tem três pares de limites: temperatura, umidade e pressão.

O valor comparado é o valor corrigido que o painel mostra, depois da média e da calibração ([capítulo 6](#cap-06-intervalos)).

Limites de fábrica, em todo slot novo:

| Grandeza | Mínimo | Máximo |
|---|---|---|
| Temperatura | 0 °C | 40 °C |
| Umidade | 20 % | 80 % |
| Pressão | 0 hPa | 1.638,3 hPa |
| Luminosidade | 0 lx | 167.772,15 lx |

As regras dos limites:

- **Mínimo abaixo do máximo.** Se o mínimo ficar igual ou maior que o máximo, o aparelho sobe o máximo para o mínimo mais 0,1.
- **Dentro da faixa da grandeza.** Cada grandeza aceita limites só dentro da sua faixa plausível ([capítulo 6](#cap-06-grandezas)). Pela web, um limite fora dela faz o aparelho recusar a gravação inteira, com `Alarm limit outside channel range`.
- **Uma casa decimal.** A página e o painel ajustam os limites em passos de 0,1.

Os limites podem ser mudados na página **Alarmes e Sons** ([A página Alarmes e Sons](#cap-07-pagina)) e no painel ([Limites no painel](#cap-07-editor-limites)).

## Ligar e desligar os alarmes de um sensor {#cap-07-ligar}

Desligar os alarmes de um sensor desliga só o alarme de limite dele. O alarme de falha continua ativo: uma falha de comunicação não é um limite que se possa desligar.

Há quatro lugares para isso:

| Onde | Como | Grava |
|---|---|---|
| Página **Alarmes e Sons** | A chave no topo do cartão do sensor | Com os botões de gravação, sem reiniciar |
| Editor de slot, em **Configurações** | **Alarmes habilitados** | Só com **Salvar e reiniciar** |
| Painel, menu do sensor | A linha **Alarmes** | Na hora |
| Painel, tela de alarme | **Desativar**, com PIN | Na hora |

De fábrica, os alarmes de todo slot vêm ligados. Religar os alarmes de um sensor, pela web ou pelo painel, também desfaz o silêncio do alarme de falha dele ([Desativar](#cap-07-desativar)).

## O que acontece quando um alarme dispara {#cap-07-disparo}

Quando um alarme dispara, o aparelho:

- toca a cigarra, com a melodia e o volume de alarme ([Sons](#cap-07-sons));
- no painel, pisca o cartão do sensor: em vermelho para limite, em âmbar para falha;
- no painel, leva o sensor em alarme para o cartão de baixo, a menos que ele já esteja fixado no cartão de cima;
- registra no log de eventos o código 470, **Alarme disparado**;
- se a linha de alarmes estiver ligada, envia o registro ao servidor ([capítulo 22](#cap-22)).

Quando nenhum sensor está mais em alarme, a cigarra para, os cartões param de piscar e o log registra o código 471, **Alarme zerado**.

::: {.figura #fig-07-painel-limite tipo="tft" arquivo="07-painel-limite.png" captura="screen dash; 2 sensores ativos; o sensor do cartão de baixo acima do máximo de temperatura, alarme não silenciado; captura na fase vermelha da piscada"}
Um alarme de limite no painel: o cartão do sensor pisca em vermelho.
:::

::: {.figura #fig-07-painel-falha tipo="tft" arquivo="07-painel-falha.png" captura="screen dash; 2 sensores ativos; o DS18B20 do cartão de baixo desligado do pino há mais de 3 leituras; captura na fase âmbar da piscada"}
Um alarme de falha no painel: o cartão pisca em âmbar e mostra Erro no lugar do valor.
:::

### No painel {#cap-07-tela-alarme}

[release]{.img} Toque no cartão que pisca para abrir a tela de alarme daquele sensor. O título mostra `!` seguido do nome do sensor, e três botões:

| Botão | O que faz | Exige |
|---|---|---|
| **Silenciar 120s** (*Silence 120s*) | Cala a cigarra e para a piscada por 120 s ([Silenciar](#cap-07-silenciar)) | Nada |
| **Desativar** (*Deactivate*) | Desliga o alarme daquele sensor ([Desativar](#cap-07-desativar)) | PIN de uma conta com a permissão **Bloqueio (painel)** |
| **Min/Max** | Volta à tela inicial com o cartão do sensor mostrando o mínimo e o máximo do dia | Nada |

::: {.figura #fig-07-tela-alarme tipo="tft" arquivo="07-tela-alarme.png" captura="tela MODE_ALARM_ACTION; a partir da screen dash com um sensor chamado Freezer acima do limite, toque no cartão que pisca; a ferramenta de mapa de telas não alcança esta tela sozinha"}
A tela de alarme de um sensor, com Silenciar 120s, Desativar e Min/Max.
:::

### No LCD {#cap-07-lcd}

[alpha]{.img} A imagem alpha tem cigarra, mas o LCD não mostra alarmes e não há como silenciar no aparelho. A cigarra toca até o valor voltar à faixa, até alguém desligar os alarmes do sensor pela web, ou até alguém ligar o **Mudo Global** ([Sons pela web](#cap-07-sons-web)). O LCD mostra `ERRO` para um sensor em falha ([capítulo 6](#cap-06-erro-aparece)).

### Na interface web {#cap-07-web}

A interface web não mostra alarmes de limite. No **Painel de Controle**, um sensor em falha aparece com o ponto vermelho e `Error` ([capítulo 13](#cap-13-tabela-sensores)); um sensor fora dos limites aparece com a leitura normal.

[release]{.img} O espelho do painel, no **Painel de Controle**, mostra o painel como ele está, inclusive o cartão piscando ([capítulo 13](#cap-13-espelho)).

Para acompanhar alarmes de longe, use a linha de alarmes ([capítulo 22](#cap-22)).

## Silenciar {#cap-07-silenciar}

**Silenciar 120s** cala o alarme por 120 s. Durante esse tempo:

- a cigarra para;
- os cartões param de piscar;
- a barra de cima do painel mostra **Silenciado:** seguido dos segundos que faltam, como `Silenciado: 97s`;
- nenhum alarme toca, nem um alarme novo de outro sensor.

O silêncio vale para todos os sensores, mesmo tocado a partir da tela de um só. Ele não exige PIN. O log registra o código 447, **Alarme silenciado via UI**, e a linha de alarmes recebe um registro de silêncio do sensor da tela (`alarm_sil` ou `err_sil`).

Quando os 120 s acabam, o log registra o código 448, **Silenciamento de alarme expirou**. Se algum sensor ainda estiver em alarme, a cigarra volta a tocar e o cartão volta a piscar. Se todos os alarmes acabarem antes, o silêncio é cancelado (código 472, **Silenciamento cancelado**).

::: {.figura #fig-07-silenciado tipo="tft" arquivo="07-silenciado.png" captura="screen dash; um sensor acima do limite; logo depois de tocar em Silenciar 120s; a barra de cima mostra Silenciado com a contagem"}
O painel durante o silêncio: a contagem na barra de cima e o cartão parado.
:::

## Desativar {#cap-07-desativar}

**Desativar** desliga o alarme do sensor da tela, e só dele. O efeito depende do alarme que está ativo:

- **Limite:** desliga os alarmes do sensor, como a linha **Alarmes** do menu do sensor. A mudança é gravada e continua valendo depois de um reinício. O log registra o código 456, **Alarmes do sensor bloqueados pelo painel**.
- **Falha:** cala só a falha daquele sensor, e o limite continua armado. O silêncio da falha dura até o sensor voltar a ler; se ele falhar de novo, o alarme volta. Religar os alarmes do sensor também o desfaz, e um reinício também. O log registra o código 449, **Todos alarmes desativados (RAM)**.

Para desativar:

1. Toque no cartão que pisca.
2. Toque em **Desativar**.
3. Escolha a conta na lista **Quem está usando o painel?** ([capítulo 8](#cap-08-identidade)).
4. Digite o PIN.

Se a conta não tiver a permissão **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm}, o painel mostra **Sem permissão** e toca o som de erro; o log registra o código 458, **Ação do painel recusada: sem permissão**. Com a permissão, a cigarra para, o painel volta à tela inicial e a linha de alarmes recebe um registro `alarm_off` ou `err_off` com o nome da conta.

## A página Alarmes e Sons {#cap-07-pagina}

A página **Alarmes e Sons** (*Alarms & Sounds*, rota `/alarms`) exige a permissão **Sistema** [PERM_SYS_CONFIG]{.perm}. Ela tem duas seções: **Limites de Alarme** e **Configuração de Sons**.

A página não tem botão próprio de gravar. As mudanças ficam preparadas na aba e são gravadas pelos botões da barra de topo ([capítulo 5](#cap-05)):

- limites e chaves de alarme valem sem reiniciar: **Testar** e **Aplicar agora** funcionam para eles;
- mudanças de som exigem reinício, e a barra oferece só **Salvar e reiniciar**.

::: {.figura #fig-07-pagina-limites tipo="web" arquivo="07-pagina-limites.png" captura="rota /alarms; largura 1280; sessão admin; 3 sensores ativos (DS18B20, DHT22 e BME280); recorte da seção Limites de Alarme"}
A seção Limites de Alarme: um cartão por sensor ativo, com a chave de alarme e um par de campos por grandeza.
:::

### Limites de Alarme {#cap-07-pagina-limites}

A seção mostra um cartão por sensor ativo. Sem sensores ativos, mostra **Nenhum sensor configurado.** (*No sensors configured.*); os sensores são criados na página **Configurações** ([capítulo 6](#cap-06-adicionar)).

Cada cartão tem:

| Elemento | O que é |
|---|---|
| Chave, à esquerda do nome | Liga ou desliga os alarmes de limite do sensor |
| Nome | O nome do slot |
| Tipo e slot | Como `BME280 · slot 4` |
| **Temp MIN** e **Temp MAX** | Os limites de temperatura, em °C |
| **Umid MIN** e **Umid MAX** | Os limites de umidade, em %, só em sensores com umidade |
| **Press MIN** e **Press MAX** | Os limites de pressão, em hPa, só em sensores com pressão |

Ao digitar um mínimo igual ou maior que o máximo, a página sobe o máximo para o mínimo mais 0,1; ao digitar um máximo igual ou menor que o mínimo, desce o mínimo.

Para mudar um limite:

1. Abra **Alarmes e Sons**.
2. Digite o novo valor no campo.
3. Toque em **Aplicar agora** na barra de topo.

O aparelho passa a usar o limite novo na verificação seguinte, sem reiniciar.

::: nota
**Limites mudados pela web não vão para a linha de alarmes.** Só as mudanças de limite feitas no painel geram o registro `alarm_lim`, com o nome da conta ([capítulo 22](#cap-22)). Pela web, a mudança fica no log de eventos como alteração de configuração.
:::

### Configuração de Sons {#cap-07-sons-web}

::: {.figura #fig-07-pagina-sons tipo="web" arquivo="07-pagina-sons.png" captura="rota /alarms; largura 1280; sessão admin; valores de fábrica; recorte da seção Configuração de Sons"}
A seção Configuração de Sons com os valores de fábrica: todos os sons ligados, volumes em 70 %.
:::

| Linha | Controles | Fábrica |
|---|---|---|
| **Toque** (*Touch*) | Melodia, botão de teste, chave | Ligado, `Click` |
| **Confirmação** (*Confirm*) | Melodia, botão de teste, chave | Ligado, **Ascendente** |
| **Erro** (*Error*) | Melodia, botão de teste, chave | Ligado, **Descendente** |
| **Alarme** (*Alarm*) | Melodia, botão de teste, chave | Ligado, `Dual Beep` |
| **Sons Web** (*Web Sounds*) | Chave | Ligado |
| **Atenção** (*Attention*) | Melodia, botão de teste, chave | Ligado, `Notify` |
| **Mudo Global** (*Global Mute*) | Chave | Desligado |
| **Vol. Sistema** (*System Volume*) | Controle deslizante, de 0 a 100 %, em passos de 5 | 70 % |
| **Vol. Alarme** (*Alarm Volume*) | Controle deslizante, de 0 a 100 %, em passos de 5 | 70 % |

O botão de teste toca a melodia escolhida no navegador, não na cigarra do aparelho, no volume do controle correspondente. Os detalhes de cada som estão em [Sons](#cap-07-sons).

Ligar **Mudo Global** desliga as outras seis chaves; ligar qualquer uma delas desliga **Mudo Global**.

::: atencao
**Uma mudança de som preparada já vale no aparelho.** Na v2.7.1, a consulta que a página faz ao aparelho para decidir se a mudança exige reinício aplica os sons na memória do aparelho antes da gravação. Ligar **Mudo Global** na página, por exemplo, pode calar a cigarra antes de você gravar, e continua assim até um reinício se você desistir. Grave ou descarte as mudanças de som logo em seguida.
:::

## O menu de alarmes do painel {#cap-07-painel}

[release]{.img} No painel, os alarmes ficam no item **Limites de Alarme** do menu de configurações. O item aparece para contas com pelo menos uma das três permissões de painel:

| Permissão | Nome na página Usuários | Libera |
|---|---|---|
| [PERM_ALARM_LIMITS]{.perm} | **Limites (painel)** | A linha **Limites de alarme** do menu do sensor |
| [PERM_ALARM_BLOCK]{.perm} | **Bloqueio (painel)** | A linha **Alarmes** e o botão **Desativar** da tela de alarme |
| [PERM_MAINT]{.perm} | **Manut. (painel)** | A linha **Manutenção** |

Para abrir o menu, toque no botão de configurações da tela inicial, escolha a conta e digite o PIN ([capítulo 8](#cap-08-identidade)). O que cada conta pode fazer é decidido pelas permissões da conta que o PIN identificou.

### A lista de sensores {#cap-07-lista}

A tela **Limites de Alarme** lista os sensores ativos, quatro por página, cada um com o nome e o estado dos alarmes: **SIM** (ligados) ou **NÃO** (desligados). As setas do rodapé mudam a seleção e **SAIR** volta ao menu.

Toque num sensor para selecioná-lo; toque de novo para abrir o menu dele.

::: {.figura #fig-07-lista-sensores tipo="tft" arquivo="07-lista-sensores.png" captura="screen alm; 3 sensores ativos, um deles com alarmes desligados"}
A lista de sensores do menu de alarmes, com o estado SIM ou NÃO de cada um.
:::

### O menu do sensor {#cap-07-menu-sensor}

O título é o nome do sensor. O menu tem três linhas:

| Linha | À direita | O que faz |
|---|---|---|
| **Limites de alarme** | Uma seta | Abre o editor de limites |
| **Alarmes** | **SIM** ou **NÃO** | Liga ou desliga os alarmes de limite do sensor, na hora |
| **Manutenção** | **NÃO**, ou o tempo que falta, como `1h45` | Abre a janela de manutenção |

Uma linha cuja permissão a conta não tem aparece apagada, com um cadeado. Tocar nela só toca o som de erro. O menu abre na primeira linha que a conta pode usar: uma conta só com **Manut. (painel)** abre em **Manutenção**.

O rodapé tem as setas, **SAIR**, que volta à lista, e **ENTRAR**, que abre a linha selecionada.

Tocar em **Alarmes** grava na hora. O log registra o código 456 ao desligar e 457 ao religar, com o nome da conta, e a linha de alarmes recebe `alarm_off` ou `alarm_on`.

::: {.figura #fig-07-menu-sensor tipo="tft" arquivo="07-menu-sensor.png" captura="screen alm -> tap(160,57) -> tap(160,57); sessão admin; sensor com alarmes ligados e sem manutenção"}
O menu de um sensor para uma conta com as três permissões de painel.
:::

::: {.figura #fig-07-menu-sensor-manut tipo="tft" arquivo="07-menu-sensor-manut.png" captura="menu do sensor aberto por uma conta que tem só a permissão Manut. (painel); caminho: tela inicial, botão de configurações, conta, PIN, Limites de Alarme, sensor"}
O mesmo menu para uma conta só com Manut. (painel): as duas primeiras linhas apagadas, com cadeado, e a seleção em Manutenção.
:::

### Limites no painel {#cap-07-editor-limites}

O título é o nome do sensor. O editor mostra uma barra por limite, quatro por página, com o nome da grandeza seguido de **MIN** ou **MAX**, como **Temperatura MIN** e **Umidade MAX**, o valor e a unidade. As setas nas pontas de cada barra diminuem e aumentam o valor.

1. Toque na barra do limite.
2. Toque nas setas das pontas para ajustar. Um toque muda 0,1. Segurando, o passo cresce: 0,5 depois de 2 s, 1 depois de 4 s e 10 depois de 6 s.
3. Toque em **SALVAR**.

As setas do rodapé mudam a barra selecionada, e **SAIR** volta ao menu do sensor sem gravar. O editor não deixa o mínimo passar do máximo: ao empurrar um, ele empurra o outro.

**SALVAR** exige a permissão **Limites (painel)** [PERM_ALARM_LIMITS]{.perm}. O aparelho grava, passa a usar os limites novos na hora, registra no log o código 442, **Limites de alarme salvos via UI**, e envia um registro `alarm_lim` por grandeza alterada, com os limites novos e o nome da conta.

::: {.figura #fig-07-editor-limites tipo="tft" arquivo="07-editor-limites.png" captura="screen alm -> tap(160,57) -> tap(160,57) -> tap(160,57); sessão admin; sensor DHT22; barra Temp Max selecionada"}
O editor de limites de um DHT22: temperatura e umidade, mínimo e máximo.
:::

## Janelas de manutenção {#cap-07-manutencao}

A janela de manutenção suspende os alarmes de um sensor por um tempo definido, enquanto alguém mexe nele. Durante a janela, aquele sensor:

- não dispara alarme de limite;
- não dispara alarme de falha, então desligar o cabo do sensor não faz a cigarra tocar;
- não gera registros de limite nem de falha na linha de alarmes.

O sensor continua sendo lido e gravado no histórico normalmente.

A janela guarda a hora em que termina, não um estado ligado ou desligado. Ela sobrevive a reinícios e fecha sozinha no prazo, e a duração máxima é de 30 dias. Um pedido maior é cortado para 30 dias.

A linha de alarmes recebe dois registros por janela: `maint_on` ao abrir, com a hora prevista do fim e o nome de quem abriu, e `maint_off` ao fechar, por comando ou por prazo. Um reinício no meio da janela não gera um `maint_on` novo. Se o sensor ainda estiver fora da faixa quando a janela fecha, o alarme dispara de novo.

### Pelo painel {#cap-07-manutencao-painel}

1. No menu do sensor, toque em **Manutenção** e depois em **ENTRAR**.
2. Na tela **Manutenção**, ajuste **Horas** e **Minutos** com as setas das pontas. A tela abre com 1 h e 0 min. As horas vão de 0 a 720; os minutos mudam de 5 em 5, de 0 a 55.
3. Toque em **INICIAR**.

A janela precisa ter pelo menos 5 min: com 0 h e 0 min, **INICIAR** só toca o som de erro. A ação exige a permissão **Manut. (painel)** [PERM_MAINT]{.perm}. O log registra o código 454, **Manutenção aberta pelo painel**, com o nome da conta.

Com a janela aberta, a mesma tela mostra o nome do sensor e o tempo que falta, como `1h45 restantes`, e o botão **FECHAR**. Toque em **FECHAR** para encerrar a janela antes do prazo; o log registra o código 455, **Manutenção encerrada pelo painel**. **SAIR** volta ao menu do sensor sem mudar nada.

::: {.figura #fig-07-manutencao-entrada tipo="tft" arquivo="07-manutencao-entrada.png" captura="menu do sensor -> Manutenção -> ENTRAR; sessão admin; janela fechada; Horas 2 e Minutos 30 ajustados, antes de INICIAR"}
A tela Manutenção com a janela fechada: horas, minutos e INICIAR.
:::

::: {.figura #fig-07-manutencao-aberta tipo="tft" arquivo="07-manutencao-aberta.png" captura="mesmo caminho, com a janela aberta há alguns minutos"}
A tela Manutenção com a janela aberta: o tempo que falta e FECHAR.
:::

### Pela web e por um servidor {#cap-07-manutencao-servidor}

A interface web não tem controle de janela de manutenção. Um servidor, ou um script, abre e fecha janelas pela interface de programação, com uma conta que tenha a permissão **Sistema**:

- o pedido diz em quantos **segundos a partir de agora** a janela termina; `0` fecha a janela;
- um valor negativo é recusado, e um valor acima de 30 dias é cortado; nos dois casos o campo volta na lista `rejected` da resposta;
- a consulta de alarmes informa, por sensor, quantos segundos faltam para a janela fechar, e `0` quando não há janela.

O formato do pedido está no [capítulo 26](#cap-26); o uso a partir de um servidor de alarmes, no [capítulo 22](#cap-22).

## Sons {#cap-07-sons}

[release]{.img} [alpha]{.img} A cigarra toca cinco tipos de som. Cada tipo tem seis melodias à escolha e pode ser ligado ou desligado:

| Som | Quando toca | Melodias |
|---|---|---|
| Toque | A cada toque no painel | `Click`, `Bubble`, `Tick`, `Snap`, `Drop`, `Chirp` |
| Confirmação | Quando uma ação dá certo | **Ascendente**, `Fanfare`, `Chime`, `Triumph`, `Sparkle`, `Resolve` |
| Erro | Quando uma ação é recusada | **Descendente**, `Buzz`, `Low`, `Harsh`, `Decline`, `Blip` |
| Alarme | Enquanto um alarme está ativo | `Dual Beep`, **Sirene**, `Rapid`, `Pulse`, `Escalate`, `Staccato` |
| Atenção | Ao abrir uma tela de confirmação no painel | `Notify`, `Bell`, `Pulse`, `Chime Low`, `Rise`, `Soft Ding` |

Os ajustes gerais:

- **Dois volumes.** O volume de alarme vale só para o som de alarme; o volume do sistema vale para os outros quatro.
- **Sons Web.** Quando ligado, o aparelho toca o som de confirmação a cada entrada na interface web e o som de erro a cada senha errada ([capítulo 13](#cap-13-entrada)).
- **Mudo Global.** Cala todos os sons, inclusive o de alarme.

Com o som de alarme desligado, ou com **Mudo Global**, um alarme continua disparando no painel, no log e na linha de alarmes, mas a cigarra não toca.

[air]{.img} O Air não tem cigarra. A seção de sons existe na página, mas não tem efeito.

### Sons no painel {#cap-07-sons-painel}

[release]{.img} O item **Sons de Alarme** do menu de configurações exige a permissão **Sistema** [PERM_SYS_CONFIG]{.perm}. A tela **Config. de Sons** tem nove linhas, em três páginas:

| Linha | Como mudar |
|---|---|
| **Vol. Sistema** | Com a linha selecionada, toque na metade esquerda para baixar e na direita para subir, de 10 em 10 |
| **Vol. Alarme** | Igual, e o painel toca a melodia de alarme como amostra |
| **Toque na Tela**, **Confirmação**, **Erro**, **Alarme**, **Atenção** | Com o som ligado, um toque o desliga. Com o som desligado, um toque abre a escolha de melodia |
| **Acesso Web** | Um toque liga ou desliga os sons de entrada pela web |
| **Mudo Global** | Ligar pede confirmação; desligar é direto |

Na escolha de melodia, o painel lista as seis melodias numeradas e toca cada uma ao ser selecionada. **SALVAR** liga o som com a melodia escolhida e desliga o **Mudo Global**; **SAIR** volta sem mudar.

Ao ligar **Mudo Global**, o painel toca o som de atenção e pergunta: **Todos os sons serão desabilitados. Tem certeza?** **Confirmar** liga o mudo e desliga todos os sons; **SAIR** volta sem mudar.

As mudanças só valem depois de **SALVAR**, no rodapé da tela. O painel grava na hora, sem reiniciar, e o log registra o código 446, **Config de som salva**.

::: {.figura #fig-07-sons-painel tipo="tft" arquivo="07-sons-painel.png" captura="screen set -> tap(105,215) -> tap(105,215) -> tap(250,215); sessão admin; valores de fábrica; primeira página"}
A tela Config. de Sons, primeira página: os dois volumes, Toque na Tela e Confirmação.
:::

::: {.figura #fig-07-melodia tipo="tft" arquivo="07-melodia.png" captura="tela Config. de Sons; som Alarme desligado; toque na linha Alarme duas vezes para abrir a escolha de melodia"}
A escolha de melodia do som de alarme, com as seis opções.
:::

::: {.figura #fig-07-mudo-confirmar tipo="tft" arquivo="07-mudo-confirmar.png" captura="tela MODE_CONFIRM_MUTE_ALL; na tela Config. de Sons, selecione Mudo Global desligado e toque nele; a ferramenta de mapa de telas não alcança esta tela sozinha"}
A confirmação do Mudo Global.
:::

## A linha de alarmes {#cap-07-linha}

Além do painel e da cigarra, o aparelho pode enviar cada mudança de alarme a um servidor, pela linha de alarmes, uma segunda linha da telemetria, desligada de fábrica. Ela envia um registro por transição, não um por leitura: limite ultrapassado (`alarm`), falha (`err`), silêncio (`alarm_sil`, `err_sil`), desativação e reativação (`alarm_off`, `err_off`, `alarm_on`), limites alterados no painel (`alarm_lim`) e janelas de manutenção (`maint_on`, `maint_off`). Os registros de ações feitas com PIN ou pela web levam o nome da conta. Para evitar picos isolados, um limite só vira registro depois de duas verificações seguidas com o valor fora da faixa; o painel e a cigarra reagem já na primeira. Os registros esperam numa fila na memória até o servidor confirmar o recebimento. Tudo isso está no [capítulo 22](#cap-22).
