# Imagens a capturar para o manual

Gerado por `tools/build_manual.py` — não edite à mão. Salve cada captura
nesta pasta com o nome indicado e rode o script de novo.

96 pendentes de 245.


## Capítulo 2 — Hardware e ligações

- [ ] `02-placa.png` (Foto) — placa SIMUT v1.1 montada, vista de cima: Pico W no soquete, conector TFT-Touch com o painel ligado, bloco Alphanumeric vazio, coluna S0–S10 com uma sonda DS18B20 em S2, cigarra, conversor HLK-PM01 e a marcação 90-240Vac visível
      A placa v1.1 montada para a release: o Pico W, o conector do painel, a coluna de sensores e a entrada de rede elétrica.

## Capítulo 3 — Instalar o firmware

- [ ] `03-bootsel.png` (Foto) — mão segurando o botão BOOTSEL de um Pico W enquanto o cabo USB é ligado; o botão bem visível, ao lado do chip RP2040
      O botão BOOTSEL, segurado enquanto o cabo USB é ligado.

## Capítulo 4 — Primeiro boot e primeira configuração

- [ ] `04-painel-data-hora.png` (Tela do painel) — release recém-gravada, sem rede configurada, logo depois do boot; tela Data e hora com as cinco colunas e as setas, a linha Sem Wi-Fi, os dados levam esta hora. em âmbar e os botões PULAR e SALVAR; sem rede, GET /api/screenshot não alcança o aparelho: use foto da tela, antes dos 30 s sem toque que levam o painel à tela inicial
      A pergunta do fim do boot num aparelho sem rede: a data e a hora do relógio provisório, prontas para acertar.
- [ ] `04-painel-ap.png` (Tela do painel) — release recém-gravada, sem rede configurada; ponto de acesso aberto pelo menu (CFG, admin, PIN, item 8 Modo de Configuração, Confirmar) ou pelo comando ap no console USB; a caixa termina com simut_SETUP, PSK e a chave, Acesse no celular: 192.168.4.1 e AP Ativo! Reinicie a placa para sair.; capturar com GET /api/screenshot pelo próprio AP, com sessão admin depois da troca de senha; se a captura não funcionar no AP, use foto da tela; borre a chave antes de publicar
      O painel com o ponto de acesso aberto: a rede, a chave e o aviso de AP ativo ficam na tela.
- [ ] `04-novo-pin.png` (Tela do painel) — release com configuração de fábrica; tela inicial → botão de configurações → admin → PIN 1234 → ENTRAR; tela Novo PIN aberta, antes de digitar
      A troca obrigatória do PIN de fábrica: a tela Novo PIN abre direto depois do 1234.

## Capítulo 5 — Configuração pela web: os três botões e a página Configurações

- [ ] `05-selo-reinicio.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; campo Fuso Horário alterado de -3 para -4 e ainda não gravado; ensaio já respondido; recorte da barra de topo
      Uma alteração que exige reinício: o selo diz Reinicia por: time e só o botão Salvar e reiniciar aparece.
- [ ] `05-tres-botoes.png` (Página web) — rota /alarms; largura 1280; sessão admin; imagem release; limite máximo de temperatura de um slot alterado; ensaio respondido Não exige reinício; recorte da barra de topo com o selo e os três botões
      Uma alteração que se aplica sem reinício: o selo diz Não exige reinício e os três botões aparecem.
- [ ] `05-confirmacao.png` (Página web) — rota /config; largura 1280; sessão admin; Nome do aparelho alterado; janela de confirmação aberta depois de tocar em Salvar e reiniciar
      A confirmação de Salvar e reiniciar, que avisa quanto tempo o aparelho fica fora do ar.
- [ ] `05-data-hora.png` (Página web) — rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos de data e hora preenchidos; mensagem Hora aplicada. abaixo do botão Aplicar Agora
      A seção Data e Hora com o NTP desligado, depois de um acerto manual.

## Capítulo 6 — Sensores e calibração

- [ ] `06-editor-bme280.png` (Página web) — rota /config; largura 390; sessão admin; imagem release; editor de um slot novo com o tipo BME280 escolhido, Pino 0 — SDA em GP4, Pino 1 — SCL em GP5, ID de hardware e nome preenchidos, Ativo ligado, ainda não gravado
      Um slot novo de BME280 no celular: dois seletores de pino, SDA e SCL.
- [ ] `06-editor-aviso.png` (Página web) — rota /config; largura 1280; sessão admin; editor do slot 1, DHT22 ativo, com o pino Data trocado para GP4, que já pertence ao slot 2 (o BMP280); a linha vermelha mostra GP4 já está em uso pelo slot 2.
      O editor avisando um conflito de GPIO antes da gravação.
- [ ] `06-procurar.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; os sensores da bancada ligados (DS18B20 no GP0 e no GP1, DHT22 no GP3 e no GP10, BMP280 no GP4/GP5); logo depois de tocar em Procurar sondas e a busca terminar; recorte dos botões e da linha de resultado
      O resultado da busca: a ROM identifica cada DS18B20; o DHT22 aparece só com o pino.
- [ ] `06-painel-sensor.png` (Tela do painel) — screen dash; 2 sensores ativos (o DS18B20 do GP0 e o BMP280 do GP4/GP5), ambos lendo, nenhum em alarme
      A tela inicial do painel com dois sensores lendo normalmente.
- [ ] `06-painel-erro.png` (Tela do painel) — screen dash; 2 sensores ativos, os dois DS18B20 (GP0 e GP1); o do GP1 solto da protoboard há mais de 3 leituras, alarmes do slot ligados
      Um sensor em erro no painel: o cartão mostra Erro no lugar da temperatura e fica âmbar pelo alarme de falha.
- [ ] `06-calibracao.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DHT22 ativo; relógio sincronizado por NTP; Temp com 2 pontos preenchidos e Reta escolhida; Umid sem correção; recorte do bloco Calibração
      O bloco Calibração de um DHT22. Cada grandeza tem sua linha de leitura, o mini gráfico da correção e seus pontos.
- [ ] `06-calibracao-suave.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; editor de um slot DS18B20; Temp com 3 pontos (0, 25 e 50 °C de referência) e Suave escolhida; recorte do bloco Calibração
      Uma correção Suave com três pontos: o mini gráfico mostra a curva passando pelos pontos sem ultrapassá-los.
- [ ] `06-calibracao-ntp.png` (Página web) — rota /config; largura 1280; sessão admin com a permissão Calibração; relógio do aparelho sem sincronização NTP; editor de um slot ativo; recorte do fim do bloco Calibração com o aviso NTP não sincronizado.
      Sem NTP, o bloco avisa em vermelho que a gravação será recusada.

## Capítulo 7 — Alarmes, sons e janelas de manutenção

- [ ] `07-painel-falha.png` (Tela do painel) — screen dash; 2 sensores ativos, os dois DS18B20 (GP0 e GP1); o do GP1, no cartão de baixo, solto da protoboard há mais de 3 leituras; captura na fase âmbar da piscada
      Um alarme de falha no painel: o cartão pisca em âmbar e mostra Erro no lugar do valor.
- [ ] `07-menu-sensor-manut.png` (Tela do painel) — menu do sensor aberto por uma conta que tem só a permissão Manut. (painel); caminho: tela inicial, botão de configurações, conta, PIN, Limites de Alarme, sensor
      O mesmo menu para uma conta só com Manut. (painel): as duas primeiras linhas apagadas, com cadeado, e a seleção em Manutenção.
- [ ] `07-manutencao-entrada.png` (Tela do painel) — menu do sensor -> Manutenção -> ENTRAR; sessão admin; janela fechada; Horas 2 e Minutos 30 ajustados, antes de INICIAR
      A tela Manutenção com a janela fechada: horas, minutos e INICIAR.
- [ ] `07-manutencao-aberta.png` (Tela do painel) — mesmo caminho, com a janela aberta há alguns minutos
      A tela Manutenção com a janela aberta: o tempo que falta e FECHAR.
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
- [ ] `08-senha-temporaria.png` (Página web) — rota /users; largura 1280; sessão admin; logo depois de Aplicar agora com uma conta nova chamada operador1; janela Senha temporária aberta; a senha trocada na página por uma fictícia no mesmo formato
      A janela Senha temporária, com a senha de uso único da conta criada.
- [ ] `08-quem.png` (Tela do painel) — screen pin; contas admin, operador1 e manutencao com PIN; nenhuma bloqueada
      A tela Quem está usando o painel?, com as contas que têm PIN.
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

- [ ] `09-ip-estatico.png` (Página web) — rota /network; largura 1280; sessão admin; DHCP desligado; IP Estático 192.0.2.10, Máscara 255.255.255.0, Gateway 192.0.2.1; DNS automático desligado com DNS Primário 192.0.2.1; alterações preparadas e ainda não gravadas
      A seção IPv4 com o DHCP desligado e os campos do IP fixo preenchidos.
- [ ] `09-servidor-web.png` (Página web) — rota /network; largura 1280; sessão admin; imagem release; certificado HTTPS instalado; página aberta por https; recorte das seções Servidor de Hora (NTP) e Servidor Web, com o interruptor Conexões persistentes visível e ligado
      A seção Servidor Web num aparelho com certificado instalado: o interruptor de conexões persistentes só aparece nesse caso.
- [ ] `09-aviso-certificado.png` (Página web) — rota https://192.0.2.10/login; largura 1280; navegador Chrome sem a exceção aceita; certificado autoassinado recém-instalado; página de aviso do navegador
      O aviso do navegador no primeiro acesso a um aparelho com certificado autoassinado.

## Capítulo 10 — Data e hora

- [ ] `10-acerto-manual.png` (Página web) — rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos Data e Hora preenchidos e a mensagem Hora aplicada.
      O acerto manual com o NTP desligado: data, hora e o botão Aplicar Agora da seção.
- [ ] `10-painel-hora-provisoria.png` (Tela do painel) — screen dash; rede configurada e NTP desligado e gravado, reiniciado e sem acerto à mão desde o boot; recorte da faixa de topo com a data e a hora em âmbar e o ? no lugar do -
      O relógio provisório no topo do painel: a data e a hora em âmbar, separadas por ?.

## Capítulo 11 — O painel original (Pico W)

- [ ] `11-principal.png` (Tela do painel) — screen dash; 3 sensores ativos: DS18B20 no slot 0 (fixado no cartão de cima), DHT22 no slot 1 (selecionado) e BMP280 no slot 2; Wi-Fi conectado com sinal forte; 12 registros pendentes; relógio acertado; nenhum alarme
      A tela inicial: a barra de cima, os dois cartões e a barra de botões com S1, S2 e CFG.
- [ ] `11-pendentes-falha.png` (Tela do painel) — screen dash; telemetria ligada com o coletor fora do ar há alguns minutos; recorte da barra de cima com a contagem de pendentes em vermelho
      A contagem de pendentes em vermelho: o último envio falhou.
- [ ] `11-aviso-web.png` (Tela do painel) — screen dash; até 5 s depois de entrar na interface web com a conta admin; recorte da barra de cima com Web: admin no lugar do relógio
      O aviso de entrada pela web, no lugar da data e da hora.
- [ ] `11-selecao.png` (Tela do painel) — screen dash; 3 sensores ativos; segure o dedo no cartão de cima por 3 s e solte; a ferramenta de mapa de telas não faz o gesto de segurar
      O cartão de cima em seleção: cinza, espelhando o sensor do cartão de baixo.
- [ ] `11-paginas.png` (Tela do painel) — screen dash; 7 slots ativos (os 5 sensores da bancada e 2 slots DS18B20 num pino vazio, com o alarme desligado), nenhum fixado fora da primeira página; barra de botões na primeira página, com S e o botão 1/2
      A barra de botões com páginas: quatro botões e o botão de página à direita.
- [ ] `11-web-ocupado.png` (Tela do painel) — screen dash; durante a exportação em CSV de um dia de histórico pela conta admin, na página Histórico e Logs
      O aviso de web ocupada: WEB ‘admin’ - toque bloqueado.
- [ ] `11-carregando.png` (Tela do painel) — tela MODE_GRAPH_LOADING; aparece por instantes ao abrir um gráfico a partir da tela inicial; transitória, pode exigir várias tentativas de captura
      A tela Carregando…, ao abrir um gráfico.
- [ ] `11-detalhe-temp.png` (Tela do painel) — screen gra -> tap(160,120); gráfico do BMP280 aberto antes, em 24H
      O detalhe numérico, página de temperatura: máximo, mínimo, média e desvio.
- [ ] `11-detalhe-umid.png` (Tela do painel) — screen gra -> tap(160,120) -> tap(160,120); gráfico do DHT22 aberto antes, em 24H; a segunda página do DHT22 é a umidade
      A página de umidade do detalhe.
- [ ] `11-detalhe-pressao.png` (Tela do painel) — screen gra -> tap(160,120) -> tap(160,120); gráfico do BMP280 aberto antes, em 24H; a segunda página do BMP280 é a pressão
      A página de pressão do detalhe.
- [ ] `11-mira.png` (Tela do painel) — screen touchcal e complete a etapa de sensibilidade com o dedo; capture a primeira mira, no canto de cima à esquerda; o toque simulado não completa a sensibilidade, porque não tem pressão
      A etapa de posição: a primeira mira, Toque na mira (1/4) e a volta [ 1 / 2 ].
- [ ] `11-calib-ok.png` (Tela do painel) — depois do oitavo toque de uma calibração com as duas voltas coerentes
      O fim de uma calibração aceita: Calibração Concluída! e ENTENDI.
- [ ] `11-calib-recusada.png` (Tela do painel) — depois do oitavo toque, tendo tocado longe da mira num dos cantos da segunda volta
      Uma calibração recusada: Toques imprecisos! Tente novamente.
- [ ] `11-alinhamento.png` (Tela do painel) — screen offset; deslocamento X +2 e Y -1 ajustado, antes de APLICAR
      O alinhamento da tela, com as quatro setas, o quadrado de zerar e os valores X e Y.
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

## Capítulo 13 — A interface web no dia a dia

- [ ] `13-entrada-bloqueio.png` (Página web) — rota /login; largura 390; sem sessão; uma senha errada acabou de ser enviada; a mensagem Bloqueado por 2s. Tentativas excessivas. está na tela e o botão Entrar está desativado
      O bloqueio depois de uma senha errada. O botão Entrar volta sozinho quando a contagem termina.
- [ ] `13-aviso-sessao-segura.png` (Página web) — rota /login; largura 1280; navegador com cookie seguro de uma visita HTTPS anterior ao mesmo endereço, aparelho agora em HTTP; logo depois de uma entrada com senha certa
      A faixa amarela que aparece quando o navegador não consegue guardar a sessão nova.
- [ ] `13-troca-obrigatoria.png` (Página web) — rota /force_chpass; largura 390; sessão de uma conta recém-criada com senha temporária; Nova Senha preenchida com uma senha que atende às quatro exigências e Repetir Nova Senha igual
      A troca de senha obrigatória. Com as duas senhas iguais e as quatro regras atendidas, a barra fica verde e o botão Salvar e Entrar fica ativo.
- [ ] `13-barra-topo.png` (Página web) — rota /alarms; largura 1280; sessão admin; um limite de alarme alterado e ainda não gravado; recorte da barra de topo e da trilha
      A barra de topo com uma alteração preparada: o selo, os botões de gravação, o botão de tema e o endereço do aparelho.
- [ ] `13-gaveta-restrita.png` (Página web) — rota /; largura 390; sessão de um operador com as permissões Painel e Histórico; gaveta aberta
      A gaveta de uma conta com Painel e Histórico: só Painel de Controle, Histórico e Logs e Licença aparecem.
- [ ] `13-acesso-negado.png` (Página web) — rota /; largura 1280; sessão de um operador sem a permissão Painel, logo depois de entrar
      A resposta Access Denied, que uma conta sem a permissão Painel recebe ao entrar.
- [ ] `13-painel-de-controle-alpha.png` (Página web) — rota /; largura 1280; sessão admin; imagem alpha, com o TFT fora e o LCD 16x2 nos GP16 a GP21; 1 sensor DHT22 ativo; telemetria desligada; bloco Tela depois de tocar em Capturar
      O Painel de Controle na alpha: o bloco Tela, numa linha própria acima dos cartões, mostra o LCD 16x2.
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

## Capítulo 15 — Histórico

- [ ] `15-calendario.png` (Página web) — rota /history; largura 1280; sessão admin; histórico sintético (tools/gen_synth_history.py) em pelo menos 20 dias do mês da captura; um dia com dados selecionado; recorte do calendário
      O calendário com os dias que têm dados destacados. O dia tocado fica selecionado, e o gráfico mostra as 24 horas dele.
- [ ] `15-baldes.png` (Página web) — rota /history; largura 1280; sessão admin; um sensor DS18B20 com histórico sintético de geladeira (tools/gen_synth_history.py); período 7d; faixa mínimo-máximo visível em torno da linha média, com os ciclos de degelo; tooltip mostrando o valor e a faixa entre colchetes
      Uma semana em baldes: a linha é a média de cada balde, e a faixa sombreada vai do mínimo ao máximo. O pico do degelo aparece mesmo resumido.
- [ ] `15-carregando.png` (Página web) — rota /history; largura 1280; sessão admin; período MAX com 60 dias de histórico sintético no aparelho; carga em andamento na metade, barra mostrando Arquivos: 31/60 e os KB recebidos, botão Cancelar visível
      A carga de um período longo: a barra conta os arquivos baixados, e Cancelar deixa na tela a parte mais recente.
- [ ] `15-exportando.png` (Página web) — rota /history; largura 1280; sessão admin; exportação em andamento de um período 1M, caixa Exporting... com 45 %, tempo estimado, contagem OK e err, e o botão Cancelar
      A caixa da exportação em andamento. Cancelar salva o CSV com o que já chegou, com _partial no nome.

## Capítulo 17 — Atualização, backup e restauração

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
      O envio da imagem. Até a v2.8.x o painel do aparelho ficava parado até o reinício; agora ele mostra a etapa (capítulo 11).
- [ ] `17-log-pos-ota.png` (Página web) — rota /history; largura 1280; sessão admin; logo depois de uma atualização; seção Eventos do Sistema carregada com INF, WRN e ERR marcados; os dois registros Config alterada do módulo OTA, WRN e INF, no fim da lista
      O log de eventos logo depois de uma atualização: o boot pós-aplicação (WRN) e a conferência da imagem (INF).

## Capítulo 18 — Recuperação

- [ ] `18-bootsel.png` (Foto) — um Pico W sendo ligado ao cabo USB com o polegar segurando o botão BOOTSEL; o botão em destaque, com uma seta; ao lado, a janela do gerenciador de arquivos do computador mostrando a unidade RPI-RP2
      O botão BOOTSEL do Pico W, segurado enquanto o cabo é ligado, e a unidade RPI-RP2 que aparece no computador.

## Capítulo 19 — SIMUT Air

- [ ] `19-montagem.png` (Foto) — um Air montado: Pico W com um DS18B20 e o resistor de 4,7 kΩ, a bateria e o carregador; os fios do GP16 e do GP17 identificados com etiquetas
      Um Air montado. O GP16 mostra quando ele está acordado e pode alimentar os sensores; o GP17 detecta o carregador.
- [ ] `19-painel-controle.png` (Página web) — rota /; largura 1280; sessão admin; imagem air em M0; 1 sensor DS18B20; cartão Registros Pendentes com alguns registros; sem o bloco de espelho do painel
      O Painel de Controle de um Air em M0: sem o espelho do painel, que não existe nesta imagem.

## Capítulo 23 — Home Assistant

- [ ] `23-evento-548.png` (Página web) — rota /history; largura 1280; sessão admin; seção Eventos do Sistema carregada, com INF marcado e o filtro 548; uma linha do evento 548 Discovery HA atualizado com contexto 4, logo abaixo de 35 MQTT conectado
      O evento 548 confirma a publicação. O contexto 4 é o número de entidades publicadas.

## Capítulo 24 — Prometheus

- [ ] `24-conta-metricas.png` (Página web) — rota /users; largura 1280; sessão admin; formulário Adicionar com Nome metricas, só a caixa Painel marcada, PIN vazio; a conta aparece na tabela com Pendente: Novo, ainda não gravada
      A conta do Prometheus, com a permissão Painel e nenhuma outra.
