# Solução de problemas {#cap-30}

Os problemas mais comuns, organizados pelo que você vê no aparelho, no navegador ou no servidor. Cada linha diz a causa provável e o que fazer. Comece pelo roteiro de diagnóstico: ele resolve a maior parte dos casos antes de você chegar às tabelas.

## Roteiro de diagnóstico {#cap-30-roteiro}

1. **Confira a imagem e a versão.** A gaveta da interface web e o comando `show system info` mostram a versão. A imagem se reconhece pelo aparelho: painel de toque é a release, LCD é o alpha, sem display é o Air; ela aparece também no campo `sys.env` de `GET /api/status`. Parte dos comportamentos deste capítulo vale só para uma imagem ou uma versão.
2. **Leia o log de eventos.** Na página **Histórico e Logs**, marque **INF**, **WRN** e **ERR**. No console USB, `show system log` mostra tudo o que está gravado. O log diz o que o aparelho tentou, o que falhou e quando ([capítulo 16](#cap-16)).
3. **Confira a rede.** A página **Rede** e o comando `show net status` mostram o endereço e o sinal. Com o sinal em −78 dBm ou abaixo, a telemetria e a linha de alarmes esperam ([capítulo 9](#cap-09)).
4. **Confira a hora.** Sem hora confiável, o histórico e a telemetria esperam. Veja se o NTP sincronizou ([capítulo 10](#cap-10)).
5. **Guarde as provas antes de mexer.** Exporte o log em CSV e baixe um backup. Um reinício ou uma restauração apagam pistas.
6. **Se a interface web não abre,** use o console USB ([capítulo 14](#cap-14)). Se nem o console responde, siga o [capítulo 18](#cap-18).

## O aparelho parou de medir ou de alarmar {#cap-30-medicao}

Esta é a falha que mais importa num equipamento de monitoramento. Confira estes casos primeiro.

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O painel ou o LCD mostram valores parados, e o histórico não cresce | O AP de configuração está no ar. Com ele aberto, o aparelho não lê os sensores, não confere os alarmes e não grava o histórico (v2.7.1) | Configure a rede pelo AP, ou reinicie ([Enquanto o AP está aberto](#cap-09-ap-aberto)) |
| Um aparelho que funciona sem Wi-Fi nunca gravou nada | Sem rede configurada, a v2.7.1 abre o AP no boot e fica nele | Configure uma rede Wi-Fi no aparelho ([capítulo 9](#cap-09-ap-aberto)) |
| Buracos de cerca de 15 min no histórico durante uma queda longa da rede | A escada de reconexão abriu o AP, e com o AP no ar o aparelho não mede (v2.7.1) | Confira no log os eventos 403 e 525. Restabeleça a rede; o trecho sem medição não é recuperado |
| O histórico parou e o log mostra 512 | O aparelho perdeu a referência de hora, e sem ela o histórico não grava | Corrija o acesso ao NTP ou acerte a hora ([capítulo 10](#cap-10)). O evento 513 marca a volta |
| O histórico parou e o log mostra 515 | Nenhum sensor está dando valor | Veja a tabela de sensores do **Painel de Controle** e o [capítulo 6](#cap-06-validacao) |
| O log mostra **Pulando arquivo de log ativo** (563) | O sistema de arquivos passou de 86 % de ocupação | Remova arquivos que você enviou, como temas, pacotes de idioma e `/web` ([capítulo 15](#cap-15-capacidade)) |
| Um sensor em alarme não fez a cigarra tocar | Som de alarme desligado, **Mudo Global**, volume em 0 ou um silêncio de 120 s em andamento | Confira os sons ([capítulo 7](#cap-07-sons)) |
| Um sensor desconectado não alarma | O slot está numa janela de manutenção, ou a falha foi calada com **Desativar** | Veja a linha **Manutenção** no menu do sensor ([capítulo 7](#cap-07-manutencao)) |
| [air]{.img} Um valor fora da faixa não gerou alarme | Nos despertares (M1), o Air não confere limites | Confira os limites no coletor da telemetria ([capítulo 19](#cap-19-alarmes)) |
| Um sensor mostra **Erro** no painel ou `ERRO` no LCD | Três leituras seguidas falharam: fio solto, pino errado ou sensor queimado | Confira a ligação e o GPIO do slot ([capítulo 6](#cap-06-erro-aparece)) |

## Instalação e primeiro boot {#cap-30-instalacao}

Detalhes nos capítulos [2](#cap-02), [3](#cap-03) e [4](#cap-04).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O aparelho reinicia sozinho, sem causa no log de eventos | Queda de tensão no cabo USB ou na fonte | Troque por um cabo de pelo menos 22 AWG e uma fonte firme ([capítulo 2](#cap-02-pico)) |
| O painel fica branco ou sem imagem | Fiação do SPI, do CS, do DC ou do RESET trocada ou solta | Confira a tabela do TFT pino a pino ([capítulo 2](#cap-02-tft)) |
| Pontos soltos em áreas lisas do painel ou do espelho da web | Fios longos ou ruins para os 62,5 MHz do TFT | Encurte a fiação; se não resolver, compile com frequência menor ([capítulo 3](#cap-03-fonte)) |
| O toque não responde | T_CS fora do GP17 ou T_IRQ fora do GP20 | Confira as ligações do toque ([capítulo 2](#cap-02-tft)) |
| O LCD mostra blocos cheios ou fica em branco | Contraste desajustado, ou LCD no 3V3 | Ajuste o potenciômetro do VO; alimente o LCD pelo 5 V ([capítulo 2](#cap-02-lcd)) |
| DS18B20 com **Erro de CRC** (103) ou falha ao ler a ROM (999) | Falta o resistor de 4,7 kΩ, ou há duas sondas no mesmo GPIO | Ponha o resistor entre o dado e o 3V3 e deixe uma sonda por GPIO ([capítulo 2](#cap-02-ds18b20)) |
| **Procurar sondas** não acha o BME280 | O sensor não está em GP4 (SDA) e GP5 (SCL) | Configure o slot à mão com os pinos certos ([capítulo 6](#cap-06-adicionar)) |
| Depois de gravar um BME280, o aparelho não termina o boot | SDA e SCL trocados, ou fora das colunas SDA e SCL, num periférico I²C | Recupere o aparelho ([capítulo 18](#cap-18)) e grave os pinos pela tabela do I²C ([capítulo 2](#cap-02-i2c)) |
| Dois BME280 mostram sempre os mesmos valores | Os dois estão em pares do mesmo periférico I²C | Mova um deles para um par do outro periférico, ou para um par cruzado ([capítulo 2](#cap-02-i2c)) |
| O log de eventos mostra `BME in bit-bang (pins have no hardware I2C)` | SDA e SCL em periféricos diferentes | Normal: o sensor é lido pelo PIO. Para usar o periférico, mova-o para um par da tabela |
| A página recusa o slot com `GP… already used by slot …` | Dois sensores no mesmo GPIO | Dê a cada sensor o seu GPIO; cada BME280 precisa do próprio par |
| [air]{.img} Os sensores do Air não respondem depois do sono | A chave de energia no GP16 atrasa ou não liga os sensores | Confira o circuito do GP16 ([capítulo 2](#cap-02-air)) |
| [air]{.img} O Air nunca hiberna | O GP17 está em nível alto: o aparelho acha que o carregador está ligado | Confira o divisor do carregador, ou desligue a função com `air charger off` ([capítulo 19](#cap-19)) |
| A unidade `RPI-RP2` não aparece | O botão BOOTSEL foi solto antes de ligar o cabo, ou o cabo só carrega e não transmite dados | Repita segurando o botão até o cabo estar ligado; troque o cabo ([capítulo 3](#cap-03-uf2)) |
| O `.bin` copiado para `RPI-RP2` não faz nada | A unidade do BOOTSEL só aceita `.uf2` | Copie o `.uf2` da mesma imagem |
| Depois de gravar, o painel ou o LCD fica apagado | Imagem de outro tipo | Grave pelo USB a imagem do seu hardware ([capítulo 3](#cap-03-escolher)) |
| O painel e a interface web estão em inglês | Não há pacote de idioma na pasta `/lang`, ou ele foi enviado e o aparelho não reiniciou | Envie `language_pt-BR.lng` pela página **Arquivos** e reinicie ([capítulo 3](#cap-03-idioma)) |
| O aparelho ficou em espanhol | Os dois pacotes estão na pasta `/lang`; o `es-ES` vem primeiro em ordem alfabética | Apague `language_es-ES.lng` e reinicie |
| Alguns textos ficaram em inglês depois de atualizar | O pacote de idioma é de uma versão anterior | Envie o pacote da versão nova e reinicie |
| O histórico e a configuração sumiram depois de gravar os arquivos da pasta `data/` | O `uploadfs` reformata o sistema de arquivos | Restaure o último backup ([capítulo 17](#cap-17-restauracao)); não use o `uploadfs` num aparelho em uso |
| A compilação falha no framework (`enableKeepAlive`) | O `patch.sh` não foi aplicado depois de instalar o framework | Rode `bash tools/arduino_pico_overrides/patch.sh` e compile de novo ([capítulo 3](#cap-03-fonte)) |
| Um aparelho 1.x voltou com a configuração de fábrica depois da atualização | O firmware 2.x não lê configurações das versões 1.x | Pegue a senha nova no console USB e configure de novo ([capítulo 4](#cap-04)) |
| O histórico de um aparelho 1.x sumiu depois da atualização | Os arquivos `.sim4` são apagados no primeiro boot | Converta cópias baixadas antes com `tools/history_v5.py --convert-v4` e envie os `.h5` para `/history` |
| [air]{.img} A porta serial do Air some e o toque de 1200 baud não funciona | O Air dorme e solta o USB | Grave com o Air acordado, em M0, ou pelo botão BOOTSEL |
| A senha do `admin` não apareceu no console | O programa serial abriu depois da moldura, ou o aparelho já reiniciou uma vez | Rode `system admin reset confirm` pelo cabo USB ([capítulo 4](#cap-04-senha)) |
| O console da release não mostra nada | O console de emergência só mostra o prompt depois de uma tecla Enter | Tecle Enter ([capítulo 14](#cap-14-usb)) |
| O aparelho novo não mostra leituras nem grava histórico | O ponto de acesso de configuração está aberto; na v2.7.1 o aparelho não mede nesse estado | Ponha o aparelho na rede ([capítulo 4](#cap-04-rede)) |
| O painel fica na tela de boot com **AP Ativo! Reinicie a placa para sair.** | Aparelho sem rede configurada, com o ponto de acesso aberto | Configure a rede pelo ponto de acesso; o painel sai dessa tela no reinício |
| [alpha]{.img} O LCD fica parado em `SIMUT 2.7.1` com a barra cheia | Pelo código da v2.7.1, o LCD não mostra as páginas do ponto de acesso aberto no boot | Pegue a rede e a chave no console USB ou com o comando `ap` ([capítulo 4](#cap-04-rede)) |
| A rede `SIMUT_SETUP` não aparece no celular | A rede real tem o nome do aparelho em minúsculas, `simut_SETUP` de fábrica | Procure pelo nome que o console ou a última linha do painel mostra |
| O celular não entra na rede do ponto de acesso | Chave errada | Confira a chave na linha `[AP] PSK` do console; ela usa só letras maiúsculas e algarismos |
| `system ssid Minha Rede` gravou só `Minha` | O console grava só até o primeiro espaço | Grave a rede pela página **Rede**, pelo ponto de acesso ([capítulo 4](#cap-04-rede)) |
| O ponto de acesso voltou cerca de 6 a 7 min depois de gravar a rede | A senha da rede está errada ou a rede está fora de alcance | Refaça a configuração pelo ponto de acesso ([capítulo 9](#cap-09-ap-quando)) |
| A página **Rede** mostra **Desconectado**, embora a interface abra pelo IP | O NTP está ligado e nenhum servidor de hora responde | Aponte um servidor de hora da rede ou acerte o relógio à mão ([capítulo 10](#cap-10-sem-ntp)) |
| `http://simut.local` não abre | Imagem alpha ou Air, ou aparelho esperando o primeiro acerto do relógio | Use o endereço IP ([capítulo 4](#cap-04-endereco)) |
| O painel e a web continuam em inglês depois de enviar o pacote | O aparelho não reiniciou: o pacote só é lido no boot | Reinicie o aparelho |
| A página só pede uma senha nova e não deixa ir a outra página | Troca obrigatória da senha de fábrica | Defina a senha nova ([capítulo 13](#cap-13-troca-obrigatoria)) |
| O PIN `1234` abre a tela **Novo PIN** em vez do menu | Troca obrigatória do PIN de fábrica | Defina o PIN novo ([capítulo 8](#cap-08-pin-fabrica)) |
| [air]{.img} O Air some do USB depois de alguns minutos | Sem atividade por 5 min, ele começa a hibernar | Religue o aparelho ou ligue o carregador para ele ficar acordado ([capítulo 19](#cap-19)) |

## Rede, Wi-Fi e ponto de acesso {#cap-30-rede}

Detalhes no [capítulo 9](#cap-09).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| A rede não aparece em **Buscar** | Rede de 5 GHz, rede oculta ou sinal fora de alcance | O aparelho só opera em 2,4 GHz. Para rede oculta, digite o nome no campo **SSID** |
| **Rádio ocupado — tente de novo em instantes** | O aparelho está buscando a própria rede para se reconectar | Espere alguns segundos e toque de novo em **Buscar** |
| A gravação recusa `net.pass` | A senha da rede tem mais de 31 caracteres, ou tem caractere de controle | Use uma senha de até 31 caracteres na rede dos aparelhos |
| O aparelho não entra na rede depois de trocar o roteador ou a senha | Rede configurada errada | Espere o AP de configuração abrir (cerca de 6 a 7 min depois de ligar), ou use o comando `ap` no console, e configure a rede pelo AP |
| O aparelho sumiu da rede e só voltou mais de uma hora depois, pelo AP | Ele tinha IP e perdeu a rede; o AP só abre ao fim de uma volta da escada (~68 min) | Normal. Para abrir o AP na hora, use o painel ou o comando `ap` |
| O AP some sozinho depois de 15 min | AP aberto com o aparelho em operação e rede configurada: o aparelho reinicia depois de 15 min para tentar a rede de novo | Grave a rede nova antes dos 15 min. Se a rede continuar fora, o AP volta em cerca de 6 a 7 min |
| Não sei a chave do AP | A chave é derivada do chip e não muda | Veja no console serial ao abrir o AP, na tela de boot do painel ou na resposta do comando `ap` |
| `http://<nome>.local` não abre | Imagem alpha ou Air (sem mDNS), aparelho ainda sem terminar de entrar na rede, ou rede que bloqueia mDNS | Use o IP. Com o NTP ligado e sem servidor NTP alcançável, o aparelho não termina de entrar na rede ([capítulo 10](#cap-10-sem-ntp)) |
| **Status** mostra **Desconectado** com um IP na linha de baixo | O aparelho ainda espera o primeiro acerto do relógio pelo NTP | Libere o acesso a um servidor NTP, aponte um servidor local, ou desligue o NTP e acerte o relógio à mão ([capítulo 10](#cap-10)) |
| Depois de mudar a **Porta HTTP**, a interface não abre | O endereço sem porta aponta para a porta antiga | Acrescente `:<porta>` ao endereço |
| Com IP fixo, o aparelho não acha servidores por nome | DNS automático ligado com IP fixo: o secundário não vale, e o primário pode ser o de fábrica | Desligue **Obter DNS automaticamente (DHCP)** e preencha **DNS Primário** |
| Depois de instalar o certificado, o endereço `http://` parou | Com certificado, o aparelho só atende HTTPS | Use `https://`, na 443 se a porta for 80 |
| `/api/tls` responde `empty body` | O `curl` enviou o corpo como formulário | Acrescente `-H 'Content-Type: application/x-pem-file'` |
| `/api/tls` responde `the key does not belong to this certificate` | Chave e certificado de pares diferentes | Envie a chave gerada junto com aquele certificado |
| `/api/tls` responde `Forbidden — admin only` | A conta não é administrador completo | Use a conta `admin` ou outra com todas as permissões |
| Instalei o par e o aparelho continua em HTTP | O par só vale no próximo boot, ou o aparelho ligou já no AP | Reinicie fora do AP. Veja no log o evento 576 |
| A interface HTTPS ficou inalcançável | Par que carrega mas cuja negociação falha | No console, `system https off confirm` |
| Depois de desligar o HTTPS, a entrada volta sempre para a página de entrada | Cookie `Secure` antigo no navegador | Siga o aviso da página de entrada ([capítulo 13](#cap-13-sessao-segura)) |
| O gestor de frota não consegue falar com o aparelho | CORS desligado, origem diferente da página, ou falta reiniciar | `system cors <origem exata, sem barra no fim>` e `reload confirm`. Confira o evento 577 no boot |
| O aparelho reconecta sozinho de tempos em tempos com sinal bom | O rádio devolveu leituras de sinal impossíveis | Normal: é a proteção contra rádio travado. Se for frequente, verifique a alimentação e a distância do roteador |
| A telemetria para com o sinal fraco | Abaixo de −78 dBm a telemetria espera | Melhore o sinal: aproxime o roteador ou use um repetidor |

## Data e hora {#cap-30-hora}

Detalhes no [capítulo 10](#cap-10).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| A página **Rede** mostra **Desconectado** com IP, e a telemetria não sai | NTP ligado sem servidor alcançável: o aparelho espera o primeiro acerto | Libere a porta UDP 123, aponte um servidor NTP da rede local, ou desligue o NTP e acerte o relógio à mão |
| O log mostra `NTP fallback: <servidor> -> pool.ntp.org` | O servidor configurado não respondeu três vezes seguidas | Confira o nome, o DNS e o firewall. A troca vale até o próximo reinício |
| A hora do painel está atrasada depois de uma falta de energia, com o NTP desligado | O relógio provisório parte do último registro e não conta o tempo desligado | Acerte o relógio pela seção **Data e Hora** depois de cada reinício, ou ligue o NTP |
| O log mostra **NTP corrigindo timestamps** com uma correção grande | O aparelho ficou desligado por esse tempo, ou o relógio provisório estava errado | Normal depois de uma falta de energia. Com o aparelho sempre ligado, investigue os eventos 524 anteriores |
| A hora do painel é diferente da hora do **Painel de Controle** na web | A web mostra no fuso do computador; o painel, no fuso do aparelho | Confira o **Fuso Horário** do aparelho e o do computador |
| A hora mudou em horas inteiras sozinha e voltou depois de um reinício | Alguém digitou no campo **Fuso Horário** e desistiu; o ensaio aplicou o fuso digitado | Reinicie o aparelho para voltar ao fuso gravado |
| A hora está errada em exatamente 1 h numa região com horário de verão | O aparelho não tem horário de verão | Ajuste o **Fuso Horário** nas datas de troca |
| O fuso da minha região tem meia hora (+5:30, −3:30) | O aparelho só aceita horas inteiras | Use a hora inteira mais próxima e leve a diferença em conta ao ler os dados |
| Medições gravadas antes de um acerto manual ficaram com a hora errada | O acerto manual não corrige o histórico, só o NTP corrige | Prefira o NTP. Com acerto manual, acerte logo depois de ligar |
| **Aplicar Agora** mostra **Falhou ao aplicar.** | Data anterior a 2020 ou conta sem a permissão **Sistema** | Confira a data e a conta |
| O LCD do alpha não mostra a hora | O LCD não tem relógio | Veja a hora na interface web |

## Contas, senhas e PIN {#cap-30-contas}

Detalhes no [capítulo 8](#cap-08).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| A conta criada pela web não aparece depois do reinício | Recusada: nome repetido ou inválido, permissão que a sua conta não tem, tabela cheia | Confira o nome e as permissões; crie de novo com uma conta que tenha todas as permissões pedidas |
| Não consigo mudar as permissões de uma conta na página **Usuários** | A página não edita permissões | Exclua e recrie a conta na mesma gravação (exclusão primeiro); as de painel mudam no item **Usuários** do painel |
| Perdi a senha de uso único antes de copiar | Ela aparece uma vez só | Toque em **Reset** na conta e grave de novo |
| A conta `viewer` não entra | A senha inicial dela não é informada | Toque em **Reset** na conta `viewer` e use a senha de uso único |
| Entro na web e vejo só `Access Denied` | A conta não tem a permissão **Painel** | Digite o endereço de uma página permitida, ou recrie a conta com **Painel** |
| Backup e atualização de firmware recusados para uma conta com todas as permissões | Essas ações exigem o nível de administrador completo | Use a conta `admin` |
| Minha conta não aparece em **Quem está usando o painel?** | A conta não tem PIN | Defina um PIN pelo botão **PIN** da página **Usuários** ou no item **Usuários** do painel |
| **PIN muito curto (mín. 4)** com um PIN definido pela web | A política exige mais caracteres que o PIN tem; a web aceita 4 a 8 dígitos sem olhar a política | Defina o PIN no painel, no item **Alterar Senha** ou em **Usuários** |
| **PIN já em uso!** | Outra conta tem esse PIN | Escolha outro PIN |
| Conta aparece com **bloqueado** no painel | Seis PINs errados seguidos dessa conta | Reinicie o aparelho ou use outra conta |
| Tela vermelha **ACESSO BLOQUEADO** | 20 PINs errados desde a última entrada certa | Reinicie o aparelho |
| Contagem **Aguarde N segundos...** no teclado | Terceira, quarta ou quinta tentativa errada de uma conta | Espere; a espera vale para qualquer conta nesse período |
| O painel pede **Novo PIN** logo depois de entrar | PIN de fábrica `1234` do `admin`, ou política de PIN mais exigente | Escolha um PIN novo que atenda à política |
| Depois de mudar a política pela web, a mudança não valeu | A política pela web só vale depois de **Salvar e reiniciar** | Grave com **Salvar e reiniciar**, ou mude no painel, que aplica na hora |
| Operador conseguiu se dar permissões de painel | No painel, quem tem **Usuários** concede as três permissões de painel sem ter todas | Dê **Usuários** só a quem pode ter também as permissões de painel |
| Bloqueio de 300 s ao entrar pela web | Nove ou mais senhas erradas seguidas a partir do mesmo computador | Espere a contagem; ela zera numa entrada certa |
| **Limite de sessões atingido. Tente mais tarde.** | Três outras contas com sessão aberta | Feche uma sessão com **Sair** ou espere 15 min de ociosidade dela |

## Configuração pela web {#cap-30-configuracao}

Detalhes no [capítulo 5](#cap-05).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Só aparece **Salvar e reiniciar**, com o selo **Alterações não salvas** | O ensaio ainda não respondeu, ou o conjunto inclui contas, slots de sensor ou calibração, que não podem ser ensaiados. No Air, é sempre assim | Espere um segundo. Se continuar, a alteração só se grava reiniciando |
| O selo mostra **Não exige reinício**, mas **Aplicar agora** reiniciou o aparelho | O conjunto tinha só campos da área de extensão (intervalo do histórico, NTP, syslog, Home Assistant, DNS automático ou secundário, conexões persistentes), que o ensaio não enxerga | Nada a corrigir: a alteração foi gravada. Para esses campos, use **Salvar e reiniciar** |
| **Testar** diz que aplicou, mas nada mudou | Os campos testados são da área de extensão, que **Testar** não aplica | Use **Salvar e reiniciar** |
| Um valor alterado voltou ao anterior depois de **Salvar e reiniciar** | O valor estava fora da faixa e o aparelho o recusou; esse botão não mostra **Campos não aplicados** | Confira a faixa do campo neste capítulo e grave de novo com um valor válido |
| **Falha ao salvar. — Forbidden (net)** (ou outra seção) | A conta não tem a permissão daquela seção, e a gravação inteira foi recusada | Peça a permissão, ou descarte as alterações daquela página (**Sair** e entrar de novo) |
| **Falha ao salvar. — Display in use. Retry shortly.** | Alguém tocou no painel há menos de 5 s | Espere alguns segundos e toque de novo no botão |
| **Falha ao salvar. — Password change required** | A conta está com troca de senha pendente | Troque a senha ([capítulo 13](#cap-13-troca-obrigatoria)) e grave de novo |
| **Falha ao salvar. — Bad payload** | O conjunto preparado passou de 6.144 bytes | Grave em partes, uma página por vez |
| A barra de gravação apareceu sem você alterar nada que importe | Os campos **Data** ou **Hora** da seção Data e Hora foram mexidos | Use o **Aplicar Agora** da seção para acertar o relógio. Para limpar a barra, **Sair** e entrar de novo |
| A página **Configurações** abre com os campos cinza e a faixa vermelha | A página não conseguiu ler a configuração do aparelho | Toque em **Tentar novamente**. Se persistir, verifique a rede e o log de eventos |
| **Registro Local** desligado, mas o log de eventos continua sendo gravado | A preferência não é consultada nesta versão | Nada a fazer; o log de eventos não tem interruptor pela web |
| Baixar a **Resolução DS18B20** não deixou a leitura mais rápida | O aparelho espera 750 ms por leitura em qualquer resolução | Deixe em 12 bits, a mais precisa |
| Os campos da política de PIN não aparecem | A imagem não tem painel de toque (alpha ou Air) | Normal: a política só existe na imagem release |
| **Resetar Calibração** não fez nada visível | Imagem alpha ou Air, sem painel de toque | Normal nessas imagens |

## Sensores e calibração {#cap-30-sensores}

Detalhes no [capítulo 6](#cap-06).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| **Falha ao salvar.** — `slot N: hardware ID cannot be empty on an active slot` | Slot ativado sem **ID de Hardware** | Preencha o ID (letras, dígitos, `_`, `-`, até 15) e grave de novo |
| **Falha ao salvar.** — `slot N: GPx already used by slot M` | Dois slots ativos no mesmo GPIO | Mude o GPIO de um deles, ou libere o outro slot na mesma gravação |
| Sensor novo não aparece no painel nem na página | A gravação não foi com **Salvar e reiniciar**, ou o slot não está **Ativo** | Abra o editor, confira **Ativo** e grave com **Salvar e reiniciar** |
| Log 105 **Divergência de hardware** num DS18B20 | Outra sonda foi ligada ao pino de um slot com ROM gravada | Volte a sonda certa (a quarentena acaba sozinha) ou use **Adotar a sonda ligada aqui** e reinicie |
| **Adotar a sonda** leu o pino errado ou respondeu **Nenhuma sonda respondeu neste GPIO.** | A adoção lê o GPIO de mesmo número que o slot | Libere o slot e crie-o de novo no GPIO certo; a ROM é lida na partida |
| Log 999 **Erro desconhecido** com um BME280/BMP280 | Falha de leitura I²C, ou temperatura fora de −40 a 85 °C | Confira SDA/SCL, a alimentação e se há mais de 2 sensores no mesmo par |
| BME280 não aparece em **Procurar sondas** | A busca só procura BMx280 em GP4/GP5 | Configure o slot à mão com os pinos usados |
| Umidade ou pressão somem de um BME280 | Valor fora de 0–100 % ou 300–1.100 hPa, descartado | Confira o sensor; um BMP280 não tem umidade |
| **Amostra (ms)** não muda o ritmo de leitura | O campo não tem efeito na v2.7.1 | O ritmo é fixo por tipo (1 s, 2 s, 5 s) |
| **Resolução DS18B20** não muda uma das sondas | A resolução só vai ao primeiro slot DS18B20 ativo | Nenhuma ação na v2.7.1; o ajuste não afeta o tempo de leitura |
| Mudar **Intervalo Histórico** com **Aplicar agora** reiniciou o aparelho | O campo exige reinício, e a classificação prévia não o vê | Use **Salvar e reiniciar** para este campo |
| Editor sem o bloco **Calibração** | Conta sem a permissão **Calibração**, ou slot novo ainda não gravado | Peça a permissão; grave o slot antes de calibrar |
| **Erro na calibração:** `NTP not synced` | Relógio não sincronizado por NTP | Sincronize o relógio ([capítulo 10](#cap-10)) e grave de novo |
| **Erro na calibração:** `rate limited` | Duas gravações de calibração em menos de 5 s | Espere 5 s e grave de novo |
| Correções sumiram depois de atualizar o firmware | A atualização apaga o `calib.csv` | Restaure o backup `.bkp` baixado antes da atualização |
| Envio de `calib.csv` pela página **Arquivos** não muda nada | O `VERSION` do arquivo enviado não é maior que o atual | Aumente o número da linha `VERSION,` e envie de novo |

## Alarmes e sons {#cap-30-alarmes}

Detalhes no [capítulo 7](#cap-07).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Desliguei os alarmes do sensor e a cigarra continua | O alarme ativo é de falha, que não se desliga pela chave | Resolva a falha, use **Desativar** na tela de alarme ou abra uma janela de manutenção |
| **Desativar** mostra **Sem permissão** | A conta do PIN não tem **Bloqueio (painel)** | Use outra conta ou peça a permissão a quem gerencia usuários |
| Linha do menu do sensor apagada, com cadeado | A conta não tem a permissão daquela linha | Peça a permissão: **Limites (painel)**, **Bloqueio (painel)** ou **Manut. (painel)** |
| Item **Limites de Alarme** não aparece no menu do painel | A conta não tem nenhuma das três permissões de painel | Conceda pelo menos uma na página **Usuários** ou no item **Usuários** do painel |
| **Falha ao salvar.** — `Alarm limit outside channel range` | Um limite fora da faixa plausível da grandeza | Corrija o valor; a gravação inteira foi recusada |
| Mudei um som na página e o aparelho mudou antes de gravar | A consulta prévia da página aplica os sons na memória (v2.7.1) | Grave ou descarte a mudança; um reinício volta ao que está gravado |
| Mudança de som pela web pede **Salvar e reiniciar** | Os sons ficam numa área da configuração que exige reinício | Use **Salvar e reiniciar**; pelo painel, a mudança vale sem reiniciar |
| Alarme novo não tocou | Estava dentro de um silêncio de 120 s, que vale para todos os sensores | Espere o fim do silêncio; a cigarra volta se o alarme persistir |
| Imagem alpha: cigarra toca sem parar | O alpha não tem como silenciar no aparelho | Desligue os alarmes do sensor em **Alarmes e Sons** ou ligue **Mudo Global**, e corrija a causa |
| Janela de manutenção não inicia pelo painel | 0 h e 0 min, ou conta sem **Manut. (painel)** | Ajuste pelo menos 5 min; use uma conta com a permissão |
| A janela terminou antes do esperado | O pedido passava de 30 dias e foi cortado, ou alguém tocou em **FECHAR** | Confira o log (códigos 454 e 455) e a lista `rejected` da resposta do servidor |
| Limite mudado pela web não aparece no servidor de alarmes | Só mudanças feitas no painel geram `alarm_lim` | Consulte `/api/alarms` ou mude pelo painel quando precisar do registro |

## Painel {#cap-30-painel}

Detalhes no [capítulo 11](#cap-11).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| A tela inicial não responde ao toque e a barra de cima mostra `WEB '...' - toque bloqueado` | Alguém lê o histórico ou o log pela interface web | Espere a operação terminar; o aviso some sozinho |
| O toque cai no lugar errado ou só funciona com força | Calibração do toque ruim ou descartada pelo alinhamento da tela | **Configurações > Calibrar Touch**; se não alcançar o menu, comece a calibração pela página **Configurações** da web ou pelo console |
| A calibração volta sempre para a primeira mira | **Toques imprecisos! Tente novamente.**: as duas voltas discordam | Segure cada mira até ela ficar verde e toque no centro dela, com a mesma pressão |
| A borda da imagem aparece cortada de um lado | Painel com a imagem deslocada | **Alinhamento da Tela**, ajuste, **APLICAR** e refaça a calibração que abre em seguida |
| O menu mostra poucos itens | A conta identificada não tem as permissões dos outros itens | Entre com outra conta, ou dê as permissões pela página **Usuários** |
| O toque no cartão abre a tela de alarme em vez do mínimo e máximo | O sensor está em alarme sem silêncio | Use **Min/Max** na tela de alarme, ou silencie |
| O gesto de 3 s no cartão de cima não faz nada | O sensor do cartão de cima está em alarme sem silêncio, ou o dedo escorregou e o toque recomeçou | Silencie o alarme; mantenha o dedo parado por 3 s |
| O botão de gráfico não abre o gráfico | O sensor está num slot de 11 a 15 | Use a página **Histórico e Logs** da web para esses slots |
| O gráfico abre sempre no mesmo período antigo, até o **Hoje** do calendário | A janela ficou presa depois de navegar no tempo (código da v2.7.1) | Toque na seta para a esquerda e depois na seta para a direita até ela apagar; ou reinicie |
| Mínimo e máximo do cartão incluem valores de ontem | Eles não zeram à meia-noite; contam desde o dia em que o aparelho ligou | Reinicie para recomeçar a contagem |
| Mínimo e máximo do cartão não acompanham as leituras | O GPIO do sensor difere do número do slot (código da v2.7.1) | Um reinício relê os valores do histórico. Onde der, ponha o sensor no slot de mesmo número do GPIO |
| `Fails` sempre `0` no **Status do Sistema** | O campo não é preenchido na v2.7.1 | Veja as falhas na página **Telemetria** ou no log de eventos |
| No boot, o celular não acha a rede `SIMUT_SETUP` | O nome real da rede é o nome do aparelho seguido de `_SETUP` | Procure `<nome>_SETUP`, como `simut_SETUP`; o nome aparece numa das últimas linhas da caixa de boot |
| O gesto do AP no boot não pega | O dedo encostou antes da linha **Mantenha a tela pressionada** ou saiu antes de 3 s | Encoste depois da linha aparecer, em até 3,5 s, e mantenha até a barra encher |
| Com o AP aberto, as leituras param e o **CFG** não abre nada | Com o AP aberto, o aparelho não mede nem atende o painel (v2.7.1) | Configure a rede pelo AP e deixe o aparelho reiniciar |
| O painel voltou sozinho à tela inicial e o tema escolhido não ficou | 30 s sem toque antes de **APLICAR** | Escolha de novo e toque em **APLICAR** |

## LCD do alpha {#cap-30-lcd}

Detalhes no [capítulo 12](#cap-12).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O LCD mostra `ERRO` fixo | Nenhum slot ativo | Configure um sensor pela interface web ([capítulo 6](#cap-06)) |
| `ERRO` aparece na vez de um sensor | O sensor não está lendo | Confira a ligação; o slot aparece no canto de baixo quando há mais de um sensor |
| O ícone de Wi-Fi é um `X` | Sem conexão ou sinal abaixo de −90 dBm | Aproxime o aparelho do roteador ou confira a rede ([capítulo 9](#cap-09)) |
| O ícone mostra sinal cheio com sinal fraco | Na v2.7.1, o nível mais baixo é uma linha na largura toda | Leia a altura do ícone, não a largura; a main já desenha a escada |
| O fim do boot mostra `Sem WiFi` / `Offline` | Rede ausente ou senha errada | Veja a escada de reconexão ([capítulo 9](#cap-09-reconexao)) |
| O celular não conecta ao AP | Chave digitada errada, ou rede errada | Confira a chave caractere a caractere, como o console a mostra, e a rede `<nome>_SETUP` |
| Com o AP aberto, o LCD fica na barra de boot cheia, ou nas leituras paradas | Na v2.7.1 o LCD não passa às páginas do AP (leitura do código) | Leia o nome da rede e a chave no console USB, ou na resposta do comando `ap`, também pelo Bluetooth ([capítulo 14](#cap-14)) |
| A cigarra toca e o LCD não mostra alarme | O LCD não mostra alarmes de limite | Veja os valores no ciclo; use a linha de alarmes ou a web para identificar o sensor |
| Os pendentes não aparecem | Mais de um sensor ativo, nenhum pendente, ou imagem v2.7.1 | Veja o **Painel de Controle** da web |

## Console e Bluetooth {#cap-30-console}

Detalhes no [capítulo 14](#cap-14).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O terminal abre e não aparece nada | Na `release`, o console não mostra cabeçalho | Tecle Enter: aparece `SIMUT>` |
| Cada letra aparece duas vezes | Eco local do terminal ligado; o aparelho já ecoa | Desligue o eco local |
| A porta `/dev/ttyACM0` não abre no Linux | O usuário não pertence ao grupo da porta | Inclua o usuário no grupo `dialout` (ou `uucp`) e entre de novo |
| `Comando desconhecido. As configuracoes ficam na interface web...` | O comando só existe no console completo, ou foi digitado errado | Veja `help`; configure pela web |
| `language en` responde "Comando desconhecido" | O console de emergência não tem `language`, embora o cabeçalho o sugira | Troque o idioma do aparelho pela web e reinicie |
| `system ssid Minha Rede` grava só `Minha` | O console corta o argumento no primeiro espaço | Use a interface web ou o AP de configuração |
| O aparelho não conecta depois de `system ssid` e `system pass` | A rede nova só vale depois de reiniciar | `reload confirm` |
| `ERROR: CLI ocupada (display em uso). Comando descartado.` | Alguém tocou no painel há menos de 5 s | Digite o comando de novo |
| O aparelho não aparece na busca Bluetooth | A janela de descoberta fecha 5 min depois do boot | Reinicie e pareie logo em seguida |
| O Bluetooth conecta mas não pede senha | O pedido só sai depois do primeiro caractere | Tecle Enter |
| `Bloqueado. Tente mais tarde.` | Senhas erradas seguidas; o bloqueio chega a 300 s e sobrevive à reconexão | Espere o prazo e use a senha do `admin` da web |
| `ERROR: Comando so pela USB (cabo serial).` | `system factory`, `format`, `admin reset` e `https off` são recusados pelo Bluetooth | Use o cabo USB |
| A sessão Bluetooth termina sozinha | 5 min sem digitar, ou a conexão caiu | Tecle Enter e digite a senha de novo |
| `debug` continua ligado depois de reiniciar | Outro comando ou a web gravou a configuração com o `debug` ligado | `debug off` e grave de novo |
| No console completo, a alteração sumiu depois de reiniciar | Faltou `write memory` | Grave com `write memory` (ou `do write memory` dentro da configuração) |
| `Comando requer modo privilegiado. Use 'enable'.` dentro da configuração | O comando é do privilegiado, como `sensor 3 name x` ou `write memory` | Use `do <comando>`, `end`, ou o modo sensor |
| `conf sensor ds18b20 resolution 12` falha | A forma do texto de `help` está errada | Use `ds18b20 resolution 12` na configuração |
| `air stop` pelo Bluetooth não tem efeito | Durante um despertar, o Air só lê o USB | Use o USB, ou ligue o carregador para o Air ficar acordado |
| `system format` deixou a interface em inglês | O formato apagou o pacote de idioma | Reinstale o pacote ([capítulo 17](#cap-17)) |

## Histórico {#cap-30-historico}

Detalhes no [capítulo 15](#cap-15).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O gráfico mostra **Sem dados.** para um dia destacado no calendário | A conta não tem a permissão **Leitura**, e a página não consegue baixar os arquivos do dia | Dê à conta **Histórico** e **Leitura** juntas |
| O gráfico mostra **Sem dados.** para o período escolhido | Nenhum registro dos sensores marcados na janela | Escolha outro período, outro dia no calendário ou outros sensores em **Origem:** |
| Aparece o aviso **Parcial: N perdidas** | N arquivos de dia não chegaram depois de 3 tentativas, ou você tocou em **Cancelar** | Toque de novo no período. Se persistir, confira a rede e o log de eventos |
| Os dias mais antigos sumiram do calendário | A ocupação passou de 86 % e o aparelho apagou os dias mais antigos | Exporte ou mantenha um coletor de telemetria. Libere espaço removendo arquivos enviados pela página **Arquivos** |
| Há um buraco no gráfico depois de uma falta de energia | O aparelho ficou desligado; a falta em si perde no máximo o último registro | Nada a fazer. Confira no log de eventos o boot e a correção de horas |
| Um sensor renomeado aparece com o nome novo nos dias anteriores | A página rotula os dados do slot com o nome e o ID atuais | Comportamento esperado. O arquivo guarda o slot, não o nome |
| Os dados de um slot removido não aparecem na página | A página só mostra os slots configurados agora | Leia os arquivos `.h5` com `tools/history_v5.py --dump-csv` |
| Arquivos que coloquei em `/history` sumiram depois de reiniciar | O boot apaga de `/history` tudo o que não é `.h5` | Guarde-os em outra pasta |
| O histórico sumiu depois de atualizar o firmware | A atualização reformata o sistema de arquivos | Restaure o `.bkp` baixado antes da atualização ([capítulo 17](#cap-17)) |
| As horas do CSV diferem das do painel | A página usa o fuso do computador; o painel, o do aparelho | Use um computador no mesmo fuso do aparelho |
| Depois de mudar o **Intervalo Histórico**, os dias antigos aparecem com os pontos espalhados | A página decodifica todos os arquivos com o intervalo atual | Leia os arquivos antigos com `history_v5.py --interval` igual ao intervalo da época |
| As horas saem erradas no `history_v5.py` | O `--interval` não é o **Intervalo Histórico** do aparelho | Passe o intervalo em segundos, como `--interval 300` para 5 min |
| A planilha junta tudo numa coluna só | O CSV usa vírgula como separador e ponto como decimal | Importe indicando a vírgula como separador e o ponto como decimal |

## Log de eventos {#cap-30-log}

Detalhes no [capítulo 16](#cap-16).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| A tabela de eventos mostra poucas linhas, só erros | De entrada, só a caixa **ERR** vem marcada | Marque **INF** e **WRN** |
| A tabela mostra `Error fetching logs` | Alguém estava usando o painel, ou a conta não tem a permissão **Logs** | Espere alguns segundos e toque em **Atualizar**. Confira as permissões da conta |
| Eventos de telemetria que aparecem no console não estão no log | O filtro por transição grava só as mudanças de estado e um registro por hora | Comportamento esperado. Veja **Registros de rotina suprimidos** (5) e use `debug on` ou o syslog para ver tudo |
| O log só vai até alguns dias atrás | O log guarda de 800 a 1.600 registros; os mais antigos saem | Envie o log a um coletor syslog para guardá-lo por mais tempo |
| Um registro `FTL` aparece com a data `Boot +` | A autópsia roda antes de o relógio ter hora | Situe o travamento pela data dos registros vizinhos |
| Vários **Boot do sistema** com `FTL` e contextos 200 a 229 | Travamentos do núcleo 0 detectados pelo watchdog | Anote o módulo (contexto − 200) e os registros irmãos, exporte o log e veja [Quando relatar um defeito](#cap-30-relatar) |
| A coluna `uptime_sec` do CSV diz `undefined` | Defeito da página na exportação do log | Use a coluna **Tempo** da tabela ou o `show system log` |
| **Limpar** responde **Falha ao limpar logs.** | A conta não tem **Logs** e **Sistema** juntas, ou tem uma troca de senha pendente | Use uma conta com as duas permissões e troque a senha pendente |
| Os nomes dos eventos aparecem sem acento | A página usa uma tabela própria, sem acentos | Comportamento esperado |
| Os nomes dos eventos aparecem em inglês | O idioma da página está em **EN** | Troque o idioma para **PT** no seletor da gaveta |
| O console mostra `log entries dropped (buffer full)` | Mais de 32 eventos chegaram enquanto o painel estava em uso ou uma operação pesada rodava | Os excedentes não foram para a flash. Nada a fazer, a não ser que se repita |
| [air]{.img} O log do Air quase não cresce | Num despertar, a sequência de boot não é gravada; sobra um registro por ciclo | Comportamento esperado. Procure o **Boot frio, não veio da hibernação** (412) |

## Atualização, backup e restauração {#cap-30-atualizacao}

Detalhes no [capítulo 17](#cap-17).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Os botões **Backup**, **Restaurar** e **Firmware** não aparecem | A conta não é o administrador completo | Entre com uma conta que tenha todas as permissões |
| **Falha ao baixar backup. Tente novamente.** na etapa 1 | O painel estava em uso, outra operação pesada rodava, ou a rede caiu | Espere alguns segundos e recomece. Nada mudou no aparelho |
| **Falha no envio (validação v=7). Cancelled.** | A imagem é de outra variante (release, alpha ou air) | Use o `.bin` da variante do aparelho. Restaure o backup antes de reiniciar |
| **Falha no envio (validação v=4)** ou **v=5** | O arquivo é pequeno demais ou grande demais para ser uma imagem | Confira que escolheu o `.bin` certo. Restaure o backup antes de reiniciar |
| **Falha no envio (validação v=6)** | O arquivo não é uma imagem do RP2040 | Use o `.bin` publicado, não o `.uf2` nem outro arquivo. Restaure o backup antes de reiniciar |
| **Falha no envio (validação v=undefined)** | O arquivo passa de 1 MiB, ou o envio caiu | Confira o arquivo e a rede. Restaure o backup antes de reiniciar |
| Depois de uma recusa no envio, o aparelho reiniciou e voltou sem rede e sem contas | A recusa reformatou o sistema de arquivos; a configuração estava só na RAM | Pegue a senha pelo console USB, configure a rede e restaure o backup ([capítulo 18](#cap-18-fabrica)) |
| **Aplicação recusada (HTTP 503)** | O painel estava em uso no instante da aplicação | Não reinicie. Repita `POST /api/ota/apply` com a sessão do administrador |
| A página de entrada não abre logo depois da atualização | O aparelho está gravando a imagem e reiniciando | Espere cerca de 1 min e recarregue |
| O aparelho continua na versão antiga | A aplicação não aconteceu | Confira a versão na gaveta e o log; refaça a atualização |
| Depois da atualização, a interface e o painel estão em inglês | O pacote de idioma foi apagado com o sistema de arquivos | Restaure o backup, ou envie o `.lng` da versão e reinicie |
| Depois da atualização, o aparelho responde em HTTP e não em HTTPS | O par do HTTPS foi apagado com o sistema de arquivos | Restaure o backup ou instale o par de novo ([capítulo 9](#cap-09-https-ota)) |
| Depois da atualização, os sensores leem sem correção | O `/calib.csv` foi apagado | Restaure o backup |
| O log mostra **Config alterada** (303), módulo `OTA`, nível `ERR`, logo depois de atualizar | O CRC da imagem gravada não confere | Grave a imagem pelo USB ([capítulo 18](#cap-18-bootsel)) |
| O aparelho não liga depois de uma falta de energia durante a atualização | O corte caiu na aplicação e o firmware ficou pela metade | BOOTSEL e `.uf2` pelo USB ([capítulo 18](#cap-18-bootsel)) |
| **Validação falhou: chip ID mismatch (backup is from another device)** | O backup é de outro aparelho | Use um backup deste aparelho |
| A restauração trouxe de volta uma senha antiga do administrador | A restauração devolve a configuração da data do backup | Troque a senha depois de restaurar |
| Faltam as medições entre a atualização e a restauração | A restauração substituiu o arquivo do dia pela cópia do backup | Restaure logo depois de atualizar ([capítulo 15](#cap-15-ota)) |
| **Falha ao aplicar: I/O error** | Falha de gravação, ou um backup com mais de 200 arquivos | Não reinicie; repita a restauração. Se persistir, veja o limite de 200 arquivos |
| A página diz **Upload concluído.**, mas o arquivo não aparece | O aparelho recusou o envio (nome inválido, sem espaço ou destino proibido) | Confira o nome, o espaço livre e a pasta |
| Um pacote de idioma enviado não fez efeito | O pacote só é lido no boot | Reinicie o aparelho |
| [air]{.img} A página do Air parou de responder durante a atualização | O Air hibernou | Ligue o carregador durante a atualização, ou ponha o Air em M0 com `air stop` |
| O reset de fábrica deixou o aparelho sem uma senha conhecida | A senha aleatória desse reset não é mostrada | Rode `system admin reset confirm` pelo console USB |

## Recuperação {#cap-30-recuperacao}

Detalhes no [capítulo 18](#cap-18).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| `system admin reset confirm` responde `ERROR: Comando so pela USB (cabo serial).` | O comando veio pelo Bluetooth | Use o cabo USB |
| [air]{.img} O console do Air recusa `system admin reset` | O console completo exige o modo privilegiado | Rode `enable` antes |
| O console mostrou `ERROR: NAO SALVOU: vale so ate reiniciar.` | A gravação da senha nova falhou | Rode o comando de novo antes de reiniciar |
| Não vi a senha inicial no boot | O console não estava conectado quando a moldura `SEC-003` apareceu | Rode `system admin reset confirm` |
| O painel mostra **ACESSO BLOQUEADO** | Vinte tentativas de PIN erradas somadas | Reinicie o aparelho |
| O console mostra `[AP] FAILED to start` | O nome do aparelho passa de 26 caracteres, ou o rádio falhou | Encurte o nome; reinicie e tente o `ap` de novo |
| O celular vê `<nome>_SETUP` e não conecta | Chave errada, ou versão anterior à v2.7.1 | Confira a chave no console; atualize para a v2.7.1 |
| O aparelho volta para o AP a cada poucos minutos | A rede configurada está errada ou fora de alcance; com o AP no ar, o aparelho não mede | Grave a rede certa pelo AP ou pelo console |
| O aparelho não aparece no USB nem na rede depois de uma atualização | O firmware ficou pela metade | Grave o `.uf2` pelo BOOTSEL |
| A unidade `RPI-RP2` não aparece | O botão não estava segurado ao ligar o cabo, ou o cabo só carrega | Repita segurando o BOOTSEL; use um cabo de dados |
| O truque dos 1200 bps não funciona | O console USB não existe: firmware pela metade, ou Air dormindo | Use o botão BOOTSEL |
| Depois de gravar pelo BOOTSEL, o aparelho se comporta de forma estranha | Restos de uma atualização interrompida na flash | `picotool erase` e grave de novo; depois restaure o backup |
| O log mostra **Falha no storage** (20) repetida | Sistema de arquivos com problema | Baixe o que puder, `system format confirm` e restaure o backup |
| [air]{.img} O Air não aparece no USB | Está dormindo; o USB some durante o sono | Ligue o carregador, ou desligue e ligue a alimentação |

## SIMUT Air {#cap-30-air}

Detalhes no [capítulo 19](#cap-19).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| [air]{.img} O Air não hiberna, com `chg=1` no `air status` | O carregador está ligado, ou o pino do carregador lê nível alto | Desligue o carregador; sem carregador, confira o divisor ou use `air charger off` |
| [air]{.img} O Air não hiberna, com `chg=0` | Uma aba do **Painel de Controle** aberta, um gestor de frota consultando sem `quiet=1`, ou comandos no console | Feche a aba; use `/api/status?quiet=1` no gestor |
| [air]{.img} O Air nunca liga o rádio | **Lote mínimo** em 0, pendentes abaixo do **Lote mínimo**, ou `skip` maior que 0 | Confira `tel=` e `skip=` no `air status` |
| [air]{.img} O coletor ficou sem dados depois de uma queda dele | Um envio que falha reserva 5 despertares sem rádio | Normal. Os registros esperam na flash e saem nos envios seguintes |
| [air]{.img} A interface web do Air não abre | O Air está em M1, sem servidor web | Ligue o carregador ou desligue e ligue a alimentação |
| [air]{.img} `air stop` não tem efeito | Foi digitado pelo Bluetooth, ou fora da janela do despertar | Use o USB durante um despertar, ou o carregador |
| [air]{.img} A linha `[AIR] alarm:` termina com `OVERRUN` | O despertar não cabe no **Intervalo Histórico** | Aumente o intervalo |
| [air]{.img} O log mostra **Ciclo Air retido em M0** (411) | Três reinícios pelo vigia seguidos | Entre, exporte o log e relate; o Air espera em M0 |
| [air]{.img} O log mostra **Wake anterior do Air não completou** (414) | Um despertar travou e foi reiniciado pelo vigia | Guarde o log e relate |
| [air]{.img} O log mostra **Boot frio, não veio da hibernação** (412) sem ninguém ter mexido | Falta de energia ou reinício | Confira a bateria e a alimentação |
| [air]{.img} As horas dos registros derivam cerca de 1 s por despertar | v2.7.1, sem o relógio carregado através do sono | Garanta despertares de telemetria para o NTP corrigir |
| [air]{.img} Baixar a resolução do DS18B20 não encurtou o despertar | O firmware espera 750 ms por conversão em qualquer resolução | Aumente o **Intervalo Histórico** para economizar bateria |
| [air]{.img} Depois de atualizar, o Air voltou a hibernar em 300 s e o carregador voltou ao GP17 | A atualização apagou o `/config/air.bin` | Restaure o backup, ou ajuste `air idle` e `air charger` de novo |
| [air]{.img} O nome `.local` do Air não responde | A imagem do Air não tem mDNS | Use o IP, visto com `show net status` |
| [air]{.img} `air charger 16` é recusado | O GP16 é o pino de alimentação dos sensores | Escolha outro pino |

## Telemetria e linha de alarmes {#cap-30-telemetria}

Detalhes nos capítulos [20](#cap-20), [21](#cap-21) e [22](#cap-22).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O coletor recebe dados de "dois aparelhos" que são o mesmo | O servidor usa o IP ou o nome como chave | Use o cabeçalho `X-SIMUT-Uid` como chave primária ([capítulo 20](#cap-20-identidade)) |
| Dois aparelhos configurados igual mostram `X-SIMUT-Cfg` diferentes | A configuração inclui sementes aleatórias das contas | Compare o valor de cada aparelho só com o histórico dele mesmo |
| No MQTT, não dá para saber de que aparelho veio uma mensagem | O MQTT não leva cabeçalhos e vários aparelhos usam o mesmo **Tópico** | Dê um **Tópico** próprio a cada aparelho |
| Nada chega ao coletor e o sinal está fraco | Com o sinal em −78 dBm ou abaixo, o aparelho adia telemetria e linha de alarmes | Melhore o sinal (posição, antena, repetidor); os registros ficam na flash e saem depois |
| O aparelho não aparece na busca mDNS | Imagem alpha ou Air, que não anunciam mDNS | Use o IP, a lista de clientes DHCP do roteador ou `GET /api/status` |
| Medições com hora errada ou fora de ordem no banco | Relógio do aparelho sem NTP, ainda na hora provisória | Ligue **Sincronizar automaticamente via NTP**; numa rede fechada, indique um servidor NTP local |
| Evento 512 no log e a telemetria parada | Sem referência de hora, o histórico não grava | Corrija a hora ou o acesso ao NTP; o evento 513 marca a volta |
| O firewall bloqueia o envio | A regra abre só a entrada, mas é o aparelho quem conecta | Libere a saída do aparelho para o coletor, na porta configurada |
| **Registros Pendentes** cresce e nada sai | **Lote mínimo** em 0, ou sinal Wi-Fi em −78 dBm ou abaixo | Defina o **Lote mínimo**; melhore o sinal |
| Evento 31 com o contexto −1 | Servidor, porta ou nome errado; firewall; TLS incompatível | Confira **IP Servidor** e **Porta**; teste o coletor com `curl` da mesma rede |
| Evento 31 com o contexto −11 | O coletor leva mais de 4 s para responder | Grave mais rápido ou responda assim que gravar |
| Evento 31 com o contexto 301 ou 302 | O coletor redireciona, e o aparelho não segue redirecionamentos | Aponte para o endereço final; para HTTPS, ligue o TLS |
| Evento 31 com o contexto 401 ou 403 | Chave de acesso errada ou ausente | Confira a **API Key** e a forma do cabeçalho |
| Evento 31 com o contexto 404 | **Endpoint** errado | Confira o caminho, começando com `/` |
| HTTP funciona, HTTPS falha com o contexto −1 | O servidor não aceita TLS 1.2 com ECDHE-GCM ou ignora o fragmento de 4.096 bytes | Ajuste o servidor: TLS 1.2, ECDHE-GCM e a extensão *Maximum Fragment Length* |
| A faixa **TLS sem validação de certificado** aparece | Sem `/cert.pem` válido | Envie o certificado da CA como `/cert.pem` pela página **Arquivos** e reinicie |
| Troquei o `/cert.pem` e nada mudou | O arquivo só é lido ao iniciar | Reinicie o aparelho |
| Mudei servidor ou porta no MQTT com **Aplicar agora** e continua no antigo | O cliente MQTT só lê a porta ao iniciar | Use **Salvar e reiniciar** |
| MQTT: evento 36 com `Bad credentials` ou `Not authorized` | **Usuário** ou **Senha** errados, ou falta de permissão no broker | Confira o usuário, a senha e a lista de acesso do broker |
| MQTT: nada chega e a porta é 80 | A **Porta** não muda ao trocar o transporte | Use 1883 (MQTT) ou 8883 (MQTTS) |
| O assinante MQTT quebra ao receber um array | Depois de uma parada, lotes com mais de 5 registros saem numa mensagem só | Aceite objeto e array |
| O coletor recebe `"camara":null` | Forma simples do marcador, com o sensor sem leitura | Use `"t0_ID":{t0}` ou `"t0":{t0}` para a chave sumir |
| JSON inválido no formato **Dinâmico** | Modelo mal escrito; o aparelho não valida | Confira a **Prévia ao Vivo** e o primeiro corpo recebido |
| JSON inválido no formato JSON, com filas longas (v2.7.1) | Lote grande montado com pouca memória | Responda 400 a corpo inválido: o cursor não avança e o lote sai de novo, menor; corrigido na v2.7.2 |
| Medições repetidas no banco | Duplicatas por desenho (confirmação perdida, queda de energia, reset do cursor) | Grave com chave única (`uid`, `ts`, canal) |
| Buraco nas medições depois de uma parada longa | Registros mais de 30 dias antes do mais novo, ou hora fora de ordem | Recupere pelo histórico do aparelho |
| A primeira ativação manda milhares de registros | O aparelho começa 30 dias antes do registro mais novo | Esperado; prepare o coletor para a carga inicial |
| Aviso `Telemetry cursor ahead of data — reset to 0` | O relógio andou para trás e o cursor ficou no futuro | Nada a fazer; confira o NTP |
| **Enviar agora** diz "Envio disparado", mas nada chega | Sinal fraco, envio já em andamento, ou lote mínimo 0 depois do primeiro lote | Confira o evento 546 e os seguintes no log; confira o sinal |
| Os envios param enquanto alguém usa o painel | O painel tem prioridade sobre a telemetria | Esperado; os envios voltam 5 s depois do último toque |
| Só um evento de falha no log, embora o coletor esteja fora há horas | O log grava transições e um registro por hora | Esperado; o console serial mostra todas as tentativas |
| Nenhum evento chega | Linha desligada, ou **IP Servidor** vazio na telemetria | Ligue **Habilitar linha de telemetria de alarmes** e confira o servidor |
| Evento 551 com 404 | Caminho errado | Confira o **Caminho HTTP**; vazio, ele vale **Endpoint** + `/alarm` |
| Os mesmos eventos chegam a cada 15 s | O coletor não confirma: HTTP fora de 2xx, ou falta a mensagem de confirmação no MQTT | Responda 2xx depois de gravar, ou publique `{"seq":[...]}` em `<tópico>/alarm/ack` |
| MQTT: a fila nunca esvazia, embora o coletor confirme | Telemetria de medições desligada (**Lote mínimo** 0): o aparelho não lê as confirmações | Ponha o **Lote mínimo** acima de 0 |
| MQTT: evento 551 com o contexto 0 a cada tentativa | A fila não cabe numa mensagem de cerca de 2 KB | Use um **Tamanho da fila (RAM)** de até 16 e confirme rápido; em último caso, reinicie, o que esvazia a fila |
| MQTT: registros somem da fila sem terem chegado | Confirmação retida no broker, ou dois aparelhos com o mesmo **Tópico** | Publique a confirmação sem retenção e apague a retida; dê um **Tópico** a cada aparelho |
| MQTT: só parte dos registros sai da fila | Confirmação com números entre aspas ou com quebras de linha | Publique a lista compacta: `{"seq":[1,2,3]}` |
| Evento 553 | Fila cheia: o coletor não confirma há tempo | Resolva a confirmação; aumente o **Tamanho da fila (RAM)** se o volume for real |
| Eventos se perdem depois de uma queda de energia | A fila vive na RAM | Esperado; confirme rápido para a fila ficar curta |
| `alarm_lim`, `alarm_on` ou `alarm_off` não chegam quando o limite muda pela web | Só as ações feitas no painel geram esses códigos | Consulte o estado pela API |
| Nenhum registro quando o valor volta ao normal | A linha não tem código de retorno ao normal | Use a telemetria de medições para saber que normalizou |
| Um novo `alarm`, `err` ou `maint_on` chega depois de um reinício | O aparelho refaz o estado das bordas ao iniciar | Trate como confirmação de estado, não como evento novo |
| `maint_on` e `alarm_lim` chegam sem `until`, `lo`, `hi` ou `user` | Modelo próprio antigo | Acrescente `"maint":{maint}`, `"lo":{lo}`, `"hi":{hi}`, `"until":{until}` e `"user":{user}` ao **2. Linha** |
| JSON inválido com `"valor":,` | Marcador fora da forma composta, sem valor naquele registro | Use a forma composta, como `"val":{val}` |
| O JSON da linha não é o do manual | O **2. Linha** foi editado e vale também no formato JSON | Confira o **2. Linha** no formato **Dinâmico** |
| A **Prévia ao Vivo** mostra `{maint}` ou `{user}` como texto | A prévia não conhece os marcadores novos | Ignore a prévia para esses marcadores; confira o corpo recebido |
| **Pendentes:** não muda | O número é lido ao abrir a página | Recarregue a página |

## Home Assistant, Prometheus e syslog {#cap-30-ha}

Detalhes nos capítulos [23](#cap-23), [24](#cap-24) e [25](#cap-25).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| O aparelho não aparece no Home Assistant depois de ligar a descoberta | O aparelho ainda não conectou ao broker: ele só conecta quando tem algo a enviar | Espere o primeiro envio depois do reinício; com **Lote mínimo (registros)** em 0, ligue a telemetria ([capítulo 23](#cap-23-ligar)) |
| Nenhum evento 548 no log | **Formato** diferente de **JSON**, ou o transporte não é **MQTT(S)** | Deixe o **Formato** em **JSON** e o **Transporte** em **MQTT(S)**, e grave com **Salvar e reiniciar** |
| Evento 548 aparece, mas nada chega ao Home Assistant | A integração MQTT do Home Assistant usa outro broker, outro prefixo ou está com a descoberta desligada | Confira o broker e o prefixo `homeassistant`; assine `homeassistant/sensor/#` com o `mosquitto_sub` para ver as mensagens |
| O dispositivo aparece, mas as entidades ficam sem valor | As medições chegam em array (lote de mais de 5 registros) | Ponha **Lote mínimo (registros)** em 1; para a fila atrasada, **Lote máximo (registros)** em 5 ou menos |
| As entidades voltam a ter valor só muito depois de uma parada | A fila atrasada saiu em lotes maiores que 5, que o Home Assistant não lê | Espere a fila acabar, ou use **Lote máximo (registros)** em 5 ou menos |
| Uma entidade fica sem valor e as outras não | O sensor desse slot está em falha e a chave dele sai do JSON | Veja o estado do sensor no **Painel de Controle** e no [capítulo 6](#cap-06-erro-aparece) |
| Entidades indisponíveis logo depois de um reinício do aparelho | O broker publicou a última vontade `offline`; o aparelho ainda não reconectou | Espere o primeiro envio depois do reinício |
| No Air, as entidades ficam indisponíveis a maior parte do tempo | O despertar termina sem despedida e o broker publica `offline` quando o **Keep-Alive** vence | Comportamento esperado; aumente o **Keep-Alive** até 300 s para adiar o `offline` ([capítulo 23](#cap-23-air)) |
| Entidade antiga, sem valores, depois de remover um slot ou mudar o ID de hardware | O aparelho não apaga mensagens de descoberta que não correspondem mais à configuração | Apague o tópico com `mosquitto_pub -r -n -t 'homeassistant/sensor/<nó>/<objeto>/config'` ([capítulo 23](#cap-23-remover)) |
| Dois dispositivos para o mesmo aparelho | O **Client ID** mudou, e o nó novo virou outro dispositivo | Apague as mensagens retidas do nó antigo com `mosquitto_pub -r -n` |
| O dispositivo apagado no Home Assistant volta sozinho | A descoberta continua ligada no aparelho, que publica tudo de novo a cada conexão | Desligue **Home Assistant Discovery** no aparelho antes de apagar |
| Trocou o transporte para HTTP e o dispositivo ficou no Home Assistant, indisponível | Sem MQTT, o aparelho não tem como apagar a descoberta | Volte ao MQTT, desligue a descoberta, espere o evento 548 e só então troque o transporte; ou apague os tópicos com `mosquitto_pub -r -n` |
| O texto do evento 548 diz `(skipped N)` | Chave com aspas ou barra invertida (ID de hardware gravado por versão antiga) ou mensagem de descoberta grande demais | Troque o ID de hardware por um só com letras, dígitos, `_` e `-`; encurte nomes muito longos |
| O link de configuração do Home Assistant não abre a página | O link é sempre `http://`, e o aparelho está em HTTPS | Abra `https://` com o mesmo endereço |
| Os gráficos do Home Assistant mostram medições atrasadas todas no mesmo instante | O Home Assistant registra a hora da chegada, não o `ts` do registro | Para histórico fiel, receba a telemetria num coletor que use o `ts` ([capítulo 21](#cap-21)) |
| Nada chega ao Home Assistant enquanto o aparelho está com o AP de configuração aberto | Com o AP aberto, o aparelho não envia telemetria | Resolva a conexão Wi-Fi ([capítulo 9](#cap-09-ap-aberto)) |
| O alvo aparece como fora do ar e o `curl` recebe `401 unauthorized` | Conta ou senha erradas no `basic_auth` | Confira a conta e a senha; teste com `curl -u` a partir do servidor do Prometheus |
| O `curl` recebe `403 forbidden` | A conta não tem a permissão **Painel** | Dê à conta a permissão **Painel** na página **Usuários** ([capítulo 8](#cap-08-mudar)) |
| O `curl` recebe `429` com `lockSec`, mesmo com a senha certa | O endereço do servidor está bloqueado por senhas erradas anteriores | Espere o `lockSec`; depois, entre uma vez pela interface web a partir desse servidor para zerar a contagem ([capítulo 24](#cap-24-bloqueio)) |
| Depois de corrigir a senha, um único erro bloqueia por 300 s | A contagem de erros continua alta: leitura bem-sucedida não a zera | Entre uma vez pela interface web a partir do servidor do Prometheus |
| Operadores bloqueados na página de entrada sem ter errado a senha | O Prometheus, com senha errada, divide o bloqueio com eles pelo mesmo endereço ou NAT | Corrija a senha no Prometheus; separe o servidor do Prometheus da rede dos operadores |
| O log de eventos se enche de 301 **Falha de login**, com contexto 0 | Senha errada no Prometheus; o syslog mostra a conta no texto | Corrija a senha; cada erro depois do nono grava um evento a cada 300 s, mais ou menos |
| A interface web fica lenta em intervalos regulares | Cada leitura do Prometheus ocupa o aparelho por cerca de 0,7 s para conferir a senha | Aumente o `scrape_interval` para 60 s; não use menos de 15 s |
| A leitura estoura o tempo em HTTPS | Outra conexão segura estava aberta, e o aparelho atende uma por vez | Mantenha o `scrape_timeout` em 10 s e as conexões persistentes ligadas ([capítulo 9](#cap-09-servidor-web)) |
| Erro de certificado no Prometheus em HTTPS | O certificado não é de uma autoridade conhecida ou não vale para o endereço do alvo | Indique o certificado em `tls_config.ca_file`; use no alvo o nome do certificado ou `tls_config.server_name` |
| A série de um sensor some e outra aparece | O nome do slot mudou, e o rótulo `name` junto | Use `hwid` nas consultas e regras |
| Um sensor some de `simut_temperature_celsius` | O sensor está em falha (`simut_sensor_ok` 0) ou ainda não tem leitura válida | Veja o sensor no **Painel de Controle** e no [capítulo 6](#cap-06-erro-aparece) |
| `simut_ntp_synced` vale 1 mas a hora está errada | Na v2.7.1, a métrica conta a hora provisória como hora certa | Não use a métrica para alertar sobre NTP; acompanhe os eventos 512 e 513 |
| Contadores voltam a zero | O aparelho reiniciou | Use `rate()` e `increase()`, que tratam a volta a zero; veja `simut_uptime_seconds` |
| O Air aparece fora do ar quase sempre | O Air só atende a rede acordado em M0 | Comportamento esperado; acompanhe o Air pela telemetria ([capítulo 24](#cap-24-air)) |
| O Air não volta a dormir e a bateria cai rápido | O Prometheus o consulta em M0 mais rápido que o prazo de inatividade, e cada leitura conta como uso | Tire o Air do Prometheus ou só o consulte no carregador |
| O aparelho some do Prometheus enquanto o AP de configuração está aberto | Com o AP aberto, a interface web só atende pela rede do AP | Resolva a conexão Wi-Fi ([capítulo 9](#cap-09-ap-aberto)) |
| Nada chega ao coletor | **IP do Coletor** vazio, interruptor desligado, ou a gravação não reiniciou o aparelho | Preencha o IPv4, ligue **Habilitar encaminhamento syslog** e grave com **Salvar e reiniciar** ([capítulo 25](#cap-25-configurar)) |
| O campo **IP do Coletor** recusa o valor | Foi digitado um nome, como `coletor.exemplo.com.br` | Use o endereço IPv4 do coletor |
| Nada chega, e o `tcpdump` no coletor vê os datagramas | O firewall do coletor bloqueia a porta, ou o rsyslog/syslog-ng não escuta UDP nessa porta | Libere a **Porta UDP** no firewall; confira o `imudp` do rsyslog ou o `transport("udp")` do syslog-ng |
| Nada chega com o **Nível Mínimo** em **Fatal** | Os únicos eventos fatais são os registros de partida, que o syslog não recebe | Use **Error** ou mais baixo |
| Linhas chegam sem hora (`-` no lugar da data) | O aparelho ainda não acertou o relógio pelo NTP nem à mão | Use a hora de chegada do coletor; corrija o NTP ([capítulo 10](#cap-10)) |
| O motivo de um reinício não aparece no coletor | Os registros de partida e a autópsia são gravados antes de o encaminhamento começar | Leia o log de eventos do aparelho ([capítulo 16](#cap-16-travamento)) |
| Faltam linhas de um período em que o Wi-Fi caiu | A fila guarda só as 8 linhas mais novas enquanto não há rede | Confira o log de eventos do aparelho, que guarda o período |
| Nada chega enquanto o AP de configuração está aberto | Com o AP aberto, o aparelho não manda syslog | Resolva a conexão Wi-Fi ([capítulo 9](#cap-09-ap-aberto)) |
| Nada chega dos despertares do Air | O Air só manda syslog acordado em M0 | Comportamento esperado; os eventos ficam no log de eventos do Air |
| Muito mais linhas no coletor que no log de eventos | O syslog não aplica o filtro de rotina; com **Info**, cada envio de telemetria vira uma linha | Use **Warning**, ou filtre no coletor pelo `MSGID` |
| Não chegam as entradas bem-sucedidas | **Nível Mínimo** em **Warning** ou acima; a entrada (300) é de nível Informação | Use **Info** |
| Dois aparelhos aparecem com o mesmo `HOSTNAME` | O `HOSTNAME` é o **Nome**, que pode se repetir | Dê um **Nome** único a cada aparelho, ou separe pelo IP de origem |
| O `HOSTNAME` aparece com hífens no lugar de letras | Espaços e letras acentuadas do **Nome** viram `-` | Use no **Nome** só letras sem acento, dígitos e hífen, se o coletor depende do nome |
| Texto de evento cortado | A linha passou de 255 bytes | Leia o detalhe completo no console serial, no momento do evento |
| Linhas estranhas atribuídas ao aparelho | O UDP não autentica: qualquer máquina pode mandar linhas com o nome do aparelho | Aceite no coletor só os IPs dos aparelhos |
| Linhas em inglês num aparelho em português | O aparelho não tem o pacote de idioma, ou o texto do detalhe não tem tradução | Instale o pacote ([capítulo 13](#cap-13-idioma)); filtre sempre pelo `MSGID` |

## API REST e gestão de frota {#cap-30-api}

Detalhes nos capítulos [26](#cap-26) e [27](#cap-27).

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| `POST /api/login` responde `401` `{"ok":false,"err":1}` com a senha certa | Faltou o `GET /api/login_init` antes, o código já foi usado numa tentativa anterior, ou passou de 60 s | Peça um código novo antes de cada tentativa |
| A entrada falha só para uma conta com acento na senha | O resumo foi calculado sobre o UTF-8; o aparelho usa Latin-1 | Codifique a senha em Latin-1 antes do SHA-256, ou use uma senha ASCII na conta do programa |
| A entrada funciona, mas todo pedido seguinte dá `401` | O cliente não guardou o cookie, ou manda um `Cookie` de outro site junto do `Authorization: Bearer` | Use um pote de cookies, ou mande só `Authorization: Bearer` sem nenhum cookie |
| A sessão do script cai sozinha a cada poucos minutos | Outro processo, ou um navegador, entra com a mesma conta e substitui a sessão | Dê uma conta a cada processo; não abra sessão por pedido |
| `403` `{"ok":false,"err":3}` na entrada | As três vagas de sessão estão ocupadas por outras contas | Saia das sessões paradas (`GET /logout`) ou espere 15 min de ociosidade delas |
| `403` numa rota que a conta deveria poder usar | Em várias rotas, `403` também significa sessão vencida | Pergunte `GET /api/perms`: `401` = entre de novo; `200` = falta a permissão |
| `429` `{"ok":false,"err":3,"retryAfter":N}` no `login_init` | Os 8 lugares da tabela de bloqueio estão com bloqueio ativo | Espere os segundos de `retryAfter` |
| Um gestor e os navegadores dos operadores se bloqueiam mutuamente | Estão atrás do mesmo NAT e dividem o bloqueio por IP | Evite erros de senha no gestor; se possível, dê a ele um endereço próprio |
| `commit_all` responde `200`, mas nada mudou | Campo recusado (veja `rejected`), campo desconhecido ignorado, ou texto com `\uXXXX` | Leia `rejected`; confira o nome da chave; mande acentos em UTF-8 direto (`ensure_ascii=False`) |
| `commit_all` responde `400` `{"error":"Missing _payload"}` | O corpo foi enviado como JSON e não como formulário | Mande `_payload` como campo de formulário, com `--data-urlencode` |
| `set_time` ou `calib` respondem `missing epoch` ou `empty body` | O JSON foi enviado como formulário, o padrão do `curl -d` | Acrescente `-H 'Content-Type: application/json'` |
| Um nome gravado aparece como `Cozinha \u00e7` | O cliente escapou o acento em `\uXXXX` | Mande UTF-8 direto no JSON |
| O ensaio (`_dry=1`) disse que não reinicia, mas a gravação reiniciou | O conjunto tinha campos que o ensaio não enxerga (`h_int`, `ntp_enabled`, `slog_*`, `m_had`, `dns_auto`, `dns2`, `web_ka`) | Grave esses campos com `_reboot=1` e conte com o reinício |
| A hora local ou o volume mudaram depois de um ensaio | O ensaio aplica o fuso e os sons de verdade | Não mande `tz` nem `sounds` num ensaio que não deve ter efeito; reinicie para voltar |
| A origem CORS foi gravada, mas o navegador continua bloqueando | A origem só vale depois de um reinício | Reinicie com `POST /api/action?op=reboot` ou grave com `_reboot=1` |
| O gestor no navegador vê "falha de rede" em vez de `401` | O aparelho está sem origem CORS, ou com outra origem | Configure a origem exata da página ([capítulo 9](#cap-09-cors)); um `OPTIONS` responde `405` `CORS disabled` quando ela falta |
| A conta criada pela API não aparece | `rejected` trouxe `users.perms`, `users.name`, `users.dup` ou `users.full` | Corrija o motivo; `users.perms` = a conta que pede não tem todas as permissões pedidas |
| A exclusão de uma conta não apagou nada | A ação usou `name` em vez de `id` | Leia o `id` em `GET /api/users` e apague por `id` |
| O PIN definido pela API não abre o painel | O PIN é mais curto que o mínimo da política, ou a política exige letras ou mais de 8 caracteres | Use só algarismos com comprimento dentro da política e de 4 a 8, ou defina o PIN no painel |
| Todo pedido depois de gravar contas dá erro de conexão | Gravar contas reinicia o aparelho logo depois de responder | Espere o aparelho voltar (`GET /api/login_init` até responder) e entre de novo |
| `503` `Display in use. Retry shortly.` em gravações | Alguém tocou no painel, ou um cliente mandou `POST /api/touch`, há menos de 5 s | Espere o `Retry-After` e repita |
| O arquivo enviado foi parar na raiz | `uploadDir` veio depois do arquivo no formulário | Ponha `-F uploadDir=...` antes de `-F file=@...`, ou use `?uploadDir=` no endereço |
| `429` `Too Fast` em `/api/ls` ou `/api/logs` | Menos de 200 ms desde o pedido anterior do mesmo IP a uma das rotas limitadas | Espace os pedidos; lembre que `ls`, `logs`, `calib` e `tls` dividem o mesmo relógio |
| `503` `Already processing` no histórico | Outro pedido de histórico está em andamento | Um pedido de histórico por vez |
| `history_multi` responde só números de estimativa, com `sliceRequired` | O período pedido gera uma resposta grande demais | Peça em fatias com `from` e `to` |
| O `.h5` baixado não tem as últimas medições | Os arquivos por dia só têm blocos selados | Leia também `GET /api/history/open` |
| A restauração responde `st` 6 | O backup é de outro aparelho | Um backup só restaura no aparelho que o gerou |
| O envio da imagem responde `422` com `"v":7` | A imagem é de outra variante (release, alpha ou Air) | Envie a imagem com o mesmo `env` que `/api/status` informa |
| `POST /api/ota/apply` responde `409` | Nenhuma imagem preparada com `commit=1`, ou o aparelho reiniciou depois do envio | Envie a imagem de novo com `op=stage&commit=1` |
| Um envio longo cai no meio | Algum equipamento da rede corta conexões longas na porta 80 | Configure outra porta web ([capítulo 9](#cap-09-servidor-web)) |
| `avahi-browse` não acha alguns aparelhos | São alpha ou Air, que não têm mDNS; ou estão em outra rede ou VLAN | Descubra-os pelos cabeçalhos `X-SIMUT-*` da telemetria no coletor |
| Dois aparelhos respondem pelo mesmo `<nome>.local` | Os dois têm o mesmo **Nome** | Dê um nome único a cada aparelho |
| O gestor gravou a configuração no aparelho errado | O DHCP deu ao aparelho o IP que era de outro | Compare `sys.uid` com o cadastro antes de cada gravação |
| As sessões do gestor caem sozinhas | Dois trabalhadores, ou um operador, usam a mesma conta no mesmo aparelho | Uma conta por processo; operadores com contas próprias |
| Operadores recebem **Limite de sessões atingido** | O gestor ocupa vagas e não sai | Uma sessão por aparelho, reaproveitada; `GET /logout` ao terminar |
| A conta de serviço cria contas, mas as de painel voltam com `users.perms` | A conta de serviço não tem as permissões de painel que tenta conceder | Dê a ela `0x0400`, `0x0800` e `0x1000` (a conta de 7433) |
| O gestor não consegue atualizar o firmware | A conta de serviço não é administrador completo | A atualização exige o administrador completo ([capítulo 17](#cap-17-ota)) |
| A página do gestor não recebe `token` na entrada | A origem da página não é exatamente a origem configurada no aparelho, ou o aparelho não reiniciou depois da configuração | Confira esquema, nome e porta da origem; reinicie o aparelho |
| A página do gestor em HTTPS não fala com os aparelhos | Aparelhos em HTTP (conteúdo misto) ou certificado em que o navegador não confia | Use HTTPS nos aparelhos, com certificados assinados por uma autoridade que os navegadores aceitem |
| O gestor no navegador perdeu um aparelho depois da atualização | A atualização apagou a origem CORS e o certificado HTTPS | Restaure o backup por um serviço no servidor ou pela página **Arquivos** do aparelho |
| Um lote de configuração reiniciou toda a frota | O modelo tinha campos que reiniciam (`tz`, `t_sec`, rede, contas) | Separe as mudanças que reiniciam e aplique-as em ondas, fora do horário de uso |
| O ensaio passou, mas a gravação reiniciou | O modelo tinha campos que o ensaio não enxerga | Veja a lista no [capítulo 26](#cap-26-commit) e conte com o reinício |
| O `cfg` de um aparelho mudou sem gravação do gestor | Alguém mudou a configuração pela página, pelo painel ou pelo console | Leia `GET /api/config` e compare com o modelo |
| Os Air nunca hibernam | O Prometheus ou o gestor consultam o Air com uma conta válida | Tire o Air da coleta; use `?quiet=1` no `/api/status` |
| O Prometheus bloqueia a entrada web de quem está no mesmo endereço | Senha errada no Basic conta no mesmo bloqueio por IP | Corrija a senha do Prometheus |
| Um aparelho atualizado continua na versão antiga | A aplicação foi recusada (`409` ou `403`) ou a imagem não foi preparada com `commit=1` | Leia a resposta de cada passo; só a versão informada pelo aparelho prova a atualização |
| O envio da imagem volta com `"v":7` | A imagem é de outra variante | Escolha `images[env]` pelo `sys.env` do aparelho |
| Depois da atualização e da restauração, textos novos aparecem em inglês | O backup restaurado trouxe os pacotes de idioma antigos | Envie os `.lng` da release nova para `/lang` e reinicie |
| Um aparelho com firmware antigo não atualiza pela rede | Versão anterior a `min_from` | Grave pelo cabo USB uma vez ([capítulo 3](#cap-03)) |
| Um aparelho sumiu do coletor | Sem rede, sem energia, sinal abaixo de −78 dBm ou aparelho parado | Compare a hora do último POST com o ritmo esperado e verifique o local |

## Quando relatar um defeito {#cap-30-relatar}

Se nada aqui explica o que você vê, relate o defeito. Um relato que traz estas informações quase sempre leva à causa:

1. A imagem (release, alpha ou Air) e a versão que a gaveta da interface web mostra.
2. O que você fez, em que ordem e a que horas, pela hora do aparelho.
3. O log de eventos exportado em CSV pela página **Histórico e Logs**, ou a saída de `show system log` pelo console USB.
4. Num travamento, os registros **Boot do sistema** com nível `FTL` e os registros irmãos logo em seguida ([capítulo 16](#cap-16-travamento)).
5. Uma captura da tela do painel, pelo espelho ou pela rota `/api/screenshot`, ou uma foto do LCD.

::: atencao
**Não publique o backup nem as credenciais.** O arquivo `.bkp` traz a configuração inteira, com a senha do Wi-Fi apenas embaralhada, e o par do certificado HTTPS. Não anexe a um relato público o backup, a chave do certificado, senhas ou a chave de API da telemetria.
:::

Relate um defeito comum nas issues do repositório: <https://github.com/angeloINTJ/simut/issues>. Uma falha de segurança vai pelo relato privado, e não por uma issue pública ([capítulo 29](#cap-29-relatar)).
