# Imagens a capturar para o manual

Gerado por `tools/build_manual.py` — não edite à mão. Salve cada captura
nesta pasta com o nome indicado e rode o script de novo.

194 pendentes de 194.


## Capítulo 1 — O SIMUT em uma página

- [ ] `01-arquitetura.png` (Diagrama) — diagrama de blocos do aparelho: à esquerda, os sensores (DS18B20, DHT22, BME280/BMP280) ligados a GP0–GP15; no centro, o RP2040 com dois blocos, Core 0 (sensores, alarmes, histórico, rede, web, console, telemetria) e Core 1 (painel TFT e toque na release; LCD na alpha; sem uso no Air); abaixo, a flash de 2 MB em duas faixas, 'programa 1 MB' e 'LittleFS 1 MB' com as pastas /config, /history, /lang, /themes e o log de eventos; à direita, as saídas: navegador (interface web), servidor de telemetria (HTTP/MQTT), linha de alarmes, Home Assistant, Prometheus e syslog; embaixo à direita, o cabo USB e o Bluetooth levando ao console
      O aparelho em blocos: os sensores entram pelo Core 0, a tela fica com o Core 1, e a flash se divide entre o programa e o sistema de arquivos.

## Capítulo 2 — Hardware e ligações

- [ ] `02-pinos-release.png` (Diagrama) — vista de cima do Pico W com os 40 pinos numerados; GP0–GP15 marcados como SLOT 0–15; GP16 MISO, GP17 T_CS, GP18 SCK, GP19 MOSI, GP20 T_IRQ, GP22 cigarra, GP26 RST, GP27 DC, GP28 CS; 3V3 (36) para TFT e sensores, VBUS (40), GNDs destacados; GP23/24/25/29 em cinza como internos do rádio
      O mapa de pinos da release: sensores à esquerda, tela, toque e cigarra à direita.
- [ ] `02-pinos-alpha.png` (Diagrama) — vista de cima do Pico W; GP0–GP15 como SLOT 0–15; GP16 RS, GP17 EN, GP18 D4, GP19 D5, GP20 D6, GP21 D7 do LCD; GP22 cigarra; VBUS (40) para o LCD, 3V3 (36) para os sensores; GP26–GP28 sem uso
      O mapa de pinos da alpha: o LCD em GP16 a GP21, a cigarra em GP22.
- [ ] `02-pinos-air.png` (Diagrama) — vista de cima do Pico W; GP0–GP15 como SLOT 0–15; GP16 saída de energia dos sensores e sinal de acordado; GP17 entrada do carregador; demais pinos de GP18 em diante sem uso
      O mapa de pinos do Air: só GP16 e GP17 além dos sensores.
- [ ] `02-air-energia.png` (Diagrama) — GP16 acionando uma chave do lado positivo (MOSFET canal P com acionamento, ou pino EN de um regulador 3,3 V) que alimenta o barramento de 3,3 V dos sensores; à parte, o carregador de 5 V com um divisor resistivo levando ao GP17; legenda 'GP16 alto = acordado'
      O Air: o GP16 corta a energia dos sensores durante o sono, e o GP17 percebe o carregador.
- [ ] `02-ds18b20.png` (Diagrama) — sonda DS18B20 de três fios ligada ao Pico W: VDD no 3V3 (pino 36), GND no GND, DQ num GPIO de slot (exemplo GP2, pino 4); resistor de 4,7 kΩ entre DQ e 3V3
      Um DS18B20: três fios e um resistor de 4,7 kΩ entre o dado e o 3V3.
- [ ] `02-dht22.png` (Diagrama) — módulo DHT22 de três terminais ligado ao Pico W: + no 3V3 (pino 36), dados num GPIO de slot (exemplo GP3, pino 5), − no GND; nota 'pull-up conforme o módulo'
      Um DHT22: alimentação, dados e terra.
- [ ] `02-bmx280.png` (Diagrama) — módulo BME280 de seis pinos ligado ao Pico W: VCC no 3V3 (pino 36), GND, SDA no GP4 (pino 6), SCL no GP5 (pino 7), CSB no 3V3, SDO no GND (endereço 0x76); nota 'pull-ups de 4,7 kΩ em SDA e SCL se o módulo não tiver'
      Um BME280 no par GP4/GP5, o par que a busca de sondas encontra.
- [ ] `02-placa.png` (Foto) — placa SIMUT v1.1 montada, vista de cima: Pico W no soquete, conector TFT-Touch com o painel ligado, bloco Alphanumeric vazio, coluna S0–S10 com uma sonda DS18B20 em S2, cigarra, conversor HLK-PM01 e a marcação 90-240Vac visível
      A placa v1.1 montada para a release: o Pico W, o conector do painel, a coluna de sensores e a entrada de rede elétrica.

## Capítulo 3 — Instalar o firmware

- [ ] `03-arquivos.png` (Diagrama) — os nove arquivos de uma versão e para onde vai cada um: .uf2 → cabo USB com BOOTSEL (aparelho novo ou recuperação); .bin → página Arquivos, atualização pela rede; language_pt-BR.lng → página Arquivos, pasta /lang; manifest.json → gestor de frota; três colunas coloridas por imagem (release, alpha, air)
      Os arquivos de uma versão e o caminho de cada um até o aparelho.
- [ ] `03-bootsel.png` (Foto) — mão segurando o botão BOOTSEL de um Pico W enquanto o cabo USB é ligado; o botão bem visível, ao lado do chip RP2040
      O botão BOOTSEL, segurado enquanto o cabo USB é ligado.

## Capítulo 4 — Primeiro boot e primeira configuração

- [ ] `04-fluxo.png` (Diagrama) — fluxograma do primeiro boot: 'liga com o console USB aberto' → 'anota a senha do admin'; depois dois ramos: release e alpha 'AP simut_SETUP → 192.168.4.1 → troca de senha → Rede → Salvar e reiniciar'; Air 'console: enable, configure terminal, system ssid, system pass, reload confirm'; os ramos se juntam em 'aparelho na rede, começa a medir' → 'pacote de idioma e reinício' → 'fuso e hora' → 'primeiro sensor' → 'PIN do painel (release)' → 'pronto para usar'
      O primeiro boot em um só desenho: a senha, a rede, a web, o idioma, os sensores e o PIN.
- [ ] `04-painel-ap.png` (Tela do painel) — release recém-gravada, sem rede configurada, depois do boot; a caixa de boot termina com simut_SETUP, PSK e a chave, e AP Ativo! Reinicie a placa para sair.; capturar com GET /api/screenshot pelo próprio AP, com sessão admin depois da troca de senha; se a captura não funcionar no AP, use foto da tela; borre a chave antes de publicar
      O painel de um aparelho novo: a rede do ponto de acesso, a chave e o aviso de AP ativo ficam na tela.
- [ ] `04-lcd-ap.png` (LCD 16×2) — alpha recém-gravada, sem rede configurada, depois do boot com o ponto de acesso aberto; linha de cima SIMUT 2.7.1, linha de baixo [##############] com a barra cheia
      O LCD de uma alpha nova com o ponto de acesso aberto: a tela de boot fica parada, e a chave está no console.
- [ ] `04-novo-pin.png` (Tela do painel) — release com configuração de fábrica; tela inicial → botão de configurações → admin → PIN 1234 → ENTRAR; tela Novo PIN aberta, antes de digitar
      A troca obrigatória do PIN de fábrica: a tela Novo PIN abre direto depois do 1234.

## Capítulo 5 — Configuração pela web: os três botões e a página Configurações

- [ ] `05-selo-reinicio.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; campo Fuso Horário alterado de -3 para -4 e ainda não gravado; ensaio já respondido; recorte da barra de topo
      Uma alteração que exige reinício: o selo diz Reinicia por: time e só o botão Salvar e reiniciar aparece.
- [ ] `05-tres-botoes.png` (Página web) — rota /alarms; largura 1280; sessão admin; imagem release; limite máximo de temperatura de um slot alterado; ensaio respondido Não exige reinício; recorte da barra de topo com o selo e os três botões
      Uma alteração que se aplica sem reinício: o selo diz Não exige reinício e os três botões aparecem.
- [ ] `05-confirmacao.png` (Página web) — rota /config; largura 1280; sessão admin; Nome do aparelho alterado; janela de confirmação aberta depois de tocar em Salvar e reiniciar
      A confirmação de Salvar e reiniciar, que avisa quanto tempo o aparelho fica fora do ar.
- [ ] `05-configuracoes.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; nenhuma alteração preparada; página no topo mostrando Identidade, Data e Hora e o início de Hardware
      A página Configurações aberta, com as seções Identidade e Data e Hora.
- [ ] `05-erro-carga.png` (Página web) — rota /config; largura 1280; sessão admin; aparelho sem responder a /api/config (por exemplo, rede interrompida depois de a página abrir); faixa de erro visível e campos desabilitados
      A faixa de erro de carga: os campos ficam desabilitados para que nada em branco seja gravado.
- [ ] `05-data-hora.png` (Página web) — rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos de data e hora preenchidos; mensagem Hora aplicada. abaixo do botão Aplicar Agora
      A seção Data e Hora com o NTP desligado, depois de um acerto manual.
- [ ] `05-hardware.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; seção Hardware inteira, com a linha da política de PIN e a dica 4 – 8
      A seção Hardware na imagem release, com a política de PIN do painel.
- [ ] `05-sensores.png` (Página web) — rota /config; largura 1280; sessão admin; dois slots configurados (um DS18B20 e um BME280); seção Sensores e GPIO com o mapa de GPIOs e a tabela de slots; editor fechado
      A seção Sensores e GPIO, com o mapa de GPIOs e a tabela de slots. O editor de slot está no capítulo 6.
- [ ] `05-syslog.png` (Página web) — rota /config; largura 1280; sessão admin; syslog ligado com IP do Coletor 192.0.2.10, porta 514 e nível Info; recorte das seções Syslog Remoto e Calibração do Touch
      As duas últimas seções da página: o syslog e a calibração do touch.

## Capítulo 6 — Sensores e calibração

- [ ] `06-hardware.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; recorte da seção Hardware
      A seção Hardware com os valores de fábrica: resolução de 12 bits, amostra de 2000 ms, histórico a cada 1 min e a política de PIN.
- [ ] `06-sensores.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; 3 slots ativos (DS18B20 em GP2, DHT22 em GP3, BME280 em GP4 e GP5); recorte da seção Sensores e GPIO
      A seção Sensores e GPIO. A faixa de GP0 a GP15 mostra quais pinos estão ocupados e por qual slot; a tabela lista só os slots em uso.
- [ ] `06-editor-ds18b20.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; imagem release; editor do slot 2 aberto, DS18B20 ativo em GP2, relógio sincronizado por NTP, sem correção
      O editor de um slot DS18B20: tipo, nome, ID de hardware, estado, o pino 1-Wire, o bloco de calibração e as operações de hardware.
- [ ] `06-editor-bme280.png` (Página web) — rota /config; largura 390; sessão admin; imagem release; editor de um slot novo com o tipo BME280 escolhido, Pino 0 — SDA em GP4, Pino 1 — SCL em GP5, ID de hardware e nome preenchidos, Ativo ligado, ainda não gravado
      Um slot novo de BME280 no celular: dois seletores de pino, SDA e SCL.
- [ ] `06-editor-aviso.png` (Página web) — rota /config; largura 1280; sessão admin; editor de um slot DHT22 ativo com o pino Data em GP4, que já pertence ao slot 2; a linha vermelha mostra GP4 já está em uso pelo slot 2.
      O editor avisando um conflito de GPIO antes da gravação.
- [ ] `06-procurar.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; um DS18B20 em GP2 e um DHT22 em GP3 ligados; logo depois de tocar em Procurar sondas e a busca terminar; recorte dos botões e da linha de resultado
      O resultado da busca: a ROM identifica o DS18B20; o DHT22 aparece só com o pino.
- [ ] `06-painel-sensor.png` (Tela do painel) — screen dash; 2 sensores ativos (um DS18B20 e um BME280), ambos lendo, nenhum em alarme
      A tela inicial do painel com dois sensores lendo normalmente.
- [ ] `06-painel-erro.png` (Tela do painel) — screen dash; 2 sensores ativos; a sonda DS18B20 desligada do pino há mais de 3 leituras, alarmes do slot ligados
      Um sensor em erro no painel: o cartão mostra Erro no lugar da temperatura e fica âmbar pelo alarme de falha.
- [ ] `06-lcd-erro.png` (LCD 16×2) — imagem alpha; 1 sensor DHT22 ativo e desligado do pino; o LCD mostra ERRO em letras grandes
      O LCD do alpha com o sensor em erro.
- [ ] `06-calibracao.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DHT22 ativo; relógio sincronizado por NTP; Temp com 2 pontos preenchidos e Reta escolhida; Umid sem correção; recorte do bloco Calibração
      O bloco Calibração de um DHT22. Cada grandeza tem sua linha de leitura, o mini gráfico da correção e seus pontos.
- [ ] `06-calibracao-suave.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DS18B20; Temp com 3 pontos (0, 25 e 50 °C de referência) e Suave escolhida; recorte do bloco Calibração
      Uma correção Suave com três pontos: o mini gráfico mostra a curva passando pelos pontos sem ultrapassá-los.
- [ ] `06-calibracao-ntp.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; relógio do aparelho sem sincronização NTP; editor de um slot ativo; recorte do fim do bloco Calibração com o aviso NTP não sincronizado.
      Sem NTP, o bloco avisa em vermelho que a gravação será recusada.

## Capítulo 7 — Alarmes, sons e janelas de manutenção

- [ ] `07-painel-limite.png` (Tela do painel) — screen dash; 2 sensores ativos; o sensor do cartão de baixo acima do máximo de temperatura, alarme não silenciado; captura na fase vermelha da piscada
      Um alarme de limite no painel: o cartão do sensor pisca em vermelho.
- [ ] `07-painel-falha.png` (Tela do painel) — screen dash; 2 sensores ativos; o DS18B20 do cartão de baixo desligado do pino há mais de 3 leituras; captura na fase âmbar da piscada
      Um alarme de falha no painel: o cartão pisca em âmbar e mostra Erro no lugar do valor.
- [ ] `07-tela-alarme.png` (Tela do painel) — tela MODE_ALARM_ACTION; a partir da screen dash com um sensor chamado Freezer acima do limite, toque no cartão que pisca; a ferramenta de mapa de telas não alcança esta tela sozinha
      A tela de alarme de um sensor, com Silenciar 120s, Desativar e Min/Max.
- [ ] `07-silenciado.png` (Tela do painel) — screen dash; um sensor acima do limite; logo depois de tocar em Silenciar 120s; a barra de cima mostra Silenciado com a contagem
      O painel durante o silêncio: a contagem na barra de cima e o cartão parado.
- [ ] `07-pagina-limites.png` (Página web) — rota /alarms; largura 1280; sessão admin; 3 sensores ativos (DS18B20, DHT22 e BME280); recorte da seção Limites de Alarme
      A seção Limites de Alarme: um cartão por sensor ativo, com a chave de alarme e um par de campos por grandeza.
- [ ] `07-pagina-sons.png` (Página web) — rota /alarms; largura 1280; sessão admin; valores de fábrica; recorte da seção Configuração de Sons
      A seção Configuração de Sons com os valores de fábrica: todos os sons ligados, volumes em 70 %.
- [ ] `07-lista-sensores.png` (Tela do painel) — screen alm; 3 sensores ativos, um deles com alarmes desligados
      A lista de sensores do menu de alarmes, com o estado SIM ou NÃO de cada um.
- [ ] `07-menu-sensor.png` (Tela do painel) — screen alm -> tap(160,57) -> tap(160,57); sessão admin; sensor com alarmes ligados e sem manutenção
      O menu de um sensor para uma conta com as três permissões de painel.
- [ ] `07-menu-sensor-manut.png` (Tela do painel) — menu do sensor aberto por uma conta que tem só a permissão Manut. (painel); caminho: tela inicial, botão de configurações, conta, PIN, Limites de Alarme, sensor
      O mesmo menu para uma conta só com Manut. (painel): as duas primeiras linhas apagadas, com cadeado, e a seleção em Manutenção.
- [ ] `07-editor-limites.png` (Tela do painel) — screen alm -> tap(160,57) -> tap(160,57) -> tap(160,57); sessão admin; sensor DHT22; barra Temp Max selecionada
      O editor de limites de um DHT22: temperatura e umidade, mínimo e máximo.
- [ ] `07-manutencao-entrada.png` (Tela do painel) — menu do sensor -> Manutenção -> ENTRAR; sessão admin; janela fechada; Horas 2 e Minutos 30 ajustados, antes de INICIAR
      A tela Manutenção com a janela fechada: horas, minutos e INICIAR.
- [ ] `07-manutencao-aberta.png` (Tela do painel) — mesmo caminho, com a janela aberta há alguns minutos
      A tela Manutenção com a janela aberta: o tempo que falta e FECHAR.
- [ ] `07-sons-painel.png` (Tela do painel) — screen set -> tap(105,215) -> tap(105,215) -> tap(250,215); sessão admin; valores de fábrica; primeira página
      A tela Config. de Sons, primeira página: os dois volumes, Toque na Tela e Confirmação.
- [ ] `07-melodia.png` (Tela do painel) — tela Config. de Sons; som Alarme desligado; toque na linha Alarme duas vezes para abrir a escolha de melodia
      A escolha de melodia do som de alarme, com as seis opções.
- [ ] `07-mudo-confirmar.png` (Tela do painel) — tela MODE_CONFIRM_MUTE_ALL; na tela Config. de Sons, selecione Mudo Global desligado e toque nele; a ferramenta de mapa de telas não alcança esta tela sozinha
      A confirmação do Mudo Global.

## Capítulo 8 — Contas, permissões e PIN

- [ ] `08-usuarios.png` (Página web) — rota /users; largura 1280; sessão admin; imagem release; contas admin (com PIN), viewer e duas contas de operador, uma delas com PIN; nenhuma alteração pendente
      A página Usuários: a tabela de contas e, abaixo, o formulário Adicionar.
- [ ] `08-usuarios-permissoes.png` (Página web) — rota /users; largura 390; sessão admin; selo de permissões de uma conta de operador aberto, mostrando os nomes
      No celular, o selo de permissões aberto mostra os nomes de cada permissão da conta.
- [ ] `08-usuarios-nova.png` (Página web) — rota /users; largura 1280; sessão admin; uma conta nova preparada com as permissões Painel e Histórico, ainda não gravada; linha com Pendente: Novo e o botão desfazer
      Uma conta preparada e ainda não gravada.
- [ ] `08-senha-temporaria.png` (Página web) — rota /users; largura 1280; sessão admin; logo depois de Salvar e reiniciar com uma conta nova chamada operador1; janela Senha temporária aberta; senha fictícia
      A janela Senha temporária, com a senha de uso único da conta criada.
- [ ] `08-quem.png` (Tela do painel) — screen pin; contas admin, operador1 e manutencao com PIN; nenhuma bloqueada
      A tela Quem está usando o painel?, com as contas que têm PIN.
- [ ] `08-menu-operador.png` (Tela do painel) — menu de configurações depois do PIN de uma conta que tem só permissões de painel; título Configurações > operador1
      O menu de uma conta só com permissões de painel: os itens sem numeração e o nome da conta no título.
- [ ] `08-teclado-3.png` (Tela do painel) — screen pin e escolha de uma conta; política de fábrica: 0-9, 3 por tecla; dois caracteres já digitados
      O teclado embaralhado da política de fábrica: quatro cartões de três caracteres.
- [ ] `08-teclado-alfa.png` (Tela do painel) — screen pin e escolha de uma conta; política 0-9 A-Z, 1 por tecla; grupo PQRS aberto
      O teclado ordenado alfanumérico, com a janela de um grupo aberta.
- [ ] `08-teclado-numerico.png` (Tela do painel) — menu > Alterar Senha, sessão admin; política de fábrica; tela Novo PIN com três dígitos digitados
      O teclado numérico ordenado, usado para definir um PIN.
- [ ] `08-pin-invalido.png` (Tela do painel) — screen pin, escolha de uma conta, um PIN errado e ENTRAR; logo depois da recusa
      Um PIN recusado: a mensagem PIN inválido! e os cartões distribuídos de novo.
- [ ] `08-espera.png` (Tela do painel) — screen pin; a mesma conta errou o PIN três vezes seguidas; durante a espera de 5 s
      A espera depois da terceira tentativa errada.
- [ ] `08-painel-bloqueado.png` (Tela do painel) — tela do teclado depois de 20 tentativas erradas somadas, sem nenhuma entrada certa no meio
      O painel travado: só um reinício libera o teclado.
- [ ] `08-pin-em-uso.png` (Tela do painel) — menu > Alterar Senha; digitar duas vezes um PIN que outra conta já usa
      A recusa de um PIN que outra conta já tem.
- [ ] `08-politica.png` (Tela do painel) — menu > Segurança do PIN, sessão admin; política de fábrica; linha Teclado selecionada
      A tela Segurança do PIN, com a política de fábrica: tamanho mínimo 4, 3 caracteres por tecla (máximo 8) e só dígitos.
- [ ] `08-politica-web.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; recorte dos três campos de PIN da seção Hardware, com 2 glifos por tecla e 0-9 A-Z escolhidos
      Os campos da política de PIN na página Configurações.
- [ ] `08-usuarios-lista.png` (Tela do painel) — screen usr; contas viewer, operador1 (com PIN, L e B) e manutencao (com PIN, M)
      A lista de contas do painel, com o ponto de quem tem PIN e as letras L, B e M.
- [ ] `08-novo-nome.png` (Tela do painel) — screen usr -> NOVO; nome manutencao digitado
      O teclado de texto para o nome da conta nova.
- [ ] `08-novo-permissoes.png` (Tela do painel) — screen usr -> NOVO -> nome -> OK; Editar limites e Manutenção em SIM
      As três permissões de painel da conta nova, antes de SEGUIR.
- [ ] `08-novo-pin.png` (Tela do painel) — mesmo caminho -> SEGUIR; tela Novo PIN com o teclado ordenado
      O PIN da conta nova, digitado duas vezes no teclado ordenado.
- [ ] `08-excluir.png` (Tela do painel) — screen usr -> conta manutencao -> Excluir usuário
      A confirmação antes de excluir uma conta no painel.

## Capítulo 9 — Rede, Wi-Fi e HTTPS

- [ ] `09-rede.png` (Página web) — rota /network; largura 1280; sessão admin; imagem release; conectado por DHCP à rede MinhaRede; DNS automático; porta 80; sem certificado HTTPS instalado
      A página Rede: à esquerda, o estado da conexão; à direita, o formulário de configuração.
- [ ] `09-ip-estatico.png` (Página web) — rota /network; largura 1280; sessão admin; DHCP desligado; IP Estático 192.0.2.10, Máscara 255.255.255.0, Gateway 192.0.2.1; DNS automático desligado com DNS Primário 192.0.2.1; alterações preparadas e ainda não gravadas
      A seção IPv4 com o DHCP desligado e os campos do IP fixo preenchidos.
- [ ] `09-servidor-web.png` (Página web) — rota /network; largura 1280; sessão admin; imagem release; certificado HTTPS instalado; página aberta por https; recorte das seções Servidor de Hora (NTP) e Servidor Web, com o interruptor Conexões persistentes visível e ligado
      A seção Servidor Web num aparelho com certificado instalado: o interruptor de conexões persistentes só aparece nesse caso.
- [ ] `09-busca.png` (Página web) — rota /network; largura 1280; sessão admin; busca concluída com 5 redes de exemplo (MinhaRede protegida -48 dBm com 4 barras, uma rede aberta com 2 barras, as outras protegidas); lista aberta abaixo do campo SSID
      A lista de redes depois de Buscar: cadeado, nome, barras e sinal em dBm, da mais forte para a mais fraca.
- [ ] `09-escada.png` (Diagrama) — linha do tempo da escada de reconexão: queda; cinco ciclos de busca, busca e tentativa de 20 s com esperas de 5, 10, 20, 40 e 80 s; três dormências de 10 min antes de cada busca; ao fim, o AP de configuração (aparelho que já teve IP) e o recomeço da escada; marcar à parte o AP rápido na primeira dormência para o aparelho que nunca teve IP
      A escada de reconexão: esperas que dobram, três dormências de 10 min e o ponto em que o aparelho abre o AP de configuração.
- [ ] `09-aviso-certificado.png` (Página web) — rota https://192.0.2.10/login; largura 1280; navegador Chrome sem a exceção aceita; certificado autoassinado recém-instalado; página de aviso do navegador
      O aviso do navegador no primeiro acesso a um aparelho com certificado autoassinado.

## Capítulo 10 — Data e hora

- [ ] `10-log-ntp.png` (Página web) — rota /history; largura 1280; sessão admin; aba de eventos carregada logo depois de um boot com NTP; linhas 524 Hora provisória do flash, 14 IP obtido, 13 NTP sincronizado, 408 NTP corrigindo timestamps com contexto de alguns segundos, 409 e 410 visíveis
      Os eventos de um boot com NTP: a hora provisória, o acerto e a correção do histórico.
- [ ] `10-acerto-manual.png` (Página web) — rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos Data e Hora preenchidos e a mensagem Hora aplicada.
      O acerto manual com o NTP desligado: data, hora e o botão Aplicar Agora da seção.
- [ ] `10-correcao.png` (Diagrama) — linha do tempo: aparelho desligado por 2 h; boot com relógio provisório a partir do último registro, 2 h atrasado; medições carimbadas com a hora provisória; acerto por NTP com correção de +7200 s; os blocos gravados desde o boot deslocados 2 h para a frente; eventos 524, 13, 408, 409 e 410 marcados na linha
      O que o primeiro acerto por NTP faz com as medições gravadas sob o relógio provisório.
- [ ] `10-painel-hora.png` (Tela do painel) — screen dash; 2 sensores ativos; relógio acertado por NTP; recorte da faixa de topo com a data e a hora
      A data e a hora no topo do painel principal, no fuso do aparelho.

## Capítulo 11 — O painel

- [ ] `11-principal.png` (Tela do painel) — screen dash; 3 sensores ativos: DS18B20 no slot 0 (fixado no cartão de cima), DHT22 no slot 1 (selecionado) e BME280 no slot 2; Wi-Fi conectado com sinal forte; 12 registros pendentes; relógio acertado; nenhum alarme
      A tela inicial: a barra de cima, os dois cartões e a barra de botões com S1, S2 e CFG.
- [ ] `11-pendentes-falha.png` (Tela do painel) — screen dash; telemetria ligada com o coletor fora do ar há alguns minutos; recorte da barra de cima com a contagem de pendentes em vermelho
      A contagem de pendentes em vermelho: o último envio falhou.
- [ ] `11-aviso-web.png` (Tela do painel) — screen dash; até 5 s depois de entrar na interface web com a conta admin; recorte da barra de cima com Web: admin no lugar do relógio
      O aviso de entrada pela web, no lugar da data e da hora.
- [ ] `11-minmax.png` (Tela do painel) — screen dash -> tap(160,150); DHT22 selecionado no cartão de baixo, com leituras desde a meia-noite
      O cartão de baixo em mínimo e máximo: temperatura e umidade, e o botão de gráfico à direita.
- [ ] `11-selecao.png` (Tela do painel) — screen dash; 3 sensores ativos; segure o dedo no cartão de cima por 3 s e solte; a ferramenta de mapa de telas não faz o gesto de segurar
      O cartão de cima em seleção: cinza, espelhando o sensor do cartão de baixo.
- [ ] `11-paginas.png` (Tela do painel) — screen dash; 7 sensores ativos, nenhum fixado fora da primeira página; barra de botões na primeira página, com S e o botão 1/2
      A barra de botões com páginas: quatro botões e o botão de página à direita.
- [ ] `11-web-ocupado.png` (Tela do painel) — screen dash; durante a exportação em CSV de um dia de histórico pela conta admin, na página Histórico e Logs
      O aviso de web ocupada: WEB ‘admin’ - toque bloqueado.
- [ ] `11-carregando.png` (Tela do painel) — tela MODE_GRAPH_LOADING; aparece por instantes ao abrir um gráfico a partir da tela inicial; transitória, pode exigir várias tentativas de captura
      A tela Carregando…, ao abrir um gráfico.
- [ ] `11-grafico.png` (Tela do painel) — screen dash -> tap(160,150) -> tap(290,150) -> tap(286,215) -> tap(286,215) -> tap(286,215); DHT22 selecionado no cartão de baixo; o gráfico abre em 1H e os três toques na lupa de menos levam a 24H; histórico de pelo menos um dia
      O gráfico de 24H de um DHT22: faixa de mínimo e máximo, linha de temperatura, curva de umidade e marcadores.
- [ ] `11-detalhe-temp.png` (Tela do painel) — screen gra -> tap(160,120); gráfico de um BME280 aberto antes, em 24H
      O detalhe numérico, página de temperatura: máximo, mínimo, média e desvio.
- [ ] `11-detalhe-umid.png` (Tela do painel) — screen gra -> tap(160,120) -> tap(160,120); mesmo BME280
      A página de umidade do detalhe.
- [ ] `11-detalhe-pressao.png` (Tela do painel) — screen gra -> tap(160,120) -> tap(160,120) -> tap(160,120); mesmo BME280
      A página de pressão do detalhe.
- [ ] `11-calendario.png` (Tela do painel) — screen gra -> tap(160,215); mês corrente com histórico em pelo menos dez dias
      O calendário: os dias com histórico têm um ponto, e o dia de hoje fica destacado.
- [ ] `11-menu.png` (Tela do painel) — screen set; a ferramenta entra como administrador; primeira página, item 1 selecionado
      O menu completo, primeira página: itens numerados de 1 a 4.
- [ ] `11-menu-p3.png` (Tela do painel) — screen set -> tap(35,215); a seta para cima dá a volta e seleciona o item 12; terceira página
      A terceira página do menu completo, com Alinhamento da Tela, Usuários, Segurança do PIN e Modo de Configuração.
- [ ] `11-temas.png` (Tela do painel) — screen thm; tema de fábrica aplicado; pelo menos quatro temas instalados
      A escolha de tema, com as amostras de cor de cada um.
- [ ] `11-idioma.png` (Tela do painel) — screen lng; pacote pt-BR carregado e em uso
      A escolha de idioma: English e o pacote instalado.
- [ ] `11-sensibilidade.png` (Tela do painel) — screen touchsens; dedo ainda não encostado; barra vazia
      A etapa de sensibilidade: a mira, a barra de estabilidade e o limite de pressão.
- [ ] `11-mira.png` (Tela do painel) — screen touchcal e complete a etapa de sensibilidade com o dedo; capture a primeira mira, no canto de cima à esquerda; o toque simulado não completa a sensibilidade, porque não tem pressão
      A etapa de posição: a primeira mira, Toque na mira (1/4) e a volta [ 1 / 2 ].
- [ ] `11-calib-ok.png` (Tela do painel) — depois do oitavo toque de uma calibração com as duas voltas coerentes
      O fim de uma calibração aceita: Calibração Concluída! e ENTENDI.
- [ ] `11-calib-recusada.png` (Tela do painel) — depois do oitavo toque, tendo tocado longe da mira num dos cantos da segunda volta
      Uma calibração recusada: Toques imprecisos! Tente novamente.
- [ ] `11-licenca.png` (Tela do painel) — screen lic; primeira página
      A primeira página da licença.
- [ ] `11-status-1.png` (Tela do painel) — screen sts; página 1
      Status do Sistema, página 1: o aparelho.
- [ ] `11-status-2.png` (Tela do painel) — screen sts -> tap(105,215); página 2; Wi-Fi conectado e NTP sincronizado
      Status do Sistema, página 2: rede e hora.
- [ ] `11-status-3.png` (Tela do painel) — screen sts -> tap(105,215) -> tap(105,215); página 3
      Status do Sistema, página 3: sensores.
- [ ] `11-status-4.png` (Tela do painel) — screen sts -> tap(35,215); a seta para cima dá a volta até a página 4; telemetria MQTT ligada, com registros pendentes
      Status do Sistema, página 4: telemetria.
- [ ] `11-alinhamento.png` (Tela do painel) — screen offset; deslocamento X +2 e Y -1 ajustado, antes de APLICAR
      O alinhamento da tela, com as quatro setas, o quadrado de zerar e os valores X e Y.
- [ ] `11-teclado-texto.png` (Tela do painel) — screen usr -> tap(270,215) -> tap(121,151); NOVO e depois a tecla do grupo pqrs, segunda linha, segunda coluna
      O teclado de texto com o grupo pqrs aberto, em minúsculas e maiúsculas.
- [ ] `11-ap-confirmar.png` (Tela do painel) — screen set -> tap(35,215) -> tap(270,215); a seta para cima dá a volta até o item 12
      A confirmação do Modo de Configuração.
- [ ] `11-mensagem-ok.png` (Tela do painel) — menu > Alterar Senha; digite duas vezes um PIN novo e válido, com ENTRAR
      Uma mensagem de sucesso: PIN salvo! e ENTENDI.
- [ ] `11-boot.png` (Foto) — boot com rede configurada e alcançável; capture durante Aguardando roteador; a captura por GET /api/screenshot não alcança o boot, então use foto da tela
      A tela de boot com as últimas etapas e o botão PULAR.
- [ ] `11-boot-gesto.png` (Foto) — boot; linha Mantenha a tela pressionada: Modo AP... no fim da caixa; foto da tela
      O convite do gesto do AP no boot.
- [ ] `11-boot-progresso.png` (Foto) — boot; dedo mantido na tela há cerca de 1,5 s dentro da janela; foto da tela
      O gesto em curso: Modo de Configuração e a barra enchendo.
- [ ] `11-boot-cancelado.png` (Foto) — boot; dedo levantado antes de a barra encher; foto da tela
      O gesto cancelado: Modo AP Cancelado.
- [ ] `11-boot-ap.png` (Foto) — boot pelo gesto completo; caixa com o nome da rede, PSK e AP Ativo!; foto da tela; a chave da foto deve ser borrada antes de publicar
      O boot no AP: o nome da rede, a chave e AP Ativo!.

## Capítulo 12 — O LCD do alpha

- [ ] `12-splash.png` (LCD 16×2) — logo depois de ligar; linha de cima SIMUT 2.7.1, linha de baixo Inicializando
      A abertura do LCD, com a versão.
- [ ] `12-boot.png` (LCD 16×2) — durante o boot, com a barra pela metade: SIMUT 2.7.1 e [#######       ]
      A barra de progresso do boot.
- [ ] `12-conectado.png` (LCD 16×2) — fim do boot, na rede, com IP; Conectado! e o endereço IP na linha de baixo (use um endereço de documentação na legenda publicada)
      O fim do boot com a rede: Conectado! e o endereço IP.
- [ ] `12-obtendo-ip.png` (LCD 16×2) — fim do boot, associado ao roteador sem endereço ainda; Conectado! e Obtendo IP...
      Associado à rede, esperando o endereço.
- [ ] `12-offline.png` (LCD 16×2) — fim do boot sem rede alcançável; Sem WiFi e Offline
      O fim do boot sem rede.
- [ ] `12-temperatura.png` (LCD 16×2) — 2 sensores ativos; na vez da temperatura do slot 0, com 23,4 °C; W e o ícone de sinal no canto de cima, S0 no canto de baixo
      Uma temperatura em algarismos grandes, com o slot no canto de baixo.
- [ ] `12-umidade.png` (LCD 16×2) — DHT22 no slot 1, na vez da umidade, com 58 %; S1 no canto de baixo
      A umidade em algarismos grandes, com %UR.
- [ ] `12-pressao.png` (LCD 16×2) — BME280 no slot 2, na vez da pressão, com 1013,2 hPa; S2 no canto de baixo
      A pressão, em caracteres normais na linha de cima.
- [ ] `12-negativa.png` (LCD 16×2) — DS18B20 num congelador, com -18,5 °C; um só sensor ativo e nenhum pendente
      Uma temperatura negativa: o traço antes dos algarismos.
- [ ] `12-sem-sensor.png` (LCD 16×2) — imagem alpha sem nenhum slot ativo, conectada ao Wi-Fi; ERRO em letras grandes e W com o ícone de sinal
      ERRO fixo: nenhum sensor ativo.
- [ ] `12-wifi-niveis.png` (Diagrama) — os seis desenhos do ícone, em grade de 5 x 8 pixels, lado a lado: X, 1, 2, 3, 4 e 5 barras; em cima, a escada da main (colunas acesas da esquerda para a direita); embaixo, os desenhos da v2.7.1 (linhas acesas de baixo para cima); bitmaps em src/display/BigFont_HD44780.h
      Os seis estados do ícone de sinal: a escada da main e as linhas da v2.7.1.
- [ ] `12-pendentes.png` (LCD 16×2) — imagem da main; 1 sensor ativo; telemetria ligada com o coletor fora do ar, 37 registros pendentes; 37 no canto de baixo à esquerda
      A contagem de pendentes no canto de baixo, com um único sensor.
- [ ] `12-pendentes-k.png` (LCD 16×2) — imagem da main; 1 sensor ativo; 2.500 registros pendentes; 2k no canto de baixo
      Mais de mil pendentes: a contagem em milhares.
- [ ] `12-ap-1.png` (LCD 16×2) — AP aberto em operação pelo comando ap, depois de abrir uma página da interface web pelo AP; página Modo AP 1 de 3 com 192.168.4.1
      A página 1 do AP: o endereço.
- [ ] `12-ap-2.png` (LCD 16×2) — mesmo estado; página Rede: 2 de 3 com simut_SETUP
      A página 2 do AP: o nome da rede.
- [ ] `12-ap-3.png` (LCD 16×2) — mesmo estado; página Senha: 3 de 3 com a chave; borre a chave na foto publicada ou use um aparelho de demonstração
      A página 3 do AP: a chave.

## Capítulo 13 — A interface web no dia a dia

- [ ] `13-entrada.png` (Página web) — rota /login; largura 1280; sem sessão; idioma Português escolhido no seletor do rodapé; campos vazios
      A página de entrada. Note o seletor de idioma no rodapé e o link Trocar senha abaixo do botão Entrar.
- [ ] `13-entrada-bloqueio.png` (Página web) — rota /login; largura 390; sem sessão; uma senha errada acabou de ser enviada; a mensagem Bloqueado por 2s. Tentativas excessivas. está na tela e o botão Entrar está desativado
      O bloqueio depois de uma senha errada. O botão Entrar volta sozinho quando a contagem termina.
- [ ] `13-trocar-senha.png` (Página web) — rota /login, depois de tocar em Trocar senha; largura 390; sem sessão; Nova Senha preenchida com 8 caracteres entre letras e dígitos, sem símbolo; Repetir Nova Senha vazia
      O formulário de troca de senha. Três requisitos estão verdes e Símbolo ainda não; o botão Salvar Nova Senha continua desativado.
- [ ] `13-aviso-sessao-segura.png` (Página web) — rota /login; largura 1280; navegador com cookie seguro de uma visita HTTPS anterior ao mesmo endereço, aparelho agora em HTTP; logo depois de uma entrada com senha certa
      A faixa amarela que aparece quando o navegador não consegue guardar a sessão nova.
- [ ] `13-troca-obrigatoria.png` (Página web) — rota /force_chpass; largura 390; sessão de uma conta recém-criada com senha temporária; Nova Senha preenchida com uma senha que atende às quatro regras e Repetir Senha igual
      A troca de senha obrigatória. Com as duas senhas iguais e as quatro regras atendidas, a barra fica verde e o botão Salvar e Entrar fica ativo.
- [ ] `13-barra-topo.png` (Página web) — rota /alarms; largura 1280; sessão admin; um limite de alarme alterado e ainda não gravado; recorte da barra de topo e da trilha
      A barra de topo com uma alteração preparada: o selo, os botões de gravação, o botão de tema e o endereço do aparelho.
- [ ] `13-gaveta.png` (Página web) — rota /; largura 1280; sessão admin; gaveta aberta; idioma PT no seletor; relógio do aparelho acertado
      A gaveta aberta para a conta admin, com os oito itens, a versão, Licença, a saudação, o seletor de idioma e Sair.
- [ ] `13-gaveta-restrita.png` (Página web) — rota /; largura 390; sessão de um operador com as permissões Painel e Histórico; gaveta aberta
      A gaveta de uma conta com Painel e Histórico: só Painel de Controle, Histórico e Logs e Licença aparecem.
- [ ] `13-acesso-negado.png` (Página web) — rota /; largura 1280; sessão de um operador sem a permissão Painel, logo depois de entrar
      A resposta Access Denied, que uma conta sem a permissão Painel recebe ao entrar.
- [ ] `13-painel-de-controle.png` (Página web) — rota /; largura 1280; sessão admin; imagem release; tema escuro; 2 sensores ativos (um DS18B20 e um BME280); relógio acertado; telemetria ligada com alguns registros pendentes; bloco Tela antes de qualquer captura
      O Painel de Controle na imagem release: cartões de estado, cartões de métricas, tabela de sensores e, à direita, o bloco Tela.
- [ ] `13-painel-de-controle-alpha.png` (Página web) — rota /; largura 1280; sessão admin; imagem alpha; 1 sensor DHT22 ativo; telemetria desligada
      O Painel de Controle numa imagem sem painel com toque: uma coluna só, sem o bloco Tela.
- [ ] `13-painel-de-controle-celular.png` (Página web) — rota /; largura 390; sessão admin; imagem release; 2 sensores ativos; página rolada até a tabela de sensores
      O Painel de Controle no celular: os cartões em duas colunas e a tabela de sensores, que rola para o lado.
- [ ] `13-espelho-ao-vivo.png` (Página web) — rota /; largura 1280; sessão admin; imagem release; Ao vivo ligado; o painel está na tela inicial com 2 sensores; recorte do bloco Tela com a linha de estado mostrando fps, kB e s
      O espelho ao vivo. O botão mostra Parar e a linha de estado dá a taxa, o tamanho e o tempo de cada quadro.
- [ ] `13-captura.png` (Página web) — rota /; largura 1280; sessão admin; imagem release; depois de tocar em Capturar, com o painel na tela inicial; recorte do bloco Tela
      Uma captura da tela do painel, exibida no bloco Tela.
- [ ] `13-espelho-ocupado.png` (Página web) — rota /; largura 1280; sessão admin; imagem release; Ao vivo ligado; uma pessoa acabou de tocar no painel com o dedo; recorte do bloco Tela com a mensagem Display em uso na linha de estado
      O espelho esperando: alguém usa o painel com o dedo, e a página tenta de novo sozinha.
- [ ] `13-tema-claro.png` (Página web) — rota /; largura 1280; sessão admin; imagem release; tema claro escolhido no botão da barra; 2 sensores ativos
      O Painel de Controle no tema claro. Compare com a figura do tema escuro no início da seção do Painel de Controle.
- [ ] `13-celular-pendente.png` (Página web) — rota /alarms; largura 390; sessão admin; um limite de alarme alterado e ainda não gravado; a barra fixa do rodapé visível
      No celular, os botões de gravação ficam numa barra fixa no rodapé.
- [ ] `13-licenca.png` (Página web) — rota /license; largura 1280; sessão admin; página rolada até o início dos Avisos de Terceiros
      A página Licença, com o resumo em linguagem simples e o texto original da licença MIT.

## Capítulo 15 — Histórico

- [ ] `15-caminho.png` (Diagrama) — fluxo da esquerda para a direita: sensores → 'registro a cada Intervalo Histórico' → caixa 'bloco aberto (RAM), até 60 registros'; seta tracejada 'cópia a cada registro' para '/history/.wip'; seta cheia 'selar (60 registros, virada do dia, correção do NTP)' para '/history/AAAAMMDD.h5', desenhado como uma pilha de blocos, cada um com 'CRC' e 'mín/máx'; ao lado, uma fila de arquivos diários com o mais antigo marcado 'apagado acima de 86 %'
      Legenda: o registro nasce na RAM, é copiado para o .wip na hora e vai para o arquivo do dia quando o bloco sela.
- [ ] `15-pagina.png` (Página web) — rota /history; largura 1280; sessão admin; imagem release; 3 sensores ativos (DS18B20, DHT22, BME280); período 24h; um sensor selecionado; histórico com 20 dias; seção de eventos ainda não carregada
      A página Histórico e Logs: à esquerda, o seletor de sensores e o calendário; à direita, os mínimos e máximos, o gráfico e os controles de período; embaixo, os eventos do sistema.
- [ ] `15-sensores.png` (Página web) — rota /history; largura 1280; sessão admin; lista Origem aberta com 3 sensores, dois marcados; recorte do cartão da esquerda
      A lista de sensores aberta. O ponto colorido é a cor da série no gráfico.
- [ ] `15-calendario.png` (Página web) — rota /history; largura 1280; sessão admin; mês de setembro de 2026 com os dias 1 a 23 destacados; dia 22 selecionado; recorte do calendário
      O calendário com os dias que têm dados destacados. O dia tocado fica selecionado, e o gráfico mostra as 24 horas dele.
- [ ] `15-grafico-dois.png` (Página web) — rota /history; largura 1280; sessão admin; dois sensores marcados, um DS18B20 e um DHT22; período 24h; legenda visível; tooltip aberto sobre um ponto; eixos °C e %RH
      Dois sensores no mesmo gráfico: temperatura no eixo da esquerda, umidade tracejada no da direita, e a legenda no topo.
- [ ] `15-baldes.png` (Página web) — rota /history; largura 1280; sessão admin; um sensor DS18B20 de geladeira; período 7d; faixa mínimo-máximo visível em torno da linha média, com os ciclos de degelo; tooltip mostrando o valor e a faixa entre colchetes
      Uma semana em baldes: a linha é a média de cada balde, e a faixa sombreada vai do mínimo ao máximo. O pico do degelo aparece mesmo resumido.
- [ ] `15-carregando.png` (Página web) — rota /history; largura 1280; sessão admin; período MAX com 60 dias no aparelho; carga em andamento na metade, barra mostrando Arquivos: 31/60 e os KB recebidos, botão Cancelar visível
      A carga de um período longo: a barra conta os arquivos baixados, e Cancelar deixa na tela a parte mais recente.
- [ ] `15-celular.png` (Página web) — rota /history; largura 390; sessão admin; um sensor; período 24h; tooltip aberto por toque
      A página no celular, com o gráfico abaixo do calendário e a leitura de um ponto aberta pelo toque.
- [ ] `15-exportando.png` (Página web) — rota /history; largura 1280; sessão admin; exportação em andamento de um período 1M, caixa Exporting... com 45 %, tempo estimado, contagem OK e err, e o botão Cancelar
      A caixa da exportação em andamento. Cancelar salva o CSV com o que já chegou, com _partial no nome.

## Capítulo 16 — Log de eventos

- [ ] `16-transicao.png` (Diagrama) — linha do tempo de 3 horas da família Telemetria: pontos cinza pequenos a cada envio (console), pontos grandes onde o registro vai para a flash; flash no 1º envio depois do boot, na 1ª falha (coletor cai), nenhuma nas falhas seguintes, na 1ª volta, e um por hora nos trechos estáveis; marcas 'Registros de rotina suprimidos (5), ctx = N' a cada hora
      Legenda: o console vê todos os eventos; a flash guarda as transições e um registro por hora de cada família.
- [ ] `16-eventos.png` (Página web) — rota /history; largura 1280; sessão admin; seção Eventos do Sistema carregada; caixas INF, WRN e ERR marcadas; cerca de 15 linhas visíveis, incluindo uma entrada (300), uma mudança de configuração (303) e uma falha de telemetria (31)
      A seção Eventos do Sistema com os três níveis marcados. O registro mais recente fica no topo.
- [ ] `16-eventos-celular.png` (Página web) — rota /history; largura 390; sessão admin; seção Eventos do Sistema carregada, rolada até a tabela; só ERR marcado
      A tabela de eventos no celular, com o filtro de entrada: só erros.

## Capítulo 17 — Atualização, backup e restauração

- [ ] `17-arquivos.png` (Página web) — rota /files; largura 1280; sessão admin; pasta raiz; pastas history, lang, themes e web; arquivos calib.csv, README.txt (com cadeado), system.blog e system.old.blog
      A página Arquivos na raiz, vista pelo administrador: os seis botões, as pastas e os arquivos. O cadeado marca o README.txt, que não pode ser apagado.
- [ ] `17-arquivos-leitura.png` (Página web) — rota /files; largura 390; sessão de um operador com as permissões Painel, Histórico e Leitura; pasta raiz
      A mesma página para uma conta só com Leitura: resta o botão Baixar.
- [ ] `17-restaurar-confirmar.png` (Página web) — rota /files; largura 1280; sessão admin; backup do próprio aparelho escolhido, validação concluída, janela de confirmação aberta com o número de arquivos e o tamanho
      A confirmação da restauração, depois da validação: o número de arquivos, o tamanho e o aviso do que será sobrescrito.
- [ ] `17-restaurar-etapa3.png` (Página web) — rota /files; largura 1280; sessão admin; aviso Etapa 3/3: Reiniciando dispositivo (~25 s)... visível
      A última etapa da restauração: a página espera o aparelho voltar e abre a entrada.
- [ ] `17-aviso-firmware.png` (Página web) — rota /files; largura 1280; sessão admin; botão Firmware tocado; janela de confirmação do navegador com o aviso de reformatação
      O aviso que abre a atualização: o que sobrevive e o que se perde.
- [ ] `17-iniciar.png` (Página web) — rota /files; largura 1280; sessão admin; backup simut_pre-ota já baixado pelo navegador (barra de downloads visível); segunda janela de confirmação aberta
      A segunda confirmação, depois de o backup chegar ao computador. Cancelar aqui não muda nada no aparelho.
- [ ] `17-enviando.png` (Página web) — rota /files; largura 1280; sessão admin; aviso Etapa 2/4: Enviando firmware (~30 s)... visível durante o envio
      O envio da imagem. O painel do aparelho fica parado até o reinício.
- [ ] `17-ota-linha-tempo.png` (Diagrama) — linha do tempo horizontal de uma atualização: 'backup .bkp baixado' (segundos, verde); 'envio e conferência da imagem, 35 a 36 s' (amarelo, rótulo 'sistema de arquivos já sobrescrito'); 'aplicação, cerca de 25 s' (vermelho, rótulo 'única janela em que um corte de energia exige BOOTSEL'); 'boot da imagem nova' (verde); marca 'interface web de volta, 52 a 56 s depois da aplicação'; embaixo, o que cada corte causa: configuração perdida, aparelho sem firmware, nada
      Legenda: as etapas de uma atualização e o que um corte de energia em cada uma custa.
- [ ] `17-log-pos-ota.png` (Página web) — rota /history; largura 1280; sessão admin; logo depois de uma atualização; seção Eventos do Sistema carregada com INF, WRN e ERR marcados; os dois registros Config alterada do módulo OTA, WRN e INF, no fim da lista
      O log de eventos logo depois de uma atualização: o boot pós-aplicação (WRN) e a conferência da imagem (INF).

## Capítulo 18 — Recuperação

- [ ] `18-bootsel.png` (Foto) — um Pico W sendo ligado ao cabo USB com o polegar segurando o botão BOOTSEL; o botão em destaque, com uma seta; ao lado, a janela do gerenciador de arquivos do computador mostrando a unidade RPI-RP2
      O botão BOOTSEL do Pico W, segurado enquanto o cabo é ligado, e a unidade RPI-RP2 que aparece no computador.

## Capítulo 19 — SIMUT Air

- [ ] `19-modos.png` (Diagrama) — máquina de estados com dois estados, M0 (acordado: web, console, Bluetooth, rádio) e M1 (ciclo: dormir, despertar, ler, gravar, dormir). Setas de M0 para M1: 'air hibernate' e 'ociosidade (air idle)'. Setas de M1 para M0: 'air stop pelo USB (desarma)', 'carregador no despertar (continua armado)', 'boot limpo (M0 pelo idle inteiro, depois volta)', 'watchdog (10 s de graça, depois volta)', '3 watchdogs seguidos (retido em M0, evento 411)'. Dentro de M1, o laço 'dormir → despertar → ler → gravar → [rádio se pendentes ≥ lote mínimo] → dormir'
      Legenda: como o Air entra e sai do ciclo de hibernação.
- [ ] `19-despertar.png` (Diagrama) — linha do tempo de dois despertares num intervalo de 60 s: faixa GP16 alta durante cada despertar; despertar de leitura com boot, aquecimento 0,4 s, amostragem (a maior parte, com a conversão do DS18B20), decisão com 'grava o histórico', e sono até completar 60 s; despertar de telemetria com a amostragem e o Wi-Fi em paralelo, decisão, conexão e envio, faixa 'rádio ligado' só nele; marcas 9,3 s (leitura) e 12,7 s (telemetria)
      Legenda: as fases de um despertar. O rádio só liga nos despertares de telemetria.
- [ ] `19-montagem.png` (Foto) — um Air montado: Pico W com um DS18B20 e o resistor de 4,7 kΩ, a bateria e o carregador; os fios do GP16 e do GP17 identificados com etiquetas
      Um Air montado. O GP16 mostra quando ele está acordado e pode alimentar os sensores; o GP17 detecta o carregador.
- [ ] `19-painel-controle.png` (Página web) — rota /; largura 1280; sessão admin; imagem air em M0; 1 sensor DS18B20; cartão Registros Pendentes com alguns registros; sem o bloco de espelho do painel
      O Painel de Controle de um Air em M0: sem o espelho do painel, que não existe nesta imagem.

## Capítulo 20 — Visão geral da integração

- [ ] `20-integracao.png` (Diagrama) — Diagrama com o aparelho SIMUT à esquerda e quatro destinos à direita. Setas saindo do aparelho: 'Telemetria (HTTP/HTTPS POST ou MQTT/MQTTS)' e 'Linha de alarmes (mesmo transporte, com confirmação)' para o coletor; 'Descoberta Home Assistant (MQTT, mensagens retidas)' para o broker; 'Syslog RFC 5424 (UDP 514)' para o coletor de syslog. Setas chegando ao aparelho, na porta web: 'API REST com sessão' vinda do servidor de gestão; 'GET /metrics' vinda do Prometheus; 'mDNS _simut._tcp' como anúncio na rede local. Uma legenda separa 'o aparelho abre a conexão' de 'o servidor abre a conexão'.
      Legenda: os dois sentidos do tráfego. As setas que saem do aparelho são conexões que ele mesmo abre; as que chegam usam a porta da interface web.

## Capítulo 21 — Telemetria de medições

- [ ] `21-cursor.png` (Diagrama) — Linha do tempo horizontal com os registros do histórico como pequenos quadrados. Uma marca vertical 'cursor' separa os registros já confirmados (cinza, à esquerda) dos pendentes (coloridos, à direita). Um colchete agrupa os primeiros pendentes como 'lote (até o Lote máximo)'. Uma seta leva o lote ao 'coletor'; a volta '2xx' move o cursor para o fim do lote; uma volta alternativa 'falha' deixa o cursor parado e aponta para 'espera e reenvia'. À direita, o bloco da hora aberta, ainda na RAM, também entra no lote.
      Legenda: o cursor separa o que o coletor já confirmou do que ainda falta. Só uma confirmação o move.
- [ ] `21-pagina-http.png` (Página web) — rota /telemetry; largura 1280; sessão admin; imagem release; Transporte HTTP(S); IP Servidor coletor.exemplo.com.br; Porta 8080; TLS desligado; Endpoint /api/telemetria; API Key preenchida (mostrada mascarada); Lote mínimo 1; Lote máximo 250; Formato JSON
      Legenda: a página Telemetria com o transporte HTTP. Note a chave mascarada no campo API Key e os botões Enviar agora e Resetar cursor de envio logo abaixo dos lotes.
- [ ] `21-pagina-mqtt.png` (Página web) — rota /telemetry; largura 1280; sessão admin; Transporte MQTT(S); IP Servidor coletor.exemplo.com.br; Porta 1883; TLS desligado (rótulo Usar MQTTS (TLS)); Tópico fabrica/camara1/data; Client ID vazio; Usuário simut; Senha vazia; QoS 0; Keep-Alive 60; Reter desligado
      Legenda: com o transporte MQTT, os campos Endpoint e API Key saem e entram os do broker. Note que a Porta não muda sozinha.
- [ ] `21-construtor.png` (Página web) — rota /telemetry; largura 1280; sessão admin; bloco Construtor com Formato Dinâmico; quadro de tags visível; 1. Global com o modelo de fábrica; 2. Linha com o modelo de dois sensores da seção Exemplos deste capítulo (t0_ID, t1_ID, u1_ID e p1_ID); 3. Separador vírgula; Prévia ao Vivo preenchida
      Legenda: o construtor no formato Dinâmico, com o quadro de tags e a prévia. A prévia usa valores de demonstração.

## Capítulo 22 — Linha de alarmes

- [ ] `22-fluxo.png` (Diagrama) — Diagrama em duas raias, HTTP e MQTT. À esquerda, a fila na RAM do aparelho com registros seq 1, 2 e 3. Raia HTTP: seta 'POST com a fila inteira' até o coletor; volta '2xx' e os três registros saem da fila; volta alternativa 'outro código ou nada em 4 s' e a fila fica, com 'novo envio em 15 s'. Raia MQTT: seta 'publish em <tópico>/alarm' até o broker e daí ao coletor; o coletor grava e publica a confirmação com os seq 1, 2 e 3 em '<tópico>/alarm/ack'; o broker entrega ao aparelho, que tira os três da fila. Uma nota mostra que, sem a confirmação, a fila inteira é publicada de novo a cada 15 s.
      Legenda: no HTTP, a resposta 2xx confirma o lote. No MQTT, a publicação não confirma nada: só a mensagem de confirmação do coletor tira os registros da fila.
- [ ] `22-bloco-alarmes.png` (Página web) — rota /telemetry; largura 1280; sessão admin; rolar até o bloco Payload de Alarmes — 2ª Linha de Telemetria; Habilitar linha de telemetria de alarmes ligado; Formato JSON; Tamanho da fila (RAM) 32; Caminho HTTP vazio; texto Pendentes: 0 visível
      Legenda: o bloco da linha de alarmes, no fim da página Telemetria. Note o número de Pendentes no texto do bloco e o Caminho HTTP vazio, que usa o Endpoint da telemetria seguido de /alarm.

## Capítulo 23 — Home Assistant

- [ ] `23-fluxo.png` (Diagrama) — Três caixas da esquerda para a direita: 'SIMUT (simut_0a1b2c)', 'Broker MQTT (broker.exemplo.com.br:1883)' e 'Home Assistant'. Setas do SIMUT para o broker, numeradas: 1 'ao conectar: homeassistant/sensor/simut_0a1b2c/<objeto>/config (retida, uma por medição)'; 2 'simut/status {status: online} (retida)'; 3 'simut/data {ts, tSTM0001, ...} a cada envio'. Seta tracejada do broker para o próprio broker: 'se o aparelho some: simut/status {status: offline} (última vontade)'. Setas do broker para o Home Assistant: 'assina homeassistant/# e cria dispositivo e entidades'; 'lê os valores em simut/data'; 'lê a disponibilidade em simut/status'. Nota no rodapé: 'o Home Assistant não manda nada ao aparelho'.
      Legenda: o caminho das mensagens. O aparelho só publica; o Home Assistant só assina. Tudo passa pelo broker.
- [ ] `23-telemetria-ha.png` (Página web) — rota /telemetry; largura 1280; sessão admin; Transporte MQTT(S); IP Servidor broker.exemplo.com.br; Porta 1883; Usar MQTTS (TLS) desligado; Tópico simut/data; Client ID vazio; Usuário simut; Reter ligado; Home Assistant Discovery ligado, com a dica visível; Lote mínimo 1; Formato JSON; recorte do bloco MQTT até o Lote mínimo
      Legenda: a opção Home Assistant Discovery no bloco do MQTT. Note a dica embaixo dela: a descoberta exige o formato JSON e só acontece no próximo envio.
- [ ] `23-evento-548.png` (Página web) — rota /history; largura 1280; sessão admin; seção Eventos do Sistema carregada, com INF marcado e o filtro 548; uma linha do evento 548 Discovery HA atualizado com contexto 4, logo abaixo de 35 MQTT conectado
      Legenda: o evento 548 confirma a publicação. O contexto 4 é o número de entidades publicadas.

## Capítulo 24 — Prometheus

- [ ] `24-coleta.png` (Diagrama) — À esquerda, 'Prometheus (a cada 60 s)'; à direita, 'SIMUT 192.0.2.10'. Seta do Prometheus para o aparelho: 'GET /metrics, Authorization: Basic metricas:…'. Dentro do aparelho, uma caixa 'confere a senha (5.000 rodadas, cerca de 0,7 s)' e uma caixa 'endereço bloqueado?'. Quatro setas de volta: '200 texto do Prometheus', '401 senha errada', '403 conta sem Painel', '429 endereço bloqueado'. Embaixo, uma escada de bloqueio: '1º erro 2 s, 2º 4 s, 3º 8 s … 9º em diante 300 s', com a nota 'mesmo bloqueio da página de entrada'.
      Legenda: uma leitura do Prometheus. O aparelho confere a senha a cada pedido, e as senhas erradas alimentam o mesmo bloqueio da página de entrada.
- [ ] `24-conta-metricas.png` (Página web) — rota /users; largura 1280; sessão admin; formulário Adicionar com Nome metricas, só a caixa Painel marcada, PIN vazio; a conta aparece na tabela com Pendente: Novo, ainda não gravada
      Legenda: a conta do Prometheus, com a permissão Painel e nenhuma outra.

## Capítulo 25 — Syslog

- [ ] `25-caminho.png` (Diagrama) — Da esquerda para a direita: 'evento (qualquer núcleo)' → 'nível ≥ Nível Mínimo?' → 'fila de 8 linhas na RAM (a mais antiga sai quando enche)' → 'laço principal, núcleo 0: até 8 linhas por passada, só com Wi-Fi conectado e fora do AP' → 'UDP para 192.0.2.10:514' → 'coletor (rsyslog, syslog-ng)'. Marcas vermelhas de perda: 'antes do encaminhamento subir, no boot', 'fila cheia', 'envio falhou', 'datagrama perdido na rede', 'travamento ou falta de energia'. Uma seta lateral: 'reinício pedido (quase todos): esvazia a fila antes de reiniciar'. Embaixo, em paralelo, 'log de eventos na flash (filtro de rotina, só código e contexto)'.
      Legenda: o caminho de uma linha de syslog e os pontos onde ela pode se perder. O log de eventos local segue por outro caminho e não depende do syslog.

## Capítulo 26 — API REST

- [ ] `26-entrada.png` (Diagrama) — diagrama de sequência entre um cliente e o aparelho: GET /api/login_init devolve nonce; o cliente calcula SHA-256 da senha; POST /api/login com user, pass e nonce; o aparelho confere e devolve Set-Cookie SIMUTSESS e o corpo {ok, redirect}; pedidos seguintes levam o cookie ou Authorization: Bearer; GET /logout encerra
      A entrada em dois pedidos. O código de uso único vale 60 s e serve para uma única tentativa.
- [ ] `26-ota.png` (Diagrama) — diagrama de sequência entre um cliente e o aparelho: GET /api/backup e conferência do CRC; POST /api/restore?op=stage&commit=1 com o .bin, resposta com v=0 e committed=1; POST /api/ota/apply, resposta 202; o aparelho reinicia; o cliente repete GET /api/login_init até responder; entra e lê a versão em /api/status; restaura o .bkp com op=apply
      A atualização pela API. O único comprovante de sucesso é a versão nova informada pelo próprio aparelho.

## Capítulo 27 — Gestão de frota

- [ ] `27-arquitetura.png` (Diagrama) — diagrama: vários aparelhos SIMUT numa rede local; setas saindo deles para o coletor (telemetria e linha de alarmes, com X-SIMUT-Uid/Ver/Env/Cfg); setas do Prometheus para cada aparelho (GET /metrics, HTTP Basic); setas do gestor de frota para cada aparelho (API REST com cookie ou Bearer); um anúncio mDNS _simut._tcp saindo dos aparelhos release para a rede local; o gestor lendo o manifest.json da release no GitHub
      Os caminhos de uma frota. Os aparelhos empurram medições e alarmes; o gestor e o Prometheus perguntam.
- [ ] `27-ondas.png` (Diagrama) — diagrama: a frota dividida em três ondas; onda 0 com um aparelho de bancada, onda 1 com poucos aparelhos de cada imagem, onda 2 com o resto; entre as ondas, uma barreira 'conferir versão, telemetria e alarmes por um período'; dentro de cada aparelho, a sequência backup, envio, aplicação, confirmação, restauração, idioma
      A atualização em ondas. Cada onda só começa depois de a anterior se provar estável.

## Capítulo 28 — Especificações e limites

- [ ] `28-flash.png` (Diagrama) — barra horizontal dos 2 MB da flash do Pico W, em escala: 1.020 KiB do programa (com a marca do teto de 1.016 KiB da atualização web), 1.024 KiB do sistema de arquivos LittleFS e 4 KiB dos dados da atualização no fim; anotar que a imagem nova é recebida na área do sistema de arquivos
      Legenda: a flash de 2 MB. O teto da atualização web fica 4 KiB abaixo do fim do espaço do programa.
