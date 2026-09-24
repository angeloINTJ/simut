# Glossário {#cap-31}

Os termos que este manual usa, com o sentido que têm no SIMUT. Cada termo aponta para o capítulo que o explica em detalhe.

## A a C {#cap-31-a-c}

Administrador completo
:   Conta com todas as permissões. Só ela faz backup, restaura, aplica uma atualização e instala o certificado HTTPS. A conta `admin` é uma delas ([capítulo 8](#cap-08-admin)).

Adotar a sonda
:   Operação do editor de slot que lê a ROM de um DS18B20 e liga o slot a ela ([capítulo 6](#cap-06-adotar)).

Air (SIMUT Air)
:   Imagem sem display que hiberna entre um despertar e outro, para funcionar a bateria. É experimental ([capítulo 19](#cap-19)).

Alpha
:   Imagem com LCD de 16×2 caracteres e console Bluetooth, sem toque ([capítulo 12](#cap-12)).

AP de configuração
:   Ver *ponto de acesso de configuração*.

Aparelho
:   O SIMUT: a placa Pico W com o firmware, os sensores e, conforme a imagem, o painel ou o LCD.

Aplicar agora
:   Botão da interface web que grava a configuração e aplica sem reiniciar o que pode ser aplicado assim ([capítulo 5](#cap-05-botoes)).

Autópsia
:   O diagnóstico que o aparelho faz no boot depois de um travamento, a partir dos vestígios que sobrevivem ao reinício. O veredito vai ao log de eventos ([capítulo 16](#cap-16-travamento)).

Backup
:   Arquivo `.bkp` com a configuração e os arquivos do aparelho, baixado pela página **Arquivos** ([capítulo 17](#cap-17-backup)).

Bloco
:   Grupo de até 60 registros do histórico, com o próprio código de verificação. Com o intervalo de fábrica, um bloco cobre 1 h ([capítulo 15](#cap-15-blocos)).

Bloco aberto
:   O bloco que ainda está recebendo registros. Ele fica na RAM, com uma cópia em `/history/.wip` ([capítulo 15](#cap-15-blocos)).

BOOTSEL
:   Botão da placa Pico W. Segurado ao ligar o cabo USB, faz a placa aparecer no computador como um disco, para gravar um arquivo `.uf2` ([capítulo 18](#cap-18-bootsel)).

Canal
:   Uma grandeza de um sensor: temperatura, umidade ou pressão. Um slot tem até quatro canais ([capítulo 6](#cap-06-grandezas)).

Coletor
:   O servidor que recebe a telemetria e a linha de alarmes ([capítulo 20](#cap-20)).

Console completo
:   O console serial com todos os comandos, nas imagens de teste e no Air ([capítulo 14](#cap-14-completo)).

Console de emergência
:   O console serial reduzido das imagens release e alpha, com 14 comandos: os de recuperação, os de rede e os de leitura do estado ([capítulo 14](#cap-14-emergencia)).

Console serial
:   A linha de comandos do aparelho, pelo cabo USB ou, no alpha e no Air, pelo Bluetooth ([capítulo 14](#cap-14)).

Conta
:   Nome, senha, PIN e permissões de uma pessoa ou de um sistema. O aparelho guarda até 32 ([capítulo 8](#cap-08-modelo)).

CORS
:   Autorização para que uma página hospedada em outro servidor, como um gestor de frota, fale com o aparelho ([capítulo 26](#cap-26-cors)).

## D a L {#cap-31-d-l}

Despertar
:   No Air, o intervalo curto em que o aparelho sai da hibernação, lê os sensores, grava e, quando é hora, envia a telemetria ([capítulo 19](#cap-19-ciclo)).

Dia selado
:   O arquivo de histórico de um dia, `/history/<dia>.h5`. Ele só contém blocos já selados ([capítulo 15](#cap-15-guardado)).

Dormência
:   Fase da escada de reconexão em que o aparelho espera 10 min antes de cada busca pela rede ([capítulo 9](#cap-09-reconexao)).

Época do slot
:   A data em que o sensor atual começou num slot. O aparelho a grava quando o tipo do slot muda, quando uma sonda adotada troca o ID do slot, ou com **Resetar época do histórico**. Na v2.7.1, nenhum gráfico usa essa data ([capítulo 6](#cap-06-epoca)).

Escada de reconexão
:   A sequência de tentativas, com esperas crescentes, que o aparelho segue quando perde a rede Wi-Fi ([capítulo 9](#cap-09-reconexao)).

Espelho do painel
:   Na imagem release, o painel mostrado ao vivo no **Painel de Controle** da interface web. Um clique no espelho vale como um toque no painel ([capítulo 13](#cap-13-espelho)).

Gestor de frota
:   Um programa ou uma página que administra vários aparelhos pela API ([capítulo 27](#cap-27)).

Histórico
:   As medições gravadas no próprio aparelho, um registro a cada **Intervalo Histórico** ([capítulo 15](#cap-15)).

ID de hardware
:   O identificador de um sensor na telemetria. No histórico, a série é a do slot ([capítulo 6](#cap-06-editor)).

Imagem
:   Uma das variantes do firmware: release, alpha ou Air. Cada aparelho recebe atualizações só da própria imagem ([capítulo 1](#cap-01)).

Janela de manutenção
:   Período em que os alarmes de um sensor ficam suspensos de propósito, com prazo, para uma limpeza ou um degelo. A linha de alarmes registra a abertura e o fim ([capítulo 7](#cap-07-manutencao)).

LCD
:   O display de 16×2 caracteres da imagem alpha ([capítulo 12](#cap-12)).

Linha de alarmes
:   O segundo canal de envio do aparelho. Ele envia um registro a cada mudança de estado de alarme, separado da telemetria de medições ([capítulo 22](#cap-22)).

LittleFS
:   O sistema de arquivos da memória flash, com 1 MB. Guarda a configuração, o histórico, o log e os pacotes de idioma ([capítulo 17](#cap-17-conteudo)).

Log de eventos
:   O registro do que o aparelho fez e do que deu errado, com código, nível e contexto ([capítulo 16](#cap-16)).

Lote mínimo e lote máximo
:   Os campos `t_int` e `t_bat` da telemetria: quantos registros pendentes disparam um envio e quantos vão, no máximo, em cada envio ([capítulo 21](#cap-21-lotes)).

## M a R {#cap-31-m-r}

M0 e M1
:   Os dois modos do Air. Em M0 ele fica acordado, com a interface web. Em M1 ele hiberna e só desperta para medir ([capítulo 19](#cap-19-modos)).

mDNS
:   O endereço pelo nome, como `http://simut.local`. Só existe na imagem release ([capítulo 9](#cap-09-mdns)).

Modo de Configuração
:   O item 12 das Configurações do painel, que abre o ponto de acesso de configuração ([capítulo 11](#cap-11-ap)).

Pacote de idioma
:   Arquivo `.lng` que traduz o painel, a interface web e o log. Só é carregado no boot ([capítulo 13](#cap-13-idioma)).

Painel
:   A tela de toque da imagem release ([capítulo 11](#cap-11)).

Pendentes
:   Registros do histórico que ainda não foram entregues ao coletor ([capítulo 21](#cap-21-quando)).

Permissão
:   Um dos bits que dizem o que uma conta pode ver e fazer, na web, no painel e na API ([capítulo 8](#cap-08-permissoes)).

PIN
:   O código que confirma, no painel, a conta escolhida. A web usa a senha ([capítulo 8](#cap-08-identidade)).

Política de PIN
:   As regras do PIN: comprimento mínimo, alfabeto e o teclado ([capítulo 8](#cap-08-politica)).

Ponto de acesso de configuração
:   A rede Wi-Fi `<nome>_SETUP` que o aparelho cria para ser configurado sem outra rede. Com ela no ar, o aparelho não mede ([capítulo 9](#cap-09-ap)).

Recompor o histórico
:   Operação que reescreve o arquivo do dia depois de uma mudança nos slots, mantendo os registros já feitos ([capítulo 6](#cap-06-recompor)).

Release
:   A imagem com painel de toque, buzzer, HTTPS e mDNS ([capítulo 1](#cap-01)).

Reset de fábrica
:   O comando `system factory confirm`, que volta a configuração ao estado de fábrica e mantém os arquivos ([capítulo 17](#cap-17-factory)).

Reset do administrador
:   O comando `system admin reset confirm`, que gera uma senha nova para a conta `admin` e a mostra uma vez, no console USB ([capítulo 17](#cap-17-admin-reset)).

## S a Z {#cap-31-s-z}

Salvar e reiniciar
:   Botão da interface web que grava a configuração e reinicia o aparelho ([capítulo 5](#cap-05-botoes)).

Selar
:   Fechar um bloco do histórico e acrescentá-lo ao arquivo do dia ([capítulo 15](#cap-15-blocos)).

Sessão
:   A entrada de uma conta na interface web ou na API. Vence depois de 15 min sem uso ([capítulo 13](#cap-13-sessoes)).

Slot
:   Uma das 16 posições de sensor do aparelho, numeradas de 0 a 15 ([capítulo 6](#cap-06-slots)).

Syslog
:   Protocolo padrão para enviar o log de eventos a um servidor na rede ([capítulo 25](#cap-25)).

Teclado embaralhado
:   O teclado de PIN do painel com 2 ou 3 caracteres por cartão. Depois de cada toque, os caracteres são redistribuídos, para que quem olha a tela não descubra o PIN ([capítulo 8](#cap-08-teclado)).

Telemetria
:   O envio das medições do histórico ao coletor, por HTTP, HTTPS, MQTT ou MQTTS ([capítulo 21](#cap-21)).

Testar
:   Botão da interface web que confere a configuração sem gravar nada ([capítulo 5](#cap-05-ensaio)).

Token Bearer
:   O código de sessão que um programa manda no cabeçalho `Authorization`, no lugar do cookie ([capítulo 26](#cap-26-token)).

Transição
:   Mudança de estado. O log de eventos grava uma falha repetida na primeira vez e depois só a cada hora; a linha de alarmes envia um registro por mudança ([capítulo 16](#cap-16-transicao)).

UF2
:   O formato de arquivo que a placa aceita no modo BOOTSEL ([capítulo 3](#cap-03)).
