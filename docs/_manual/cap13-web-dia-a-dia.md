# A interface web no dia a dia {#cap-13}

Este capítulo mostra como entrar na interface web do SIMUT, como andar entre as páginas e como ler a página **Painel de Controle**, inclusive o espelho do painel. Ele é para quem usa o aparelho pelo navegador todos os dias; a configuração de cada página está nos capítulos indicados no fim.

## O que é a interface web {#cap-13-visao}

O próprio aparelho serve a interface web. Ela não depende de internet: as páginas, a folha de estilo, os ícones e o desenho dos gráficos saem do aparelho, então tudo funciona numa rede local isolada. Toda imagem que vai a campo leva a interface inteira dentro do firmware.

A interface tem 11 páginas e dois arquivos comuns:

| Página | Rota | Para que serve |
|---|---|---|
| Entrada | `/login` | Entrar e trocar a própria senha |
| Troca de senha obrigatória | `/force_chpass` | Definir a senha no primeiro acesso |
| Painel de Controle | `/` | Leituras, estado do aparelho e espelho do painel |
| Histórico e Logs | `/history` | Gráficos, calendário e eventos |
| Alarmes e Sons | `/alarms` | Limites de alarme e sons |
| Telemetria | `/telemetry` | Envio de medições e linha de alarmes |
| Configurações | `/config` | Identidade, hora, sensores e syslog |
| Rede | `/network` | Wi-Fi, IP, DNS, NTP e servidor web |
| Usuários | `/users` | Contas e permissões |
| Arquivos | `/files` | Arquivos, backup, restauração e atualização |
| Licença | `/license` | A licença do software |

Os dois arquivos comuns são `/style.css`, a aparência das páginas internas, e `/lang.js`, que cuida do tema, do idioma, da barra de topo, da gaveta de navegação e dos botões de gravação.

O aparelho envia as páginas com `Cache-Control: no-store`. O navegador sempre pede a versão atual, e o botão **Voltar** do navegador recarrega a página em vez de mostrar uma cópia antiga. Os dois arquivos comuns ficam guardados no navegador por até 7 dias, mas cada página os pede com uma marca da versão no endereço; depois de uma atualização de firmware, as páginas novas buscam os arquivos novos sozinhas.

::: nota
**O aparelho atende uma requisição por vez.** Cada aba aberta no **Painel de Controle** consulta o aparelho a cada 3 s, e o espelho do painel pede quadros sem pausa. Com várias abas abertas, as respostas ficam em fila. Feche as abas que você não está usando.
:::

### HTTP e HTTPS {#cap-13-https}

[release]{.img}

Na imagem release, com um certificado instalado, o aparelho atende só em HTTPS: na porta 443 quando a porta configurada é a padrão (80), ou na porta configurada, se ela foi mudada. Nessa situação o endereço `http://` deixa de responder, porque o aparelho nunca serve os dois ao mesmo tempo. As imagens alpha e Air atendem só em HTTP. A instalação do certificado está no [capítulo 9](#cap-09).

Em HTTPS o aparelho atende uma conexão segura por vez. Por isso as páginas enfileiram as próprias consultas, uma de cada vez, com 8 s de limite para cada uma, e carregam mais devagar que em HTTP. A opção **Conexões persistentes (keep-alive)**, na página **Rede**, reduz esse custo ([capítulo 9](#cap-09)).

### No SIMUT Air {#cap-13-air}

[air]{.img}

No Air, a interface web só existe enquanto o aparelho está em M0: logo depois de ligar, com o carregador conectado ou depois de `air stop` no console. Um despertar para medir ou transmitir não sobe o servidor web.

Toda consulta feita com uma sessão aberta reinicia a contagem de ociosidade do Air. Uma aba aberta no **Painel de Controle** consulta o aparelho a cada 3 s e, portanto, o mantém acordado. Feche a aba quando terminar. O ciclo do Air está no [capítulo 19](#cap-19).

## Como abrir a interface {#cap-13-abrir}

1. Conecte o computador ou o celular à mesma rede do aparelho.
2. No navegador, digite o endereço do aparelho, por exemplo `http://192.0.2.10`.
3. Se a porta do servidor web foi mudada, acrescente-a ao endereço, por exemplo `http://192.0.2.10:8080`.

Sem sessão aberta, o aparelho leva você para a página de entrada.

O nome também serve, se o mDNS estiver ativo: é o nome do aparelho seguido de `.local`, como `http://simut.local`. Onde descobrir o endereço está no [capítulo 4](#cap-04) e no [capítulo 9](#cap-09). No ponto de acesso de configuração, o endereço é `http://192.168.4.1`, e qualquer outro endereço digitado leva à página **Rede** ([capítulo 4](#cap-04)).

Um caminho que não existe responde `404: Not Found`.

## A página de entrada {#cap-13-entrada}

::: {.figura #fig-13-entrada tipo="web" arquivo="13-entrada.png" captura="rota /login; largura 1280; sem sessão; idioma Português escolhido no seletor do rodapé; campos vazios"}
A página de entrada. Note o seletor de idioma no rodapé e o link Trocar senha abaixo do botão Entrar.
:::

A página mostra, de cima para baixo:

- o logotipo SIMUT e a legenda "Sistema de Monitoramento Universal e Telemetria", sempre em português;
- **Usuário** (*Username*): o nome da conta;
- **Senha** (*Password*);
- **Mostrar senha** (*Show password*): mostra a senha digitada;
- **Entrar** (*Sign In*);
- uma linha de mensagens;
- o link **Trocar senha** (*Change password*);
- no rodapé, o seletor de idioma, com **English** e **Português** ([Idioma](#cap-13-idioma)).

Para entrar:

1. Digite o nome da conta em **Usuário**.
2. Digite a senha em **Senha**.
3. Toque em **Entrar**.

A página transforma a senha num resumo SHA-256 antes de enviá-la: a senha que você digitou não viaja pela rede. Cada tentativa leva também um código de uso único, que a página pede ao aparelho quando abre. Esse código vale 60 s.

Com a senha certa, o aparelho abre o **Painel de Controle**. Se a conta precisa trocar a senha, abre antes a [troca de senha obrigatória](#cap-13-troca-obrigatoria).

[release]{.img} A cada entrada, o painel do aparelho mostra `Web: <conta>` na barra de cima por 5 s.

Se a opção **Sons Web** estiver ligada ([capítulo 7](#cap-07)), o aparelho toca o som de confirmação a cada entrada e o som de erro a cada senha errada.

### Mensagens e bloqueio {#cap-13-bloqueio}

| Mensagem | Quando aparece | O que fazer |
|---|---|---|
| **Bloqueado por Ns. Tentativas excessivas.** (*Locked for Ns. Too many attempts.*) | Depois de cada senha errada, com contagem regressiva | Espere a contagem terminar e tente de novo |
| **Limite de sessões atingido. Tente mais tarde.** (*System is full. Try again later.*) | Três outras contas já têm sessão aberta | Veja [Sessões](#cap-13-sessoes) |
| **Usuário ou senha incorretos.** (*Invalid credentials.*) | O aparelho recusou a tentativa sem aplicar bloqueio, por exemplo com um código de uso único que já tinha sido usado | Tente de novo |
| **Erro de conexão.** (*Connection error.*) | A página não conseguiu falar com o aparelho | Confira a rede e o endereço |

Toda senha errada bloqueia novas tentativas por um tempo que dobra a cada erro seguido: 2 s no primeiro, depois 4, 8, 16, 32, 64, 128 e 256 s, e 300 s a partir do nono erro. Durante a contagem, o botão **Entrar** fica desativado; quando ela termina, a página prepara sozinha a tentativa seguinte. Se você reabrir a página no meio de um bloqueio, a contagem aparece com o tempo que falta.

O bloqueio vale para o endereço do computador, não para a conta. A contagem de erros só volta a zero quando uma entrada, ou uma troca de senha, dá certo a partir daquele endereço. O mesmo bloqueio vale para a conta e a senha usadas no endereço `/metrics` ([capítulo 24](#cap-24)).

Tentar de novo durante a contagem não adianta: o aparelho recusa sem conferir a senha e informa o tempo que falta.

::: {.figura #fig-13-entrada-bloqueio tipo="web" arquivo="13-entrada-bloqueio.png" captura="rota /login; largura 390; sem sessão; uma senha errada acabou de ser enviada; a mensagem Bloqueado por 2s. Tentativas excessivas. está na tela e o botão Entrar está desativado"}
O bloqueio depois de uma senha errada. O botão Entrar volta sozinho quando a contagem termina.
:::

::: atencao
**Página de entrada aberta há mais de 1 minuto.** O código de uso único vence 60 s depois que a página abre. Se você demorar mais que isso para tocar em **Entrar**, a tentativa é recusada com um bloqueio curto, mesmo com a senha certa. Espere a contagem e entre de novo, ou recarregue a página antes de entrar.
:::

### Trocar a senha pela página de entrada {#cap-13-trocar-senha}

O link **Trocar senha** troca a senha de qualquer conta que saiba a senha atual, sem abrir sessão.

1. Toque em **Trocar senha**.
2. Preencha **Usuário**, **Senha Atual** (*Current Password*) e **Nova Senha** (*New Password*).
3. Acompanhe a barra de força e a lista de requisitos: **Mínimo 8 caracteres** (*At least 8 characters*), **Letra** (*Letter*), **Dígito** (*Digit*) e **Símbolo** (*Symbol*). Cada requisito atendido fica verde.
4. Repita a senha nova em **Repetir Nova Senha** (*Repeat New Password*).
5. Toque em **Salvar Nova Senha** (*Save New Password*).

O botão **Salvar Nova Senha** só fica ativo quando os quatro requisitos estão atendidos e as duas senhas novas são iguais. Para a página:

- letra é uma letra de A a Z, maiúscula ou minúscula, sem acento;
- símbolo é qualquer caractere que não seja letra de A a Z nem dígito, então um espaço ou uma letra acentuada contam como símbolo.

A marca **Mostrar senha** desta tela mostra os três campos de uma vez. O link **← Voltar ao Login** volta ao formulário de entrada.

| Resultado | O que a página mostra |
|---|---|
| Senhas novas diferentes entre si | **As senhas não coincidem.** |
| Senha nova igual à atual | **A nova senha deve ser diferente da atual.** |
| Senha atual errada | Volta ao formulário de entrada e mostra o bloqueio, porque conta como uma tentativa errada |
| Sucesso | **Senha trocada. Faça login.** Depois de 1,5 s, volta ao formulário de entrada com o nome da conta preenchido |

Em HTTP, a página envia só os resumos SHA-256 das senhas, e o aparelho não tem como conferir a força da senha nova: valem apenas as regras da página. Em HTTPS, a página envia as senhas dentro da conexão cifrada, e o aparelho também exige pelo menos 8 caracteres com uma letra e um dígito.

Trocar a senha por aqui também cumpre a troca obrigatória da conta. Nenhuma sessão é aberta: entre em seguida com a senha nova.

::: {.figura #fig-13-trocar-senha tipo="web" arquivo="13-trocar-senha.png" captura="rota /login, depois de tocar em Trocar senha; largura 390; sem sessão; Nova Senha preenchida com 8 caracteres entre letras e dígitos, sem símbolo; Repetir Nova Senha vazia"}
O formulário de troca de senha. Três requisitos estão verdes e Símbolo ainda não; o botão Salvar Nova Senha continua desativado.
:::

### O aviso de sessão segura antiga {#cap-13-sessao-segura}

Às vezes a entrada dá certo e o navegador volta para a página de entrada logo em seguida. Quando isso acontece em menos de 30 s, a página mostra uma faixa amarela: **Login aceito, mas o navegador guarda uma sessão segura de uma visita HTTPS anterior. Abra uma janela anônima ou limpe os cookies deste site e entre de novo.**

A causa é um cookie de sessão marcado como seguro, deixado por uma visita anterior em HTTPS ao mesmo endereço. Uma página `http://` não pode enviar nem substituir esse cookie. Isso acontece, por exemplo, quando o certificado foi removido e o aparelho voltou a atender em HTTP.

Para resolver, abra uma janela anônima ou apague os cookies do endereço do aparelho no navegador, e entre de novo.

::: {.figura #fig-13-aviso-sessao-segura tipo="web" arquivo="13-aviso-sessao-segura.png" captura="rota /login; largura 1280; navegador com cookie seguro de uma visita HTTPS anterior ao mesmo endereço, aparelho agora em HTTP; logo depois de uma entrada com senha certa"}
A faixa amarela que aparece quando o navegador não consegue guardar a sessão nova.
:::

## A troca de senha obrigatória {#cap-13-troca-obrigatoria}

Algumas contas precisam trocar a senha antes de usar a interface:

- as contas de fábrica, no primeiro acesso;
- as contas criadas ou resetadas por um administrador na página **Usuários**, que recebem uma senha temporária mostrada uma única vez ([capítulo 8](#cap-08));
- a conta `admin`, depois do comando `system admin reset` no console ([capítulo 14](#cap-14)).

Enquanto a troca estiver pendente, toda página interna leva a esta. É a própria página de entrada, com a marca SIMUT no alto, mostrando só o formulário de senha nova: **Bem-vindo!** (*Welcome!*) e o texto **Defina uma nova senha forte para acessar o sistema.**

1. Digite a senha nova em **Nova Senha** (*New Password*).
2. Acompanhe a barra de força e as quatro exigências embaixo dela — **Mínimo 8 caracteres**, **Letra**, **Dígito** e **Símbolo** —, que se marcam uma a uma conforme a senha as atende.
3. Repita a senha em **Repetir Nova Senha** (*Repeat New Password*).
4. Toque em **Salvar e Entrar** (*Save & Login*).

A marca **Mostrar senha** (*Show password*) mostra os campos. O botão **Salvar e Entrar** só fica ativo com as quatro exigências atendidas — pelo menos 8 caracteres, uma letra de A a Z, um dígito e um símbolo — e as duas senhas iguais. A barra fica vermelha, depois amarela, e verde quando a senha atende às quatro.

A página não pede a senha atual: a sessão já provou quem você é. Com a senha salva, o aparelho abre o **Painel de Controle**, na mesma sessão.

Se o aparelho recusar a senha, a página mostra `Error updating password.` embaixo do botão, sempre em inglês. Há duas causas:

- em HTTPS, a senha tem menos de 8 caracteres, ou falta letra ou dígito;
- alguém tocou no painel do aparelho nos últimos 5 s. Espere e toque em **Salvar e Entrar** de novo.

O seletor de idioma é o mesmo da página de entrada. Não há como seguir para outra página antes de trocar a senha. Para desistir, feche o navegador ou abra o endereço `/logout`.

::: {.figura #fig-13-troca-obrigatoria tipo="web" arquivo="13-troca-obrigatoria.png" captura="rota /force_chpass; largura 390; sessão de uma conta recém-criada com senha temporária; Nova Senha preenchida com uma senha que atende às quatro exigências e Repetir Nova Senha igual"}
A troca de senha obrigatória. Com as duas senhas iguais e as quatro regras atendidas, a barra fica verde e o botão Salvar e Entrar fica ativo.
:::

## Sessões {#cap-13-sessoes}

Ao entrar, o aparelho abre uma sessão e grava no navegador o cookie `SIMUTSESS`. O cookie não é legível por scripts da página (`HttpOnly`), só viaja em pedidos feitos pela própria página do aparelho (`SameSite=Strict`) e, em HTTPS, só viaja por conexão cifrada (`Secure`).

As regras das sessões:

- **Três sessões ao todo.** O aparelho mantém no máximo três sessões abertas. Navegadores, scripts e o gestor de frota, que usa o mesmo tipo de sessão pelo cabeçalho `Authorization: Bearer` ([capítulo 27](#cap-27)), dividem essas três vagas. Uma quarta conta recebe **Limite de sessões atingido. Tente mais tarde.**
- **Uma sessão por conta.** Entrar de novo com a mesma conta, em qualquer navegador, substitui a sessão anterior dessa conta. A aba antiga perde o acesso: a tabela de sensores do **Painel de Controle** passa a mostrar `Connection Error`, e a próxima página aberta nela leva à entrada.
- **15 min de ociosidade.** Uma sessão sem nenhuma consulta por 15 min expira. Toda consulta renova a sessão, e o **Painel de Controle** consulta o aparelho a cada 3 s: enquanto ele estiver aberto, a sessão não expira e ocupa uma vaga.
- **Um reinício encerra todas.** As sessões ficam só na memória do aparelho. Depois de **Salvar e reiniciar**, a página se recarrega sozinha cerca de 12 s depois e mostra a entrada.

Para sair, abra a gaveta de navegação e toque em **Sair** (*Logout*). O aparelho encerra a sessão, o navegador apaga o cookie e a página de entrada volta.

Alterações preparadas e ainda não gravadas ficam guardadas na aba do navegador enquanto você passa de uma página a outra ([capítulo 5](#cap-05)). **Sair** as descarta, e uma entrada nova também.

O log de eventos registra as sessões ([capítulo 16](#cap-16)):

| Código | Quando |
|---|---|
| 300 | Entrada aceita (`Login OK: <conta>`) e saída (`Logout: <conta>`) |
| 301 | Tentativa de entrada recusada |
| 302 | Uma sessão pediu uma página sem ter a permissão |
| 304 | Sessão expirada por ociosidade |

## A barra de topo {#cap-13-barra}

Toda página interna começa com a mesma barra. Da esquerda para a direita:

- **Menu:** o botão com três traços abre a [gaveta de navegação](#cap-13-gaveta). Ela é a única navegação da interface, em qualquer largura de tela.
- **Marca e versão:** `SIMUT` seguido da versão do firmware, por exemplo `SIMUT 2.7.1`. Até o aparelho responder, aparece `SIMUT IoT`.
- **Gravação:** um selo e os botões **Testar** (*Test*), **Aplicar agora** (*Apply now*) e **Salvar e reiniciar** (*Save & restart*). Eles só aparecem quando há alterações preparadas e ainda não gravadas. O selo diz **Alterações não salvas** (*Unsaved changes*), **Não exige reinício** (*No restart needed*) ou **Reinicia por:** seguido do motivo. O que cada botão faz está no [capítulo 5](#cap-05).
- **Tema:** o botão redondo alterna entre o tema claro e o escuro. Ele mostra o sol quando a página está escura e a lua quando está clara, isto é, o tema para onde ele leva ([Tema claro e escuro](#cap-13-tema)).
- **Estado:** um ponto e o endereço IP do aparelho. O ponto fica cinza enquanto a página carrega, verde quando o aparelho respondeu à consulta da sessão e vermelho quando não respondeu. O endereço só aparece para contas com a permissão **Painel**; para as outras, fica `--`.

Abaixo da barra, uma trilha mostra onde você está: `SIMUT › Painel de Controle`, por exemplo.

Os avisos da interface aparecem logo abaixo da barra, sem cobri-la, e somem sozinhos depois de alguns segundos.

::: {.figura #fig-13-barra-topo tipo="web" arquivo="13-barra-topo.png" captura="rota /alarms; largura 1280; sessão admin; um limite de alarme alterado e ainda não gravado; recorte da barra de topo e da trilha"}
A barra de topo com uma alteração preparada: o selo, os botões de gravação, o botão de tema e o endereço do aparelho.
:::

## A gaveta de navegação {#cap-13-gaveta}

Toque no botão de menu para abrir a gaveta. Ela desliza da esquerda, com 280 pixels de largura, e escurece o resto da página. Para fechá-la, toque no **X**, toque fora dela ou pressione **Esc**. Com a gaveta aberta, a página atrás não rola.

De cima para baixo, a gaveta mostra:

- os itens de navegação, nesta ordem: **Painel de Controle** (*Dashboard*), **Histórico e Logs** (*History & Logs*), **Alarmes e Sons** (*Alarms & Sounds*), **Telemetria** (*Telemetry*), **Configurações** (*System Config*), **Rede** (*Network*), **Usuários** (*Users*) e **Arquivos** (*Files*);
- a versão do firmware, por exemplo `SIMUT 2.7.1`;
- **Licença** (*License*);
- uma saudação com o nome da conta, o seletor de idioma e **Sair**.

A página atual aparece destacada na cor de acento.

A saudação é **Olá** (*Hello*) seguida do nome da conta. Quando o relógio do aparelho está acertado, ela muda com a hora do dia, contada no fuso do seu computador: **Bom dia** das 5h às 11h59, **Boa tarde** das 12h às 17h59 e **Boa noite** no resto do dia.

::: {.figura #fig-13-gaveta tipo="web" arquivo="13-gaveta.png" captura="rota /; largura 1280; sessão admin; gaveta aberta; idioma PT no seletor; relógio do aparelho acertado"}
A gaveta aberta para a conta admin, com os oito itens, a versão, Licença, a saudação, o seletor de idioma e Sair.
:::

### O que cada permissão mostra {#cap-13-permissoes}

A gaveta esconde os itens que a conta não pode abrir. O aparelho confere de novo em cada página e em cada consulta, então esconder é só conforto: uma conta sem a permissão recebe recusa mesmo digitando o endereço.

| Permissão | Itens que ela mostra na gaveta | Observação |
|---|---|---|
| **Painel** [PERM_DASHBOARD]{.perm} | **Painel de Controle** e **Licença** | É a página aberta depois da entrada |
| **Histórico** [PERM_HISTORY]{.perm} | **Histórico e Logs** | Gráficos, calendário e exportação |
| **Logs** [PERM_LOGS]{.perm} | **Histórico e Logs** | Eventos do sistema |
| **Sistema** [PERM_SYS_CONFIG]{.perm} | **Alarmes e Sons**, **Telemetria** e **Configurações** | No **Painel de Controle**, também libera a captura, o espelho e o tema do painel |
| **Rede** [PERM_NET_CONFIG]{.perm} | **Rede** | |
| **Leitura** [PERM_FILE_READ]{.perm} | **Arquivos** | A página **Arquivos** só abre com esta |
| **Upload** [PERM_FILE_UPLOAD]{.perm} | **Arquivos** | Sem **Leitura**, o item aparece e a página recusa |
| **Excluir** [PERM_FILE_DELETE]{.perm} | **Arquivos** | Sem **Leitura**, o item aparece e a página recusa |
| **Usuários** [PERM_USER_MGR]{.perm} | **Usuários** | |
| **Calibração** [PERM_CALIB]{.perm} | Nenhum item próprio | A calibração fica no editor de slot de **Configurações**, que exige também **Sistema** ([capítulo 6](#cap-06)) |
| **Limites (painel)** [PERM_ALARM_LIMITS]{.perm}, **Bloqueio (painel)** [PERM_ALARM_BLOCK]{.perm}, **Manut. (painel)** [PERM_MAINT]{.perm} | Nenhum | Valem só no painel ([capítulo 8](#cap-08)) |

A página **Histórico e Logs** abre com **Histórico** ou com **Logs**, mas cada metade consulta o aparelho com a própria permissão: a de gráficos exige **Histórico**, e a de eventos exige **Logs** ([capítulo 15](#cap-15), [capítulo 16](#cap-16)).

::: atencao
**Uma conta sem Painel não tem para onde ir depois de entrar.** A entrada sempre leva ao **Painel de Controle**. Sem a permissão **Painel**, o aparelho responde com uma página em branco que diz apenas `Access Denied`, sem barra e sem gaveta. Digite o endereço de uma página que a conta pode abrir, como `http://192.0.2.10/network`, ou peça a um administrador para incluir **Painel** na conta. Uma conta criada no painel só tem permissões de painel e não abre nenhuma página web.
:::

::: {.figura #fig-13-gaveta-restrita tipo="web" arquivo="13-gaveta-restrita.png" captura="rota /; largura 390; sessão de um operador com as permissões Painel e Histórico; gaveta aberta"}
A gaveta de uma conta com Painel e Histórico: só Painel de Controle, Histórico e Logs e Licença aparecem.
:::

::: {.figura #fig-13-acesso-negado tipo="web" arquivo="13-acesso-negado.png" captura="rota /; largura 1280; sessão de um operador sem a permissão Painel, logo depois de entrar"}
A resposta Access Denied, que uma conta sem a permissão Painel recebe ao entrar.
:::

## O Painel de Controle {#cap-13-painel-de-controle}

O **Painel de Controle** (rota `/`) é a primeira página depois da entrada e exige a permissão **Painel**. Ele consulta o aparelho a cada 3 s e mostra:

- uma faixa de cartões de estado;
- uma faixa de cartões de métricas;
- a tabela de sensores;
- [release]{.img} o bloco **Tela** (*Display Capture*), com o espelho do painel ([O espelho do painel](#cap-13-espelho)).

Em telas com mais de 900 pixels de largura, o bloco **Tela** ocupa uma coluna à direita, de 360 pixels. Em telas mais estreitas, ele desce para baixo da tabela. As imagens alpha e Air não têm painel com toque nem o bloco **Tela**, e a página fica numa coluna só.

::: {.figura #fig-13-painel-de-controle tipo="web" arquivo="13-painel-de-controle.png" captura="rota /; largura 1280; sessão admin; imagem release; tema escuro; 2 sensores ativos (um DS18B20 e um BME280); relógio acertado; telemetria ligada com alguns registros pendentes; bloco Tela antes de qualquer captura"}
O Painel de Controle na imagem release: cartões de estado, cartões de métricas, tabela de sensores e, à direita, o bloco Tela.
:::

::: {.figura #fig-13-painel-de-controle-alpha tipo="web" arquivo="13-painel-de-controle-alpha.png" captura="rota /; largura 1280; sessão admin; imagem alpha; 1 sensor DHT22 ativo; telemetria desligada"}
O Painel de Controle numa imagem sem painel com toque: uma coluna só, sem o bloco Tela.
:::

### Cartões de estado {#cap-13-cartoes-estado}

| Cartão | Valor | Linha de baixo |
|---|---|---|
| **Data e Hora** (*System Date & Time*) | Data e hora do relógio do aparelho, no formato `dd/mm/aaaa hh:mm:ss` | **Sincronizado**, em verde |
| **Tempo Ativo** (*System Uptime*) | Tempo desde o último reinício, como `2d 05h 13m 09s` | — |
| **Sinal Wi-Fi** (*WiFi Signal*) | Intensidade do sinal agora, em dBm | `min <pior> / <melhor> max`: o pior e o melhor sinal vistos desde o último reinício |
| **Registros Pendentes** (*Pending Records*) | Registros do histórico que ainda não foram enviados pela telemetria, como `12 pkts` | O estado da telemetria (tabela abaixo) |
| **Uso de RAM** (*RAM Usage*) | Porcentagem da memória em uso, com uma barra | `<livre> KB Livre / <total> KB Total` |
| **Armazenamento** (*Flash Storage*) | Porcentagem do sistema de arquivos em uso, com uma barra | `<livre> KB Livre / <total> KB Total` |

As barras de **Uso de RAM** e **Armazenamento** ficam amarelas acima de 70 % e vermelhas acima de 85 %. O espaço do sistema de arquivos é relido no máximo a cada 10 s.

A linha de baixo de **Registros Pendentes**:

| Linha | Quando |
|---|---|
| **Telemetria desligada** (*Telemetry disabled*) | O lote mínimo da telemetria é 0 ([capítulo 21](#cap-21)) |
| **Sincronizado** (*Synchronized*) | Telemetria ligada e nada pendente |
| **Aguardando sinc.** (*Waiting telemetry sync*) | Telemetria ligada e registros esperando envio |
| **Telemetria indisponível** (*Telemetry unavailable*) | O aparelho não informou a contagem; o valor mostra **Avaliando...** (*Evaluating...*) |

**Data e Hora** mostra o relógio do aparelho convertido para o fuso horário do seu computador, não para o fuso configurado no aparelho. Se os dois fusos forem diferentes, a hora da página difere da hora do painel. O fuso do aparelho está no [capítulo 10](#cap-10).

::: atencao
**Sincronizado aparece mesmo antes do primeiro acerto do relógio.** Na v2.7.1, o aparelho sempre informa o relógio como sincronizado, e a página nunca mostra **Falha NTP**. Enquanto o relógio do sistema não foi acertado, nem por NTP nem manualmente, **Data e Hora** mostra uma data de 1969 ou 1970 ao lado de **Sincronizado**. O histórico continua sendo gravado com o relógio provisório do aparelho. Para acertar o relógio, veja o [capítulo 10](#cap-10).
:::

### Cartões de métricas {#cap-13-metricas}

Os números desta faixa são contados desde o último reinício e voltam a zero a cada reinício.

| Cartão | Valor | Linha de baixo |
|---|---|---|
| **Maior Bloco** (*Largest Block*) | O maior bloco contínuo de memória livre, em KB | `min`: o menor valor visto |
| **Heap Mín.** (*Heap Min Seen*) | A menor quantidade de memória livre já vista, em KB | **menor livre** |
| **Reconexões Rede** (*Net Reconnects*) | `<wifi> / <mqtt>`: quantas vezes o aparelho obteve IP no Wi-Fi e quantas vezes se conectou ao broker MQTT | **wifi / mqtt** |
| **Telemetria Env.** (*Telemetry Sent*) | Envios de telemetria aceitos, como `1.2k ok` | `<n> falhas · <n> retry`: envios que falharam e novas tentativas agendadas |
| **Dados Telemetria** (*Telemetry Data*) | Soma dos tamanhos dos envios, em KB | `últ. <n> ms`: o tempo do último envio |
| **Leituras Sensor** (*Sensor Reads*) | Leituras de sensor bem-sucedidas, somando todos os sensores | `<n> erro`: leituras que falharam |
| **Saves Config** (*Config Saves*) | Pedidos de gravação da configuração, inclusive os que não mudaram nada | **para flash** |

Contagens a partir de mil aparecem abreviadas: `1.2k`, `3.45M`. A primeira conexão ao Wi-Fi já conta em **Reconexões Rede**. **Maior Bloco** menor que **Heap Mín.** indica memória fragmentada; os dois ajudam a diagnosticar um aparelho que reinicia sozinho ([capítulo 30](#cap-30)).

### Tabela de sensores {#cap-13-tabela-sensores}

A tabela lista os sensores ativos, um por linha:

| Coluna | O que mostra |
|---|---|
| **Status** | Ponto verde, ou vermelho quando o sensor está em erro |
| **Slot** | O número do slot, de 0 a 15 |
| **Tipo** (*Type*) | O tipo do sensor e, embaixo, quantos pinos ele usa e a função de cada um, como `1p · 1-Wire` ou `2p · SDA,SCL` |
| **ID** (*ID Sensor*) | O identificador de hardware do slot |
| **Leitura** (*Reading*) | O valor atual, com as grandezas do sensor separadas por barras |
| **Nome** (*Sensor Name*) | O nome dado ao slot |

A leitura segue o formato do navegador, com ponto decimal: a temperatura com duas casas e `ºC`, a umidade com uma casa e `%`, e a pressão com uma casa e `hPa`. Um BME280, por exemplo, aparece como `23.45 ºC | 55.2% | 1013.2 hPa`. Um sensor em erro mostra `Error`; um sensor que ainda não tem leitura mostra `--`.

Enquanto carrega, a tabela mostra **Carregando sensores...** (*Loading sensors...*). Sem sensores ativos, mostra `No active sensors.`; os sensores são criados na página **Configurações** ([capítulo 6](#cap-06)). Quando a consulta falha, a tabela mostra `Connection Error` em vermelho: o aparelho está fora do ar, reiniciando, ou a sessão acabou. Recarregue a página.

::: {.figura #fig-13-painel-de-controle-celular tipo="web" arquivo="13-painel-de-controle-celular.png" captura="rota /; largura 390; sessão admin; imagem release; 2 sensores ativos; página rolada até a tabela de sensores"}
O Painel de Controle no celular: os cartões em duas colunas e a tabela de sensores, que rola para o lado.
:::

## O espelho do painel {#cap-13-espelho}

[release]{.img}

Na imagem release, o bloco **Tela** do **Painel de Controle** mostra o painel do aparelho no navegador. Ele tem uma área de imagem, uma linha de estado e três controles:

- a lista de temas do painel;
- **Capturar** (*Capture screen*): uma foto da tela;
- **Ao vivo** (*Live view*): o espelho contínuo, que também aceita cliques como toques no painel.

Antes do primeiro uso, a área de imagem mostra **Clique para capturar** (*Click Capture to view screen*).

Os três controles exigem a permissão **Sistema**. Uma conta só com **Painel** vê o bloco, mas o aparelho recusa a captura, o espelho e a troca de tema.

### Ver o painel ao vivo {#cap-13-ao-vivo}

1. Toque em **Ao vivo**. O botão passa a dizer **Parar** (*Stop*), e a linha de estado mostra **Clique no espelho para tocar no painel** (*Click the mirror to touch the panel*).
2. Acompanhe o painel na área de imagem. A cada quadro, a linha de estado mostra a taxa, o tamanho e o tempo do quadro, no formato `<quadros> fps · <tamanho> kB · <tempo> s`.
3. Toque em **Parar** para encerrar.

A página pede um quadro por vez e só pede o seguinte depois de desenhar o anterior. Cada quadro chega em 30 faixas de 8 linhas, comprimidas por paleta quando isso compensa. Na bancada, um quadro levou 213 ms dentro do aparelho, com o **Painel de Controle** na tela (medido em 19/09/2026, v2.5.0-beta). O tempo que a página mostra inclui também a rede.

A imagem é ampliada sem suavização, para mostrar as cores que o painel de 320 × 240 pixels de fato mostra.

Com a aba escondida, o espelho não pede quadros; ele volta quando você retorna à aba.

::: {.figura #fig-13-espelho-ao-vivo tipo="web" arquivo="13-espelho-ao-vivo.png" captura="rota /; largura 1280; sessão admin; imagem release; Ao vivo ligado; o painel está na tela inicial com 2 sensores; recorte do bloco Tela com a linha de estado mostrando fps, kB e s"}
O espelho ao vivo. O botão mostra Parar e a linha de estado dá a taxa, o tamanho e o tempo de cada quadro.
:::

### Tocar no painel pelo espelho {#cap-13-tocar-espelho}

Com o espelho ao vivo, clique na imagem (ou toque nela, no celular) no ponto que você tocaria no painel. A página converte o ponto para as coordenadas do painel, de 0 a 319 na horizontal e de 0 a 239 na vertical, e o aparelho trata o clique como um toque de dedo.

- Um círculo vermelho marca o ponto na hora. O quadro seguinte o apaga.
- Depois de um clique, a página espera 600 ms antes de pedir o próximo quadro, para o painel terminar de redesenhar.
- Se o aparelho recusar o toque, a linha de estado mostra **Toque recusado** (*Touch refused*) com o código da resposta, por exemplo `Toque recusado (403)` para uma conta sem **Sistema**.
- Um clique conta como atividade no painel: reinicia a contagem de 30 s que devolve o painel à tela inicial.

O espelho não pula a proteção do painel. Para abrir as configurações do painel, você escolhe a conta e digita o PIN no teclado do painel pelo espelho, como faria com o dedo. O que se pode fazer ali depende das permissões da conta que o PIN identificou, não da sua sessão web ([capítulo 8](#cap-08), [capítulo 11](#cap-11)).

::: atencao
**Um toque reserva o aparelho por 5 s.** Todo toque no painel, feito com o dedo ou pelo espelho, faz o aparelho adiar trabalhos pesados por 5 s. Nesse intervalo ele recusa com a mensagem `Display in use. Retry shortly.` a captura, **Salvar e reiniciar**, o envio e a exclusão de arquivos, o backup, a restauração, a atualização, o carregamento de gráficos e de eventos e a troca de senha obrigatória. Espere 5 s depois do último toque e repita. O espelho em si continua: ele é liberado depois de um toque feito por ele mesmo.
:::

### Capturar a tela {#cap-13-captura}

1. Toque em **Capturar**. Se o espelho estiver ao vivo, ele para, porque os dois não podem ler o painel ao mesmo tempo.
2. Espere a área de imagem sair de **Capturando...** (*Capturing Display...*).

A captura é uma imagem BMP de 320 × 240 pixels e 230.454 bytes, em que cada linha da tela é lida três vezes e vale a leitura que se repete. Por isso ela é mais lenta que um quadro do espelho: 1,69 s na bancada (medido em 18/09/2026, v2.4.10-beta). Use a captura quando precisar de uma imagem fiel para registro. Para guardá-la, use o menu do navegador sobre a imagem.

Se a captura falhar, a área de imagem mostra **Falha na leitura** (*Read Failed*) por 2 s. As causas são um toque no painel nos últimos 5 s, inclusive um clique no espelho, outra captura em andamento ou uma conta sem **Sistema**.

::: {.figura #fig-13-captura tipo="web" arquivo="13-captura.png" captura="rota /; largura 1280; sessão admin; imagem release; depois de tocar em Capturar, com o painel na tela inicial; recorte do bloco Tela"}
Uma captura da tela do painel, exibida no bloco Tela.
:::

### Trocar o tema do painel {#cap-13-tema-painel}

A lista do bloco **Tela** traz os temas do painel: os de fábrica e os instalados em `/themes`. Escolha um tema na lista, e o painel troca de tema na hora e grava a escolha, sem reiniciar. A cada 3 s, a lista volta a mostrar o tema que o painel está usando. Por isso, numa conta sem **Sistema**, a escolha volta atrás sozinha. Os temas do painel estão no [capítulo 11](#cap-11).

Esta lista muda o painel do aparelho. O tema da interface web é outro, e fica no botão da barra de topo ([Tema claro e escuro](#cap-13-tema)).

### Quando o espelho espera ou para {#cap-13-espelho-para}

| Situação | O que a linha de estado mostra | O que acontece |
|---|---|---|
| Alguém tocou no painel com o dedo nos últimos 5 s | **Display em uso. Tente novamente em alguns segundos.** | A página tenta de novo a cada 1,5 s e volta sozinha |
| Outra captura em andamento, de outra aba ou de outra pessoa | **Display em uso. Tente novamente em alguns segundos.** | A página tenta de novo a cada 1,5 s |
| O aparelho recusou o quadro por outro motivo, ou a conexão caiu | **Falha na leitura** | O espelho para; toque em **Ao vivo** para recomeçar |

Quem está diante do painel tem prioridade. Mesmo no meio de um quadro, o aparelho deixa passar o toque de um dedo; a partir desse toque, o espelho espera os 5 s.

::: {.figura #fig-13-espelho-ocupado tipo="web" arquivo="13-espelho-ocupado.png" captura="rota /; largura 1280; sessão admin; imagem release; Ao vivo ligado; uma pessoa acabou de tocar no painel com o dedo; recorte do bloco Tela com a mensagem Display em uso na linha de estado"}
O espelho esperando: alguém usa o painel com o dedo, e a página tenta de novo sozinha.
:::

## Tema claro e escuro {#cap-13-tema}

A interface tem um tema escuro e um tema claro. Sem escolha guardada, ela segue a preferência do sistema operacional: claro se o sistema pede claro, escuro nos outros casos.

Para trocar, toque no botão redondo da barra de topo. O botão mostra o tema para onde ele leva: o sol quando a página está escura, a lua quando está clara.

A escolha fica guardada no navegador, não no aparelho nem na conta. Ela vale para todas as páginas daquele navegador, inclusive a de entrada e a de troca de senha obrigatória, que não têm o botão. Em outro navegador ou em outro computador, a escolha é outra.

::: {.figura #fig-13-tema-claro tipo="web" arquivo="13-tema-claro.png" captura="rota /; largura 1280; sessão admin; imagem release; tema claro escolhido no botão da barra; 2 sensores ativos"}
O Painel de Controle no tema claro. Compare com a figura do tema escuro no início da seção do Painel de Controle.
:::

## Idioma {#cap-13-idioma}

A interface traz o inglês embutido e pode mostrar mais um idioma: o do pacote de idioma que o aparelho carregou.

O aparelho carrega um pacote no boot, e só quando o idioma do aparelho não é o inglês. O pacote é um arquivo `language_<código>.lng` na pasta `/lang`, como `language_pt-BR.lng`. Se houver mais de um, vale o primeiro em ordem alfabética. Depois de trocar o idioma do aparelho ([capítulo 11](#cap-11), [capítulo 14](#cap-14)) ou o arquivo do pacote ([capítulo 17](#cap-17)), reinicie o aparelho para a interface web oferecê-lo.

Há dois seletores:

- **Na gaveta de navegação:** `EN` e o código curto do pacote carregado, como `PT` ou `ES`. Sem pacote carregado, só `EN` aparece.
- **Na página de entrada:** **English** e **Português**, sempre com esses nomes. **Português** usa o pacote carregado, qualquer que seja o idioma dele; sem pacote, os textos continuam em inglês.

Os dois seletores gravam a mesma escolha no navegador. Ela vale para todas as páginas daquele navegador e muda os textos na hora, sem recarregar. Na primeira visita de um navegador sem escolha guardada, a interface usa o pacote do aparelho, se houver um.

O idioma do navegador muda os textos da interface web; nos nomes dos eventos, ele escolhe entre o inglês e o idioma do pacote do aparelho ([capítulo 16](#cap-16)). O idioma do painel e do console é o do aparelho.

Alguns textos aparecem sempre em inglês, qualquer que seja a escolha: por exemplo `No active sensors.`, `Connection Error`, `Access Denied`, `Error updating password.` e a unidade `pkts` dos registros pendentes.

## No celular {#cap-13-celular}

A interface se ajusta a telas de até 640 pixels de largura:

- A barra de topo esconde a versão para o endereço do aparelho caber inteiro; a versão continua na gaveta.
- Os botões e os campos têm pelo menos 44 pixels de altura, para o dedo.
- A gaveta ocupa no máximo 84 % da largura da tela.
- Os botões de gravação saem da barra de topo e viram uma barra fixa no rodapé, com os botões lado a lado. Quando os três aparecem, **Salvar e reiniciar** fica com o dobro da largura. O selo de estado não aparece.
- Ao digitar num campo, a página rola para mantê-lo acima do teclado da tela e da barra do rodapé.
- No **Painel de Controle**, os cartões ficam em duas colunas fixas, e a tabela de sensores rola para o lado.

::: {.figura #fig-13-celular-pendente tipo="web" arquivo="13-celular-pendente.png" captura="rota /alarms; largura 390; sessão admin; um limite de alarme alterado e ainda não gravado; a barra fixa do rodapé visível"}
No celular, os botões de gravação ficam numa barra fixa no rodapé.
:::

## A página Licença {#cap-13-licenca}

A página **Licença** (rota `/license`) exige a permissão **Painel** e fica no pé da gaveta. Ela tem quatro blocos:

- **Licença de Software** (*Software License*), com a frase **Software livre. Abaixo, o que isso significa na prática.**;
- **Em resumo** (*In short*): um resumo da licença em linguagem simples, com o aviso de que ele não substitui o texto da licença;
- **Licença MIT** (*MIT License*): o texto da licença, em inglês, porque é a versão original e a que tem valor legal;
- **Avisos de Terceiros** (*Third-Party Notices*): as 17 bibliotecas e componentes de terceiros usados no firmware, com autor, licença e endereço de cada um.

Os avisos de terceiros incluem o BTstack, a pilha Bluetooth das imagens alpha e Air, cuja licença é gratuita para uso não comercial e exige uma licença separada da BlueKitchen GmbH para uso comercial. Confira a lista antes de distribuir o aparelho.

::: {.figura #fig-13-licenca tipo="web" arquivo="13-licenca.png" captura="rota /license; largura 1280; sessão admin; página rolada até o início dos Avisos de Terceiros"}
A página Licença, com o resumo em linguagem simples e o texto original da licença MIT.
:::

## Onde cada página está documentada {#cap-13-mapa}

| Página | Rota | Permissão para abrir | Onde está |
|---|---|---|---|
| Entrada | `/login` | Nenhuma | Este capítulo |
| Troca de senha obrigatória | `/force_chpass` | Sessão com troca pendente | Este capítulo |
| Painel de Controle | `/` | **Painel** | Este capítulo |
| Histórico e Logs | `/history` | **Histórico** ou **Logs** | Gráficos, calendário e exportação: [capítulo 15](#cap-15). Eventos: [capítulo 16](#cap-16) |
| Alarmes e Sons | `/alarms` | **Sistema** | [Capítulo 7](#cap-07) |
| Telemetria | `/telemetry` | **Sistema** | [Capítulo 21](#cap-21). Linha de alarmes: [capítulo 22](#cap-22). Home Assistant: [capítulo 23](#cap-23) |
| Configurações | `/config` | **Sistema** | A página e os três botões: [capítulo 5](#cap-05). Sensores e calibração: [capítulo 6](#cap-06). Política de PIN: [capítulo 8](#cap-08). Data e hora: [capítulo 10](#cap-10). Syslog: [capítulo 25](#cap-25) |
| Rede | `/network` | **Rede** | [Capítulo 9](#cap-09) |
| Usuários | `/users` | **Usuários** | [Capítulo 8](#cap-08) |
| Arquivos | `/files` | **Leitura** | Arquivos, backup, restauração e atualização: [capítulo 17](#cap-17) |
| Licença | `/license` | **Painel** | Este capítulo |

As rotas que as páginas usam para consultar e gravar dados estão no [capítulo 26](#cap-26). O que cada permissão libera está no [capítulo 8](#cap-08).
