# Sensores e calibração {#cap-06}

Este capítulo mostra como dizer ao aparelho quais sensores estão ligados e em quais pinos, como ele valida cada leitura e como corrigir um sensor contra um instrumento de referência. É para quem instala e mantém o aparelho; os limites de alarme de cada grandeza estão no [capítulo 7](#cap-07).

## Onde os sensores são configurados {#cap-06-onde}

Os sensores ficam na página **Configurações** (*System Config*, rota `/config`), em duas seções:

- **Hardware** (*Hardware & Sampling*): a resolução do DS18B20, o intervalo do histórico e a política de PIN do painel;
- **Sensores e GPIO** (*Sensors & GPIO*): a lista de slots e o editor de cada um.

A página exige a permissão **Sistema** [PERM_SYS_CONFIG]{.perm}. A calibração, dentro do editor de slot, exige também a permissão **Calibração** [PERM_CALIB]{.perm}; sem ela, o editor abre sem o bloco de calibração e não mostra erro ([Requisitos da calibração](#cap-06-requisitos)).

Tudo o que você muda nestas seções fica preparado na aba do navegador até você gravar. Os três botões de gravação estão no [capítulo 5](#cap-05). Mudanças de slot e de calibração só são gravadas com **Salvar e reiniciar** (*Save & restart*): o aparelho não aceita testá-las nem aplicá-las sem reiniciar.

::: nota
**Por que mexer num sensor reinicia o aparelho.** Tipo, pinos e identificação de um slot montam o circuito de leitura na partida. O aparelho não troca esse circuito com ele funcionando; reinicia e monta de novo.
:::

## A seção Hardware {#cap-06-hardware}

::: {.figura #fig-06-hardware tipo="web" arquivo="06-hardware.png" captura="rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; recorte da seção Hardware"}
A seção Hardware com os valores de fábrica: resolução de 12 bits, amostra de 2000 ms, histórico a cada 1 min e a política de PIN.
:::

| Campo | O que faz | Faixa | Fábrica | Reinicia |
|---|---|---|---|---|
| **Resolução DS18B20** (*DS18B20 Resolution*) | Resolução da conversão de temperatura das sondas DS18B20 | **9-bit**, `10-bit`, `11-bit`, **12-bit** | 12-bit | Sim |
| **Amostra (ms)** (*Sample Interval (ms)*) | Gravado, mas sem efeito na v2.7.1 ([Amostra (ms)](#cap-06-amostra)) | 1000 a 60000 | 2000 | Sim |
| **Intervalo Histórico (min)** (*History Recording Interval (min)*) | De quantos em quantos minutos o histórico grava um registro | 1 a 1440 | 1 | Sim |
| **Teclado do painel: glifos por tecla** | Política de PIN do painel | 1, 2 ou 3 | 3 | Sim |
| **Caracteres do PIN** | Política de PIN do painel | `0-9` ou `0-9 A-Z` | `0-9` | Sim |
| **Tamanho mínimo do PIN** | Política de PIN do painel | 4 até o teto do teclado | 4 | Sim |

Os três campos de PIN estão explicados no [capítulo 8](#cap-08-politica). Nas imagens alpha e Air, que não têm painel com toque, a página esconde esses três campos.

### Resolução DS18B20 {#cap-06-resolucao}

A resolução define o menor passo da temperatura medida pelo DS18B20: 9 bits é o passo mais grosso e 12 bits o mais fino. O aparelho envia a resolução na partida, pelo GPIO do primeiro slot DS18B20 ativo. Uma sonda DS18B20 ligada a outro GPIO não recebe o ajuste.

O aparelho sempre espera 750 ms pela conversão, que é o tempo da resolução de 12 bits. Uma resolução menor, portanto, não deixa a leitura mais rápida.

### Amostra (ms) {#cap-06-amostra}

::: atencao
**Na v2.7.1, este campo não muda nada.** O aparelho valida o valor (de 1000 a 60000 ms), grava e informa na página, mas nenhuma parte do firmware o usa. O ritmo de leitura é fixo por tipo de sensor ([Leitura e histórico](#cap-06-intervalos)). Mudar o campo só provoca um reinício.
:::

### Intervalo Histórico (min) {#cap-06-intervalo-historico}

É o intervalo entre dois registros do histórico, de 1 min a 1440 min (24 h). O valor de fábrica é 1 min. O que o histórico grava em cada registro está em [Leitura e histórico](#cap-06-intervalos), e o histórico em si no [capítulo 15](#cap-15).

::: atencao
**Grave o intervalo com Salvar e reiniciar.** A classificação feita antes de gravar não enxerga este campo, e o selo da barra pode mostrar **Não exige reinício** (*No restart needed*) quando ele é a única mudança. Mesmo assim, **Aplicar agora** reinicia o aparelho, e **Testar** não aplica o valor.
:::

## A seção Sensores e GPIO {#cap-06-secao}

::: {.figura #fig-06-sensores tipo="web" arquivo="06-sensores.png" captura="rota /config; largura 1280; sessão admin; imagem release; 3 slots ativos (DS18B20 em GP2, DHT22 em GP3, BME280 em GP4 e GP5); recorte da seção Sensores e GPIO"}
A seção Sensores e GPIO. A faixa de GP0 a GP15 mostra quais pinos estão ocupados e por qual slot; a tabela lista só os slots em uso.
:::

A seção mostra, de cima para baixo:

- o aviso **GP0–GP15 estão disponíveis para sensores; GP16 em diante pertencem ao display, ao touch e ao buzzer.**;
- a faixa de pinos, de `GP0` a `GP15`. Um pino ocupado aparece destacado, com o número do slot dono ao lado;
- a tabela de slots;
- os botões **+ Adicionar slot de sensor** (*+ Add sensor slot*) e **Procurar sondas** (*Scan for probes*).

A faixa e a tabela mostram o estado que o aparelho terá depois de gravar, e não o estado gravado: um slot com alteração pendente aparece com fundo destacado.

A tabela tem uma linha por slot em uso:

| Coluna | O que mostra |
|---|---|
| **Slot** | O número do slot, de 0 a 15, e um ponto verde quando o slot está ativo |
| **Tipo** | O tipo do sensor |
| **GPIOs** | Os pinos atribuídos, como `GP4 GP5` |
| **ID de Hardware** | A chave do sensor no histórico e na telemetria |
| **Nome** | O nome dado ao slot |

Toque numa linha para abrir o editor daquele slot. Sem nenhum slot em uso, a tabela diz **Nenhum slot de sensor configurado ainda.** Com os 16 slots em uso, o botão de adicionar some e aparece **Os 16 slots estão em uso. Libere um para adicionar outro.** Se a página não conseguir ler os slots do aparelho, mostra **Não foi possível carregar os slots de sensores.**; recarregue a página.

### Slots {#cap-06-slots}

O aparelho tem 16 slots, numerados de 0 a 15. Um slot é uma posição da configuração, não um pino: ele guarda o tipo do sensor, os GPIOs que o sensor usa (de 1 a 4), a identificação e os limites de alarme. O número do slot é a identidade do sensor para o histórico, para o console serial e para a linha de alarmes, por isso a página o mostra.

De fábrica, os 16 slots estão vazios e nenhum GPIO está ocupado.

### Tipos de sensor {#cap-06-tipos}

Todas as imagens (release, alpha e Air) trazem os quatro tipos:

| Tipo | Pinos no editor | Grandezas |
|---|---|---|
| DS18B20 | 1: `1-Wire` | Temperatura |
| DHT22 | 1: `Data` | Temperatura e umidade |
| BME280 | 2: `SDA` e `SCL` | Temperatura, umidade e pressão |
| BMP280 | 2: `SDA` e `SCL` | Temperatura e pressão |

Sobre os sensores I²C (BME280 e BMP280):

- **Um por par de pinos.** A leitura aceitaria dois sensores no mesmo par SDA/SCL, em `0x76` e `0x77`, mas a gravação recusa dois slots ativos no mesmo GPIO. Dê a cada sensor o seu par, e siga as regras do I²C do [capítulo 2](#cap-02-i2c): um segundo sensor em outro par do mesmo periférico repete a leitura do primeiro.
- **O aparelho confere o chip.** Se você escolheu BME280 e o chip é um BMP280, ou o contrário, o aparelho corrige o tipo na partida pelo identificador do chip e grava a correção.
- **Pares do periférico I²C.** Com SDA e SCL num par que o periférico I²C do RP2040 aceita, a leitura usa esse periférico. Em outros pares, o aparelho lê o sensor pelo PIO do RP2040, sem o periférico. A ligação está no [capítulo 2](#cap-02).

## O editor de slot {#cap-06-editor}

::: {.figura #fig-06-editor-ds18b20 tipo="web" arquivo="06-editor-ds18b20.png" captura="rota /config; largura 1280; sessão admin com a permissão Calibração; imagem release; editor do slot 2 aberto, DS18B20 ativo em GP2, relógio sincronizado por NTP, sem correção"}
O editor de um slot DS18B20: tipo, nome, ID de hardware, estado, o pino 1-Wire, o bloco de calibração e as operações de hardware.
:::

O editor abre sobre a página. Para fechá-lo, toque no **×** do canto, toque fora da caixa ou toque em **Concluído** (*Done*). Fechar não descarta nada: as edições continuam preparadas.

| Campo | O que é | Regras |
|---|---|---|
| **Tipo** (*Type*) | O tipo do sensor. **— vazio —** deixa o slot sem sensor | Os tipos da tabela acima |
| **Nome** (*Name*) | Nome livre, mostrado no painel, na página e nos gráficos | Até 31 caracteres, sem aspas, sem barra invertida e sem caracteres de controle |
| **ID de Hardware** (*Hardware ID*) | A chave do sensor no histórico e na telemetria | De 1 a 15 caracteres: letras sem acento, dígitos, `_` e `-`. Obrigatório num slot ativo |
| **Ativo** (*Active*) | Liga o slot. Só um slot ativo ocupa GPIOs e é lido | Exige tipo, todos os pinos e o ID de hardware |
| **Alarmes habilitados** (*Alarms enabled*) | Liga os alarmes de limite do slot ([capítulo 7](#cap-07)) | Ligado de fábrica |
| **Atribuição de GPIO** (*GPIO assignment*) | Um seletor por pino do tipo, como **Pino 0 — SDA** | `GP0` a `GP15`. Um GPIO de outro slot ativo aparece desativado, como `GP7 (slot 3)` |

Abaixo dos campos, o editor mostra, nesta ordem:

- o bloco **Calibração** (*Calibration*), só para slots ativos já gravados e só para contas com a permissão **Calibração** ([Calibração](#cap-06-calibracao));
- **Gravação no histórico** (*History recording*), com o botão **Recompor o histórico agora** ([Recompor o histórico](#cap-06-recompor));
- **Hardware**, com **Adotar a sonda ligada aqui** (só para DS18B20) e **Resetar época do histórico** ([Operações de hardware](#cap-06-operacoes));
- uma linha de aviso em vermelho, quando há um problema;
- os botões **Liberar slot** (*Free slot*), **Descartar alterações** (*Discard staged*) e **Concluído**;
- o lembrete **As edições são acumuladas conforme você digita e gravadas ao Salvar e Reiniciar.**

::: atencao
**O ID de hardware é a chave da telemetria, não do histórico.** O texto do editor avisa: **Chave usada pelo histórico e pela telemetria. Alterá-la inicia uma nova série.** Na v2.7.1, isso vale para o coletor: a telemetria identifica cada leitura pelo ID, e um ID novo aparece no servidor como outra série ([capítulo 21](#cap-21)). O histórico do aparelho identifica a série pelo slot: os gráficos e o CSV continuam a mesma série e mostram, também nos dias antigos, o ID e o nome atuais do slot. Troque o ID só quando o sensor físico for outro.
:::

Se você ativar um slot sem digitar o ID de hardware, o aparelho recusa a gravação ([Erros ao gravar](#cap-06-erros-gravar)). Um slot ativo que chegue à partida sem ID, gravado por outro caminho que não esta página, recebe um ID automático: o tipo seguido do número do slot com dois dígitos, como `DHT2202` para um DHT22 no slot 2. Se o nome também estiver vazio, ele vira o tipo e o slot, como `DHT22 #2`.

### Adicionar um sensor {#cap-06-adicionar}

1. Abra **Configurações** e role até **Sensores e GPIO**.
2. Toque em **+ Adicionar slot de sensor**. O editor abre no slot livre de menor número.
3. Escolha o **Tipo**. O formulário ganha um seletor para cada pino do tipo.
4. Escolha o GPIO de cada pino em **Atribuição de GPIO**.
5. Digite o **ID de Hardware**, por exemplo `FREEZER-1`.
6. Digite o **Nome**, por exemplo `Freezer da sala 2`.
7. Ligue **Ativo**.
8. Confira se a linha de aviso vermelha está vazia.
9. Toque em **Concluído**.
10. Toque em **Salvar e reiniciar** na barra de topo e confirme.

O aparelho reinicia, e a página se recarrega sozinha. O sensor aparece no **Painel de Controle** e no painel logo depois das primeiras leituras.

::: {.figura #fig-06-editor-bme280 tipo="web" arquivo="06-editor-bme280.png" captura="rota /config; largura 390; sessão admin; imagem release; editor de um slot novo com o tipo BME280 escolhido, Pino 0 — SDA em GP4, Pino 1 — SCL em GP5, ID de hardware e nome preenchidos, Ativo ligado, ainda não gravado"}
Um slot novo de BME280 no celular: dois seletores de pino, SDA e SCL.
:::

Um DS18B20 criado pela página não conhece ainda o número de série da sonda. Na partida seguinte, o aparelho lê esse número (a ROM) no pino, liga o slot a ele e grava. A partir da partida seguinte a essa, o aparelho passa a conferir a sonda ([Como o aparelho valida as leituras](#cap-06-validacao)).

::: nota
**Um slot novo e o histórico do dia.** A página avisa que um slot adicionado ou renomeado hoje pode não ser gravado no arquivo de histórico do dia, e oferece **Recompor o histórico agora** para isso.
:::

### Editar um sensor {#cap-06-editar}

1. Na tabela, toque na linha do slot.
2. Mude os campos.
3. Toque em **Concluído**.
4. Toque em **Salvar e reiniciar**.

Para desistir das mudanças de um slot antes de gravar, abra o editor dele e toque em **Descartar alterações**. O botão descarta também as mudanças de calibração desse slot e relê o que está gravado no aparelho.

Ao trocar o **Tipo** de um slot, o aparelho trata o sensor como outro: grava a data da troca como a época do slot ([Resetar época do histórico](#cap-06-epoca)) e apaga a ROM guardada.

### Desligar sem apagar {#cap-06-desligar}

Para parar de ler um sensor sem perder sua configuração, desligue **Ativo** e grave com **Salvar e reiniciar**. O slot continua na tabela, sem o ponto verde, e seus GPIOs ficam livres para outros slots.

### Remover um sensor {#cap-06-remover}

1. Na tabela, toque na linha do slot.
2. Toque em **Liberar slot**.
3. Confirme a pergunta **Liberar este slot? Tipo, GPIOs e identificação serão apagados.**
4. Toque em **Salvar e reiniciar**.

O slot volta a ficar vazio e sai da tabela. O histórico já gravado continua no aparelho. As correções de calibração do sensor continuam no arquivo `calib.csv`.

### Erros ao gravar {#cap-06-erros-gravar}

O editor confere as regras enquanto você digita e mostra o primeiro problema na linha vermelha:

| Aviso no editor | O que fazer |
|---|---|
| **Escolha um tipo antes de ativar este slot.** | Escolha o **Tipo** ou desligue **Ativo** |
| **Atribua um GPIO ao pino** `0 (SDA).` | Escolha o GPIO do pino indicado |
| `GP4` **já está em uso pelo slot** `2.` | Escolha outro GPIO, ou libere o pino no outro slot |

Na gravação, o aparelho confere as mesmas regras e mais algumas. Se alguma falhar, nada é gravado, o aparelho não reinicia, e a página mostra **Falha ao salvar.** (*Save failed.*) seguido do motivo, em inglês:

| Motivo mostrado | Causa |
|---|---|
| `slot N: set a type before enabling` | Slot ativo sem tipo |
| `slot N: pin K (SDA) not assigned` | Falta o GPIO de um pino do tipo |
| `slot N: GPx already used by slot M` | Dois slots ativos no mesmo GPIO |
| `slot N: GPx not a sensor pin (0-15)` | GPIO acima de GP15, enviado por outro meio que não a página |
| `slot N: hardware ID cannot be empty on an active slot` | Slot ativo sem **ID de Hardware** |
| `slot N: invalid hwId` | O ID tem caractere fora de letras, dígitos, `_` e `-`, ou mais de 15 caracteres |
| `slot N: invalid name` | O nome tem aspas, barra invertida ou caractere de controle |
| `slot N: driver not in firmware` | Tipo que esta imagem não traz |

::: {.figura #fig-06-editor-aviso tipo="web" arquivo="06-editor-aviso.png" captura="rota /config; largura 1280; sessão admin; editor de um slot DHT22 ativo com o pino Data em GP4, que já pertence ao slot 2; a linha vermelha mostra GP4 já está em uso pelo slot 2."}
O editor avisando um conflito de GPIO antes da gravação.
:::

## Procurar sondas {#cap-06-procurar}

O botão **Procurar sondas** varre os pinos do aparelho em busca de sensores ligados. Ele só informa o que encontrou: não cria nem muda nenhum slot.

1. Toque em **Procurar sondas**. O botão fica desativado e a página mostra **Procurando...** (*Scanning...*).
2. Espere o resultado, que chega em poucos segundos. A página espera no máximo 10 s.
3. Leia a lista, como `2 encontrada(s): GP2 (28FF641E8716045C), GP3`.

| Resultado | O que é |
|---|---|
| `GPn (` ROM `)` | Um DS18B20 no pino `n`; o código entre parênteses é a ROM da sonda |
| `GPn` sem ROM | Um sensor que respondeu como DHT22 no pino `n` |
| `GP255` | Um BME280 ou BMP280 nos pinos GP4 (SDA) e GP5 (SCL) |
| **Nenhuma sonda respondeu.** (*No probes answered.*) | Nada respondeu, ou a busca não terminou em 10 s |
| **Falha na ação.** (*Action failed.*) | O aparelho recusou ou não respondeu |

A busca por BME280 e BMP280 olha só o par GP4/GP5. Um sensor I²C em outros pinos não aparece na busca, mas funciona se você configurar o slot à mão.

::: atencao
**A busca mexe nos pinos.** Para testar cada pino, o aparelho reconfigura um a um os GPIOs de GP0 a GP16, inclusive os de sensores ativos, e a página não recarrega os sensores depois. Faça a busca antes de configurar os slots, ou reinicie o aparelho depois dela.
:::

::: {.figura #fig-06-procurar tipo="web" arquivo="06-procurar.png" captura="rota /config; largura 1280; sessão admin; imagem release; um DS18B20 em GP2 e um DHT22 em GP3 ligados; logo depois de tocar em Procurar sondas e a busca terminar; recorte dos botões e da linha de resultado"}
O resultado da busca: a ROM identifica o DS18B20; o DHT22 aparece só com o pino.
:::

## Grandezas e unidades {#cap-06-grandezas}

Cada slot informa uma ou mais grandezas, que o aparelho chama de canais:

| Grandeza | Chave | Unidade | Faixa aceita na calibração | Limites de alarme de fábrica |
|---|---|---|---|---|
| Temperatura | `temp` | °C | −327,68 a 327,66 | 0 a 40 |
| Umidade | `hum` | % | 0 a 102,3 | 20 a 80 |
| Pressão | `press` | hPa | 0 a 1.638,3 | a faixa inteira |
| Luminosidade | `lux` | lx | 0 a 167.772,15 | a faixa inteira |

A chave é o nome da grandeza na interface de programação, na telemetria e no arquivo de calibração. Na página, os rótulos curtos são **Temp**, **Umid**, **Press** e **Luz**.

A luminosidade existe na tabela de grandezas, mas nenhum dos quatro tipos de sensor a informa na v2.7.1.

## Leitura e histórico {#cap-06-intervalos}

O aparelho lê cada sensor no seu próprio ritmo e grava o histórico no ritmo do **Intervalo Histórico**. São duas coisas diferentes.

**Ritmo de leitura.** É fixo por tipo:

| Tipo | Pausa entre leituras |
|---|---|
| DS18B20 | 1 s, mais os 750 ms da conversão |
| DHT22 | 2 s |
| BME280 e BMP280 | 5 s |

**Média.** Cada grandeza guarda as últimas 10 leituras. Com as 10 guardadas, o aparelho descarta as 2 menores e as 2 maiores e tira a média das 6 restantes. Antes disso, logo depois da partida, usa a média simples do que tem. A correção de calibração é aplicada sobre essa média. O resultado é o valor que o painel, a página e a telemetria mostram.

**Histórico.** A cada **Intervalo Histórico**, o aparelho grava um registro com o valor corrigido de cada grandeza naquele instante. Um sensor em erro entra no registro sem valor. O primeiro registro depois de um reinício não espera o intervalo inteiro, desde que o relógio já esteja acertado.

[air]{.img} No Air, o aparelho lê os sensores a cada despertar ([capítulo 19](#cap-19)).

## Como o aparelho valida as leituras {#cap-06-validacao}

Uma leitura falha quando o sensor não responde, quando a conferência dos dados falha ou quando o valor está fora do que o aparelho aceita:

| Tipo | O que o aparelho confere |
|---|---|
| DS18B20 | O CRC da leitura. Temperatura fora de −50 a 150 °C é recusada. A cada 5 leituras, confere se a sonda no pino é a mesma cuja ROM está gravada |
| DHT22 | A soma de verificação. Sem resposta em 150 ms, a leitura falha |
| BME280 e BMP280 | Temperatura fora de −40 a 85 °C faz a leitura falhar. Umidade fora de 0 a 100 % e pressão fora de 300 a 1.100 hPa são descartadas, só naquela grandeza |

Um valor que não é número (infinito, indefinido) nunca entra na média.

**Estado de erro.** Um sensor entra em erro depois de 3 falhas seguidas e sai dele depois de 5 leituras boas seguidas. Enquanto está em erro, o aparelho não usa as leituras dele.

**Sonda trocada.** Um DS18B20 com ROM gravada só é lido se a sonda no pino for a mesma. Se outra sonda aparecer, o slot entra em erro na hora, em quarentena. A cada 10 ciclos de leitura, o aparelho relê a ROM do pino. Quando a sonda certa volta, a quarentena acaba sozinha e o log registra `Hardware match restored`. Para aceitar a sonda nova, use **Adotar a sonda ligada aqui** ([Operações de hardware](#cap-06-operacoes)). Um DS18B20 sem ROM gravada aceita qualquer sonda no pino.

O log de eventos registra a entrada em erro, com o GPIO do sensor ([capítulo 16](#cap-16)):

| Código | Evento | Causa típica |
|---|---|---|
| 101 | Timeout de sensor | DHT22 sem resposta |
| 102 | Erro de checksum | DHT22 com dados corrompidos |
| 103 | Erro de CRC | DS18B20 com dados corrompidos ou sem resposta |
| 104 | Sensor fora de range | DS18B20 fora de −50 a 150 °C |
| 105 | Divergência de hardware | Outra sonda no pino de um DS18B20 |
| 999 | Erro desconhecido | Falha de leitura de um BME280 ou BMP280, ou falha ao ler a ROM do DS18B20 |
| 100 | Sensor recuperado | 5 leituras boas depois de um erro |

### Como o erro aparece {#cap-06-erro-aparece}

- **Painel** [release]{.img}: o cartão do sensor mostra **Erro** (*ERROR*) em letras grandes, no lugar do valor. O alarme de falha deixa o cartão âmbar ([capítulo 7](#cap-07-tipos)).
- **LCD** [alpha]{.img}: quando o LCD chega ao sensor em erro, mostra `ERRO` em letras grandes. O mesmo `ERRO` aparece quando não há nenhum sensor ativo.
- **Interface web**: na tabela de sensores do **Painel de Controle**, o ponto fica vermelho e a leitura mostra `Error` ([capítulo 13](#cap-13-tabela-sensores)).
- **Histórico**: os registros do período em erro ficam sem valor para aquele sensor.

::: {.figura #fig-06-painel-sensor tipo="tft" arquivo="06-painel-sensor.png" captura="screen dash; 2 sensores ativos (um DS18B20 e um BME280), ambos lendo, nenhum em alarme"}
A tela inicial do painel com dois sensores lendo normalmente.
:::

::: {.figura #fig-06-painel-erro tipo="tft" arquivo="06-painel-erro.png" captura="screen dash; 2 sensores ativos; a sonda DS18B20 desligada do pino há mais de 3 leituras, alarmes do slot ligados"}
Um sensor em erro no painel: o cartão mostra Erro no lugar da temperatura e fica âmbar pelo alarme de falha.
:::

::: {.figura #fig-06-lcd-erro tipo="lcd" arquivo="06-lcd-erro.png" captura="imagem alpha; 1 sensor DHT22 ativo e desligado do pino; o LCD mostra ERRO em letras grandes"}
O LCD do alpha com o sensor em erro.
:::

## Operações de hardware {#cap-06-operacoes}

As operações da seção **Hardware** do editor agem no aparelho na hora. Elas não passam por **Salvar e reiniciar**.

### Adotar a sonda ligada aqui {#cap-06-adotar}

Só aparece em slots DS18B20. Use depois de trocar uma sonda, quando o slot está em quarentena por divergência de hardware.

1. Abra o editor do slot.
2. Toque em **Adotar a sonda ligada aqui** (*Adopt the probe wired here*).

O aparelho lê a ROM da sonda, liga o slot a ela e grava. Se a ROM já constar do arquivo `calib.csv`, o slot recebe o ID de hardware e o nome guardados para ela; se não constar, recebe o ID `LIB_SENS`. Quando o ID muda, o aparelho grava a data como a época do slot ([Resetar época do histórico](#cap-06-epoca)).

| Mensagem | Significado |
|---|---|
| **Slot vinculado à sonda encontrada nele.** | A sonda foi adotada |
| **Nenhuma sonda respondeu neste GPIO.** | Nada respondeu no pino lido |
| **A sonda respondeu com uma ROM inválida.** | A ROM lida falhou na conferência |

::: atencao
**A adoção lê o GPIO de mesmo número que o slot.** Para o slot 3, o aparelho lê a sonda em GP3 e grava GP3 como pino do slot. Com o slot num GPIO de outro número, a adoção lê o pino errado e muda o pino do slot. Nesse caso, em vez de adotar, libere o slot e crie-o de novo no mesmo GPIO: o aparelho lê a ROM nova na partida.
:::

A adoção é gravada na hora, mas a leitura em andamento continua com a sonda antiga até o aparelho reiniciar. Reinicie depois de adotar.

### Resetar época do histórico {#cap-06-epoca}

Grava o instante atual como a época do slot: a data em que o sensor atual começou nele. Use quando trocar o sensor físico de um slot, para registrar a troca.

1. Abra o editor do slot.
2. Toque em **Resetar época do histórico** (*Reset history epoch*).
3. Confirme a pergunta. A página mostra **Época do histórico resetada.**

::: nota
**Na v2.7.1, a época não muda os gráficos.** O aparelho grava a data, mas nenhum gráfico, exportação ou envio de telemetria a usa. Os registros do sensor antigo e do novo aparecem como uma série só. Para ver só um deles, escolha a janela pelo período ou pelo calendário ([capítulo 15](#cap-15)).
:::

### Recompor o histórico {#cap-06-recompor}

O botão **Recompor o histórico agora** (*Rebind history now*) vale para o aparelho inteiro, não só para o slot aberto. Ele reescreve o arquivo de histórico do dia para os slots gravados, mantém os registros já feitos e reinicia o aparelho no fim. O histórico está no [capítulo 15](#cap-15).

1. Grave antes as mudanças pendentes. Com alguma pendente, a página recusa e mostra **Salve e Reinicie antes — recompor agora usaria a configuração anterior.**
2. Toque em **Recompor o histórico agora** e confirme.
3. Espere a mensagem **Histórico reescrito** seguida da contagem. A página se recarrega cerca de 25 s depois.

Se o arquivo do dia não puder ser lido, a página pergunta se deve recriá-lo vazio. Recriar perde os registros de hoje; os dias anteriores ficam intactos. Se o aparelho não responder, a página mostra **Sem resposta do dispositivo. Recarregue a página e confira o log antes de tentar de novo.**

## Calibração {#cap-06-calibracao}

A calibração corrige a leitura de um sensor para que ela coincida com a de um instrumento de referência. Cada grandeza de cada sensor tem a sua correção: um BME280 tem três, uma para temperatura, uma para umidade e uma para pressão.

A correção é feita por pontos. Cada ponto liga um valor bruto, o que o sensor leu, ao valor de referência, o que o instrumento confiável mostrou no mesmo momento. Uma grandeza aceita até 5 pontos.

| Pontos | O que o aparelho faz |
|---|---|
| Nenhum | Nenhuma correção. Vale a leitura do sensor |
| 1 | Soma uma diferença constante, o offset, a toda leitura |
| 2 a 5 | Muda a correção entre os pontos, conforme a interpolação escolhida |

Abaixo do menor valor bruto e acima do maior, a correção fica igual à do ponto da ponta. O aparelho nunca projeta uma inclinação para fora da faixa medida.

### Reta ou Suave {#cap-06-interpolacao}

A **Interpolação** (*Interpolation*) define como a correção passa de um ponto ao outro:

- **Reta** (*Straight*): linhas retas entre os pontos. É a escolha de fábrica.
- **Suave** (*Smooth*): uma curva cúbica monótona, do tipo PCHIP. Ela passa por todos os pontos sem ultrapassar nenhum deles entre um ponto e o seguinte.

A escolha **Suave** precisa de 3 pontos ou mais. Com menos, ela se comporta como **Reta**, e a página avisa: **Suave é uma cúbica monótona: dobra pelas âncoras sem jamais ultrapassá-las. Precisa de 3+ pontos; com menos, comporta-se como reta.**

### Requisitos {#cap-06-requisitos}

- **Permissões.** A conta precisa de **Sistema** [PERM_SYS_CONFIG]{.perm}, para abrir a página, e de **Calibração** [PERM_CALIB]{.perm}, para ver e gravar o bloco.
- **Relógio acertado por NTP.** Sem o relógio sincronizado por NTP, o aparelho recusa a gravação da calibração. O bloco avisa em vermelho: **NTP não sincronizado.** A sincronização está no [capítulo 10](#cap-10).
- **Slot ativo e gravado.** O bloco só aparece em slots ativos já gravados no aparelho. Num slot novo, grave o slot primeiro e calibre depois.
- **Uma leitura ao vivo**, para os pontos em que você deixar o valor bruto vazio.
- **Uma gravação a cada 5 s.** O aparelho recusa uma segunda gravação de calibração antes disso.

### Calibrar um sensor {#cap-06-calibrar}

::: {.figura #fig-06-calibracao tipo="web" arquivo="06-calibracao.png" captura="rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DHT22 ativo; relógio sincronizado por NTP; Temp com 2 pontos preenchidos e Reta escolhida; Umid sem correção; recorte do bloco Calibração"}
O bloco Calibração de um DHT22. Cada grandeza tem sua linha de leitura, o mini gráfico da correção e seus pontos.
:::

Para cada grandeza, o bloco mostra:

- uma linha com o nome da grandeza, a leitura **Bruto** (*Raw*) e a leitura **corrigido** (*corrected*), como `Temp — Bruto: 23.61 °C → corrigido: 23.40 °C`;
- um mini gráfico da correção: a linha do zero, que é o sensor sem correção, e a curva com os pontos;
- sem pontos, **Sem correção — padrão do sensor.** (*No correction — sensor default.*), ou **Offset constante** (*Constant offset*) seguido do valor, quando a correção é um offset antigo gravado sem ponto de origem;
- os pontos, um por linha, cada um com **Bruto**, **Referência** (*Reference*), o botão **↻** (**Usar a leitura bruta atual**) e o botão **×** (**Remover este ponto**);
- **+ Adicionar ponto** (*Add point*), até 5 pontos;
- **Remover correção** (*Remove correction*);
- **Interpolação:** com **Reta** e **Suave**.

Para calibrar:

1. Ponha o sensor e o instrumento de referência no mesmo ambiente e espere os dois estabilizarem.
2. Abra o editor do slot.
3. Na grandeza a corrigir, toque em **+ Adicionar ponto**.
4. Toque em **↻** para copiar a leitura bruta atual para **Bruto**. Se preferir, deixe **Bruto** vazio: o aparelho capta a leitura bruta no momento da gravação.
5. Digite em **Referência** o valor que o instrumento mostra.
6. Para mais pontos, mude o ambiente (por exemplo, outra temperatura), espere estabilizar e repita os passos 3 a 5.
7. Escolha **Reta** ou **Suave**.
8. Confira se a linha de aviso vermelha do bloco está vazia.
9. Toque em **Concluído** e depois em **Salvar e reiniciar**.

Os pontos não precisam estar em ordem: o aparelho os ordena pelo valor bruto. Na gravação, o aparelho grava a calibração primeiro e aplica a correção aos sensores; em seguida grava o resto e reinicia. O log de eventos registra o código 407, **Sensores calibrados**.

Para tirar a correção de uma grandeza, toque em **Remover correção** e grave.

::: {.figura #fig-06-calibracao-suave tipo="web" arquivo="06-calibracao-suave.png" captura="rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DS18B20; Temp com 3 pontos (0, 25 e 50 °C de referência) e Suave escolhida; recorte do bloco Calibração"}
Uma correção Suave com três pontos: o mini gráfico mostra a curva passando pelos pontos sem ultrapassá-los.
:::

::: {.figura #fig-06-calibracao-ntp tipo="web" arquivo="06-calibracao-ntp.png" captura="rota /config; largura 1280; sessão admin com a permissão Calibração; relógio do aparelho sem sincronização NTP; editor de um slot ativo; recorte do fim do bloco Calibração com o aviso NTP não sincronizado."}
Sem NTP, o bloco avisa em vermelho que a gravação será recusada.
:::

### Erros da calibração {#cap-06-erros-calibracao}

O bloco confere os pontos enquanto você digita e mostra o primeiro problema, precedido do nome da grandeza:

| Aviso | O que fazer |
|---|---|
| **no máximo 5 pontos.** | Remova um ponto |
| **todo ponto precisa de bruto e referência numéricos.** | Preencha **Referência** com um número; **Bruto** vazio ou numérico |
| **sem leitura ao vivo para captar — preencha o valor bruto.** | O sensor não tem leitura agora. Digite o valor bruto |
| **ponto fora da faixa plausível** seguido da faixa | Corrija o valor; as faixas estão em [Grandezas e unidades](#cap-06-grandezas) |
| **dois pontos com o mesmo valor bruto.** | Dois pontos com o mesmo bruto, até a segunda casa decimal. Remova um |

Na gravação, uma recusa do aparelho aparece como **Erro na calibração:** (*Calibration error:*) seguido do motivo, e nada é gravado:

| Motivo | Causa |
|---|---|
| `NTP not synced` | Relógio não sincronizado por NTP |
| `rate limited` | Menos de 5 s desde a gravação de calibração anterior |
| `slot N temp: point out of range` | Ponto fora da faixa da grandeza |
| `slot N temp: duplicate calibration points` | Dois pontos com o mesmo valor bruto |
| `slot N temp: no live reading to capture` | Bruto vazio e o sensor sem leitura |
| `Forbidden` | A conta não tem a permissão **Calibração** |

Se a gravação da calibração demorar mais de 30 s, a página mostra **Tempo esgotado na calibração. Tente novamente.**

### O arquivo calib.csv {#cap-06-calib-csv}

As correções ficam no arquivo `/calib.csv`, na raiz do sistema de arquivos do aparelho, e não na configuração. O arquivo aparece na página **Arquivos** ([capítulo 17](#cap-17)).

A primeira linha é `VERSION,` seguida de um número que cresce a cada gravação. As outras linhas têm uma correção cada:

```text
VERSION,1790000000
28FF641E8716045C,FREEZER-1,Freezer da sala 2,-18.40,-18.00,4.10,4.00
E6614103E7512B29,tSALA-1,Sala,cub,15.20,15.00,22.80,22.50,30.10,30.00
E6614103E7512B29,uSALA-1,Sala,55.00,52.00
```

| Coluna | Conteúdo |
|---|---|
| 1 | A ROM do DS18B20; para os outros tipos, o número de série da placa |
| 2 | O ID de hardware; para os outros tipos, a letra da grandeza (`t`, `u`, `p`) seguida do ID |
| 3 | O nome |
| 4 | `cub` quando a interpolação é **Suave**; ausente quando é **Reta** |
| Resto | Os pares bruto e referência, um número por coluna |

Uma linha de DS18B20 sem pontos continua no arquivo: ela guarda a associação entre a ROM, o ID e o nome, que a adoção de sonda consulta.

Você pode enviar um `calib.csv` pela página **Arquivos**. O aparelho só o aceita se o número de `VERSION` for maior que o do arquivo atual; caso contrário, descarta o envio.

::: perigo
**A atualização de firmware apaga o calib.csv.** A atualização pela web reformata o sistema de arquivos, e só a configuração é preservada. O arquivo de calibração se perde, e os sensores voltam a ler sem correção. O backup (`.bkp`), que a página de atualização baixa antes de começar, leva o arquivo: restaure-o depois da atualização ([capítulo 17](#cap-17)).
:::
