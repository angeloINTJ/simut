# SIMUT — Manual do Usuário

**Firmware:** v2.3.2-beta · **Hardware:** Raspberry Pi Pico W (RP2040 + CYW43439) · **Licença:** MIT
**Repositório:** https://github.com/angeloINTJ/simut

[English](MANUAL.md) | **Português**

> **Este é um software beta.** Ele é testado em hardware real, mas não é um
> instrumento metrológico certificado. Não faça dele o único controle de um
> armazenamento regulado sem validá-lo contra a sua própria referência.

Tudo o que está abaixo foi conferido contra um dispositivo rodando a
v2.3.2-beta. Onde um número é citado, ele foi medido e não estimado; onde o
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

3. **Entre em uma rede.** Configure o Wi-Fi pelo display touch. O dispositivo
   responde a mDNS, então é alcançável em `http://simut.local` além de pelo IP.
   `show net status` na serial imprime o endereço, se você precisar.

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

- **Toque em um card de sensor** para alternar a visão de mín/máx.
- **Toque no ícone de gráfico** na visão de mín/máx para abrir o histórico
  daquele sensor.
- **Toque em CFG** para chegar às configurações — isso pede o PIN do display,
  se houver um definido.

### Configurações

Alcançadas pelo CFG. Cobrem temas visuais, limites de alarme, sons de alarme,
idioma da interface, o PIN do display, calibração do touch, sensibilidade do
touch, alinhamento do display, status do sistema e o texto da licença. Desde a
v2.1.9 a tela de PIN/senha é um teclado para a ponta do dedo: oito teclas
grandes de grupo abrem um popup com as duas caixas ao mesmo tempo, de modo que
qualquer um dos 91 caracteres aceitos custa exatamente dois toques.

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
| `/network` | Wi-Fi, endereçamento estático, mDNS, NTP |
| `/alarms` | Limites e ações por sensor |
| `/users` | Contas e permissões |
| `/files` | Navegador do sistema de arquivos: upload, download, exclusão, criação de diretórios — mais backup completo, restauração e atualização de firmware (OTA) |
| `/history` | Gráficos de histórico, exportação CSV e o visualizador do log de eventos do sistema |
| `/license` | Texto da licença |

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

Envie os dois pela página Files como `/config/web_cert.pem` e
`/config/web_key.pem` e reinicie. Com a porta web no padrão 80, o listener
HTTPS se move para a 443, então `https://<ip-do-dispositivo>` funciona; uma
porta configurada explicitamente é honrada como está. A chave privada pode ser
enviada mas nunca baixada, e o `system format` a apaga junto com o resto de
`/config`.

O que esperar:

- O certificado é autoassinado, então o navegador avisa uma vez — inspecione e
  aceite. O cookie de sessão ganha a flag `Secure`.
- O HTTP puro para de responder: há um único servidor e agora ele fala TLS. Um
  handshake custa cerca de 0,5–0,7 s neste chip, e **um cliente TLS é atendido
  por vez** — uma segunda conexão simultânea é descartada.
- Um par ausente ou impossível de interpretar nunca consegue trancar você do
  lado de fora: o dispositivo cai para HTTP puro na porta configurada. É assim
  também que o HTTPS é desligado — sobrescreva o `/config/web_key.pem` com
  qualquer arquivo inválido e reinicie.
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

Cinco contas no máximo. Três sessões podem estar ativas ao mesmo tempo. As
senhas são hasheadas com um salt aleatório por usuário.

Dez bits de permissão, concedidos de forma independente:

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
| Intervalo | Configurável, com um limite de lote por envio |
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
perfis, e qual deles você tem depende do build do firmware.

### Firmware de release — dez comandos

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
| `reload` | Reinicia |
| `help` | Lista estes |

Comandos destrutivos exigem `confirm` como palavra final.

> **As mudanças feitas aqui não persistem.** O console de emergência não tem
> `write memory`, então tudo o que ele altera vale para a sessão em execução e
> some no próximo reboot. O `system admin reset`, em particular, entrega uma
> senha *só para este boot* — tempo suficiente para entrar e definir uma de
> verdade pela interface web.

Este console substituiu um de 56 comandos na v1.5.6-beta. Os comandos que foram
cortados já tinham equivalentes na web, e removê-los devolveu 44,5 KB de flash.

### Firmware de teste — o console completo

Os builds `pico_w_test` trazem os 56 comandos com modos estilo Cisco
(`enable` → `configure terminal` → `write memory`), mais `touch sim` e `screen`
para dirigir o display por script. É o build que as suítes automatizadas em
`tools/` exigem. Não é o que deve estar num dispositivo que alguém usa.

Referência completa: [CLI-Manual.md](CLI-Manual.md) *(em português)*.

### Bluetooth

Manuais anteriores documentavam um console Bluetooth. **Ele não é compilado no
firmware de release** — o `BluetoothManager.cpp` está excluído do build.

---

## 14. Recuperação

| Sintoma | O que fazer |
|---|---|
| Esqueci a senha de admin | `system admin reset confirm` pela serial, depois entre com a senha impressa e defina uma nova pela interface web |
| Responde na serial mas não na rede | `show net status` — sem IP, reconfigure o Wi-Fi pelo display |
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
| Contas de usuário | 5 |
| Sessões web simultâneas | 3 |
| Bits de permissão | 10 |
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
| `/logout` | GET | Encerra a sessão |

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
