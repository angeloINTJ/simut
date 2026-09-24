# Contas, permissões e PIN {#cap-08}

Este capítulo explica quem pode fazer o quê no aparelho: as contas, as 13 permissões, a página **Usuários**, as senhas da interface web e o PIN com que cada pessoa se identifica no painel. É para quem administra o aparelho, e é a referência de permissões do manual inteiro.

## O modelo de contas {#cap-08-modelo}

O aparelho guarda até 32 contas. Cada conta tem:

- um **nome**, de 1 a 15 caracteres, único sem diferenciar maiúsculas de minúsculas;
- uma **senha**, para a interface web;
- um conjunto de **permissões**, uma por ação ([As 13 permissões](#cap-08-permissoes));
- [release]{.img} um **PIN** opcional, para se identificar no painel ([Identidade no painel](#cap-08-identidade)).

A mesma conta serve à web e ao painel. Uma pessoa que usa os dois tem uma conta só, com uma senha para a web e um PIN para o painel.

### As contas de fábrica {#cap-08-fabrica}

| Conta | Permissões | Senha inicial | PIN inicial |
|---|---|---|---|
| `admin` | Todas, inclusive as exclusivas do administrador | 8 caracteres aleatórios, mostrados uma vez no console serial USB na primeira partida ([capítulo 4](#cap-04)) | `1234`, com troca obrigatória |
| `viewer` | **Painel** e **Histórico** | Não divulgada: gere uma com **Reset** antes de usar a conta | Nenhum |

As duas contas precisam trocar a senha no primeiro acesso pela web ([capítulo 13](#cap-13-troca-obrigatoria)). Para usar a conta `viewer`, toque em **Reset** na página **Usuários** e entregue a senha de uso único que aparece; se não for usá-la, exclua a conta.

A conta `admin` ocupa sempre a primeira posição da tabela e não pode ser excluída, nem pela web nem pelo painel. Nenhuma conta nova pode se chamar `admin`.

### Como a senha é guardada {#cap-08-hash}

O aparelho nunca guarda a senha. Ele guarda um resumo feito com HMAC-SHA256 em 5.000 rodadas, com um sal aleatório de 8 bytes gerado pelo hardware para cada conta e com o número de série da placa misturado à chave.

Contas antigas, gravadas no formato anterior de 2.500 rodadas, são convertidas sozinhas para o formato atual na primeira entrada certa.

O PIN do painel também é guardado só como resumo, com um sal próprio do aparelho e o número de série da placa.

## As 13 permissões {#cap-08-permissoes}

Cada permissão libera um grupo de ações. O aparelho confere a permissão em cada página, em cada consulta da interface de programação e em cada ação do painel: esconder um item da tela é só conforto.

| Permissão | Nome na página | Na interface web | No painel | Na interface de programação |
|---|---|---|---|---|
| [PERM_DASHBOARD]{.perm} `0x0001` | **Painel** | Páginas **Painel de Controle** e **Licença** | Nada | `GET /api/status`, `/api/themes`; `/metrics` ([capítulo 24](#cap-24)) |
| [PERM_HISTORY]{.perm} `0x0002` | **Histórico** | Página **Histórico e Logs**: gráficos, calendário e exportação | Nada | Consultas e exportação do histórico; baixar arquivos de `/history` |
| [PERM_LOGS]{.perm} `0x0004` | **Logs** | Página **Histórico e Logs**: eventos. Limpar o log exige também **Sistema** | Nada | Consulta e exportação do log de eventos |
| [PERM_SYS_CONFIG]{.perm} `0x0008` | **Sistema** | Páginas **Alarmes e Sons**, **Telemetria** e **Configurações**; no **Painel de Controle**, captura, espelho e tema do painel | Itens **Temas Visuais**, **Sons de Alarme**, **Idioma do Sistema**, **Calibrar Touch** e **Alinhamento da Tela** | Configuração, alarmes, sensores, hora, envio e cursor da telemetria, reinício, captura e toque remoto do painel |
| [PERM_NET_CONFIG]{.perm} `0x0010` | **Rede** | Página **Rede** | Item **Modo de Configuração** | Configuração de rede e busca de redes Wi-Fi |
| [PERM_FILE_READ]{.perm} `0x0020` | **Leitura** | Página **Arquivos**: listar e baixar | Nada | Listar e baixar arquivos; validar um backup |
| [PERM_FILE_UPLOAD]{.perm} `0x0040` | **Upload** | Página **Arquivos**: enviar arquivos e criar pastas | Nada | Envio de arquivo e criação de pasta |
| [PERM_FILE_DELETE]{.perm} `0x0080` | **Excluir** | Página **Arquivos**: apagar arquivos | Nada | Exclusão de arquivo |
| [PERM_USER_MGR]{.perm} `0x0100` | **Usuários** | Página **Usuários** | Itens **Usuários** e **Segurança do PIN**; definir o PIN de outra conta | Lista de contas, estado dos bloqueios de entrada, gravação de contas |
| [PERM_CALIB]{.perm} `0x0200` | **Calibração** | Bloco **Calibração** do editor de slot, que exige também **Sistema** | Nada | Leitura e gravação da calibração |
| [PERM_ALARM_LIMITS]{.perm} `0x0400` | **Limites (painel)** | Nada | Editor de limites do sensor | Nada |
| [PERM_ALARM_BLOCK]{.perm} `0x0800` | **Bloqueio (painel)** | Nada | Linha **Alarmes** do menu do sensor e **Desativar** na tela de alarme | Nada |
| [PERM_MAINT]{.perm} `0x1000` | **Manut. (painel)** | Nada | Linha **Manutenção** do menu do sensor | Nada |

Detalhes que a tabela não mostra:

- **Histórico e Logs** abre com **Histórico** ou com **Logs**, mas cada metade da página consulta o aparelho com a sua própria permissão ([capítulo 13](#cap-13-permissoes)).
- Baixar um arquivo de `/history` exige **Leitura** e **Histórico**; baixar um arquivo de log de eventos exige **Leitura** e **Logs**.
- No painel, o item **Limites de Alarme** aparece com qualquer uma das três permissões de painel, e cada linha do menu do sensor exige a sua ([capítulo 7](#cap-07-menu-sensor)).
- Sem nenhuma permissão, a conta entra na web mas não tem página para abrir ([capítulo 13](#cap-13-permissoes)).
- Os itens **Alterar Senha** (o próprio PIN), **Licença** e **Status do Sistema** do painel valem para qualquer conta identificada.
- Os gráficos, o calendário e o mínimo e máximo do painel não exigem PIN.

As rotas exatas de cada permissão estão no [capítulo 26](#cap-26).

### O administrador e as ações exclusivas {#cap-08-admin}

Além das 13 permissões, existe o nível de administrador completo, que só a conta `admin` tem de fábrica. Quatro ações exigem exatamente esse nível, e nenhuma combinação das 13 permissões as libera:

| Ação | Onde |
|---|---|
| Baixar o backup completo | Página **Arquivos** ([capítulo 17](#cap-17)) |
| Restaurar um backup | Página **Arquivos** ([capítulo 17](#cap-17)) |
| Enviar e aplicar uma atualização de firmware | Página **Arquivos** ([capítulo 17](#cap-17)) |
| [release]{.img} Instalar o certificado HTTPS | [Capítulo 9](#cap-09) |

A página **Usuários** concede no máximo as 13 permissões. Uma conta criada pela web, mesmo com todas elas marcadas, não faz backup nem atualiza o firmware. O nível completo só se concede pelo console completo, com `user perm <nome> admin`. As imagens release e alpha vêm com o console de emergência, que não tem esse comando: nelas, só a conta `admin` tem o nível completo ([capítulo 14](#cap-14)).

### Ninguém concede o que não tem {#cap-08-subconjunto}

Desde a v2.7.0, quem cria uma conta pela web só pode marcar permissões que a própria conta tem. Uma conta com **Usuários** e **Painel**, por exemplo, cria contas com **Painel**, com **Usuários** ou com as duas, e nada além disso.

Pelo mesmo motivo, só a própria conta `admin`, ou uma conta com o nível completo, define ou apaga o PIN do `admin` pela web.

::: atencao
**A página não avisa quando uma conta é recusada.** O aparelho recusa só a ação que fere a regra, grava o resto e reinicia. Depois do reinício, a conta não está na lista. Pela interface de programação, a resposta traz o motivo em `rejected`, como `"rejected":["users.perms"]` ([Motivos de recusa](#cap-08-recusas)).
:::

::: atencao
**No painel, a regra não vale.** Uma conta com **Usuários** pode dar as três permissões de painel a qualquer conta da lista, inclusive a si mesma, mesmo sem tê-las ([Usuários no painel](#cap-08-usuarios-painel)). Dê a permissão **Usuários** só a quem pode ter também as permissões de painel.
:::

## A página Usuários {#cap-08-pagina}

A página **Usuários** (*Users*, rota `/users`) exige a permissão **Usuários** [PERM_USER_MGR]{.perm}. O título é **Gestão de Acessos** (*Access Management*). Ela tem a tabela de contas e o formulário **Adicionar** (*Add New User*).

Toda mudança nesta página fica preparada na aba e só vale com **Salvar e reiniciar**. Qualquer gravação de contas reinicia o aparelho, e a página se recarrega sozinha depois.

::: {.figura #fig-08-usuarios tipo="web" arquivo="08-usuarios.png" captura="rota /users; largura 1280; sessão admin; imagem release; contas admin (com PIN), viewer e duas contas de operador, uma delas com PIN; nenhuma alteração pendente"}
A página Usuários: a tabela de contas e, abaixo, o formulário Adicionar.
:::

### A tabela de contas {#cap-08-tabela}

| Coluna | O que mostra |
|---|---|
| **#** | A posição da conta, de 0 a 31. A conta `admin` é a 0 |
| **Usuário** (*User*) | O nome, seguido do selo **PIN** quando a conta tem PIN de painel |
| **Permissões** (*Permissions*) | Um selo com o número de permissões. Toque nele para ver os nomes. A conta `admin` mostra **Super** |
| **Ações** (*Actions*) | Os botões da conta |

Os botões de cada conta:

| Botão | O que prepara |
|---|---|
| **PIN** | Definir ou apagar o PIN de painel da conta ([PIN pela web](#cap-08-pin-web)) |
| **Reset** | Uma senha nova de uso único; a conta troca a senha na próxima entrada |
| **Excluir** (*Del*) | A exclusão da conta |

A linha da conta `admin` tem só **PIN** e o selo **Protegido** (*Protected*): ela não pode ser resetada nem excluída pela página. A senha do `admin` se troca pela própria conta ([capítulo 13](#cap-13-trocar-senha)) ou, em recuperação, pelo console ([capítulo 18](#cap-18)).

Uma ação preparada aparece na linha até a gravação: **Pendente: PIN**, **Pendente: Reset**, **Pendente: Excluir**, ou uma linha nova com **Pendente: Novo** e o botão **↶**, que desfaz a última ação preparada.

::: {.figura #fig-08-usuarios-permissoes tipo="web" arquivo="08-usuarios-permissoes.png" captura="rota /users; largura 390; sessão admin; selo de permissões de uma conta de operador aberto, mostrando os nomes"}
No celular, o selo de permissões aberto mostra os nomes de cada permissão da conta.
:::

### Criar uma conta {#cap-08-criar}

O formulário **Adicionar** tem:

- **Nome** (*Username*): de 1 a 15 caracteres, sem aspas nem barra invertida;
- as 13 caixas de permissão, com os nomes da tabela de permissões;
- **PIN do painel (opcional)** (*Panel PIN (optional)*), só na imagem release;
- o botão **Criar** (*Create User*).

Para criar uma conta:

1. Digite o **Nome**.
2. Marque as permissões.
3. Se a pessoa vai usar o painel, digite um PIN em **PIN do painel (opcional)**.
4. Toque em **Criar**. A conta aparece na tabela com **Pendente: Novo**.
5. Toque em **Salvar e reiniciar** na barra de topo e confirme.
6. Copie a senha que aparece na janela **Senha temporária** (*Temporary password*).
7. Toque em **Guardei — recarregar** (*I saved it — reload*).

::: {.figura #fig-08-usuarios-nova tipo="web" arquivo="08-usuarios-nova.png" captura="rota /users; largura 1280; sessão admin; uma conta nova preparada com as permissões Painel e Histórico, ainda não gravada; linha com Pendente: Novo e o botão desfazer"}
Uma conta preparada e ainda não gravada.
:::

A janela **Senha temporária** lista cada conta criada ou resetada nessa gravação, com a senha de uso único ao lado. A senha tem 8 caracteres, entre letras maiúsculas e dígitos, sem `O`, `0`, `I` e `1`. Ela aparece só nessa janela: o aparelho guarda apenas o resumo. A janela não fecha com **Esc**, e a página só recarrega depois de **Guardei — recarregar**.

Entregue a senha à pessoa. Na primeira entrada, ela é levada à troca de senha obrigatória ([capítulo 13](#cap-13-troca-obrigatoria)).

::: {.figura #fig-08-senha-temporaria tipo="web" arquivo="08-senha-temporaria.png" captura="rota /users; largura 1280; sessão admin; logo depois de Salvar e reiniciar com uma conta nova chamada operador1; janela Senha temporária aberta; senha fictícia"}
A janela Senha temporária, com a senha de uso único da conta criada.
:::

### Mudar as permissões de uma conta {#cap-08-mudar}

A página **Usuários** não edita as permissões de uma conta existente. Para mudá-las pela web:

1. Toque em **Excluir** na conta e confirme **Excluir?**.
2. Crie a conta de novo, com o mesmo nome e as permissões novas.
3. Toque em **Salvar e reiniciar**.
4. Entregue a nova senha de uso único.

As duas ações podem ir na mesma gravação. O aparelho as aplica na ordem em que foram preparadas; por isso, prepare a exclusão antes da criação, ou o nome ainda estará em uso. O PIN de painel da conta excluída se perde com ela; defina-o de novo na criação.

Outros caminhos:

- as três permissões de painel de uma conta mudam no painel, sem apagar a conta ([Usuários no painel](#cap-08-usuarios-painel));
- qualquer permissão muda pelo console completo, com `user perm`, que vem instalado só no Air ([capítulo 14](#cap-14)).

### Excluir uma conta {#cap-08-excluir}

1. Toque em **Excluir** na linha da conta.
2. Confirme a pergunta **Excluir?**.
3. Toque em **Salvar e reiniciar**.

O aparelho apaga o registro inteiro da conta, inclusive o resumo da senha e o PIN, e reinicia. A exclusão é feita pela posição da conta na tabela. O log de eventos guarda o nome da conta nas entradas antigas.

### Resetar a senha de uma conta {#cap-08-reset}

1. Toque em **Reset** na linha da conta.
2. Confirme a pergunta **Forçar reset?**.
3. Toque em **Salvar e reiniciar**.
4. Copie a nova senha na janela **Senha temporária**.

O reset troca só a senha. As permissões e o PIN continuam como estavam.

### PIN pela web {#cap-08-pin-web}

[release]{.img} O botão **PIN** abre uma pergunta do navegador com a regra, `PIN: 4 a 8 de 0-9. Vazio remove o PIN.`

1. Toque em **PIN** na linha da conta.
2. Digite o PIN, ou deixe vazio para apagar o PIN da conta.
3. Toque em **OK**. A linha mostra **Pendente: PIN**.
4. Toque em **Salvar e reiniciar**.

Pela web, o PIN tem sempre de 4 a 8 dígitos, qualquer que seja a política de PIN do aparelho. O aparelho recusa um PIN que outra conta já use.

::: atencao
**Um PIN definido pela web pode não servir no painel.** Com uma política de mínimo acima de 4, um PIN mais curto que o mínimo é aceito pela web, mas o teclado do painel o recusa com **PIN muito curto (mín. 4)** antes de conferir. Com a política de letras ou de mais de 8 caracteres, defina o PIN no painel ([O próprio PIN](#cap-08-proprio-pin)).
:::

### Motivos de recusa {#cap-08-recusas}

A página não mostra estas recusas: a gravação continua, o aparelho reinicia e a ação recusada não aparece. Um script que grava contas pela interface de programação recebe a lista em `rejected` ([capítulo 26](#cap-26)).

| Motivo | Causa |
|---|---|
| `users.name` | Nome vazio, com mais de 15 caracteres, com aspas, barra invertida ou caractere de controle, ou igual a `admin` |
| `users.dup` | Já existe uma conta com esse nome |
| `users.perms` | Permissões fora das 13, ou alguma que a sua conta não tem |
| `users.full` | As 32 posições estão ocupadas |
| `users.pin` | PIN fora da regra ou já usado por outra conta |
| `users.id` | Conta inexistente, a conta `admin` numa exclusão ou reset, ou o PIN do `admin` pedido por quem não é administrador |

## Senhas e sessões da web {#cap-08-senhas}

A entrada, as mensagens, a troca de senha e as sessões estão no [capítulo 13](#cap-13-entrada). Em resumo:

| Regra | Valor |
|---|---|
| Senha nova, na página | Pelo menos 8 caracteres, com letra, dígito e símbolo ([capítulo 13](#cap-13-trocar-senha)) |
| Senha nova, conferida pelo aparelho | Só em HTTPS: pelo menos 8 caracteres, com letra e dígito |
| Troca obrigatória | Contas de fábrica, contas novas, contas resetadas e o `admin` depois de `system admin reset` ([capítulo 13](#cap-13-troca-obrigatoria)) |
| Sessões | Três ao todo, uma por conta; expiram depois de 15 min sem uso; um reinício encerra todas ([capítulo 13](#cap-13-sessoes)) |
| Sair | **Sair**, na gaveta de navegação |
| Bloqueio por senha errada | Por endereço de computador: 2 s na primeira, dobrando a cada erro, até 300 s ([capítulo 13](#cap-13-bloqueio)) |

O bloqueio guarda até 8 endereços ao mesmo tempo. A contagem de erros de um endereço para de subir em 12, o que mantém a espera em 300 s. Se os 8 lugares estiverem ocupados por bloqueios ainda ativos, o aparelho recusa uma entrada de um endereço novo com o código HTTP 429 e diz quanto esperar. Quem tem a permissão **Usuários** consulta esses bloqueios pela interface de programação.

## Identidade no painel {#cap-08-identidade}

[release]{.img} O painel não usa senha. Para abrir as configurações, a pessoa escolhe a própria conta numa lista e digita o PIN dela. A partir daí, o que o painel deixa fazer depende das permissões dessa conta. Cada vez que alguém toca no botão de configurações da tela inicial, o painel pede a identificação de novo; e depois de 30 s sem toque, ele volta sozinho à tela inicial.

Só contas com PIN conseguem se identificar. Uma conta sem PIN não aparece na lista.

### Entrar {#cap-08-entrar}

1. Na tela inicial, toque em **CFG**, na barra de botões de baixo, logo depois do último sensor ([capítulo 11](#cap-11-rodape)).
2. Na tela **Quem está usando o painel?**, toque na sua conta para selecioná-la e toque de novo, ou toque em **ENTRAR**.
3. Digite o PIN no teclado ([O teclado](#cap-08-teclado)).
4. Toque em **ENTRAR**.

Com o PIN certo, o painel toca o som de confirmação e abre o menu de configurações. O título mostra `Configurações >` seguido do nome da conta, e o menu lista só os itens que as permissões da conta alcançam. Com o menu filtrado, os itens aparecem sem os números.

A lista mostra quatro contas por página. As setas do rodapé andam uma conta por vez; com mais de oito contas, andam uma página por vez. **SAIR** volta à tela inicial. Uma conta bloqueada por tentativas erradas aparece com **bloqueado** em vermelho e não abre o teclado.

::: {.figura #fig-08-quem tipo="tft" arquivo="08-quem.png" captura="screen pin; contas admin, operador1 e manutencao com PIN; nenhuma bloqueada"}
A tela Quem está usando o painel?, com as contas que têm PIN.
:::

::: {.figura #fig-08-menu-operador tipo="tft" arquivo="08-menu-operador.png" captura="menu de configurações depois do PIN de uma conta que tem só permissões de painel; título Configurações > operador1"}
O menu de uma conta só com permissões de painel: os itens sem numeração e o nome da conta no título.
:::

### O teclado {#cap-08-teclado}

O teclado depende da política de PIN ([A política de PIN](#cap-08-politica)) e da finalidade da tela.

**Para se identificar, com 2 ou 3 caracteres por tecla,** o painel mostra cartões embaralhados. Cada cartão traz 2 ou 3 caracteres, e um toque no cartão vale por qualquer um deles: o painel nunca pergunta qual. Depois de cada toque, os caracteres são distribuídos de novo, então quem olha a tela não descobre o PIN pelos cartões tocados, nem se dois caracteres do PIN são iguais. O aparelho confere todas as combinações possíveis contra o PIN da conta escolhida.

| Política | Cartões na tela | Caracteres por cartão |
|---|---|---|
| `0-9`, 3 por tecla | 4 | 3, com 2 símbolos de enfeite entre eles |
| `0-9`, 2 por tecla | 6 | 2, com 2 símbolos de enfeite entre eles |
| `0-9 A-Z`, 3 por tecla | 12 | 3 |
| `0-9 A-Z`, 2 por tecla | 18 | 2 |

Os símbolos de enfeite completam os cartões para que todos tenham o mesmo número de caracteres. Eles nunca fazem parte de um PIN.

**Com 1 caractere por tecla,** o painel mostra um teclado ordenado, que não embaralha:

- com `0-9`, um teclado numérico: `1` a `9` em três linhas e o `0` sozinho no meio da quarta;
- com `0-9 A-Z`, nove teclas de grupo, `0-9`, `ABC`, `DEF`, `GHI`, `JKL`, `MNO`, `PQRS`, `TUV` e `WXYZ`. O primeiro toque abre o grupo; o segundo escolhe o caractere. Um toque fora da janela do grupo a fecha sem digitar nada.

**Para definir um PIN,** o painel usa sempre o teclado ordenado do alfabeto da política, qualquer que seja o número de caracteres por tecla. Escolher um PIN é o oposto de esconder um: o embaralhamento só custaria toques.

Em todos os teclados, a linha de pontos mostra quantos caracteres já foram digitados, e o rodapé tem:

- a tecla de apagar, que tira o último caractere;
- **SAIR**, que abandona a tela e funciona até durante uma espera;
- **ENTRAR**, que envia o PIN.

::: {.figura #fig-08-teclado-3 tipo="tft" arquivo="08-teclado-3.png" captura="screen pin e escolha de uma conta; política de fábrica: 0-9, 3 por tecla; dois caracteres já digitados"}
O teclado embaralhado da política de fábrica: quatro cartões de três caracteres.
:::

::: {.figura #fig-08-teclado-alfa tipo="tft" arquivo="08-teclado-alfa.png" captura="screen pin e escolha de uma conta; política 0-9 A-Z, 1 por tecla; grupo PQRS aberto"}
O teclado ordenado alfanumérico, com a janela de um grupo aberta.
:::

::: {.figura #fig-08-teclado-numerico tipo="tft" arquivo="08-teclado-numerico.png" captura="menu > Alterar Senha, sessão admin; política de fábrica; tela Novo PIN com três dígitos digitados"}
O teclado numérico ordenado, usado para definir um PIN.
:::

### PIN errado e bloqueios {#cap-08-bloqueios}

Um PIN errado faz o painel mostrar **PIN inválido!**, tocar o som de erro e distribuir os cartões de novo. Um PIN mais curto que o mínimo da política não chega a ser conferido: o painel mostra **PIN muito curto (mín. 4)**.

As tentativas erradas são contadas por conta:

| Tentativa errada da conta | O que acontece |
|---|---|
| 1ª e 2ª | Nada; pode tentar de novo na hora |
| 3ª | Espera de 5 s |
| 4ª | Espera de 15 s |
| 5ª | Espera de 60 s |
| 6ª | A conta fica bloqueada no painel até o aparelho reiniciar |

Durante uma espera, o teclado some e a tela mostra **Tentativas Excedidas** e **Aguarde N segundos...**. A espera vale para o teclado inteiro, não só para a conta que errou. Uma entrada certa zera a contagem daquela conta.

Além disso, o painel inteiro trava depois de 20 tentativas erradas somadas entre todas as contas, contadas desde a última entrada certa. A tela fica vermelha, com **ACESSO BLOQUEADO** e **Reinicialização requerida**, e só um reinício destrava. As contagens ficam só na memória: um reinício zera todas.

A web não é afetada por nenhum desses bloqueios.

::: {.figura #fig-08-pin-invalido tipo="tft" arquivo="08-pin-invalido.png" captura="screen pin, escolha de uma conta, um PIN errado e ENTRAR; logo depois da recusa"}
Um PIN recusado: a mensagem PIN inválido! e os cartões distribuídos de novo.
:::

::: {.figura #fig-08-espera tipo="tft" arquivo="08-espera.png" captura="screen pin; a mesma conta errou o PIN três vezes seguidas; durante a espera de 5 s"}
A espera depois da terceira tentativa errada.
:::

::: {.figura #fig-08-painel-bloqueado tipo="tft" arquivo="08-painel-bloqueado.png" captura="tela do teclado depois de 20 tentativas erradas somadas, sem nenhuma entrada certa no meio"}
O painel travado: só um reinício libera o teclado.
:::

### O PIN de fábrica {#cap-08-pin-fabrica}

A conta `admin` sai de fábrica com o PIN `1234`. Na primeira identificação com ele, o painel abre direto a tela **Novo PIN**, e o log registra o código 302 com o texto **PIN padrão detectado; forçando troca.** A troca fica pendente até o `admin` escolher um PIN diferente de `1234`; escolher `1234` de novo não a desfaz.

Da mesma forma, quando a política de PIN fica mais exigente, toda conta com PIN é marcada para trocá-lo, e o painel abre **Novo PIN** na próxima identificação de cada uma ([A política de PIN](#cap-08-politica)).

Nos dois casos, a conta já está identificada quando a tela **Novo PIN** abre. **SAIR** leva ao menu sem trocar, e a tela volta na identificação seguinte.

### O próprio PIN {#cap-08-proprio-pin}

Qualquer conta identificada troca o próprio PIN pelo item **Alterar Senha** do menu (o 5º, *Change Password*).

1. Toque em **Alterar Senha**.
2. Na tela **Novo PIN**, digite o PIN novo no teclado ordenado e toque em **ENTRAR**.
3. Na tela **Confirme o PIN**, digite de novo e toque em **ENTRAR**.

| Mensagem | Significado |
|---|---|
| **PIN salvo!** | Gravado; vale na próxima identificação |
| **Os PINs não coincidem!** | As duas digitações diferem; o painel volta a **Novo PIN** |
| **PIN muito curto (mín. 4)** | Menos caracteres que o mínimo da política |
| **PIN inválido!** | Fora da política |
| **PIN já em uso!** | Outra conta já tem esse PIN. Nada é gravado |

Cada mensagem de resultado tem o botão **ENTENDI**, que volta à tela de onde a ação partiu. O log registra o código 445, **PIN do display alterado**.

O PIN precisa ser único porque dois PINs iguais deixariam de identificar quem está no painel. Por isso o aparelho recusa um PIN já usado, sem dizer de quem é.

::: {.figura #fig-08-pin-em-uso tipo="tft" arquivo="08-pin-em-uso.png" captura="menu > Alterar Senha; digitar duas vezes um PIN que outra conta já usa"}
A recusa de um PIN que outra conta já tem.
:::

### A política de PIN {#cap-08-politica}

A política define como são os PINs de todas as contas. Ela tem três ajustes, que dependem um do outro:

| Ajuste | Painel | Página Configurações | Valores | Fábrica |
|---|---|---|---|---|
| Caracteres por tecla | **Teclado** | **Teclado do painel: glifos por tecla** | 1, 2 ou 3 | 3 |
| Alfabeto | **Caracteres** | **Caracteres do PIN** | `0-9` ou `0-9 A-Z` | `0-9` |
| Tamanho mínimo | **Tamanho mínimo** | **Tamanho mínimo do PIN** | De 4 até o teto do teclado | 4 |

O teto de tamanho depende dos caracteres por tecla: 8 com 3, 12 com 2 e 16 com 1. Com 2 ou 3 caracteres por tecla, o aparelho confere todas as combinações que os toques podem formar, e mais caracteres custariam tempo demais. Um toque além do teto não é aceito.

Como escolher:

- **Mais caracteres por tecla** escondem melhor o PIN de quem olha a tela, mas facilitam um chute às cegas.
- **O alfabeto `0-9 A-Z`** torna um chute às cegas muito mais difícil sem custo nenhum. As letras são sempre maiúsculas.
- **Um PIN mais longo** é a defesa principal contra chutes.

**No painel.** O item **Segurança do PIN** (o 11º, *PIN security*) exige a permissão **Usuários** [PERM_USER_MGR]{.perm}.

1. Toque numa linha para selecioná-la e toque de novo para mudar o valor. **Tamanho mínimo** sobe até o teto e volta a 4; **Teclado** mostra o teto ao lado, como `3  (max 8)`; **Caracteres** alterna entre `0-9` e `0-9 A-Z`.
2. Toque em **SALVAR**. O painel mostra **PIN salvo!**.

Ao mudar um ajuste, o painel corrige os outros para uma combinação possível. **SAIR** descarta. A política vale na próxima tela de PIN, sem reiniciar, e o log registra o código 459, **Política de PIN alterada pelo painel**.

**Na web.** Os três campos ficam na seção **Hardware** da página **Configurações**, que exige a permissão **Sistema** ([capítulo 6](#cap-06-hardware)). A dica do tamanho mínimo mostra a faixa permitida, como `4 – 8`. Pela web, a mudança exige **Salvar e reiniciar**. O log registra o mesmo código 459, com a marca `[web]`.

Quando a política fica mais exigente em qualquer ajuste (mínimo maior, alfabeto menor ou teto menor), toda conta com PIN é marcada para trocá-lo. A conta continua se identificando com o PIN antigo e é levada a **Novo PIN** logo depois ([O PIN de fábrica](#cap-08-pin-fabrica)). Afrouxar a política não marca ninguém.

::: {.figura #fig-08-politica tipo="tft" arquivo="08-politica.png" captura="menu > Segurança do PIN, sessão admin; política de fábrica; linha Teclado selecionada"}
A tela Segurança do PIN, com a política de fábrica: tamanho mínimo 4, 3 caracteres por tecla (máximo 8) e só dígitos.
:::

::: {.figura #fig-08-politica-web tipo="web" arquivo="08-politica-web.png" captura="rota /config; largura 1280; sessão admin; imagem release; recorte dos três campos de PIN da seção Hardware, com 2 glifos por tecla e 0-9 A-Z escolhidos"}
Os campos da política de PIN na página Configurações.
:::

### Usuários no painel {#cap-08-usuarios-painel}

O item **Usuários** (o 10º, *Users*) exige a permissão **Usuários** [PERM_USER_MGR]{.perm}. Ele lista todas as contas menos a `admin`, com:

- um ponto depois do nome, quando a conta tem PIN;
- as letras `L`, `B` e `M` à direita, acesas para as permissões **Limites (painel)**, **Bloqueio (painel)** e **Manut. (painel)** que a conta tem.

O rodapé tem **SAIR** e **NOVO**. Toque numa conta para selecioná-la e de novo para abrir o editor dela.

::: {.figura #fig-08-usuarios-lista tipo="tft" arquivo="08-usuarios-lista.png" captura="screen usr; contas viewer, operador1 (com PIN, L e B) e manutencao (com PIN, M)"}
A lista de contas do painel, com o ponto de quem tem PIN e as letras L, B e M.
:::

**Criar uma conta no painel:**

1. Toque em **NOVO**.
2. Digite o nome no teclado de texto, com até 15 caracteres, e toque em **OK**. O **X** do título volta à lista.
3. Na tela seguinte, marque as permissões de painel: toque em **Editar limites**, **Bloquear alarmes** e **Manutenção** para alternar entre **SIM** e **NÃO**.
4. Toque em **SEGUIR**.
5. Digite o PIN da conta em **Novo PIN** e de novo em **Confirme o PIN**, tocando em **ENTRAR** a cada vez.

O painel mostra **Usuário salvo!** e grava na hora, sem reiniciar. O log registra o código 450, **Usuário criado pelo painel**, com o nome de quem criou e da conta nova.

| Mensagem | Causa |
|---|---|
| **Nome inválido ou repetido** | Nome com aspas, barra invertida ou caractere de controle, já usado por outra conta, ou `admin` |
| **Sem vaga para conta nova** | As 32 posições estão ocupadas |
| **PIN já em uso!** | Outra conta tem esse PIN; a conta não é criada |
| **Sem permissão** | A conta identificada não tem mais a permissão **Usuários** |

Uma conta criada no painel tem só as três permissões de painel e uma senha web aleatória que ninguém conhece. Ela não abre nenhuma página web até um administrador recriá-la pela página **Usuários** com permissões web e entregar a senha de uso único ([Mudar as permissões de uma conta](#cap-08-mudar)).

::: {.figura #fig-08-novo-nome tipo="tft" arquivo="08-novo-nome.png" captura="screen usr -> NOVO; nome manutencao digitado"}
O teclado de texto para o nome da conta nova.
:::

::: {.figura #fig-08-novo-permissoes tipo="tft" arquivo="08-novo-permissoes.png" captura="screen usr -> NOVO -> nome -> OK; Editar limites e Manutenção em SIM"}
As três permissões de painel da conta nova, antes de SEGUIR.
:::

::: {.figura #fig-08-novo-pin tipo="tft" arquivo="08-novo-pin.png" captura="mesmo caminho -> SEGUIR; tela Novo PIN com o teclado ordenado"}
O PIN da conta nova, digitado duas vezes no teclado ordenado.
:::

**Editar uma conta no painel.** O editor mostra o nome da conta no título e cinco linhas:

| Linha | O que faz |
|---|---|
| **Editar limites** | Alterna a permissão **Limites (painel)** |
| **Bloquear alarmes** | Alterna a permissão **Bloqueio (painel)** |
| **Manutenção** | Alterna a permissão **Manut. (painel)** |
| **Definir PIN** | Define um PIN novo para a conta, digitado duas vezes |
| **Excluir usuário** | Exclui a conta, depois de confirmar |

As três primeiras linhas só mudam na tela; **SALVAR** grava, e o painel mostra **Usuário salvo!** (código 453, **Permissões alteradas pelo painel**). As permissões web da conta ficam como estavam. **Definir PIN** grava ao fim da confirmação (código 452, **PIN do painel definido**).

**Excluir uma conta no painel:**

1. No editor da conta, toque em **Excluir usuário**.
2. Na pergunta **Excluir este usuário?**, com o nome da conta embaixo, toque em **EXCLUIR**. **CANCELAR** volta ao editor.

O painel mostra **Usuário excluído** e grava na hora, sem reiniciar (código 451, **Usuário removido pelo painel**). O painel não exclui a conta `admin` nem a conta que está identificada no momento.

::: {.figura #fig-08-excluir tipo="tft" arquivo="08-excluir.png" captura="screen usr -> conta manutencao -> Excluir usuário"}
A confirmação antes de excluir uma conta no painel.
:::

::: nota
**Por que o painel não reinicia e a web reinicia.** O painel muda uma conta de cada vez e grava na hora. A página Usuários grava as contas junto com o resto das alterações preparadas, e o aparelho reinicia para que todas passem a valer juntas.
:::

## Auditoria {#cap-08-auditoria}

O aparelho registra quem fez cada ação:

| Onde | O que fica registrado |
|---|---|
| Log de eventos, web | Entrada aceita (`Login OK: <conta>`, código 300), recusada (301), página sem permissão (302), sessão expirada (304) e gravações de configuração (303) |
| Log de eventos, painel | PIN aceito (308), com o nome da conta e o tempo que a conferência levou; PIN recusado (309); conta bloqueada (310) |
| Log de eventos, ações do painel | Códigos 442, 445 e 450 a 459. Todos levam a posição da conta no contexto, e a maioria leva também o nome no texto |
| Linha de alarmes | O nome da conta nos registros de ações feitas com PIN (desativar, bloquear, limites, manutenção) e nas janelas de manutenção abertas pela web ([capítulo 22](#cap-22)) |
| Painel | O nome da conta no título do menu de configurações |

O código 311, **Entrada do teclado casa com duas contas**, era de uma versão em que o PIN identificava a conta sozinho. Desde que a conta é escolhida antes do PIN, ele não ocorre mais.

Como as posições da tabela de contas são reaproveitadas, o que identifica a pessoa num registro antigo é o nome no texto, e não o número da posição. A leitura do log de eventos está no [capítulo 16](#cap-16).

## O que existe em cada imagem {#cap-08-imagens}

| Recurso | release | alpha | Air |
|---|---|---|---|
| Contas, senhas e sessões da web | Sim | Sim | Sim, enquanto a interface web está no ar ([capítulo 19](#cap-19)) |
| As 13 permissões | Sim | Sim | Sim |
| Permissões de painel | Usadas no painel | Existem na página, sem efeito | Existem na página, sem efeito |
| PIN, lista de contas e teclado do painel | Sim | Não | Não |
| Campo e botão **PIN** na página **Usuários** | Sim | Escondidos | Escondidos |
| Política de PIN em **Configurações** | Sim | Escondida | Escondida |
| Senha conferida pelo aparelho em HTTPS | Sim, com certificado instalado | Não | Não |

Nas imagens alpha e Air, o aparelho recusa um PIN enviado pela interface de programação (`users.pin`), mas cria a conta normalmente.

## Recuperação {#cap-08-recuperacao}

| Situação | Onde está a saída |
|---|---|
| Ninguém sabe a senha do `admin` | `system admin reset` no console serial ([capítulo 18](#cap-18)) |
| Painel travado por tentativas erradas | Reinicie o aparelho ([capítulo 18](#cap-18)) |
| Uma conta bloqueada no painel | Reinicie o aparelho, ou identifique-se com outra conta |
| O PIN do `admin` foi esquecido | O `admin` define outro pelo botão **PIN** da página **Usuários**. Sem a senha web do `admin`, use antes `system admin reset` ([capítulo 18](#cap-18)) |
| Nenhuma conta com PIN | Defina um PIN pela página **Usuários**: sem conta com PIN, a lista do painel fica vazia e **ENTRAR** só toca o som de erro |
| Conta sem nenhuma página web | Dê a ela a permissão **Painel** ([capítulo 13](#cap-13-permissoes)) |
