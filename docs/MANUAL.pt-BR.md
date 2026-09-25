# SIMUT — Manual do Usuário

**Firmware:** v2.7.2 · **Hardware:** Raspberry Pi Pico W (RP2040 + CYW43439) · **Licença:** MIT
**Repositório:** https://github.com/angeloINTJ/simut

[English](MANUAL.md) | **Português**

> **Este não é um instrumento metrológico certificado.** Ele é testado em
> hardware real, e esta versão traz 8,2 h de soak e seis idas e voltas de OTA
> na imagem aqui publicada, mas não o faça o único controle de um
> armazenamento regulado sem validá-lo contra a sua própria referência.

**Três builds compartilham este manual.** A maior parte dele descreve o build
*release* — o do display touch. Duas variantes diferem, e cada uma está marcada
onde difere: o build **alpha** conduz um LCD de caracteres 16×2 no lugar, e o
**SIMUT Air** não tem display nenhum e passa a maior parte da vida dormindo com
bateria. A §17 é sobre o Air; as diferenças do alpha estão anotadas ao longo do
texto.

Tudo o que está abaixo foi conferido contra um dispositivo rodando. Onde um
número é citado, ele foi medido e não estimado; onde o
comportamento não foi testado ou é sabidamente incompleto, o texto diz isso em
vez de se calar.

---

## Sumário

1. [O que é o SIMUT](#1-o-que-é-o-simut)
2. [Hardware](#2-hardware)
3. [Primeira inicialização](#3-primeira-inicialização)
4. [Sensores e o modelo de slots](#4-sensores-e-o-modelo-de-slots)
5. [O display do dispositivo](#5-o-display-do-dispositivo)
6. [A interface web](#6-a-interface-web)
7. [Alarmes](#7-alarmes)
8. [Histórico e logs](#8-histórico-e-logs)
9. [Usuários e permissões](#9-usuários-e-permissões)
10. [Telemetria](#10-telemetria)
11. [Backup e restauração](#11-backup-e-restauração)
12. [Atualizações de firmware](#12-atualizações-de-firmware)
13. [O console serial](#13-o-console-serial)
14. [Recuperação](#14-recuperação)
15. [Especificações](#15-especificações)
16. [Referência da API HTTP](#16-referência-da-api-http)
17. [SIMUT Air — o build a bateria](#17-simut-air--o-build-a-bateria)

---

## 1. O que é o SIMUT

Um datalogger de temperatura, umidade e pressão que roda inteiramente em um
único Raspberry Pi Pico W. Ele lê até dezesseis sensores, desenha-os em um
display touch, serve a própria interface web na sua LAN, mantém uma trilha de
auditoria e atualiza o próprio firmware pelo ar.

Não há componente em nuvem nem conta. A telemetria para um endpoint externo
existe, mas vem desligada, e o dispositivo é plenamente utilizável sem que
nunca lhe tenham dado um.

**O que ele não é.** Não é certificado para armazenamento regulado, não tem
sensoriamento redundante e não guarda um segundo slot de firmware para o qual
recuar. As seções abaixo são explícitas sobre cada um desses limites onde eles
importam.

### Os três builds

A mesma árvore de fontes produz três imagens. Elas compartilham o modelo de
sensores, o formato do histórico, a interface web e as permissões; o que muda é
o que está ligado e quanto tempo o aparelho fica acordado.

| Build | Display | Acordado | Para quê |
|---|---|---|---|
| **release** (`pico_w_release`) | TFT touch 320×240 | sempre | instalações na tomada, com alguém em frente ao aparelho |
| **alpha** (`pico_w_alpha`) | LCD de caracteres 16×2 | sempre | o mesmo, em hardware mais barato; ganha console Bluetooth e modo AP pelo próprio aparelho |
| **Air** (`pico_w_air`) | nenhum | ~13% do tempo | instalações a bateria que reportam e voltam a dormir — ver §17 |

O build alpha alterna os slots de sensor nas duas linhas do LCD, mostra o
progresso do boot como uma barra, e carrega um console Bluetooth porque não tem
painel touch por onde entrar em modo AP. O canto superior esquerdo é o `W` e um
ícone de sinal cujas barras enchem da esquerda para a direita; com um único
sensor, o canto inferior esquerdo mostra os registros de telemetria esperando
envio, como a barra superior do TFT — o número até 999, milhares inteiros com
`k` a partir daí, e nada quando a fila está vazia. Com mais de um sensor, esse
canto diz qual slot está na tela (`S0`, `S3`…). O Bluetooth dele fica **descobrível
por cinco minutos após o boot** e não mais, então uma unidade sem ninguém por
perto para de se anunciar; um celular já pareado continua conectando, e
reiniciar reabre a janela para parear outro.

### O projeto em um parágrafo

Dois cores com uma divisão estrita. O **Core 0** cuida dos sensores, do Wi-Fi,
do servidor web, da telemetria, do histórico e do console serial. O **Core 1**
não faz nada além de conduzir o display, lendo snapshots lock-free do estado
compartilhado. É essa divisão que faz uma rede movimentada não engasgar a tela
— e é também a origem da classe mais traiçoeira de bug do projeto, já que uma
escrita em flash precisa parar o Core 1 antes de apagar qualquer coisa de onde
ele possa estar executando.

---

## 2. Hardware

| Peça | Especificação |
|---|---|
| Microcontrolador | Raspberry Pi Pico W — RP2040, Cortex-M0+ duplo, 264 KB de SRAM, 2 MB de flash |
| Rádio | CYW43439 (Wi-Fi 2,4 GHz) — o blob do rádio ocupa ~232 KB do slot de aplicação |
| Display | TFT ILI9341 320×240 via SPI |
| Touch | Painel resistivo XPT2046 |
| Sensores | 16 slots em GPIO0–GPIO15 |
| Buzzer | Piezo passivo, acionado pelo PIO |
| Armazenamento | Flash interna: 1020 KB de aplicação, 1 MB de sistema de arquivos, 4 KB de metadados |

**Alocação de GPIO.** GPIO0–GPIO15 estão disponíveis para sensores. GPIO16 em
diante pertencem ao display, ao painel touch e ao buzzer, e o seletor de pinos
da interface web não os oferece.

Pinagem completa e notas de montagem: [WIRING.md](WIRING.md).

---

## 3. Primeira inicialização

1. **Grave o firmware.** Segure o BOOTSEL enquanto conecta o Pico ao USB e
   copie `simut_v2.3.2-beta.uf2` para a unidade `RPI-RP2` que aparecer. A placa
   reinicia sozinha no SIMUT.

2. **Anote a senha de admin.** Na primeira inicialização sem configuração
   armazenada, uma senha de admin aleatória de 8 caracteres é gerada e impressa
   **uma única vez** na serial USB a 115200 baud. Anote — ela é guardada apenas
   como hash com salt, e nada a recupera depois exceto um reset.

3. **Entre em uma rede.** Uma unidade sem rede configurada abre sozinha o
   ponto de acesso de configuração — `<nome>_SETUP`, WPA2, com a chave impressa
   no console USB e no display (§14) — e o portal em `http://192.168.4.1`
   recebe o nome e a senha da rede. O Air não abre sozinho: digite `ap` no
   console dele. Pela USB, `system ssid` e `system pass` fazem o mesmo. A
   imagem release responde a mDNS, então é alcançável em `http://simut.local`
   além de pelo IP. `show net status` na serial imprime o endereço, se você
   precisar.

   > O mDNS vem ligado e custa 15.272 B de flash — medido, linkando a imagem
   > das duas formas. Defina `SIMUT_MDNS=0` em `src/simut_config.h` para
   > removê-lo e alcançar o dispositivo somente por IP.

4. **Troque a senha.** O primeiro login na web é obrigado a passar por uma
   troca de senha antes que qualquer página carregue.

5. **Adicione sensores.** Na interface web, em **System Config → Sensors &
   GPIO**, adicione slots manualmente ou use **Scan for probes** para descobrir
   dispositivos 1-Wire em um pino.

**Um dispositivo de fábrica não provisiona sensor nenhum.** Os dezesseis slots
sobem vazios e não reivindicam GPIO algum. Isso mudou na v1.6.0-beta: o
firmware anterior pré-ativava o slot 10 como um DHT22 no GP10, o que tornava
esse pino inatribuível numa placa que não tinha sensor ali, e um reset de
fábrica o trazia de volta.

---

## 4. Sensores e o modelo de slots

### Um modelo, dezesseis slots intercambiáveis

Um slot é uma posição, não um papel. Qualquer slot aceita qualquer sensor
suportado, em qualquer combinação, e nenhum deles é especial. O que identifica
um sensor é o seu próprio **ID de hardware** — assim os offsets de calibração,
os limites de alarme e os registros de histórico seguem o dispositivo físico
que você ligou, não a posição em que o ligou.

| Tipo | Barramento | Canais | Pinos por slot |
|---|---|---|---|
| DS18B20 | 1-Wire | temperatura | 1 |
| DHT22 | fio único | temperatura, umidade | 1 |
| BME280 | I²C | temperatura, umidade, pressão | 2 (SDA, SCL) |
| BMP280 | I²C | temperatura, pressão | 2 (SDA, SCL) |

O BMP280 virou um tipo próprio na v1.6.0-beta. Antes disso ele compartilhava o
`TYPE_BME280`, que declara um canal de umidade que a peça não tem — de modo que,
qualquer que fosse o chip que você tinha, o firmware errava sobre um dos dois.

### Calibração

Cada sensor carrega uma curva de correção **por grandeza que mede**, definida
por até **5 pontos de calibração**. Um ponto emparelha a leitura bruta com o
valor que um instrumento confiável mostrou no mesmo instante. A correção é
interpolada entre os pontos e **mantida constante além do primeiro e do
último** — o dispositivo nunca extrapola uma inclinação fora da faixa que você
de fato mediu. Um ponto é o clássico offset constante; zero pontos significa
**nenhuma correção** (vale a saída do próprio sensor), e o editor diz isso
explicitamente.

Com 3+ pontos você escolhe a **interpolação** por grandeza: **Straight**
(linear por trechos, o padrão) ou **Smooth** (uma cúbica monótona — Fritsch–
Carlson/PCHIP — sobre os offsets). A Smooth dobra pelas âncoras sem jamais
ultrapassá-las: em todo intervalo a correção fica dentro da faixa que os dois
pontos vizinhos definem, e sua inclinação achata para zero na primeira e na
última âncora, de modo que ela encontra as zonas constantes sem um bico.
Splines que ultrapassam (Catmull-Rom, cúbica natural) foram recusadas por
princípio — uma ultrapassagem é uma correção maior do que qualquer coisa que o
instrumento de referência algum dia mostrou.

O editor fica no diálogo de slot da página `/config`, um bloco por grandeza: as
leituras bruta e corrigida lado a lado, as linhas de pontos, um botão de
captura que preenche o campo bruto com a leitura atual, e **Remove correction**
para voltar ao padrão do sensor. Um ponto cujo campo bruto fique vazio é
capturado da leitura ao vivo no momento em que você salva — esse é o
equivalente em um clique ao antigo fluxo de referência única. Os pontos
precisam ter valores brutos distintos (com duas casas decimais) e as duas
coordenadas precisam ficar dentro da faixa plausível da grandeza; o painel
avisa enquanto você digita, com as mesmas regras que o firmware aplica.

Tudo é armazenado em `/calib.csv`, chaveado pela ROM 1-Wire no caso de um
DS18B20 e pelo serial da placa + ID de hardware nas peças sem ROM. A linha
canônica é `key,id,name,raw,ref[,raw,ref,…]` — tudo o que uma linha tem a dizer
fica depois do nome, um número por coluna do CSV, de modo que uma planilha abre
o arquivo direto. Uma curva suave acrescenta uma célula `cub` logo depois do
nome (`key,id,name,cub,raw,ref,…`). Outros dois formatos coexistem,
distinguidos pela contagem de campos: `key,id,name` (uma linha de identidade de
DS18B20 sem correção) e o legado de 4 colunas `key,id,offset,name` escrito por
firmwares antigos, que é lido como o offset constante que sempre foi e é
carregado nesse formato até que pontos de verdade o substituam — um offset sem
âncora conhecida não tem células de ponto em que se transformar. Um firmware
antigo lendo uma linha de pontos enxerga **nenhuma** correção (nunca uma
errada).
Renomear um ID de hardware migra as linhas; remover uma correção apaga a linha,
exceto nas linhas de DS18B20, que servem também como o banco ROM→ID/nome que o
`sensor accept` lê.

**O pareamento do DS18B20 é automático.** Um DS18B20 provisionado pelo editor
de slots é salvo apenas com o GPIO; no reinício que segue o Save & Restart, o
firmware lê a ROM da sonda no fio, adota-a no slot e re-chaveia a linha do
sensor no `calib.csv` por esse número de série — migrando qualquer correção
salva enquanto o sensor estava não pareado. Desse boot em diante, a ROM é
verificada periodicamente e uma sonda trocada é posta em quarentena em vez de
silenciosamente se passar pela sonda calibrada. Uma sonda ausente no boot
simplesmente pareia no próximo reinício.

Dois DHT22 idênticos na mesma placa calibram de forma independente, o que não
era verdade antes da v1.6.0-beta: os offsets das peças sem ROM eram um único
par de linhas válido para o dispositivo inteiro, aplicado ao primeiro sensor
desse tipo que aparecesse na lista de runtime.

As correções se aplicam à leitura filtrada (depois da média aparada), de modo
que a rejeição de outliers sempre opera sobre valores físicos brutos, e todo
consumidor — display, histórico, alarmes, telemetria — enxerga o valor
corrigido.

A calibração exige a permissão `CALIB` e precisa do NTP sincronizado.

> **O `/calib.csv` não sobrevive a uma atualização de firmware.** Veja a
> [§12](#12-atualizações-de-firmware) para o que uma atualização preserva e o
> que ela não preserva.

### Pipeline de leitura

As leituras passam por uma janela deslizante de média aparada de 10 amostras
antes de chegar ao display, ao histórico ou à telemetria. A resolução do
DS18B20 (9–12 bits) e o intervalo de amostragem são definidos em **System
Config → Hardware & Sampling**.

---

## 5. O display do dispositivo

O painel é 320×240 com uma camada touch resistiva. O firmware tem 21 modos de
interface distintos. Um mapa visual completo — cada tela, com a rota exata para
alcançá-la — é gerado a partir de um dispositivo real pelo
[`tools/screen_mapper.py`](../tools/screen_mapper.py) e publicado em
[docs/images/screens/screens.md](images/screens/screens.md).

A interface inteira foi redesenhada visualmente na v2.1.5 (widgets, acentos
Latin-1, renderização composta por DMA) e endurecida na v2.1.10 para que toda
tela mantenha o conteúdo dentro de uma área segura de 4 px — o offset de
alinhamento de tela (±4 px por eixo, Settings → Screen alignment) pode deslocar
a imagem sem nunca cortar nada.

### Dashboard

Dois cards de sensor — um painel superior e um inferior — acima de um rodapé de
até cinco botões. Os botões do rodapé selecionam slots, paginam entre eles
quando há mais de quatro ativos e abrem as configurações (**CFG**).

- **Toque em um card de sensor** para alternar a visão de mín/máx — em
  qualquer ponto do card, nos dois painéis.
- **Segure o card superior por três segundos** para fixá-lo no sensor que ele
  mostra, ou para soltá-lo. Solto, o painel superior segue o slot selecionado
  no rodapé; fixado, fica num sensor só enquanto o rodapé move o painel
  inferior. Fixar também sai do mín/máx. (Até a v2.4.9-beta um toque rápido na
  borda direita do card superior fazia isso também — não faz mais.)
- **Toque no ícone de gráfico** na visão de mín/máx para abrir o histórico
  daquele sensor.
- **Toque em CFG** para chegar às configurações — isso pede o **seu PIN**.

### Primeiro a conta, depois o PIN

O CFG abre a **lista de contas**: escolha a sua e só então digite o PIN. Cada
conta tem o seu PIN; o admin de fábrica começa com `1234` e é obrigado a
trocá-lo no primeiro acesso (um aparelho atualizado herda o PIN do display que
tinha, se era numérico).

> **Por que a conta vem antes (config v25).** Até a v24 o PIN *era* a
> identidade: o painel percorria a tabela inteira procurando a conta a que a
> sequência pertencia. Duas consequências ruins vinham daí. Um palpite cego
> valia contra **todas** as contas de uma vez — com a tabela cheia e 4 dígitos,
> **20,2 %** por tentativa. E quando duas contas casavam com a mesma sequência,
> **nenhuma** entrava, porque o painel não tinha como perguntar qual era: 15 %
> dos logins, medidos na bancada. Escolhendo a conta antes, o aparelho verifica
> **um** digest: o palpite cai para **0,81 %** e a ambiguidade deixa de existir
> por construção.

Uma conta bloqueada aparece marcada **na lista**, antes do PIN — a diferença
entre "você digitou errado" e "esta conta acabou as tentativas".

**A escada de bloqueio é por conta**, com um teto de painel por cima: a 3ª
falha espera 5 s, a 4ª 15 s, a 5ª 60 s, e a **6ª tranca aquela conta** até o
próximo reboot. Vinte falhas somadas trancam **o painel inteiro**, também até
reiniciar. As duas coisas são necessárias: só por painel (o que a v24 tinha),
seis toques errados de qualquer um fechavam o painel de todo mundo; só por
conta, um atacante ganharia 32 × 6 tentativas.

### O teclado: embaralhado ou ordenado, conforme a política

Um teclado fixo entrega o PIN a quem olha por cima do ombro — as posições dos
dedos são sempre as mesmas. Então o painel pode distribuir o alfabeto em
**cartões de 2 ou 3 glifos** e **sortear de novo a cada toque**: o cartão
inteiro é um botão, o toque diz apenas *"é um destes"*, e nem o aparelho fica
sabendo qual. Dois toques no mesmo lugar não são os mesmos glifos, então quem
observa não consegue nem dizer se dois caracteres do PIN são iguais.

Com **dígitos e 3 glifos** (o padrão, e o que a v24 fazia) são quatro cartões;
as duas posições que sobram levam um **símbolo** de enchimento, para que todo
cartão mostre três glifos e a largura não diga nada.

**Com 1 glifo por tecla não há sorteio.** Um conjunto de um não esconde nada de
quem lê a tela — o caractere está escrito na tecla pressionada —, então
embaralhar só custaria a memória muscular do operador. Nesse modo o teclado é
**ordenado**: o pad numérico de sempre para `0-9`, ou um **alfanumérico de dois
toques** (nove grupos, `0-9 ABC … WXYZ`, e o caractere no popup) para `0-9A-Z`.

![PIN](images/screens/panel-pin-keypad.png) ![PIN inválido](images/screens/panel-pin-invalid.png)

> **Ao DEFINIR um PIN o teclado é sempre o ordenado**, qualquer que seja a
> política. Embaralhar serve para esconder um PIN que alguém já tem de quem
> está olhando; escolher um é o problema oposto, e caçar o caractere num
> sorteio só custa toques.

#### A política, e o que cada eixo compra

`user policy <min> <teclado> <alfabeto>` no CLI, o item **Política de PIN** no
painel, ou a página de configuração na web.

| eixo | efeito | preço |
|---|---|---|
| **alfabeto** `0-9` → `0-9A-Z` | palpite cego 168× mais caro com 4 caracteres | nenhum: a busca não fica mais cara |
| **teclado** 3 → 2 → 1 glifo | menos candidatos por toque ⇒ palpite mais caro | quem observa passa a ver mais |
| **comprimento** | cada caractere multiplica | toques, e **CPU** |

Atenção: o teclado é **soma zero**: o conjunto que esconde o caractere de quem observa
é o mesmo que a busca tem de percorrer. O **alfabeto** é o único eixo que não é
troca.

**O teto de comprimento é de CPU, não de gosto.** A árvore de toques custa
`S+S²+…+Sⁿ` SHA-256, medidos em **36,6 µs** cada no ferro, e o Core 0 fica
parado nisso — o servidor web espera atrás. Daí **16/12/8** caracteres para 1/2/3
glifos por tecla. Com 3 glifos, 10 toques seriam 3,2 s e 16 seriam 39 minutos.

**Apertar a política marca toda conta que tem PIN para trocá-lo** no próximo
acesso. É inevitável: o aparelho guarda apenas o *digest* e não tem como saber
se um PIN antigo ainda cabe na regra nova.

Quem define PINs: o próprio usuário (item **Alterar Senha**), um administrador
no item **Usuários** do painel, a página `/users` da web ou `user pin` no CLI.
O rodapé é o mesmo das outras telas do painel: ⌫, **SAIR** e **ENTRAR**. SAIR
responde até durante um bloqueio, porque é a saída da tela. (A licença tinha um
botão aqui e não tem mais — ela é um item do menu.)

### Configurações

Alcançadas pelo CFG, depois do PIN. O título diz **quem entrou** —
"Configurações > *nome*" — porque a sessão do painel dura até sair da árvore e
tudo o que for feito nela sai assinado com esse nome. O menu lista **só o que a
conta pode**:
temas, sons, idioma, calibração e alinhamento pedem `SYS_CONFIG`; **Alarmes**
pede qualquer um dos três bits do painel; **Usuários** pede `USER_MGR`; PIN,
licença e status são de todos. Um operador com os bits de alarme vê quatro
itens; o admin vê os dez.

![menu do operador](images/screens/panel-menu-operator.png)

### Alarmes, por sensor e por bit

A lista de sensores abre, para o sensor selecionado, um menu com três linhas —
cada uma atrás do seu próprio bit, e uma linha sem o bit aparece apagada com
um cadeado:

| Linha | Bit | O que faz |
|---|---|---|
| **Limites de alarme** | `0x0400` | o editor de limites de sempre; SALVAR grava e manda `alarm_lim` com `lo`/`hi` |
| **Alarmes SIM/NÃO** | `0x0800` | liga/desliga os alarmes do sensor; manda `alarm_on`/`alarm_off` |
| **Manutenção** | `0x1000` | abre uma janela em **horas e minutos** (teto 30 dias); dentro dela o sensor não gera limite nem falha e o painel/cigarra ficam calados; mostra o tempo restante e FECHAR encerra antes da hora |

![menu do sensor](images/screens/panel-sensor-menu.png) ![manutenção](images/screens/panel-maint-entry.png) ![restante](images/screens/panel-maint-remaining.png) ![só manutenção](images/screens/panel-sensor-menu-maint-only.png)

Toda ação é do usuário identificado: vai para o log de eventos com
`ctx = conta×100 + slot` e para a 2ª linha de telemetria com `"user"` (ver
§10 e `docs/API_POST.md`). O **Desativar** do pop-up de alarme também pede o PIN
e o bit de bloqueio.

### Usuários

Item do menu para quem tem `USER_MGR`. Lista as contas — **menos o admin**,
que não tem nada aqui que se possa mudar: os bits dele são todos, ele não é
excluível, e o PIN dele é o item **Alterar Senha** do próprio menu dele. As
letras **L B M** dizem quais dos três bits do painel a conta tem; um ponto
depois do nome diz que ela tem PIN. **NOVO** cria uma conta em três telas:
nome (teclado), os três bits, PIN duas vezes. Uma conta criada aqui é **só do
painel** — não entra na web até um administrador lhe dar um bit de página e
resetar a senha. Selecionar uma conta abre o editor: os bits, **Definir PIN**
e **Excluir usuário** (com confirmação). Um PIN que já é de outra conta é
recusado na hora.

![usuários](images/screens/panel-users-list.png) ![novo usuário](images/screens/panel-new-user-keyboard.png) ![bits](images/screens/panel-new-user-bits.png) ![PIN em uso](images/screens/panel-pin-in-use.png) ![excluir](images/screens/panel-delete-confirm.png)

Desde a v2.1.9 o teclado de texto (hoje usado para o nome) é para a ponta do
dedo: oito teclas grandes de grupo abrem um popup com as duas caixas ao mesmo
tempo, de modo que qualquer um dos 91 caracteres aceitos custa dois toques.

O **System status** é a tela que vale conhecer: nome do dispositivo, versão do
firmware, serial da placa, uptime, heap livre, uso da flash e temperatura da
placa — a forma mais rápida de confirmar o que um dispositivo está realmente
rodando.

### Temas

O build de release compila um tema, mas o dispositivo não está limitado a ele.
**Até oito temas customizados** vivem no sistema de arquivos como arquivos
`.thm` em `/themes` — texto puro, uma cor por papel (24 papéis, cobrindo cada
elemento que o display desenha: cromo, valores e as cores de estado de
alarme/atenção/seleção, mais os carimbos de data do gráfico), escritos como
`#RRGGBB` ou `0xRRGGBB`. Chaves ausentes caem para valores de estoque seguros,
então arquivos antigos de 17 cores continuam válidos. Temas prontos acompanham
o projeto em [`data/themes/`](../data/themes/) — envie os que quiser pela
página `/files`.

Escreva os seus com o editor em
[`tools/theme-editor/`](../tools/theme-editor/). É um pequeno app web que faz
login no dispositivo, envia um tema de prévia, aplica-o e o apaga em seguida —
de modo que o painel à sua frente repinta enquanto você escolhe as cores, em
vez de depois de um ciclo de upload e reboot.

Outros quarenta e nove temas existem como pacotes de build em
`src/simut_config.h` (`SIMUT_THEMES_HEALTH`, `_PRO`, `_MEDICAL`, `_SAFETY`,
`_RETRO`, `_NATURE`, `_UTILITY`). Todos os sete vêm comentados por padrão;
descomentar um compila suas paletas a aproximadamente 85 bytes cada. Toda
paleta embutida passa pela mesma auditoria de contraste da coleção curada
(texto pequeno ≥ 4,5:1, valores ≥ 3:1 contra os fundos reais).

### Histórico

A visão de gráfico plota um sensor ao longo de um intervalo selecionável
(1H · 6H · 12H · 24H · 7D), com navegação para trás e para frente no tempo,
um seletor de calendário e zoom. Desde a v2.1.8 o gráfico agrega em baldes de
tempo com uma banda real de mín/máx em torno da linha da média — um pico de um
minuto não pode ser amostrado para fora da figura — e sensores de pressão
ganham um segundo eixo em hPa (v2.1.7). Uma tela de detalhe numérico dá o
máximo, o mínimo, a média e o desvio padrão do intervalo em tela.

### Enquanto a web segura o dispositivo

Quando um cliente web está executando uma operação longa — transmitindo o
histórico, exportando logs — a barra superior mostra o usuário que o está
segurando e **o touch é rejeitado no dashboard** até que ela termine. O aviso é
deliberado: ele conta o porquê antes de você tocar, não depois.

---

## 6. A interface web

Servida do próprio dispositivo. Faça login em `http://simut.local` ou no IP do
dispositivo.

| Página | O que faz |
|---|---|
| `/` | Dashboard: estatísticas do sistema, uso de memória e de flash, tabela de sensores ao vivo e um painel de captura do display que lê a tela física |
| `/config` | Identidade do dispositivo, data e hora, hardware e amostragem, o mapa de GPIO e os slots de sensores, telemetria |
| `/network` | Wi-Fi (com busca das redes ao alcance), endereçamento estático, mDNS, NTP |
| `/alarms` | Limites e ações por sensor |
| `/users` | Contas e permissões |
| `/files` | Navegador do sistema de arquivos: upload, download, exclusão, criação de diretórios — mais backup completo, restauração e atualização de firmware (OTA) |
| `/history` | Gráficos de histórico, exportação CSV e o visualizador do log de eventos do sistema |
| `/license` | Texto da licença |

### Escolher a rede Wi-Fi em vez de digitá-la

**Buscar**, ao lado do campo SSID em `/network`, lista o que o rádio ouve:
nome, se é protegida e o sinal. Tocar numa linha preenche o SSID e põe o cursor
na senha. A lista guarda doze redes em ordem de sinal; uma rede que responde
por vários rádios (malha) aparece **uma vez**, no mais forte, e redes ocultas
não aparecem — não há o que tocar, e o SSID delas continua sendo digitado.

Funciona **no modo AP**, que é o caso para o qual isso foi feito: um aparelho
nunca configurado serve a página de configuração pelo próprio ponto de acesso,
e esse é justamente o momento em que ele com certeza **não** está na rede cujo
nome está pedindo. A varredura precisa da interface estação, que o modo AP
deixa desligada, então o aparelho a levanta **ao lado** do ponto de acesso — a
página que você está lendo continua de pé. Na bancada, duas varreduras de
dentro do AP levaram 0,93 s cada e não perderam **nenhuma** consulta; a página
ainda tolera perder algumas, porque um rádio fora do canal é um rádio que não
está servindo a página.

O sucesso rotineiro da busca não vai para o log de eventos: `SYS_WIFI_SCAN` é
filtrado por transição dentro da família, e a varredura de reconexão já disparou
esse código antes de alguém abrir a página. Uma busca **recusada** pelo driver,
ou que nunca termina, é WARN e sempre fica registrada.

Se voltar **Rádio ocupado**, quem está com o rádio é a reconexão — o aparelho
está procurando a própria rede — e a busca é recusada em vez de enfileirada.
Tente de novo em instantes.

### As alterações ficam pendentes até você mandar aplicar

Editar um campo não muda nada no aparelho: a página **encena** a alteração e
uma barra no topo diz o que fazer com ela. Quantos botões aparecem depende da
resposta do **aparelho**, não da página.

| botão | o que faz |
|---|---|
| **Salvar e reiniciar** | grava e reinicia — sempre, mesmo que a mudança não exigisse |
| **Aplicar agora** | grava e aplica **sem reiniciar** |
| **Testar** | aplica **sem gravar**: um reinício desfaz, e o pendente continua lá para você salvar ou descartar |

Os dois últimos só aparecem quando a mudança **pode** ser aplicada ao vivo —
limites de alarme, manutenção, 2ª linha de alarmes, telemetria pelo lado HTTP,
tema e idioma. Rede, contas, provisionamento de sensor, fuso e a própria
política de PIN continuam exigindo reinício, e nesse caso a barra mostra só
"Salvar e reiniciar" e diz **por quê**.

> A página **pergunta ao aparelho** (um ensaio que não muda nada) em vez de
> decidir sozinha. A regra é comparar a configuração encenada com a corrente,
> e só o aparelho sabe fazer isso: um campo digitado de volta ao valor atual
> não é mudança nenhuma.

**Testar** não sobrevive a uma queda de energia — é para experimentar um
limite, não para configurar. Enquanto o pendente estiver lá, a barra continua
visível.

### Autenticação

O login é uma troca em duas etapas: o navegador busca um nonce em
`/api/login_init`, faz o hash da senha no lado do cliente e posta o hash junto
com o nonce. A sessão é um cookie `SIMUTSESS`.

Dois detalhes importam se você for automatizar contra ele:

- A página faz o hash de **cada unidade de código UTF-16 como um byte** — isso
  é latin-1, não UTF-8. Uma senha que contenha caracteres acima de U+00FF não
  pode ser reproduzida por um hash UTF-8.
- Falhas repetidas disparam um bloqueio exponencial medido em segundos.

### Servir a interface por HTTPS

O servidor web roda HTTPS quando um par de certificados está provisionado, e
HTTP puro caso contrário. Gere um par por dispositivo na sua estação de
trabalho (EC P-256 de propósito — o handshake dela cabe neste heap, onde o de
uma RSA-2048 não caberia):

```bash
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout web_key.pem -out web_cert.pem -days 3650 -nodes -subj "/CN=simut"
```

O par mora em `/config/web_cert.pem` e `/config/web_key.pem`, e há dois jeitos
de colocá-lo lá.

**Num dispositivo em serviço — `POST /api/tls`.** Só admin, o mesmo portão de
um apply de OTA, porque um certificado decide em quem o navegador confia a
partir do próximo boot. Mande os dois blocos PEM concatenados, em qualquer
ordem:

```bash
python3 tools/install_tls_cert.py --host 192.168.1.50 \
  --cert web_cert.pem --key web_key.pem --reboot
# ou na mão, já autenticado:
cat web_cert.pem web_key.pem | curl -X POST --data-binary @- \
  -H 'Content-Type: application/x-pem-file' http://192.168.1.50/api/tls
```

O dispositivo recusa o par a menos que ele decodifique **e a chave pertença ao
certificado** — ele deriva a chave pública da privada e compara com a do
certificado, então um par trocado vira um `400` agora, em vez de um HTTPS que
simplesmente não sobe no próximo boot com o par que funcionava já apagado. Uma
chave cifrada por senha é identificada como tal, não chamada de inválida. A
resposta diz o que o próximo boot vai servir; o servidor em execução não é
trocado por baixo de você, então reinicie quando lhe convier.

Essa é a única rota que escreve em `/config`, e é estreita de propósito: dois
caminhos fixos, nenhum nome de arquivo vindo da requisição. A página Files
continua recusando `/config` por inteiro — desde a auditoria de 29/08/2026,
porque um par forjado no cofre de credenciais é um homem-no-meio na sessão do
admin depois do próximo reboot — e o manual apontou para ela mesmo assim por um
mês ([#133](https://github.com/angeloINTJ/simut/issues/133)).

**Numa unidade nova — na primeira gravação.** Coloque os dois arquivos em
`data/config/` e envie a imagem do sistema de arquivos com `pio run -t
uploadfs`, que reformata a partição e por isso só serve para uma unidade que
não tem nada a perder. Com a porta web no padrão
80, o listener HTTPS se move para a 443, então `https://<ip-do-dispositivo>`
funciona; uma porta configurada explicitamente é honrada como está. A chave
privada nunca é servida pelo `/download`, e o `system format` a apaga junto com
o resto de `/config`.

O que esperar:

- O certificado é autoassinado, então o navegador avisa uma vez — inspecione e
  aceite. O cookie de sessão ganha a flag `Secure`.
- O HTTP puro para de responder: há um único servidor e agora ele fala TLS. Um
  handshake custa cerca de 0,5–0,7 s neste chip, e **um cliente TLS é atendido
  por vez** — uma segunda conexão simultânea é descartada.
- Um par ausente ou impossível de interpretar nunca consegue trancar você do
  lado de fora: o dispositivo cai para HTTP puro na porta configurada. Para
  desligar o HTTPS, rode `system https off confirm` no console USB (§13) — ele
  apaga o par e reinicia. Sobrescrever a chave pela página Files é recusado
  pelo mesmo guarda que bloqueia enviar uma.
- As atualizações de firmware ainda são melhor feitas por HTTP puro (§12):
  passar uma imagem de ~1 MB pelo TLS é lento neste chip e os caminhos de
  recuperação documentados pressupõem HTTP.
- **Desligar o HTTPS, no mesmo navegador:** uma vez que você tenha entrado por
  HTTPS, o cookie de sessão carrega a flag `Secure`, e os navegadores se recusam
  a enviar ou sobrescrever um cookie `Secure` a partir de uma página `http://`
  pura. Assim, o primeiro login depois de voltar para HTTP pode quicar direto de
  volta para a tela de login — o login foi aceito, mas nenhum cookie de sessão
  chegou ao dispositivo. A página de login detecta isso e avisa; a correção é
  abrir uma janela anônima ou limpar os cookies deste site (o cookie de sessão é
  de sessão, então simplesmente fechar e reabrir o navegador também o limpa).

### Captura do display

`GET /api/screenshot` devolve um BMP 320×240 de 24 bits lido de volta do
framebuffer do painel pelo SPI. É a tela real, e não uma re-renderização, e é a
partir dela que o mapa de telas da §5 é construído.

O botão **Ao vivo**, ao lado da captura, usa `GET /api/screen_stream`: o mesmo
painel, mas um quadro por requisição em faixas de 8 linhas, cada uma comprimida
por RLE de paleta (1 byte de cor, 1 byte de contagem) ou enviada crua, o que for
menor. Um quadro médio sai em 9,5 kB no lugar dos 230 kB do BMP, e o espelho
anda perto de 1 quadro por segundo porque lê cada linha uma vez — a captura BMP
lê três vezes e vota, que é o que a torna a referência forense e a torna lenta.
Um pixel ocasional errado é o preço do espelho; para conferir cores, use a
captura.

Com o espelho ligado, **clicar nele toca o painel**: a página converte o clique
para coordenada de painel e chama `POST /api/touch` com `x` e `y`. É o mesmo
`touch sim` do console, e obedece ao mesmo teclado de PIN — tocar em Ajustes
pede a senha do display como pediria para um dedo.

Segurar o clique — ou o dedo, numa tela de toque — faz um **toque longo**. A
página mede o tempo e desenha um anel que fica verde no limiar de 3 s do painel;
ao soltar, manda a duração como `ms`, e o aparelho segura o toque esse tempo (de
100 a 15000 ms) — o que o `touch hold` do console faz. Menos de 250 ms é um
toque comum. O aparelho reproduz o toque depois que você solta, então a tela só
reage aí.

Atenção: quem chamar `/api/touch` por fora da página precisa **esperar ~600 ms antes
de pedir o próximo quadro**. Um toque vira um evento que o Core 0 consome no
laço dele, e uma captura ocupa esse mesmo core: pedir o quadro logo em seguida
fotografa a tela antes da transição. A espera não pode morar no aparelho
justamente porque é o aparelho que fica bloqueado durante a requisição.

---

## 7. Alarmes

Cada slot de sensor carrega os próprios limites e é habilitado
independentemente. Os limites são definidos na interface web em `/alarms`, ou
no dispositivo em **Settings → Alarm Limits** — selecione uma linha e toque na
zona ON/OFF dela para abrir o editor.

Um alarme em curso levanta o buzzer, a menos que esteja mudo, marca o sensor no
dashboard e escreve um registro no log de auditoria.

O **mudo global** fica no dispositivo em **Settings → Alarm Sounds** e pede
confirmação, porque silencia todos os canais de alarme de uma vez.

---

## 8. Histórico e logs

### Registros de histórico

As leituras são escritas em `/history/YYYYMMDD.h5` num formato binário compacto
(**V5**). O intervalo de gravação é de um minuto por padrão e configurável de 1
a 1440.

Os registros V5 são chaveados por **slot × canal**, não por ID de hardware —
renomear um ID não interrompe mais a gravação (isso era um comportamento da V4).
Onde um renomeio morde hoje é na **calibração**: as linhas do `/calib.csv` dos
sensores sem ROM são chaveadas por ID de hardware, então renomeie pelo editor
de slots (que migra as linhas) em vez de editar arquivos. Um slot adicionado ou
renomeado hoje ainda precisa do `/api/history_rebind` (o botão no editor de
slots) para ganhar a sua coluna no arquivo do dia que congelou o schema à
meia-noite.

A exportação está disponível como CSV em `/history`: desde a v2.1.8 a página
baixa os arquivos `.h5` brutos do dia (mais a hora aberta via
`/api/history/open`) e tanto a decimação do gráfico quanto a decodificação do
CSV acontecem no navegador — o dispositivo só serve bytes. O endpoint do pacote
`.simx` `/api/export/history.bin` continua alcançável por URL para scripts, mas
não é mais o caminho do botão de CSV, e ele para na última hora selada.

### Log de eventos

A trilha de auditoria é um log binário persistente de registros de 12 bytes:

| Campo | Bytes | Observações |
|---|---|---|
| epoch | 4 | carimbo de tempo absoluto |
| uptime | 3 | **segundos**, repartido em dois campos, saturando em ~194 dias |
| code | 2 | código numérico do evento |
| context | 2 | específico do código |
| flags | 1 | nível e módulo |

A coluna de uptime guardava horas inteiras até a v1.6.2-beta, o que significava
que qualquer dispositivo que reiniciasse mais de uma vez por hora escrevia zero
em todo registro que algum dia fez. **Os registros escritos por firmwares
antigos leem o antigo campo de horas como segundos** — na prática, zero, que é
o que aquele campo já continha.

O log é visualizável em `/history`, exportável como CSV e despejável pelo
console serial com `show system log`. Note que o despejo serial imprime o
código numérico e o contexto, **não texto livre**: a mensagem descritiva de um
evento existe apenas na saída serial ao vivo no momento em que ele acontece.

---

## 9. Usuários e permissões

Trinta e duas contas no máximo (config v24; eram cinco). Três sessões web
podem estar ativas ao mesmo tempo. As senhas são hasheadas com um salt
aleatório por usuário; o PIN do painel, com um salt do aparelho, porque o
painel identifica pelo PIN e compara um digest só contra todas as contas.

Treze bits de permissão, concedidos de forma independente:

| Bit | Permissão | Concede |
|---|---|---|
| `0x0001` | DASHBOARD | Ver as leituras ao vivo |
| `0x0002` | HISTORY | Ver e exportar o histórico |
| `0x0004` | LOGS | Ver o log de eventos |
| `0x0008` | SYS_CONFIG | Configuração do dispositivo e da amostragem |
| `0x0010` | NET_CONFIG | Configuração de rede |
| `0x0020` | FILE_READ | Navegar e baixar arquivos |
| `0x0040` | FILE_UPLOAD | Enviar arquivos |
| `0x0080` | FILE_DELETE | Excluir arquivos |
| `0x0100` | USER_MGR | Gerenciar contas |
| `0x0200` | CALIB | Calibrar sensores |
| `0x0400` | ALARM_LIMITS | **Painel:** editar limites de alarme |
| `0x0800` | ALARM_BLOCK | **Painel:** ligar/desligar os alarmes de um sensor |
| `0x1000` | MAINT | **Painel:** abrir/encerrar manutenção |

Os três últimos valem no painel (§5); na web a seção de alarmes segue com
`SYS_CONFIG`. Cada conta pode ter um **PIN do painel** (4–8 dígitos, único
entre as contas).

**Admin é todos os bits ligados.** Três operações exigem admin completo em vez
de um único bit: colocar uma imagem de firmware em staging
(`/api/restore?op=stage`), aplicá-la (`/api/ota/apply`) e baixar o backup
completo (`GET /api/backup`).

---

## 10. Telemetria

Desligada por padrão. Quando habilitada, o dispositivo posta as leituras para
um endpoint que você especifica.

| Ajuste | Opções |
|---|---|
| Transporte | HTTP POST ou MQTT |
| Payload | JSON, CSV ou um template customizado |
| Segurança | TLS suportado |
| Disparo | Um lote mínimo: o aparelho transmite quando essa quantidade de registros está esperando (0 desliga a telemetria) |
| Tamanho do envio | Um lote máximo: uma fila maior sai em lotes de até esse tamanho até acabar |
| Home Assistant Discovery | Somente MQTT, checkbox opt-in |
| Syslog remoto | RFC 5424 sobre UDP, opt-in (veja abaixo) |

### Home Assistant Discovery

Com o transporte MQTT e o payload JSON selecionados, marcar **Home Assistant
Discovery** faz o dispositivo publicar mensagens de configuração retidas do
[MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
a cada conexão com o broker. O Home Assistant então cria o dispositivo e uma
entidade de sensor por medição automaticamente — temperatura e umidade por slot
ativo, mais pressão — com a disponibilidade guiada pela mensagem *will*
existente em `<base do tópico>/status`. Nenhum YAML é necessário do lado do HA.

As entidades aparecem depois do primeiro envio que segue um save (salvar
reinicia o dispositivo, e as configurações pegam carona na próxima conexão com
o broker). Desmarcar a caixa publica payloads retidos vazios nos mesmos tópicos
na conexão seguinte, o que remove as entidades do Home Assistant. Renomear o ID
de hardware de um sensor o re-registra sob o novo id; a entidade antiga
permanece até que o tópico retido seja limpo no broker ou o HA a remova
manualmente.

### Métricas Prometheus

`GET /metrics` serve o formato de exposição em texto do Prometheus: leituras ao
vivo por slot (temperatura/umidade/pressão com os rótulos `slot`/`hwid`/`name`),
gauges de heap e de sistema de arquivos, estado de WiFi/MQTT, os contadores de
telemetria e os contadores de operações de flash / ciclo de vida do Core 1.
Este é o complemento **pull** da telemetria push acima: o dispositivo não
armazena nem repete nada — o Prometheus é dono da retenção, da plotagem
(Grafana) e do alerta, e uma coleta que falha aparece do lado dele como
`up == 0`.

Um coletor não tem como executar o fluxo de login, então, além do cookie de
sessão normal, a rota aceita **HTTP Basic** com um usuário e a senha **em
claro** de qualquer conta que tenha a permissão de dashboard. Credenciais
erradas alimentam o mesmo bloqueio exponencial por IP do formulário de login.
Cada coleta verifica a senha por inteiro (~0,7 s no dispositivo), então mantenha
o `scrape_interval` em 15 s ou mais:

```yaml
scrape_configs:
  - job_name: simut
    scrape_interval: 30s
    basic_auth:
      username: admin
      password: <sua senha>
    static_configs:
      - targets: ["<ip-do-dispositivo>"]
```

### Syslog remoto (trilha de auditoria)

Desligado por padrão. Quando habilitado (System Settings → *Remote Syslog*), o
dispositivo encaminha cada evento de log como uma mensagem
[RFC 5424](https://www.rfc-editor.org/rfc/rfc5424) por **UDP** para um coletor
syslog ou SIEM. Esta é a trilha de auditoria de que uma instalação regulada
precisa: o log de eventos no dispositivo vive num anel rotativo de no máximo
~1600 registros, então uma cópia que sai da caixa, somente-acréscimo, é o que
um auditor de fato aceita.

| Ajuste | Significado |
|---|---|
| IP do coletor | O endereço **IPv4 na LAN** do SIEM — um hostname não é aceito (veja abaixo) |
| Porta UDP | Padrão 514 |
| Nível mínimo | Só os registros neste nível ou acima são encaminhados (Debug/Info/Warning/Error/Fatal) |

Ele **não** é um segundo transporte de telemetria, de propósito. UDP é
dispara-e-esquece: não há handshake, não há cliente TLS, não há cursor em flash
nem estado de reconexão — nada da maquinaria (ou dos modos de falha) que o envio
de telemetria carrega. O dispositivo nunca repete um datagrama e nunca bloqueia
uma leitura por causa de um; um `WARN`/`FATAL` levantado logo antes de um reboot
é despejado na saída, mas um travamento duro dos dois cores não salva nada, e o
syslog não promete entrega, por design.

O coletor é um **endereço IPv4, não um hostname**: o ajuste vive num campo de 8
bytes, sem espaço para um nome de 64 caracteres, um coletor na mesma LAN é
endereçado por IP na prática, e isso evita um caminho de falha de resolução DNS
no laço quente de log.

Cada linha mapeia mecanicamente para a RFC 5424: o nível do SIMUT vira a
severidade do syslog (facility `local0`), a tag (`NET`, `CLI`, …) vira o
APP-NAME, o código numérico do log vira o MSGID (estável e independente de
idioma — mapeie-o de volta com a tabela de códigos abaixo), e o contexto/core/
uptime viajam num elemento de structured-data. **Antes de o relógio
sincronizar**, o timestamp é o NILVALUE `-` da RFC 5424 em vez da data
provisória do build epoch, de modo que uma linha nunca chega ao SIEM carimbada
no passado. Um registro higienizado para caber em um datagrama:

```
<132>1 2026-08-19T17:04:00Z picofridge NET - 524 [simut@32473 ctx="-18" core="0" up="12345"] Provisional time in use
```

O ID do structured-data usa o enterprise number `32473` — o valor que a IANA
reserva para exemplos — porque o SIMUT não tem um PEN registrado; um site que
registre o seu troca essa única constante.

### Tokens de template

| Token | Resolve para |
|---|---|
| `{TS}` | Carimbo de tempo |
| `{DEV}` | Nome do dispositivo |
| `{t0}`…`{t15}` | Temperatura do slot N |
| `{u0}`…`{u15}` | Umidade do slot N |
| `{p0}`…`{p15}` | Pressão do slot N |
| `{DHT_ID}` | ID de hardware do sensor DHT |

Os tokens `{tAMB}`, `{uAMB}` e `{pAMB}` foram removidos na v1.6.0-beta junto
com o slot ambiente privilegiado pelo qual eles resolviam. Use os tokens de
slot numerados em vez deles.

Os registros que não podem ser entregues ficam enfileirados; o dashboard mostra
a contagem de pendentes.

---

## 11. Backup e restauração

`GET /api/backup` baixa o sistema de arquivos inteiro como um único `.bkp`. O
formato carrega um CRC32 sobre o payload e é **amarrado ao chip ID**, de modo
que uma imagem não pode ser restaurada numa placa diferente por acidente.

A restauração é `POST /api/restore` — `op=validate` confere uma imagem sem
escrever, `op=apply` a escreve. Um apply bem-sucedido reinicia o dispositivo
para que nada guarde um cache velho do que havia na flash.

**Faça um backup antes de toda atualização de firmware.** A §12 explica por
quê.

---

## 12. Atualizações de firmware

### Leia isto primeiro

**As atualizações pelo ar funcionam da v1.6.2-beta em diante, e só de lá.**
Todo build anterior trazia um aplicador cuja alimentação do watchdog escrevia o
bit de reset em vez de recarregar o contador: ele reiniciava o chip antes de
copiar um único setor, enquanto toda camada acima dele reportava sucesso. O
sintoma era um dispositivo que anunciava uma atualização bem-sucedida e seguia
rodando o firmware antigo.

Um dispositivo que já esteja na v1.6.2-beta ou posterior consegue receber esta
versão pelo ar. Qualquer coisa mais antiga ainda está rodando o aplicador
quebrado e não tem caminho pelo ar para sair dele: grave a v1.6.2-beta ou
posterior pelo USB uma vez, e as atualizações funcionam normalmente daí em
diante.

### O que uma atualização destrói

O staging divide a partição de flash com o sistema de arquivos, então uma
atualização **o reformata**. Um snapshot leva o `/config/system.bin` para o
outro lado — credenciais de Wi-Fi, usuários e slots de sensores sobrevivem
automaticamente, e o dispositivo volta para a rede sem assistência.

Nada mais sobrevive. **Pacotes de idioma, o `/calib.csv` e todo o histórico
armazenado se perdem.** Baixe um backup antes.

### Não existe rollback

O slot de aplicação é único. A imagem é validada antes de ser gravada e
verificada de novo no boot seguinte, mas se uma imagem ruim bootar mal não há
um segundo slot para o qual recuar — a recuperação é o botão BOOTSEL e um cabo
USB. Veja [RECOVERY.md](RECOVERY.md).

### O procedimento

Pela interface web: o painel de atualização de firmware na página **`/files`**,
ao lado de Backup e Restore. Ou diretamente:

```bash
# 1. Stage — envia e valida. ~29 s para uma imagem de 957 KB.
curl -b cookies.txt -F "file=@simut_v2.3.2-beta.bin" \
     "http://simut.local/api/restore?op=stage&commit=1"
# -> {"st":5,"bytes":957696,"crc32":"...","v":0,"dsize":957500,"dcrc":"...","committed":1}

# 2. Apply — responde 202 na hora, depois derruba tudo e reinicia.
curl -b cookies.txt -X POST "http://simut.local/api/ota/apply"
# -> {"accepted":true,"mode":"apply"}
```

O staging precisa reportar `committed: 1` e `v: 0` antes que o apply faça
qualquer coisa. O `/api/ota/apply` responde **409** quando não há atualização
validada pendente.

Note que `bytes` e `dsize` diferem, e devem mesmo: `bytes` conta o padding 0xFF
que fecha a última página de 256 bytes, que é o que o aplicador copia, enquanto
`dsize` e `dcrc` descrevem os bytes que de fato chegaram.

### O que é verificado

| Etapa | Verificação |
|---|---|
| Upload | Tamanho entre 100 KB e o slot de aplicação de 1020 KB |
| Upload | CRC32/MPEG-2 sobre os primeiros 252 bytes contra os 4 bytes que vêm em seguida — a mesma checagem que a boot ROM do RP2040 faz, de modo que um arquivo que não seja uma imagem RP2040 válida é rejeitado antes que qualquer coisa seja apagada |
| Apply | O aplicador copia o staging para o slot de aplicação a partir da SRAM, com as interrupções desligadas |
| Próximo boot | A imagem instalada tem o CRC conferido contra os metadados e o veredito é registrado no log |

O veredito pós-apply aparece no console serial como
`[INF][OTA] image verified, NNNNNN B`. Ele existe ali e em nenhum outro lugar —
o log persistente guarda apenas o código numérico, então, depois do fato, o
nível (`INF` versus `ERR`) é o que distingue o sucesso de uma divergência.

### Comportamento medido

21 atualizações consecutivas na bancada, todas bem-sucedidas:

| Etapa | Tempo |
|---|---|
| Upload e staging (957.500 B) | 29,2 s ± 0,07 (32,1 KiB/s) |
| `/api/ota/apply` → 202 | 0,1 s |
| Janela do aplicador — apagar e gravar | 25,1 s ± 0,10 |
| Reboot → imagem verificada | 9,4 s ± 0,06 |
| **Interface web inacessível** | **48,4 s** |

Aproximadamente dois terços do tempo fora do ar são o aplicador; o resto é o
Wi-Fi reassociando. O heap livre se moveu 24 bytes ao longo de toda a corrida, e
nenhum boot produziu um panic.

Revalidado na linha 2.1 (v2.1.9): dois ciclos completos de stage+apply com uma
imagem de 1.001.964 B, 30,7 s por stage, apply aceito de primeira nas duas
vezes, e o veredito lido de volta como a string de versão — nunca inferido pelo
tempo.

---

## 13. O console serial

USB CDC a **115200 baud, 8N1**, com DTR asserted. O console existe em dois
perfis, e qual deles você tem depende do build do firmware:

| build | console |
|---|---|
| `pico_w_release`, `pico_w_alpha` | o console de emergência, catorze comandos |
| `pico_w_test` | o console completo, 56 comandos e quatro modos |
| `pico_w_test_https` | o console completo, mais o servidor TLS — a imagem de bancada para qualquer coisa de HTTPS |
| `pico_w_air` | o console completo, desde 18/09/2026 — veja abaixo |

### Firmware de release — catorze comandos

A imagem que os usuários rodam traz um console de recuperação, não uma
interface de configuração. A configuração vive na interface web.

| Comando | Finalidade |
|---|---|
| `show net status` | IP, sinal, pool de buffers, abortos de envio |
| `show system info` | Dispositivo, firmware, serial, Wi-Fi, fuso horário, NTP |
| `show system log` | Despeja o log de eventos |
| `debug on` / `debug off` | Log verboso para esta sessão |
| `system admin reset` | Reseta a senha de admin para uma aleatória |
| `system format` | Apaga o sistema de arquivos |
| `system https off` | Desabilita o HTTPS (apaga o par de certificados) e cai para HTTP |
| `system factory` | Restaura os padrões de fábrica |
| `system ssid <nome>` | Define o nome da rede Wi-Fi — **salvo na hora** |
| `system pass <senha>` | Define a senha do Wi-Fi — **salva na hora** |
| `system cors <origem>` / `off` | Libera a página do gerenciador web naquela origem a falar com este aparelho pelo navegador — **gravado na hora**, vale no próximo boot |
| `ap` | Sobe o ponto de acesso de configuração — **WPA2**, chave impressa neste console |
| `reload` | Reinicia |
| `help` | Lista estes |

Os dois comandos de Wi-Fi são como se move uma unidade sem display para outra
rede: defina os dois e depois `reload confirm` para reconectar.

Comandos destrutivos exigem `confirm` como palavra final, e quatro deles —
`system factory`, `system format`, `system admin reset` e `system https off` —
são **recusados pelo Bluetooth**. São recuperações, e uma recuperação alcançável
pelo rádio só ajuda quem já entrou.

> **A maioria das mudanças feitas aqui não persiste.** O console de emergência
> não tem `write memory`, então o que ele altera vale para a sessão em execução
> e some no próximo reboot. O `debug on` é o caso em que isso é de propósito.
>
> **Três comandos são exceção e salvam na hora:** `system ssid`, `system pass` e
> `system admin reset`. Os de rede sempre salvaram; o reset de senha não salvava
> até esta versão, e num SIMUT Air — onde todo wake é um boot — a senha que ele
> imprimia expirava cerca de um minuto depois, o que fazia a única recuperação
> de uma web trancada não recuperar nada. Agora ele grava na flash antes de
> imprimir, e diz `NAO SALVOU: vale so ate reiniciar` se a gravação falhar.

A senha impressa é aleatória, 8 caracteres de um alfabeto sem O/0 e I/1,
mostrada **uma única vez**. O login web seguinte é forçado a trocá-la.

Este console substituiu um de 56 comandos na v1.5.6-beta. Os comandos que foram
cortados já tinham equivalentes na web, e removê-los devolveu 44,5 KB de flash.

### Firmware de teste — o console completo

Os builds `pico_w_test` trazem os 56 comandos com modos estilo Cisco (`enable` →
`configure terminal` → `write memory`), mais `touch sim`, `touch hold` e
`screen` para dirigir o display por script. É o build que as suítes
automatizadas em `tools/` exigem. Não é o que deve estar num dispositivo que
alguém usa.

**O SIMUT Air também traz, desde 18/09/2026.** Aquele build é headless: o
console serial e o Bluetooth são a única interface local que ele tem, e
responder "a configuração vive na interface web" para quem está com o cabo na
mão não ajuda ninguém justamente quando a web é o que não se alcança. Ele ficou
no console de emergência por um motivo só — a CLI completa custa 45.056 B e a
imagem tinha 876 B de folga quando a decisão foi tomada. A dieta da v2.4.9-beta
liberou 62.420 B ali; `write memory` e os quatro modos funcionam como no
`pico_w_test`, e os cinco comandos `air` continuam onde estavam.

Referência completa: [CLI-Manual.md](CLI-Manual.md) *(em português)*.

### Bluetooth

**Não existe no firmware de release** — o `BluetoothManager.cpp` está excluído
daquele build. Ele **é** compilado nas imagens **alpha** e **Air**, onde não há
painel touch por onde iniciar o modo AP, e autentica com a senha web do admin.

Esse console tem o mesmo lockout exponencial do login web — 2 s depois da
primeira senha errada, dobrando até um teto de 300 s — mantido na RAM, então
derrubar e reabrir o enlace não zera nada. A descoberta fecha cinco minutos
depois do boot. Dos comandos de recuperação, só o `ap` é permitido pelo enlace.

---

## 14. Recuperação

### Modo AP — a rede de setup

Quando o aparelho não está na rede, ele mesmo levanta uma: `<nome>_SETUP`,
**WPA2**, portal em `http://192.168.4.1`. A senha é derivada do número de série
da placa (não é configurável, e sobrevive a um reset de fábrica) — o aparelho a
publica de quatro maneiras:

| Onde | Vale para |
|---|---|
| Console USB, na linha `[AP] PSK :` | todos |
| Resposta do comando `ap`, no canal que pediu (USB ou Bluetooth) | todos |
| O terminal de boot do painel: no boot, junto de "Conecte-se à rede …"; e, desde a v2.7.2, nas últimas linhas sempre que o AP abre em operação (Configurações → 12, `ap`, a escada) — rede, `PSK`, 192.168.4.1 | release (TFT) |
| Terceira página do LCD, enquanto o AP está no ar | alpha |

E há quatro maneiras de entrar nele:

| Como | Vale para | Observação |
|---|---|---|
| **Sozinho**, quando não há Wi-Fi configurado | release, alpha | é o estado de fábrica; o Air fica de fora (ver abaixo) |
| **Sozinho**, quando não consegue entrar na rede | release, alpha | na **primeira dormência** se ele nunca teve endereço desde que ligou (roteador trocado, senha mudada, aparelho mudado de lugar) — **medido: 6–7 min** (421 s numa corrida, 358–382 s noutra); depois de **uma rodada inteira** da escada se ele tinha endereço e perdeu — ~68 min por aritmética, não medido até o fim. O AP se desfaz em 15 min e o aparelho volta a tentar a rede |
| **Configurações → 12. Modo de Configuração** | release (TFT) | pede confirmação; exige o bit de rede |
| **Comando `ap`** | todos | pela USB, e pelo Bluetooth nas imagens alpha e Air |
| **Segurando a tela durante o boot** | release (TFT) | o painel pede e mostra a barra de 3 s; ver abaixo |

**O SIMUT Air fica de fora das duas entradas automáticas.** O rádio dele só
existe dentro de um wake, o timeout de 15 min do AP só devolve à STA quando há
SSID configurado — então num aparelho sem SSID esse estado não tem saída — e um
Air que nunca hiberna é uma bateria na bancada. O canal dele é a CLI, pela USB
ou pelo Bluetooth, que ele tem completa desde 18/09/2026.

**O gesto do toque é o mais frágil dos cinco.** A janela abre quando o painel
escreve "Segure a tela para o modo AP" e dura 3,5 s; segurar 3 s dentro dela
liga o AP. Até a v2.7.0 essa janela corria **antes** do Core 1 existir, e o Core
1 é quem desenha o TFT: a instrução chegava ao vidro 38 ms depois de a janela
fechar (medido em 22/09/2026 — janela `[3919..7419] ms`, Core 1 em `7457 ms`),
de modo que quem obedecia ao que o painel dizia estava sempre atrasado. Desde a
v2.7.1 a janela roda com o painel desenhando, então **o que está escrito na tela
é verdade enquanto está escrito**.

| Sintoma | O que fazer |
|---|---|
| Esqueci a senha de admin | `system admin reset confirm` pela **serial USB** (o comando é recusado pelo Bluetooth), depois entre com a senha impressa — a web obriga a trocá-la. Desde esta versão o reset sobrevive a um reboot, então não há pressa |
| Responde na serial mas não na rede | `show net status` — sem IP, entre em modo AP (acima) e reconfigure o Wi-Fi pelo portal |
| O roteador mudou e o aparelho sumiu da rede | Ele abre a rede de setup sozinho — **medido: 6–7 min** (421 s numa corrida, 358–382 s noutra), porque nunca chegou a ter endereço nesse boot. Ou force pelo painel, pelo `ap` na USB, ou pelo `ap` por Bluetooth (alpha e Air) |
| Vejo a rede `_SETUP` mas o celular não conecta | Corrigido na v2.7.1. Até a v2.7.0, um `ap` disparado enquanto o aparelho estava **caçando uma rede que não existe** subia um AP visível e inassociável (medido: 45 s e timeout, contra 4,07 s depois da correção) — e é justamente aí que se usa o `ap`. Em firmware mais antigo, `reload confirm` e o `ap` logo no boot |
| Vejo a rede `_SETUP` no celular mas não sei a senha | Ela é derivada da placa e nunca foi em branco desde a v2.4.1-beta. Leia-a no console USB, na resposta do `ap`, no boot do TFT ou no LCD do alpha |
| Tela em branco depois de ajustar o offset do display | Corrigido na v1.6.2-beta. Em firmwares mais antigos, um reset de fábrica limpa o offset armazenado |
| A atualização reportou sucesso mas a versão não mudou | O defeito do aplicador descrito na §12. Grave a v1.6.2-beta pelo USB |
| Não enumera no USB de jeito nenhum | Resgate por BOOTSEL — veja [RECOVERY.md](RECOVERY.md) |

---

## 15. Especificações

### Limites

| | |
|---|---|
| Slots de sensor | 16 (GPIO0–GPIO15) |
| Canais por sensor | 4 (temperatura, umidade, pressão, lux) |
| Pinos por sensor | até 4 |
| Contas de usuário | 32 |
| Sessões web simultâneas | 3 |
| Bits de permissão | 13 |
| Janela de média | 10 amostras, média aparada |
| Pontos do gráfico no TFT | 200 |
| Intervalo do histórico | 1–1440 minutos, padrão 1 |

### Mapa da flash

| Região | Offset | Tamanho |
|---|---|---|
| Aplicação | `0x000000` | 1020 KB |
| Staging / LittleFS | `0x0FF000` | 1024 KB |
| Snapshot de configuração | últimos 4 KB do staging | 4 KB |
| Metadados de OTA | `0x1FF000` | 4 KB |

A área de staging e o sistema de arquivos são a mesma região física. É por isso
que uma atualização reformata o sistema de arquivos, e por isso que o snapshot
da configuração vive no setor de metadados.

### Build

| | |
|---|---|
| Tamanho do firmware | 1.011.244 B — ~97% do slot de aplicação de 1020 KB |
| RAM no link | 123.124 B de 262.144 B |
| Heap livre em serviço | ~46 KB (rig de referência: cinco sensores, pacote de idioma pt-BR; medido na v2.1.10 — ainda não remedido depois das mudanças do pool TLS da 2.3.x) |
| Firmware do rádio | ~232 KB do slot de aplicação |

---

## 16. Referência da API HTTP

Todas as rotas exigem uma sessão autenticada, salvo indicação em contrário.
Permissões entre colchetes.

### Sessão

| Rota | Método | Observações |
|---|---|---|
| `/api/login_init` | GET | Devolve um nonce. **Aberta, não exige sessão** |
| `/api/login` | POST | `user`, `pass` (sha256, latin-1), `nonce` |
| `/api/login_chpass` | POST | Troca a senha na tela de login |
| `/api/force_chpass` | POST | Conclui uma troca de senha forçada |
| `/logout` | GET | Encerra a sessão. Lê o cookie `SIMUTSESS` **ou** `Authorization: Bearer` — uma página de navegador em outra origem só consegue mandar o segundo. Responde 204 a quem veio por Bearer e 302 para `/login` a quem veio por cookie |

### Leitura de estado

| Rota | Método | Observações |
|---|---|---|
| `/api/status` | GET | Uptime, heap, uso da flash, RSSI |
| `/metrics` | GET | Exposição em texto do Prometheus [DASHBOARD]. Cookie de sessão **ou** HTTP Basic (usuário + senha em claro) — veja a §10 |
| `/api/sensors` | GET | Leituras ao vivo por slot |
| `/api/config` | GET | Configuração do dispositivo |
| `/api/network` | GET | Configuração de rede |
| `/api/alarms` | GET | Limites |
| `/api/users` | GET | Contas [USER_MGR] |
| `/api/perms` | GET | Bits de permissão da sessão |
| `/api/sec_status` | GET | Estado de bloqueio e de segurança |
| `/api/themes` | GET | Temas disponíveis |
| `/api/lang` | GET | Dicionário de idioma |

### Histórico e logs

| Rota | Método | Observações |
|---|---|---|
| `/api/history_multi` | GET | Registros de um intervalo [HISTORY] |
| `/api/history/open` | GET | A hora ainda aberta, na RAM, como um stream V5 de bloco único [HISTORY] |
| `/api/history_days` | GET | Quais dias têm dados |
| `/api/history_rebind` | POST | Reaponta os registros para um novo ID de hardware |
| `/api/export/history.bin` | GET | Exportação binária bruta |
| `/api/logs` | GET | Log de eventos [LOGS] |
| `/api/logcodes` | GET | Nomes dos eventos: os do pacote ativo, depois os em inglês — texto puro [LOGS] |
| `/api/export/logs.bin` | GET | Exportação binária bruta |
| `/api/clear_logs` | POST | Apaga o log |

### Arquivos

| Rota | Método | Observações |
|---|---|---|
| `/api/ls` | GET | Lista um diretório — o parâmetro é `dir` |
| `/api/upload` | POST | Upload [FILE_UPLOAD] |
| `/api/delete` | POST | Exclui — o parâmetro é `file` [FILE_DELETE] |
| `/api/mkdir` | POST | Cria um diretório |
| `/download` | GET | Baixa um arquivo [FILE_READ] |

### Configuração

| Rota | Método | Observações |
|---|---|---|
| `/api/save_sys` | POST | Salva a configuração do sistema [SYS_CONFIG] |
| `/api/commit_all` | POST | Aplica um lote de alterações |
| `/api/set_time` | POST | Acerta o relógio |
| `/api/calib` | GET/POST | Offsets de calibração [CALIB] |
| `/api/action` | POST | Ações multiplexadas — `tel_sync`, `tel_reset`, `sensor_scan`, `scan_results`, `sensor_accept`, `sensor_wipe` |
| `/api/reset_touch_cal` | POST | Limpa a calibração do touch |

### Firmware e backup

| Rota | Método | Observações |
|---|---|---|
| `/api/backup` | GET | Baixa o sistema de arquivos como `.bkp` — **somente admin** |
| `/api/restore` | POST | `op=validate` \| `op=apply` \| `op=stage&commit=1` — **o stage é somente admin** |
| `/api/ota/apply` | POST | Aplica uma atualização em staging — **somente admin**, responde 202 |

### Display

| Rota | Método | Observações |
|---|---|---|
| `/api/screenshot` | GET | BMP 320×240 de 24 bits lido do painel |
| `/api/screenshot_chunk` | GET | Um bloco de 16 linhas com um CRC32, para transferência verificável |
| `/api/screen_stream` | GET | Um quadro do painel em faixas com RLE de paleta (espelho ao vivo) |
| `/api/touch` | POST | Toca o painel em `x` (0..319) e `y` (0..239) — coordenadas do painel; com `ms` (100..15000), segura o toque esse tempo |
| `/api/keypad` | GET | O teclado do PIN **como está no vidro agora**: `faces` (uma por tecla, POSICIONAL — o pad numérico ordenado tem duas VAZIAS), `kb` (`cards`, `num` ou `groups`), `grid`, `policy` e `pop` para o popup do alfanumérico de dois toques. Descreve o vidro, não o segredo: num teclado sorteado nunca diz qual casa do cartão é o caractere |

---

## 17. SIMUT Air — o build a bateria

O `pico_w_air` é o mesmo firmware com o display compilado fora e um ciclo de
hibernação acrescentado. Ele é para um lugar sem tomada e sem ninguém por
perto: acorda no relógio, lê os sensores, grava a leitura na flash e volta a
dormir. Só liga o rádio quando tem leituras suficientes para valer o envio.

> **Experimental.** O ciclo e os números de energia foram medidos na bancada,
> não numa instalação de campo. Nada disso é certificado, e o consumo citado
> abaixo é aritmética sobre medições de **tempo**, não uma medição de corrente.

### Dois modos

| | **M0 — operacional** | **M1 — o ciclo** |
|---|---|---|
| Rádio | ligado | só num wake de telemetria |
| Servidor web | rodando | **não sobe** |
| Bluetooth, mDNS | rodando | não sobem |
| Console serial | completo | responde, mas a janela é de segundos |
| Termina quando | o `air idle` expira sem atividade | você roda `air stop`, ou o carregador é detectado |

Um **boot limpo** — energia aplicada, RUN, `reload`, um OTA — é lido como
*tem gente ali* e cai em M0 com o `air idle` inteiro para trabalhar. Um boot que
veio da hibernação volta direto para o ciclo.

### Comandos

| Comando | O que faz |
|---|---|
| `air status` | Uma linha: fase, período do wake, intervalo do histórico, ocioso, armado, telemetria pendente, rádio, carregador, bateria |
| `air hibernate` (`air sleep`) | Arma o ciclo e entra nele agora |
| `air stop` (`air wake`) | Cancela o ciclo, volta a M0, desarma na flash |
| `air idle <10..65535>` | Segundos de silêncio em M0 antes de hibernar sozinho |
| `air charger <0..29 \| off>` | GPIO que fica em nível alto quando o carregador está ligado |

O `air status` se lê assim:

```
Air: phase=0 wake=60s hist=60s backoff=0s idle=300s armed=1 dirty=0
     tel=31/5 skip=0 radio=1 chg=0 bat=50 cyc=4038ms wip=1
```

`phase=0` é M0. `armed=1` quer dizer que o ciclo está registrado na flash e vai
retomar sozinho depois de um reset — isso é de propósito, para que uma unidade
que reinicia em campo não fique acordada até a bateria acabar. `tel=31/5` são 31
registros esperando contra um lote mínimo de 5.

### Quanto custa um wake

Medido no rig com intervalo de histórico de 60 s e um DS18B20 a 12 bits:

| | |
|---|---|
| Wake só de leitura | **9,31 s** — dos quais 6,83 s são o tempo de conversão do próprio sensor |
| Wake com telemetria | ~12,7 s |
| Dormindo | ~51 s |
| Período medido | 60,5–60,6 s contra um ajuste de 60 s |
| Ciclo útil | ~13% |

Contra as correntes de bancada (25 mA lendo, 80 mA transmitindo, 2 mA dormindo),
uma leitura por minuto e telemetria a cada quinto wake, isso dá cerca de
**8,2 mA médios e 17 dias num 18650 de 3400 mAh** — aritmética, não medição.

**Para onde vai o tempo.** Dois terços de um wake são a janela de média de dez
amostras enchendo do zero, e o DS18B20 leva 750 ms por conversão a 12 bits.
Reduzir o `MOVING_AVG_WINDOW` ou a resolução do sensor é a alavanca que resta, e
as duas mudam o número gravado, então nenhuma foi aplicada por você.
Note também que o driver hoje espera 750 ms fixos seja qual for a resolução
configurada, então **baixar a resolução agora custa precisão e não devolve
tempo** enquanto essa espera não seguir o ajuste.

### Coisas que surpreendem

- **Ele some do USB enquanto dorme.** O deep sleep desconecta o dispositivo. Uma
  porta que desaparece no meio de um comando é o ciclo funcionando, não uma
  queda.
- **A web só existe em M0.** Um wake não sobe o listener, então
  `http://<ip>/` recusa a conexão na maior parte de cada minuto. Rode `air stop`
  pela serial antes, ou pegue um boot limpo.
- **Só requisição autenticada segura o aparelho acordado.** Um poll anônimo tem
  orçamento de três extensões por boot e nada além disso, então uma sonda de
  monitoramento não consegue manter uma unidade a bateria de pé para sempre.
  Estar logado rearma o ocioso a cada requisição, que é o que dá janela ao
  operador.
- **O carregador cancela o ciclo.** Um wake que encontra o pino configurado em
  nível alto sobe M0 completo em vez disso. O ciclo continua armado na flash,
  então desconectar e deixar o `air idle` expirar devolve o aparelho ao sono sem
  nada para reativar.
- **Um wake grava um registro de log, não oito.** Os oito registros de init do
  boot são suprimidos num wake — eles descrevem um boot que já aconteceu — e um
  boot frio continua gravando todos, mais o `APP_AIR_COLD_BOOT`. Se esse código
  aparecer em campo, o aparelho perdeu energia.

### Telemetria a bateria

O gatilho é **quantidade**, não tempo. O `t_int` é o lote mínimo: o rádio fica
desligado até haver essa quantidade de registros esperando. O `t_bat` limita
quantos vão num envio. Com `t_int=5` e uma leitura por minuto, sete de cada oito
wakes não ligam o rádio — que é o ponto, já que o rádio é a coisa mais cara que
um wake pode fazer.

O `t_bat` é um teto, não uma promessa. Uma fila longa — um coletor que ficou
uma hora fora, por exemplo — sai em lotes do tamanho que a memória livre
permite, que pode ser menor que o `t_bat`: com `t_bat=250`, um Air acordado em
M0 envia uns 190 por vez. Cada lote termina no último registro que coube
inteiro, e o seguinte começa logo depois dele.

---

## Onde buscar ajuda

- [Fiação e pinagem](WIRING.md)
- [Recuperação](RECOVERY.md)
- [Atualizações pelo ar](OTA_USAGE.md)
- [Glossário](GLOSSARY.md)
- [Referência do console serial](CLI-Manual.md) *(em português)*
- [Mapa de telas](images/screens/screens.md)
- [Reportar um bug](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- [Política de segurança](https://github.com/angeloINTJ/simut/blob/main/SECURITY.md)
