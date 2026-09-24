# O console serial e o Bluetooth {#cap-14}

O console serial (a CLI) é a porta de entrada por texto do aparelho: pelo cabo USB em todas as imagens e pelo Bluetooth no alpha e no Air. Este capítulo mostra como se conectar, os 14 comandos do console de emergência, o console completo e os comandos do Air, para quem instala, recupera ou opera o aparelho sem a interface web.

## Os dois consoles {#cap-14-perfis}

Toda configuração do dia a dia fica na interface web ([capítulo 5](#cap-05)). O console existe para quando a web não serve: descobrir o endereço, ler o log de eventos, recuperar a senha do administrador, trocar a rede Wi-Fi, reiniciar. Por isso a imagem `release` e a `alpha` trazem só o **console de emergência**. O **console completo**, com modos e dezenas de comandos, vem no Air, que não tem tela nem web fora do modo acordado, e nas imagens de teste, compiladas da fonte.

| Imagem | Console | Bluetooth |
|---|---|---|
| `release` | Emergência, 14 comandos | Não |
| `alpha` | Emergência, 14 comandos | Sim |
| Air | Completo, com os comandos `air` | Sim, com o aparelho acordado em operação |
| Imagens de teste (`pico_w_test`, `pico_w_test_https`) | Completo, com os comandos do painel e do PIN | Não |

Os comandos e as palavras-chave são sempre em inglês. As respostas saem no idioma do aparelho, o mesmo do painel, lido no boot; em português, elas vêm sem acentos, para qualquer terminal mostrar. De fábrica, o idioma é o português.

## Conectar pelo USB {#cap-14-usb}

O Pico W aparece no computador como uma porta serial USB. Use 115200 baud, 8 bits, sem paridade, 1 bit de parada (8N1).

| Sistema | Nome da porta | Programas |
|---|---|---|
| Linux | `/dev/ttyACM0` (ou `ACM1`, ...) | `picocom`, `screen`, `minicom`, `pio device monitor` |
| Windows | `COM3`, `COM4`, ... (veja no Gerenciador de Dispositivos) | PuTTY (tipo de conexão **Serial**), monitor serial do Arduino IDE |
| macOS | `/dev/cu.usbmodem...` | `screen`, `picocom` |

```bash
# Linux
picocom -b 115200 /dev/ttyACM0
# macOS (o nome exato aparece com: ls /dev/cu.usbmodem*)
screen /dev/cu.usbmodem1101 115200
```

No Linux, o seu usuário precisa pertencer ao grupo que controla a porta (`dialout` ou `uucp`, conforme a distribuição).

Como o console se comporta:

- **Eco:** o aparelho repete cada caractere que recebe. Desligue o eco local do terminal, ou cada letra sai duas vezes.
- **Enter:** tanto CR quanto LF encerram a linha. Uma linha vazia só mostra o prompt de novo.
- **Apagar:** Backspace e Delete apagam o último caractere.
- **Tamanho:** uma linha tem no máximo 256 caracteres; uma linha maior é descartada inteira.
- **Início:** no alpha e no Air acordado em operação, o console mostra um cabeçalho durante o boot, logo depois da etapa do Wi-Fi. Na `release` e nas imagens de teste, ele não mostra nada até você teclar Enter; então aparece o prompt `SIMUT>`.
- **Boot:** com o terminal aberto durante o boot, aparecem algumas linhas de diagnóstico que começam por `[DBG]`. Elas não são erros.

```text
===========================================
 SIMUT IoT CLI 2.7.1
 Digite 'help' (ou 'ajuda', '?')
 For English: 'language en'
===========================================
SIMUT> 
```

::: nota
**O painel tem prioridade.** [release]{.img} Nos 5 s depois de um toque no painel, o aparelho guarda até dois comandos do console e os executa quando o painel sossega. Um terceiro comando nessa janela é descartado com a mensagem `ERROR: CLI ocupada (display em uso). Comando descartado.`; digite-o de novo.
:::

## Conectar pelo Bluetooth {#cap-14-bluetooth}

[alpha]{.img} [air]{.img} O alpha e o Air oferecem o mesmo console por Bluetooth clássico, no perfil de porta serial (SPP). O aparelho aparece com o nome configurado para ele, como `simut`.

### Parear {#cap-14-parear}

O aparelho fica visível nas buscas por 5 minutos depois que o Bluetooth liga, durante o boot, logo depois da etapa do Wi-Fi. Depois disso, ele some das buscas, mas um celular ou computador que já pareou continua conectando. Para parear um aparelho novo depois dos 5 minutos, reinicie o SIMUT e pareie logo em seguida.

1. Ligue o SIMUT e, nos primeiros 5 minutos, procure dispositivos Bluetooth no celular ou no computador.
2. Pareie com o nome do aparelho. O SIMUT não pede confirmação no pareamento.
3. Abra um aplicativo de terminal serial Bluetooth (SPP) e conecte ao aparelho, ou abra a porta serial que o sistema criou para ele.

O fechamento da janela de descoberta vai para o log de eventos com o código 307, **Bloqueio Bluetooth**, e o texto `BT discovery window closed`.

### Entrar {#cap-14-bt-entrar}

A senha do console por Bluetooth é a senha da conta `admin` da interface web.

1. Conecte e tecle Enter. O aparelho responde com o pedido de senha.
2. Digite a senha do `admin` e tecle Enter. Cada caractere aparece como `*`.

```text
--- Conexao Bluetooth estabelecida ---
Senha do admin: **********

===========================================
 SIMUT IoT CLI 2.7.1
 Acesso concedido. Digite 'help'.
 (For English: 'language en')
===========================================
SIMUT> 
```

A entrada vai para o log com o código 300, **Login bem-sucedido** (`BT admin login`). A senha tem no máximo 64 caracteres. Se você trocar a senha do `admin` pela web, a do Bluetooth muda junto.

### Senha errada e bloqueio {#cap-14-bt-bloqueio}

Cada senha errada responde `Acesso negado.` e bloqueia o Bluetooth por um tempo que dobra a cada erro seguido:

| Erro seguido | Bloqueio |
|---|---|
| 1º | 2 s |
| 2º | 4 s |
| 3º | 8 s |
| 4º | 16 s |
| 5º | 32 s |
| 6º | 64 s |
| 7º | 128 s |
| 8º | 256 s |
| 9º em diante | 300 s |

Durante o bloqueio, o aparelho descarta tudo o que chega pelo Bluetooth e avisa uma vez: `Bloqueado. Tente mais tarde.` O bloqueio continua valendo se você desconectar e conectar de novo; só a senha certa, depois do prazo, zera a contagem. Cada erro vai para o log com o código 301, **Falha de login**, e cada bloqueio com o código 307, com os segundos no contexto.

### Sessão {#cap-14-bt-sessao}

- **Inatividade:** depois de 5 minutos sem nada digitado, a sessão termina com `[SEGURANCA] Sessao encerrada (5 min inativo).` (código 304, **Sessão expirada**). Tecle Enter para pedir a senha de novo.
- **Desconexão:** desconectar encerra a sessão na hora. Quem conectar depois precisa da senha.
- **Mesma saída nos dois canais:** o console escreve a mesma saída no USB e no Bluetooth. Quem está no cabo vê o que se faz pelo Bluetooth, e o contrário.
- **Comandos só pelo USB:** `system factory`, `system format`, `system admin reset` e `system https off` são recusados pelo Bluetooth, inclusive dentro de `do`, com `ERROR: Comando so pela USB (cabo serial).` O log registra o código 302, **Acesso não autorizado**. O comando `ap` funciona pelo Bluetooth: abrir o AP a partir do celular, quando a senha do Wi-Fi está errada, é o motivo de o Bluetooth existir.

[air]{.img} O Air não liga o Bluetooth nos despertares do ciclo de hibernação, só quando está acordado em operação ([capítulo 19](#cap-19)).

## O console de emergência {#cap-14-emergencia}

[release]{.img} [alpha]{.img} O console de emergência tem um só modo, com o prompt `SIMUT>`, e 14 comandos:

| Comando | O que faz |
|---|---|
| `help` (ou `ajuda`, `?`) | Lista os comandos |
| `show net status` | Mostra o endereço IP, o sinal do Wi-Fi e contadores de envios interrompidos da web |
| `show system info` | Mostra nome, versão, número de série, resolução do DS18B20, rede Wi-Fi, fuso, servidor NTP e estado do log |
| `show system log` | Despeja o log de eventos gravado, até 2.000 registros |
| `debug on`, `debug off`, `debug` | Liga ou desliga a transmissão do log ao vivo; sem argumento, mostra o estado |
| `system admin reset confirm` | Gera uma senha nova para o `admin` e a mostra uma vez |
| `system format confirm` | Formata o sistema de arquivos e reinicia |
| `system factory confirm` | Volta a configuração de fábrica e reinicia |
| `system https off confirm` | Apaga o certificado HTTPS e reinicia em HTTP |
| `system ssid <nome>` | Grava o nome da rede Wi-Fi |
| `system pass <senha>` | Grava a senha da rede Wi-Fi |
| `system cors <origem>` ou `system cors off` | Grava a origem liberada para o gestor de frota |
| `ap` (ou `apmode`, `ap-mode`) | Abre o ponto de acesso de configuração |
| `reload confirm` | Reinicia o aparelho |

Um comando que não está na lista responde `ERROR: Comando desconhecido. As configuracoes ficam na interface web; 'help' lista o que resta aqui.` e vai para o log com o código 585, **Comando desconhecido**.

::: atencao
**O cabeçalho oferece um comando que não existe aqui.** O cabeçalho do console, no USB e no Bluetooth, sugere `language en` (ou `language pt`). Esse comando só existe no console completo; no console de emergência ele responde "Comando desconhecido". Para trocar o idioma do console, troque o idioma do aparelho pela interface web ou pelo painel e reinicie.
:::

### Confirmar comandos destrutivos {#cap-14-confirm}

Os comandos que apagam dados ou reiniciam exigem a palavra `confirm` no fim da linha. Sem ela, o aparelho só avisa e diz a linha exata:

```text
SIMUT> reload
ATENCAO: vai reiniciar o dispositivo.
Use 'reload confirm' para prosseguir.
SIMUT> reload confirm
```

Exigem `confirm`: `reload`, `system admin reset`, `system format`, `system factory` e `system https off`. O aviso de alguns deles sugere a forma `conf system ... confirm`; o prefixo `conf` é aceito e ignorado, então as duas formas valem.

### O que fica gravado {#cap-14-persistencia}

O console de emergência não tem `write memory`. Cada comando que precisa sobreviver a um reinício grava por conta própria:

| Comando | Quando vale |
|---|---|
| `system ssid`, `system pass` | Gravados na hora; valem na próxima conexão, depois de `reload confirm` |
| `system admin reset` | Gravado na hora; a senha nova vale já |
| `system cors` | Gravado em `/config/cors.txt` na hora; vale no próximo boot |
| `system factory`, `system format`, `system https off` | Na hora, com reinício |
| `debug on`, `debug off` | Só nesta sessão: `Vale para esta sessao; nao persiste apos reiniciar.` |

::: nota
**O estado do `debug` pode acabar gravado.** O `debug` fica na configuração em memória. Se, na mesma sessão, outro comando gravar a configuração (`system ssid`, `system pass`, `system admin reset`) ou alguém salvar pela web, o estado do `debug` vai junto e passa a valer depois do reinício. Desligue o `debug` antes de gravar qualquer coisa.
:::

### help {#cap-14-help}

Em português, a lista vem do pacote de idioma instalado; sem pacote, ou em inglês, vem do texto embutido no firmware. O começo:

```text
===========================================
    SIMUT - CONSOLE DE EMERGENCIA
===========================================
 As configuracoes ficam na interface web.
 Este console e o caminho de volta quando
 a web nao pode ser alcancada.
 Comandos destrutivos exigem ' confirm'
 (ex.: 'reload confirm').

show net status
  IP, RSSI, hora - encontrar o dispositivo
show system info
  Nome, firmware, serial, SSID do WiFi
...
```

### show net status {#cap-14-show-net}

Mostra o endereço IP e o sinal. Sem conexão, o sinal aparece como `-100 dBm`; com o AP aberto, o endereço é `192.168.4.1`. A linha `Abortos de envio` conta respostas da interface web interrompidas desde o boot, por prazo, por trava ou por desconexão do navegador; serve para diagnóstico. O comando também registra o endereço no log (código 527, **Mostrar IP**).

```text
SIMUT> show net status

--- Status da Rede ---
 IP: 192.0.2.10
 RSSI: -58 dBm
 Abortos de envio: prazo 0 | latch 0 | desconexao 0
-------------------------------------------
```

O comando não mostra o estado do NTP, embora o texto de ajuda diga "hora". Para ver se o relógio está acertado, use a interface web ([capítulo 10](#cap-10)).

### show system info {#cap-14-show-info}

```text
SIMUT> show system info
-------------------------------------------
 [SISTEMA]
 Dispositivo:
 simut
 Firmware: 2.7.1
 Serial: E66138935F2C1A2B
 [SENSORES]
 Precisao DS18: 12-bit
 [CONECTIVIDADE]
 WiFi SSID:
 MinhaRede
 Fuso: GMT-3
 Servidor NTP:
 pool.ntp.org (default)
 Logging: ATIVO
-------------------------------------------
```

O `Serial` é o número de série da placa, a chave da calibração dos sensores sem ROM no arquivo `calib.csv` ([capítulo 6](#cap-06-calib-csv)). Sem rede configurada, a linha do SSID mostra `<nao configurado>`. Sem servidor NTP configurado, aparece `pool.ntp.org (default)`.

### show system log {#cap-14-show-log}

Despeja o log de eventos gravado na memória, do arquivo antigo para o atual, até 2.000 registros. Cada linha tem:

| Campo | Exemplo | O que é |
|---|---|---|
| Hora | `1790164800` | O instante, em segundos desde 1970 (UTC) |
| `up` | `up2h05m` | O tempo desde o boot, quando o evento aconteceu |
| `C` | `C0` | O núcleo do processador (0 ou 1) |
| Nível | `[INF]` | `DBG`, `INF`, `WRN`, `ERR` ou `FTL` |
| Origem | `[NET   ]` | O subsistema: `APP`, `NET`, `SEC`, `STO`, `WEB`, ... |
| `code` | `code=527` | O código do evento ([apêndice B](#ap-b)) |
| `ctx` | `ctx=0` | O contexto, um número que o evento carrega |

```text
SIMUT> show system log

--- SYSTEM LOG START ---
1790164800 up2h05m   C0 [INF][NET   ] code=527 ctx=0
1790165112 up2h10m   C0 [WRN][SEC   ] code=301 ctx=0
--- SYSTEM LOG END ---
```

O log de eventos está no [capítulo 16](#cap-16).

### debug on e debug off {#cap-14-debug}

`debug on` transmite cada evento do log para o console no momento em que acontece, inclusive durante um boot que nunca chega a subir a web. Cada linha traz a hora, o tempo desde o boot, o núcleo, o nível, a origem e a descrição do evento:

```text
SIMUT> debug on
OK: Debug: LIGADO
Vale para esta sessao; nao persiste apos reiniciar.
[14:05:12][UP 02:05:40][C0][INF][NET] Mostrar IP: 192.0.2.10
```

`debug off` para a transmissão. O log continua sendo gravado com o `debug` ligado ou desligado.

### system admin reset {#cap-14-admin-reset}

Gera uma senha aleatória de 8 caracteres para a conta `admin` (letras maiúsculas e algarismos, sem `O`, `0`, `I` e `1`), grava na hora e marca a troca obrigatória no próximo acesso pela web ([capítulo 13](#cap-13-troca-obrigatoria)). A senha aparece uma única vez:

```text
SIMUT> system admin reset confirm
Senha admin resetada. Nova senha (unica vez):
 K4XW7PQA
Trocar no 1o login via web (forcado).
```

Se a gravação falhar, o aparelho avisa `ERROR: NAO SALVOU: vale so ate reiniciar.`: a senha funciona até o próximo reinício. Pelo Bluetooth, o comando é recusado. A recuperação completa está no [capítulo 18](#cap-18).

### system format, system factory e system https off {#cap-14-recuperacao}

| Comando | O que apaga | Depois do reinício |
|---|---|---|
| `system format confirm` | O sistema de arquivos inteiro: configuração, contas, histórico, log de eventos, pacote de idioma, temas e certificado. O firmware fica | O aparelho volta como novo; sem pacote de idioma, a interface fica em inglês |
| `system factory confirm` | A configuração: contas, rede, sensores, telemetria e o resto. O histórico e os arquivos ficam | Configuração de fábrica. A senha nova do `admin` não é mostrada: rode `system admin reset confirm` depois do reinício ([capítulo 17](#cap-17-factory)) |
| `system https off confirm` | O certificado e a chave do HTTPS | A interface web volta a responder em HTTP ([capítulo 9](#cap-09-https-off)) |

Sem rede configurada depois do reinício, a imagem `release` e a `alpha` abrem o AP de configuração no boot ([capítulo 9](#cap-09-ap-quando)). O `system format` mostra `OK: Formatando LittleFS... reboot em seguida.` antes de começar; o `system factory` reinicia sem mensagem; o `system https off` diz `OK: HTTPS desligado. Reiniciando em HTTP...`, ou `OK: Nenhum certificado presente. Reiniciando...` quando não havia certificado.

::: perigo
**`system format` apaga o histórico.** Não há como desfazer. Baixe um backup e o histórico antes, se a web ainda responder ([capítulo 17](#cap-17)).
:::

### system ssid e system pass {#cap-14-wifi}

```text
SIMUT> system ssid MinhaRede
OK: SSID salvo. Use 'reload confirm' para reconectar.
SIMUT> system pass senha-da-rede
OK: Senha salva. Use 'reload confirm' para reconectar.
SIMUT> reload confirm
```

O nome da rede tem de 1 a 31 caracteres e a senha, até 31, sem caracteres de controle. `system pass` sem senha grava uma senha vazia, para rede aberta.

::: atencao
**Sem espaços.** O console separa as palavras por espaço e grava só a primeira: `system ssid Minha Rede` grava `Minha`. Para uma rede ou uma senha com espaço, use a interface web ([capítulo 9](#cap-09-wifi)) ou o AP de configuração.
:::

### system cors {#cap-14-cors}

Libera a página do gestor de frota a consultar o aparelho pelo navegador ([capítulo 9](#cap-09-cors)). A origem é `esquema://servidor[:porta]`, sem barra no fim, com até 64 caracteres; `off` desliga.

```text
SIMUT> system cors http://coletor.exemplo.com.br:8080
OK: Origem salva. Use 'reload confirm' para aplicar.
```

Sem argumento, o comando só mostra o uso: `system cors` vazio não apaga a origem gravada.

### ap {#cap-14-ap}

Abre o ponto de acesso de configuração na hora e responde com a chave da rede:

```text
SIMUT> ap
OK: Modo AP iniciado (WPA2). Senha: K7PX4MRW2A
Conecte-se ao AP e acesse http://192.168.4.1
```

O nome da rede é o nome do aparelho seguido de `_SETUP`. O log registra o código 403, **AP iniciado**, com o contexto 0. O AP aberto em operação fecha sozinho em 15 minutos, com um reinício ([capítulo 9](#cap-09-ap-aberto)).

## O console completo {#cap-14-completo}

[air]{.img} O console completo segue o modelo dos roteadores: modos em camadas, com prompts diferentes, e alterações que ficam na memória até `write memory`. Ele existe no Air e nas imagens de teste. A lista exaustiva, com todos os apelidos, está em `docs/CLI-Manual.md`; esta seção mostra a estrutura e os comandos mais úteis, conferidos no código da v2.7.1.

### Os modos {#cap-14-modos}

| Modo | Prompt | Como entrar | Para quê |
|---|---|---|---|
| EXEC | `SIMUT>` | É o modo inicial | Consultas, `debug`, `language`, `time`, operações de telemetria |
| Privilegiado | `SIMUT#` | `enable` | Gravar, reiniciar, recuperar, operar sensores |
| Configuração | `SIMUT(config)#` | `configure terminal` (ou `config terminal`), a partir do privilegiado | Alterar a configuração |
| Sensor | `SIMUT(config-sensor-N)#` | `sensor <N>`, a partir da configuração ou do privilegiado | Configurar um slot, sem repetir o número |

| Comando | Efeito |
|---|---|
| `enable` | Do EXEC ao privilegiado. Não pede senha: quem tem o cabo tem o aparelho |
| `disable` | Do privilegiado ao EXEC |
| `exit` | Sobe um nível: do sensor à configuração, da configuração ao privilegiado, do privilegiado ao EXEC |
| `end` | Da configuração ou do sensor direto ao privilegiado |
| `do <comando>` | Na configuração ou no sensor, executa um comando do EXEC ou do privilegiado, como `do show sensors` ou `do write memory` |
| `?` ou `help` | Lista os comandos do modo atual |

Um comando fora do seu modo responde com o modo que ele exige, como `ERROR: Comando requer modo configuracao. Use 'configure terminal'.`. O prefixo `conf` (ou `configure`) antes de um comando é aceito e ignorado: `conf system name x` é o mesmo que `system name x`.

### Gravar: write memory {#cap-14-write-memory}

A maior parte das alterações vale na hora, mas só na memória. Depois de cada uma, o console lembra:

```text
SIMUT(config)# system name laboratorio2
RAM OK. Use 'write memory' para salvar.
SIMUT(config)# do write memory
OK: Config salva no Flash!
```

`write memory` grava a configuração, relê os sensores e ajusta o histórico ao conjunto novo de sensores, sem reiniciar. Alterações não gravadas se perdem no próximo reinício.

Gravam por conta própria, sem `write memory`: `system ssid`, `system pass`, `wifi ssid`, `wifi pass`, `system cors`, `system admin reset`, os comandos `air idle` e `air charger`, e as recuperações que reiniciam. O comando `time` acerta o relógio na hora e não é gravado.

Algumas alterações só valem depois de `reload confirm`: a porta web, a origem do CORS, a rede Wi-Fi e o nome do aparelho no Bluetooth.

### Consultas: show {#cap-14-show}

Valem no EXEC, no privilegiado e na configuração; no modo sensor, use `do show ...`.

| Comando | O que mostra |
|---|---|
| `show sensors` | Os slots ativos: GPIOs, tipo, grandezas, nome, ROM, identificação e limites de alarme |
| `show sensor types` | Os tipos de sensor compilados, com os pinos e as grandezas |
| `show gpio` (ou `gpio`) | O mapa dos GPIOs em uso |
| `show system info` | Como no console de emergência |
| `show net status` | Como no console de emergência |
| `show system log` | Como no console de emergência |
| `show storage stats` | O uso do sistema de arquivos |
| `show metrics` | Tempo ligado, memória livre e as métricas de rede, telemetria, alarmes, sensores, armazenamento e do núcleo 1 |
| `show themes` | Os temas instalados, com o número e o identificador |

```text
SIMUT> show sensors

--- Sensores Configurados ---
 [Slot 00] GPIO=2 | DS18B20  | T     | Freezer
          ROM: 28FF641E0F16047A
          ALARMES: LIGADO  [T: -25.0 .. -15.0]
 [Slot 01] GPIO=4,SDA/5 | BME280   | T+H+P | Sala
          ALARMES: DESL
```

### Sistema e rede {#cap-14-sistema}

Na configuração (`SIMUT(config)#`), salvo indicação:

| Comando | Efeito | Faixa |
|---|---|---|
| `system name <nome>` | Nome do aparelho, que também dá nome ao AP e ao Bluetooth | 1 a 31 caracteres |
| `system timezone <horas>` | Fuso em horas inteiras, aplicado na hora | −12 a +14 |
| `system ntp <servidor>` | Servidor de hora; vazio usa o padrão | Até 31 caracteres |
| `ntp on`, `ntp off` | Liga ou desliga o acerto por NTP | |
| `system history_interval <min>` | Intervalo de gravação do histórico | 1 a 1440 min |
| `system theme <nº ou id>` | Tema do painel, aplicado na hora | Veja `show themes` |
| `ds18b20 resolution <bits>` | Resolução dos DS18B20 | 9 a 12 |
| `system ssid <nome>`, `wifi ssid <nome>` | Rede Wi-Fi, gravada na hora | 1 a 31 caracteres |
| `system pass <senha>`, `wifi pass <senha>` | Senha do Wi-Fi, gravada na hora | Até 31 caracteres |
| `ip dhcp`, `ip static` | Endereço por DHCP ou fixo | |
| `ip addr <ipv4>`, `ip mask <ipv4>`, `ip gateway <ipv4>`, `ip dns <ipv4>` | Os campos do endereço fixo | |
| `dns auto`, `dns manual <ip1> [ip2]` | DNS pelo DHCP ou manual | |
| `web port <porta>` | Porta do servidor web, depois de `reload` | 1 a 65535 |
| `system cors <origem>`, `system cors off` | Como no console de emergência | |
| `system touch reset confirm` | [release]{.img} Descarta a calibração do toque e abre o assistente no painel ([capítulo 11](#cap-11-calibrar)); vale também no privilegiado | |

E, no EXEC ou no privilegiado:

| Comando | Efeito |
|---|---|
| `time AAAA-MM-DD HH:MM:SS` | Acerta o relógio na hora local, sem gravar; o ano precisa ser 2026 ou depois ([capítulo 10](#cap-10-manual)) |
| `language pt`, `language en` | Troca o idioma do console, do painel e do log na hora; `write memory` grava. `language` sozinho mostra o atual |
| `debug on`, `debug off` | Como no console de emergência; aqui, `write memory` grava o estado |

As recuperações `system admin reset`, `system factory`, `system format` e `system https off` funcionam no privilegiado e na configuração, com `confirm`, e são recusadas pelo Bluetooth como no console de emergência.

### Telemetria: tel {#cap-14-tel}

Na configuração:

| Comando | Efeito | Faixa |
|---|---|---|
| `tel server <endereço>` | Servidor do coletor | Até 63 caracteres |
| `tel port <porta>` | Porta do coletor | 1 a 65535 |
| `tel path <caminho>` | Caminho no coletor, como `/api/v1/dados` | Até 31 caracteres |
| `tel batch <n>` | Máximo de registros por envio | 1 a 250 |
| `tel interval <n>` | Lote mínimo: quantos registros esperam antes de um envio; `0` desliga | 0 a 20000 |
| `tel crypto on`, `tel crypto off` | Liga ou desliga o TLS | |
| `tel mode json`, `tel mode csv`, `tel mode custom` | Formato do payload | |

O texto de `help` do console completo ainda descreve `tel batch` com "max 50" e `tel interval` em milissegundos; valem as faixas acima. Ele também sugere `conf sensor ds18b20 resolution`, que o console não entende: a forma certa é `ds18b20 resolution <bits>`. O transporte (HTTP ou MQTT) e os modelos de payload se configuram pela web ([capítulo 21](#cap-21)).

Operações:

| Comando | Modo | Efeito |
|---|---|---|
| `tel sync` | EXEC ou privilegiado | Força um envio agora |
| `tel dump` | EXEC ou privilegiado | Mostra no console o próximo payload enviado, uma vez |
| `tel reset` | Privilegiado | Volta o cursor da telemetria: os próximos envios cobrem até 30 dias para trás. Não pede `confirm` |

### Linha de alarmes: alarm {#cap-14-alarm}

| Comando | Modo | Efeito |
|---|---|---|
| `alarm show` | EXEC, privilegiado ou configuração | Estado, modo, fila, descartes, caminho e modelos |
| `alarm dump` | EXEC ou privilegiado | Mostra no console o próximo envio de alarmes |
| `alarm flush` | Privilegiado | Esvazia a fila sem confirmação do servidor. Não pede `confirm` |
| `alarm set on`, `alarm set off` | Configuração | Liga ou desliga a linha de alarmes |
| `alarm set mode json\|csv\|custom` | Configuração | Formato |
| `alarm set qmax <n>` | Configuração | Tamanho da fila, de 1 a 64; de fábrica, 32 |
| `alarm set path <caminho>` | Configuração | Caminho no coletor; vazio usa o da telemetria com `/alarm` |
| `alarm set glob <modelo>`, `alarm set line <modelo>`, `alarm set sep <separador>` | Configuração | Modelos do modo `custom`, sem espaços |

```text
SIMUT> alarm show
Alarmes: LIGADO | modo json | fila 0/32 | descartados 0
PATH: (telPath+/alarm)
```

Os `alarm set` valem na hora e precisam de `write memory` para ficar. A linha de alarmes está no [capítulo 22](#cap-22).

### Sensores: sensor {#cap-14-sensor}

A forma mais cômoda é o modo sensor: na configuração, `sensor <N>` entra no slot N, e os comandos seguintes dispensam o prefixo `sensor <N>`. Fora dele, no privilegiado, use a forma completa, como `sensor 3 name Sala`.

| No modo sensor | Forma completa | Efeito |
|---|---|---|
| `create <tipo>` | `sensor <N> create <tipo>` | Cria o slot do zero: `ds18b20`, `dht22`, `bme280` ou `bmp280`, e lista os pinos que ele exige |
| `type <tipo>` | `sensor <N> type <tipo>` | Troca só o tipo |
| `pin <índice>,<gpio>` | `sensor <N> pin <índice>,<gpio>` | Liga o pino do sensor a um GPIO de 0 a 15 |
| `name <nome>` | `sensor <N> name <nome>` | Nome, de 1 a 31 caracteres |
| `hwid <id>` | `sensor <N> hwid <id>` | Identificação, de 1 a 15 caracteres: letras, algarismos, `_` e `-` |
| `active on`, `active off` | `sensor <N> active on\|off` | Liga ou desliga o slot; ligar exige o tipo e todos os pinos |
| `alarm on`, `alarm off` | `sensor <N> alarm on\|off` | Liga ou desliga os alarmes do slot |
| `tmin <valor>`, `tmax`, `hmin`, `hmax` | `sensor <N> tmin <valor>` | Limites de temperatura e umidade; `tempmin`, `hummax` e afins também valem |
| `pressmin <valor>`, `pressmax` | `sensor <N> pressmin <valor>` | Limites de pressão, em hPa |

```text
SIMUT(config)# sensor 2
Slot 2 nao configurado. Use 'create <tipo>' para configurar.
Entrando configuracao do sensor — Slot 2 (Unknown)
SIMUT(config-sensor-2)# create dht22
OK: Slot 2: DHT22 — 1 pino(s) necessarios:
  pin[0] = Data (pull-up)
GPIOs disponiveis: 0,1,3,6,7,8,9,10,11,12,13,14,15
Atribua com: sensor 2 pin <idx>,<gpio>
RAM OK. Use 'write memory' para salvar.
SIMUT(config-sensor-2)# pin 0,6
OK: Slot 2 pin[0]=GPIO 6 (Data)
Todos os pinos atribuidos. Proximo: sensor 2 name "<nome>"
RAM OK. Use 'write memory' para salvar.
SIMUT(config-sensor-2)# name Estufa
OK: Slot 2 name=Estufa
RAM OK. Use 'write memory' para salvar.
SIMUT(config-sensor-2)# tmax 30
OK: Slot 2 tmax=30
RAM OK. Use 'write memory' para salvar.
SIMUT(config-sensor-2)# end
SIMUT# write memory
OK: Config salva no Flash!
```

O console recusa um GPIO que outro slot ativo já usa. Os limites aceitam a faixa física da grandeza: temperatura de −327,68 a 327,66, umidade de 0 a 102,3 e pressão de 0 a 1638,3.

Operações, no privilegiado:

| Comando | Efeito |
|---|---|
| `sensor scan` | Procura sensores nos GPIOs e lista o que achou (também no EXEC) |
| `sensor accept <n>` | Lê o DS18B20 ligado ao GPIO n e o grava no slot de mesmo número, na hora ([capítulo 6](#cap-06-adotar)) |
| `sensor wipe <slot> confirm` | Recomeça a época do histórico do slot ([capítulo 6](#cap-06-epoca)) |
| `sensor remove <slot> confirm` | Desliga e limpa o slot |
| `sensor reschema confirm` | Reescreve o esquema do histórico de hoje a partir dos slots atuais, sem perder registros ([capítulo 6](#cap-06-recompor)) |

`sensor wipe` e `sensor remove` precisam de `write memory` para ficar; `sensor accept` grava na hora, e `sensor reschema` age no histórico na hora.

### Contas: user {#cap-14-user}

Na configuração:

| Comando | Efeito |
|---|---|
| `user add <nome> <senha>` | Cria uma conta web com as permissões **Painel**, **Histórico** e **Calibração** |
| `user del <nome>` | Exclui uma conta; o nome é comparado com maiúsculas e minúsculas; `admin` não pode ser excluída |
| `user pass <nome> <senha>` | Troca a senha de uma conta, sem troca obrigatória depois |
| `user perm <nome> <papel>` | Troca as permissões: `admin`, `operator`, `viewer`, `none` ou uma máscara como `0x1C03` |
| `user pin <nome> <PIN>` | [release]{.img} Define o PIN de painel de uma conta; `off` remove |
| `user policy <mín> <teclado> <alfabeto>` | [release]{.img} Define a política de PIN |

As senhas precisam de pelo menos 8 caracteres, com uma letra e um algarismo, e no máximo 64. Os papéis de `user perm`:

| Papel | Também aceito | Máscara | Permissões |
|---|---|---|---|
| `admin` | `full` | `0xFFFF` | Todas as 13 |
| `operator` | `operador` | `0x0207` | **Painel**, **Histórico**, **Logs** e **Calibração** |
| `viewer` | `leitor` | `0x0003` | **Painel** e **Histórico** |
| `none` | `nenhum` | `0x0000` | Nenhuma |

A máscara soma os valores das permissões da tabela do [capítulo 8](#cap-08-permissoes). O console não aplica a regra de que ninguém concede o que não tem ([capítulo 8](#cap-08-subconjunto)): quem está no cabo já pode tudo.

`user pin` e `user policy` existem só nas imagens de teste, que têm painel. `user pin` diz qual conta já usa um PIN repetido, o que o painel nunca diz (`ERRO: PIN ja pertence a operador1`). `user policy 4 3 0` grava tamanho mínimo 4, teclado com 3 caracteres por tecla e só algarismos, e responde quantas contas vão precisar renovar o PIN, como `OK: Politica: 4/3/0 - renovar: 2`. O tamanho máximo depende do teclado: 16, 12 ou 8 caracteres para o teclado 1, 2 ou 3. A política está no [capítulo 8](#cap-08-politica).

Todos os comandos `user` precisam de `write memory` para ficar.

### O painel pelo console {#cap-14-painel}

[release]{.img} Nas imagens de teste, quatro comandos do privilegiado servem para automação e para capturar telas:

| Comando | Efeito |
|---|---|
| `screen <tag>` | Abre uma tela do painel direto, como administrador: `dash`, `set`, `thm`, `lng`, `pwd`, `lic`, `sts`, `alm`, `gra`, `touchcal`, `touchsens`, `offset`, `usr`, `pin` ([referência das telas](#cap-11-referencia)) |
| `touch sim <X> <Y>` | Simula um toque no ponto (X de 0 a 319, Y de 0 a 239) |
| `touch hold <X> <Y> [ms]` | Simula um toque **mantido** por `ms` milissegundos (padrão 3500, de 100 a 15000) e depois solta: alcança os gestos de toque longo, como o de 3 s que fixa o cartão de cima, que o `touch sim` não alcança |
| `show display keypad` | Mostra o que cada tecla do teclado de PIN embaralhado tem na tela agora |

### Manutenção {#cap-14-manutencao}

No privilegiado:

| Comando | Efeito |
|---|---|
| `write memory` | Grava a configuração |
| `clear log confirm` | Apaga o log de eventos |
| `reload confirm` | Reinicia, gravando antes o histórico em memória |
| `ap` | Abre o AP de configuração |

## Os comandos do Air {#cap-14-air}

[air]{.img} O Air acrescenta cinco comandos, válidos em qualquer modo:

| Comando | Efeito |
|---|---|
| `air status` | Uma linha com o estado do ciclo de hibernação |
| `air hibernate` (ou `air sleep`) | Arma o ciclo e começa a hibernar agora; o USB some quando o aparelho dorme |
| `air stop` (ou `air wake`) | Cancela o ciclo e volta à operação acordada |
| `air idle <segundos>` | Tempo sem comando antes de hibernar sozinho, de 10 a 65535 s; de fábrica, 300 s |
| `air charger <gpio>` ou `air charger off` | GPIO que fica em nível alto com o carregador ligado; de fábrica, GP17 |

`air idle` e `air charger` gravam na hora, em `/config/air.bin`, sem `write memory`. `air charger` aceita os GPIOs 0 a 22 e 26 a 28 e recusa um GPIO que um sensor ativo ou a alimentação dos sensores (GP16) usam. As respostas desses dois estão sempre em inglês:

```text
SIMUT> air idle 600
OK: air idle set
SIMUT> air charger 17
OK: charger sense on GP17 (now on battery)
```

Durante um despertar do ciclo, o Air só lê o console USB: `air stop` digitado pelo Bluetooth nessa hora não tem efeito. Pelo USB, ele funciona em qualquer despertar. O ciclo está no [capítulo 19](#cap-19).

### air status {#cap-14-air-status}

```text
SIMUT> air status
Air: phase=0 wake=60s hist=60s backoff=0s idle=300s armed=1 dirty=0 tel=12/10 skip=0 radio=1 chg=0 bat=250 cyc=1830ms wip=1
```

| Campo | O que é |
|---|---|
| `phase` | A fase do ciclo: 0 acordado em operação; 1 aquecendo os sensores; 2 lendo e conectando; 3 decidindo; 4 sem uso; 5 esperando a hora; 6 enviando a telemetria; 7 indo dormir |
| `wake` | O próximo intervalo de sono, em segundos: o maior entre o intervalo do histórico e a espera da telemetria |
| `hist` | O intervalo do histórico, em segundos |
| `backoff` | Quanto falta da espera da telemetria depois de uma falha, em segundos |
| `idle` | O tempo sem comando antes de hibernar, em segundos; mais curto depois de um ciclo interrompido por um reinício |
| `armed` | 1 quando o ciclo está armado e sobrevive a reinícios |
| `dirty` | Quantos boots seguidos retomaram o ciclo depois de um reinício inesperado; zera no primeiro sono bom |
| `tel` | Registros pendentes da telemetria, seguidos do lote mínimo (`tel interval`) |
| `skip` | Despertares que ainda vão pular a telemetria depois de uma falha |
| `radio` | 1 com o rádio Wi-Fi ligado |
| `chg` | 1 com o carregador ligado: o Air não hiberna enquanto está no carregador |
| `bat` | O tamanho de lote que a telemetria ajustou sozinha |
| `cyc` | A duração do último envio, em milissegundos |
| `wip` | Quantas vezes o bloco aberto do histórico foi gravado neste boot; o esperado num despertar é 1 |

Consultado de dentro de um despertar, antes de o registro ser gravado, `wip` aparece como 0.
