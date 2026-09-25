# Rede, Wi-Fi e HTTPS {#cap-09}

Este capítulo cobre a página **Rede**, a escolha da rede Wi-Fi, o que o aparelho faz quando perde a rede, o ponto de acesso de configuração durante a operação, o IP estático, o DNS, a porta web, o mDNS e o HTTPS com certificado próprio. É para quem instala o aparelho numa rede e para a equipe de TI que cuida dessa rede. A primeira configuração da rede, no primeiro boot, está no [capítulo 4](#cap-04).

## A página Rede {#cap-09-pagina}

A página **Rede** (*Network*, rota `/network`) exige a permissão **Rede** [PERM_NET_CONFIG]{.perm}. Ela tem duas partes:

- **Status da Conexão** (*Current Connection Status*): o estado da rede agora, só para leitura;
- **Configuração** (*Network Configuration*): o formulário, com as seções **Wi-Fi**, **IPv4**, **Configuração de DNS**, **Servidor de Hora (NTP)** e **Servidor Web**.

Como nas outras páginas de configuração, cada alteração fica preparada e só vale depois de gravada pelos botões da barra de topo ([capítulo 5](#cap-05)). Quase tudo nesta página exige reinício: com uma alteração da página **Rede** preparada, conte com **Salvar e reiniciar**.

::: {.figura #fig-09-rede tipo="web" arquivo="09-rede.png" captura="rota /network; largura 1280; sessão admin; imagem release; conectado por DHCP à rede MinhaRede; DNS automático; porta 80; sem certificado HTTPS instalado"}
A página Rede: à esquerda, o estado da conexão; à direita, o formulário de configuração.
:::

### Status da Conexão {#cap-09-status}

| Linha | O que mostra |
|---|---|
| **Status** | **Conectado** (*Connected*), em verde, ou **Desconectado** (*Disconnected*), em vermelho |
| **IP** | O endereço IPv4 em uso. No ponto de acesso de configuração, `192.168.4.1` |
| **Máscara** | A máscara de sub-rede em uso |
| **Gateway** | O gateway em uso |
| **DNS** | O servidor DNS em uso |
| **MAC** | O endereço MAC do rádio |

Os valores são os que estão em uso agora, não os configurados: com DHCP, são os que o roteador entregou.

**Conectado** quer dizer que o aparelho terminou de entrar na rede. Com o NTP ligado, isso inclui o primeiro acerto do relógio: até lá, a linha diz **Desconectado** mesmo com um IP na linha de baixo ([capítulo 10](#cap-10-sem-ntp)).

### Wi-Fi {#cap-09-wifi}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **SSID** (*SSID (Network Name)*) | Nome da rede Wi-Fi | 1 a 31 caracteres, sem caracteres de controle | Vazio | Reinicia (`net`) | `ssid` |
| **Senha** (*Password (Leave empty to keep current)*) | Senha da rede | Até 31 caracteres; vazio mantém a senha atual | Vazio | Reinicia (`net`) | `pass` |

- O botão **Buscar** (*Scan*), ao lado do **SSID**, lista as redes ao alcance ([Escolher a rede pela busca](#cap-09-busca)).
- O botão com o olho, ao lado da **Senha**, mostra ou esconde o que você digitou (**Mostrar senha** / **Ocultar senha**).
- O campo **Senha** sempre abre vazio. O aparelho nunca devolve a senha gravada, nem para um administrador.
- Um **SSID** vazio na gravação é ignorado: o aparelho mantém a rede atual.

O rádio do Pico W só opera em 2,4 GHz. Uma rede só de 5 GHz não aparece na busca e não aceita o aparelho.

::: atencao
**Senha de Wi-Fi com mais de 31 caracteres não cabe.** O WPA2 aceita senhas de 8 a 63 caracteres, mas o aparelho guarda no máximo 31. Uma senha mais longa é recusada na gravação (`net.pass`). Numa rede assim, use uma senha mais curta ou uma rede separada para os aparelhos.
:::

::: nota
**A senha digitada fica na aba até ser gravada.** Como toda alteração preparada, ela fica guardada na aba do navegador até você gravar, tocar em **Sair** ou fechar a aba ([capítulo 5](#cap-05-gravacao)). Não deixe uma alteração de senha preparada num computador compartilhado.
:::

### IPv4 {#cap-09-ipv4}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **DHCP** (*Obtain IP automatically (DHCP)*) | Pede o endereço ao roteador | Ligado ou desligado | Ligado | Reinicia (`net`) | `use_dhcp` |
| **IP Estático** (*Static IP Address*) | Endereço fixo do aparelho | IPv4, como `192.0.2.10` | `192.168.1.100` | Reinicia (`net`) | `ip` |
| **Máscara** (*Subnet Mask*) | Máscara da sub-rede | IPv4, como `255.255.255.0` | `255.255.255.0` | Reinicia (`net`) | `mask` |
| **Gateway** | Roteador da sub-rede | IPv4 | `192.168.1.1` | Reinicia (`net`) | `gw` |

Com **DHCP** ligado, os três campos de baixo ficam esmaecidos e não são gravados. Para usar IP fixo:

1. Desligue **DHCP**.
2. Preencha **IP Estático**, **Máscara** e **Gateway**.
3. Na seção **Configuração de DNS**, desligue **Obter DNS automaticamente (DHCP)** e preencha **DNS Primário** ([DNS](#cap-09-dns)).
4. Toque em **Salvar e reiniciar**.
5. Depois do reinício, abra a interface pelo endereço novo.

Um endereço que não é um IPv4 válido é recusado (`net.ip`, `net.mask` ou `net.gw`), e o aparelho mantém o anterior ([capítulo 5](#cap-05-recusados)). O aparelho não confere se o endereço está livre na rede: reserve-o no roteador ou escolha um fora da faixa do DHCP.

::: {.figura #fig-09-ip-estatico tipo="web" arquivo="09-ip-estatico.png" captura="rota /network; largura 1280; sessão admin; DHCP desligado; IP Estático 192.0.2.10, Máscara 255.255.255.0, Gateway 192.0.2.1; DNS automático desligado com DNS Primário 192.0.2.1; alterações preparadas e ainda não gravadas"}
A seção IPv4 com o DHCP desligado e os campos do IP fixo preenchidos.
:::

### DNS {#cap-09-dns}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Obter DNS automaticamente (DHCP)** (*Obtain DNS automatically (DHCP)*) | Usa o DNS que o roteador entrega | Ligado ou desligado | Ligado | Reinicia (`web`) | `dns_auto` |
| **DNS Primário** (*Primary DNS*) | Primeiro servidor DNS | IPv4 | `8.8.8.8` | Reinicia (`net`) | `dns1` |
| **DNS Secundário** (*Secondary DNS*) | Segundo servidor DNS | IPv4, ou vazio para não usar | Vazio | Reinicia (`web`) | `dns2` |

Com **Obter DNS automaticamente (DHCP)** ligado, os dois campos ficam esmaecidos. O DNS que vale em cada combinação:

| DHCP | DNS automático | DNS primário em uso | DNS secundário em uso |
|---|---|---|---|
| Ligado | Ligado | O do roteador | O do roteador |
| Ligado | Desligado | **DNS Primário** | **DNS Secundário**, se preenchido |
| Desligado | Desligado | **DNS Primário** | **DNS Secundário**, se preenchido |
| Desligado | Ligado | **DNS Primário** | Nenhum |

Com IP fixo não há roteador para entregar DNS: o **DNS Primário** vale sempre. Desligue o DNS automático para poder editá-lo e para o secundário valer.

O aparelho precisa de DNS para achar servidores por nome: o NTP ([capítulo 10](#cap-10)) e o coletor da telemetria, quando configurado por nome ([capítulo 21](#cap-21)).

Um **DNS Primário** inválido é ignorado sem aviso, e o aparelho mantém o anterior. Um **DNS Secundário** inválido é recusado com `net.dns2`.

### Servidor de Hora (NTP) {#cap-09-ntp}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Endereço do Servidor** (*Server Address*) | Servidor NTP preferido | Nome ou IP, até 31 caracteres; vazio usa `pool.ntp.org` | Vazio | Reinicia (`time`) | `ntp_server` |

A dica abaixo do campo diz **Deixe vazio para usar o padrão (pool.ntp.org)**. Se o NTP estiver desligado na página **Configurações**, a seção mostra a faixa **NTP está desabilitado.** com o atalho **Habilite em Configurações do Sistema →**. Como o aparelho escolhe e troca de servidor está no [capítulo 10](#cap-10-ntp).

### Servidor Web {#cap-09-servidor-web}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Porta HTTP** (*HTTP Port*) | Porta do servidor web | 1 a 65535 | 80 | Reinicia (`web`) | `web_port` |
| **Conexões persistentes (keep-alive)** (*Persistent connections (keep-alive)*) | Reaproveita a conexão entre pedidos | Ligado ou desligado | Ligado | Reinicia (`web`) | `web_ka` |

A dica da porta diz **Padrão: 80. Após salvar, o navegador redireciona automaticamente para a nova porta.** Depois de **Salvar e reiniciar**, a página avisa **Salvo. Redirecionando para nova porta...** e abre o endereço novo cerca de 15 s depois. Guarde a porta: sem ela, o endereço sem porta não responde. A porta vale também para o HTTPS, com uma exceção: com a porta 80, o HTTPS atende na 443 ([HTTPS](#cap-09-https)).

**Conexões persistentes (keep-alive)** está ligado de fábrica desde a v2.3.0 e vale para HTTP e HTTPS. Em HTTPS, ele evita uma negociação TLS a cada pedido, e a dica diz que as páginas carregam cerca de 60 % mais rápido. O interruptor só aparece quando há um certificado HTTPS instalado. Desligue-o apenas se um proxy ou um cliente se comportar mal com conexões persistentes.

::: {.figura #fig-09-servidor-web tipo="web" arquivo="09-servidor-web.png" captura="rota /network; largura 1280; sessão admin; imagem release; certificado HTTPS instalado; página aberta por https; recorte das seções Servidor de Hora (NTP) e Servidor Web, com o interruptor Conexões persistentes visível e ligado"}
A seção Servidor Web num aparelho com certificado instalado: o interruptor de conexões persistentes só aparece nesse caso.
:::

## Escolher a rede pela busca {#cap-09-busca}

O botão **Buscar** lista as redes Wi-Fi ao alcance do aparelho, para você escolher sem digitar o nome.

1. Toque em **Buscar**. O botão muda para **Buscando...**.
2. Espere a lista, normalmente em cerca de 1 s.
3. Toque na rede desejada. O nome vai para o campo **SSID**, a lista se fecha e o cursor vai para **Senha**.
4. Digite a senha e toque em **Salvar e reiniciar**.

Cada linha da lista mostra:

- um cadeado, se a rede é protegida; nada, se é aberta;
- o nome da rede;
- quatro barras de sinal;
- o sinal em dBm.

Pare o ponteiro sobre a linha para ver **Rede protegida** ou **Rede aberta** e o sinal.

| Barras | Sinal |
|---|---|
| 4 | −55 dBm ou mais |
| 3 | De −67 a −56 dBm |
| 2 | De −78 a −68 dBm |
| 1 | −78 dBm ou abaixo |

A lista tem no máximo 12 redes, da mais forte para a mais fraca. Uma rede com vários pontos de acesso, como uma rede mesh, aparece uma vez só, com o sinal do ponto mais forte. Redes ocultas não aparecem: digite o nome delas no campo **SSID**.

A busca funciona também de dentro do ponto de acesso de configuração. O aparelho liga a interface de estação ao lado do AP só para buscar, e o AP continua no ar.

Mensagens possíveis:

| Mensagem | Quando |
|---|---|
| **Nenhuma rede encontrada** (*No networks found*) | A busca terminou sem nenhuma rede visível |
| **Rádio ocupado — tente de novo em instantes** (*Radio busy — try again in a moment*) | O aparelho está buscando a própria rede para se reconectar, ou o rádio recusou a busca |
| **A busca falhou** (*The scan failed*) | A busca não terminou. O aparelho desiste depois de 15 s, e a página, depois de cerca de 18 s |

::: {.figura #fig-09-busca tipo="web" arquivo="09-busca.png" captura="rota /network; largura 1280; sessão admin; busca concluída com 5 redes de exemplo (MinhaRede protegida -48 dBm com 4 barras, uma rede aberta com 2 barras, as outras protegidas); lista aberta abaixo do campo SSID"}
A lista de redes depois de Buscar: cadeado, nome, barras e sinal em dBm, da mais forte para a mais fraca.
:::

A mesma busca está disponível para integradores em `GET /api/wifi/scan`, com a permissão **Rede**: a primeira chamada, com `?again=1`, dispara a busca e responde `{"scanning":true}`; as seguintes devolvem a lista quando ela fica pronta ([capítulo 26](#cap-26)).

## Quando a rede cai: a escada de reconexão {#cap-09-reconexao}

O aparelho nunca desiste da rede configurada. Quando não consegue entrar, ele tenta de novo com intervalos crescentes, e descansa quando as tentativas se esgotam. É a escada de reconexão.

### Os degraus {#cap-09-degraus}

Uma tentativa de entrar na rede leva até 20 s. Antes de cada tentativa, o aparelho busca a rede para conferir se ela está no ar. Se a rede não aparece em duas buscas seguidas, o aparelho tenta entrar assim mesmo: é assim que uma rede oculta, que nunca aparece em busca, volta a conectar. Cada busca tem 15 s de prazo.

Depois de cada tentativa que falha, a espera até a próxima dobra:

| Falha | Espera antes da próxima busca |
|---|---|
| Queda da conexão (ponto de partida) | 5 s |
| 1ª | 10 s |
| 2ª | 20 s |
| 3ª | 40 s |
| 4ª | 80 s |
| 5ª | Dormência: 10 min |

O intervalo tem teto de 120 s, que a escada não chega a atingir: a 5ª falha já leva à dormência.

Na dormência, a espera antes de cada busca passa a ser de 10 min. O aparelho cumpre três dormências. Se a rede não voltar, ele volta ao primeiro degrau e registra **Dormancy over — back to fast retries**. Uma conexão bem-sucedida, a qualquer momento, zera a escada.

Durante a escada, o aparelho continua medindo, gravando o histórico e atendendo o painel. A telemetria pendente espera a rede voltar ([capítulo 21](#cap-21)). Isso muda quando a escada abre o AP de configuração: veja [Enquanto o AP está aberto](#cap-09-ap-aberto).

::: {.figura #fig-09-escada tipo="diagrama" arquivo="09-escada.png" captura="linha do tempo da escada de reconexão: queda; cinco ciclos de busca, busca e tentativa de 20 s com esperas de 5, 10, 20, 40 e 80 s; três dormências de 10 min antes de cada busca; ao fim, o AP de configuração (aparelho que já teve IP) e o recomeço da escada; marcar à parte o AP rápido na primeira dormência para o aparelho que nunca teve IP"}
A escada de reconexão: esperas que dobram, três dormências de 10 min e o ponto em que o aparelho abre o AP de configuração.
:::

### Outros motivos para reconectar {#cap-09-vigilancia}

Conectado, o aparelho confere o sinal uma vez por minuto. Um sinal impossível, 0 dBm ou mais, ou abaixo de −120 dBm, indica que o rádio parou de responder de verdade, mesmo que o Wi-Fi ainda se diga conectado. Duas leituras impossíveis seguidas fazem o aparelho derrubar a conexão e entrar na escada, com o evento **Implausible RSSI twice — link presumed dead, reconnecting**.

### Como ler a escada no log de eventos {#cap-09-log}

A página **Histórico e Logs** mostra os eventos ([capítulo 16](#cap-16)). Os que contam a história da rede:

| Código | Texto do evento | Quando |
|---|---|---|
| 522 | **Gerenciador WiFi iniciando** | No boot, com rede configurada |
| 520 ou 521 | **Modo DHCP ativado** ou **Modo IP estático ativado** | No boot |
| 523 | **SSID WiFi não configurado** | No boot, sem rede configurada |
| 12 | `Scanning for SSID (backoff=<n>s)` | Cada busca da escada |
| 10 | `SSID found, connecting...` | A rede apareceu na busca |
| 10 | `SSID not in scan — associating anyway` | Tentativa sem ver a rede, depois de duas buscas |
| 525 | `Retry in <n>s` | Uma tentativa falhou; o contexto é o número de falhas seguidas |
| 526 | `Dormant: retry in 600s` | Início de uma dormência |
| 526 | `Dormancy over — back to fast retries` | Fim das três dormências |
| 14 | `IP: <endereço>` | Entrou na rede |
| 11 | `WiFi signal lost, entering stealth scan` | A conexão caiu |
| 11 | `Implausible RSSI twice — link presumed dead, reconnecting` | O rádio parou de responder; o contexto é a leitura |
| 12 | `Scan never finished — abandoning it` | Uma busca passou do prazo de 15 s |
| 403 | **AP iniciado** | O AP de configuração abriu ([O AP durante a operação](#cap-09-ap)) |

O log de eventos não grava cada repetição. Uma falha que se repete, como `Retry in <n>s`, é gravada na primeira vez e depois só a cada hora, enquanto durar. A volta da rede (`IP: ...`) é gravada porque encerra a falha. Por isso, uma escada longa aparece no log como poucas linhas. O filtro está explicado no [capítulo 16](#cap-16).

## O AP de configuração durante a operação {#cap-09-ap}

[release]{.img} [alpha]{.img}

O ponto de acesso de configuração é uma rede Wi-Fi que o próprio aparelho cria para você configurá-lo quando ele não consegue entrar na sua rede. O primeiro uso, num aparelho novo, está no [capítulo 4](#cap-04).

A rede do AP:

| Item | Valor |
|---|---|
| Nome da rede | O nome do aparelho seguido de `_SETUP`, como `simut_SETUP` |
| Segurança | WPA2, com uma chave de 10 caracteres |
| Endereço do aparelho | `http://192.168.4.1` |
| Qualquer outro endereço | Leva ao aparelho ([capítulo 13](#cap-13-abrir)) |

A chave é derivada da identidade do chip e é sempre a mesma para aquele aparelho, mesmo depois de um reset de fábrica. Ela usa letras maiúsculas e algarismos, sem `O`, `0`, `I` e `1`, para ser lida em voz alta sem confusão. A chave aparece:

- no console serial, quando o AP abre;
- no painel, quando o aparelho liga já no AP ou abre o AP em operação (pelo menu, pela escada ou pelo comando `ap`);
- no LCD do alpha, que na v2.7.1 quase nunca chega a mostrá-la ([capítulo 12](#cap-12-ap));
- na resposta do comando `ap` do console ([capítulo 14](#cap-14)).

Anote a chave do seu aparelho na primeira configuração.

::: nota
**O nome do AP e o limite de 32 caracteres.** Um nome de rede Wi-Fi tem no máximo 32 caracteres, e o do AP é o nome do aparelho com mais 6. Mantenha o nome do aparelho com até 26 caracteres: com um nome maior, o nome da rede passa do limite e o AP pode não abrir. Se o AP não abrir, o console avisa e o log registra a falha.
:::

### Quando o AP abre {#cap-09-ap-quando}

| Situação | Quando abre |
|---|---|
| O aparelho liga sem rede configurada | No boot |
| Gesto de toque no boot ([capítulo 4](#cap-04)) | No boot |
| Item do menu do painel, com a permissão **Rede** no painel ([release]{.img}, [capítulo 11](#cap-11)) | Na hora |
| Comando `ap` no console ([capítulo 14](#cap-14)) | Na hora |
| Escada de reconexão, aparelho que **nunca** obteve IP desde que ligou | Na primeira dormência: cerca de 6 a 7 min depois de ligar, com uma rede que não existe (medido em bancada em 22/09/2026, durante o desenvolvimento da v2.7.1) |
| Escada de reconexão, aparelho que **já** obteve IP desde que ligou | Ao fim de uma volta inteira da escada: cerca de 68 min quando a rede não aparece nas buscas, pela aritmética da escada; menos, se a rede aparece e só a entrada falha |

As duas regras da escada são diferentes de propósito. Um aparelho que nunca entrou na rede depois de ligar provavelmente está com a rede errada: o roteador foi trocado, a senha mudou ou o aparelho mudou de lugar. Ele abre o AP logo. Um aparelho que estava funcionando e perdeu a rede provavelmente está diante de uma queda passageira, e abrir o AP cedo tiraria da rede, por 15 min, um aparelho que voltaria sozinho.

O log de eventos registra o motivo no contexto do evento 403: 0 para o comando `ap` ou o painel, 1 para o gesto no boot, 2 para aparelho sem rede configurada e 3 para a escada.

### Enquanto o AP está aberto {#cap-09-ap-aberto}

::: perigo
**Com o AP aberto, o aparelho não mede.** Enquanto o AP de configuração está no ar, o aparelho não lê os sensores, não confere os limites dos alarmes, não grava o histórico e não envia telemetria nem syslog. O painel fica na tela do AP, com a rede, a chave e o endereço. O LCD fica com os últimos valores lidos, que só se atualizam enquanto alguém usa a interface web pelo AP. Um alarme novo não soa.

Desde a v2.7.1 o AP também abre sozinho, e isso tem duas consequências:

- **Aparelho sem rede configurada** fica no AP desde o boot e não mede até alguém gravar uma rede. Não use a v2.7.1, a v2.7.2 nem a v2.7.3 num aparelho que vai funcionar sem Wi-Fi.
- **Aparelho que perde a rede** por mais de uma volta da escada (cerca de 68 min) passa a alternar 15 min no AP, sem medir, com cerca de 6 a 7 min medindo, até a rede voltar. Numa queda longa, ele fica sem medir cerca de dois terços do tempo.

Esses números vêm da leitura do código da v2.7.1 e não foram medidos no aparelho. O log de eventos mostra quando isso aconteceu: o evento 403 com contexto 2 ou 3 marca o início, e o 525 `AP mode timeout, rebooting to STA` marca o fim de cada período.
:::

- A telemetria e o syslog esperam a rede voltar; o que não foi medido não é recuperado.
- O aparelho não tenta a rede configurada enquanto o AP está aberto.
- Um AP aberto com o aparelho em operação (pela escada, pelo painel ou pelo comando `ap`), com uma rede configurada, dura 15 min. Depois disso o aparelho reinicia para tentar a rede de novo, com ou sem alguém conectado ao AP. O evento é **Timeout na conexão WiFi** (525) com o texto `AP mode timeout, rebooting to STA`. Se a rede ainda não estiver lá, o AP volta na primeira dormência, cerca de 6 a 7 min depois.
- Um AP aberto no boot, pelo gesto ou por falta de rede configurada, fica aberto até alguém gravar uma rede ou reiniciar o aparelho.

Para sair do AP com a rede certa:

1. Conecte o celular ou o computador à rede `<nome>_SETUP`, com a chave.
2. Abra `http://192.168.4.1` e entre com uma conta que tenha a permissão **Rede**.
3. Na página **Rede**, toque em **Buscar**, escolha a rede e digite a senha.
4. Toque em **Salvar e reiniciar**.

O aparelho reinicia e tenta a rede nova.

::: atencao
**HTTP ou HTTPS no AP.** Um aparelho que liga já no AP atende sempre em HTTP, mesmo com certificado instalado: é a porta de recuperação para um certificado com defeito. Um AP aberto com o aparelho já em operação, pela escada, pelo painel ou pelo comando `ap`, mantém o protocolo que o servidor web escolheu no boot. Numa release com certificado instalado, use `https://192.168.4.1`.
:::

[air]{.img} O Air não abre o AP sozinho, nem no boot sem rede configurada: o rádio dele só existe durante um despertar, e ninguém está diante de um aparelho hibernando para usar o AP. O comando `ap` abre o AP também no Air, mas o jeito indicado de configurar a rede do Air é o console, por USB ou Bluetooth ([capítulo 19](#cap-19)).

## mDNS: o endereço pelo nome {#cap-09-mdns}

[release]{.img}

Na imagem release, o aparelho responde pelo nome `<nome>.local` na rede local, como `http://simut.local`. O nome é o campo **Nome** da página **Configurações** ([capítulo 5](#cap-05-identidade)). Não há nada a configurar.

O aparelho também se anuncia como um serviço `_simut._tcp`, para que um programa descubra todos os aparelhos da rede sem saber os endereços. O anúncio leva a porta do servidor web e quatro campos de texto:

| Campo | Conteúdo |
|---|---|
| `uid` | Identidade do chip, 16 algarismos hexadecimais |
| `ver` | Versão do firmware, como `2.7.3` |
| `env` | Imagem: `release` |
| `tls` | `1` com HTTPS, `0` com HTTP |

Com HTTPS na porta padrão, o anúncio leva a porta 443.

O nome e o serviço passam a responder quando o aparelho termina de entrar na rede. Com o NTP ligado, isso inclui o primeiro acerto do relógio ([capítulo 10](#cap-10-sem-ntp)). Uma falha registra **Falha ao iniciar mDNS** (528).

Para procurar os aparelhos num computador com Linux:

```bash
avahi-browse -rt _simut._tcp
```

As imagens alpha e Air não têm mDNS. Use o endereço IP.

## HTTPS {#cap-09-https}

[release]{.img}

Só a imagem release tem servidor HTTPS. Sem certificado instalado, ela atende em HTTP, como as outras. Com um par de certificado e chave instalado, ela passa a atender só em HTTPS a partir do reinício seguinte.

### Como o HTTPS se comporta {#cap-09-https-comportamento}

- **Porta:** com a **Porta HTTP** em 80, o HTTPS atende na 443, e `https://192.0.2.10` funciona. Com outra porta configurada, o HTTPS atende nela.
- **Um protocolo só:** o endereço `http://` deixa de responder. O aparelho nunca serve os dois ao mesmo tempo.
- **Um cliente seguro por vez:** o aparelho atende uma conexão TLS por vez. As páginas enfileiram as próprias consultas ([capítulo 13](#cap-13-https)).
- **Protocolo e cifras:** só TLS 1.2, com troca de chaves ECDHE e cifra AES-GCM de 128 ou 256 bits. Um cliente que só fale TLS 1.1 ou menos, ou só cifras CBC ou ChaCha20, não conecta.
- **Sessão:** o cookie de sessão ganha a marca `Secure` ([capítulo 13](#cap-13-sessoes)).
- **AP:** ligado já no AP, o aparelho atende em HTTP ([Enquanto o AP está aberto](#cap-09-ap-aberto)).
- **Falha segura:** se o par está ausente, vazio, maior que 8 KB ou não pode ser lido, o aparelho sobe em HTTP na porta configurada e registra **Certificado web inválido (HTTP)** (576). Um certificado ruim nunca tranca você do lado de fora.

### Gerar um par autoassinado {#cap-09-gerar}

Gere um par para cada aparelho, num computador com OpenSSL 1.1.1 ou mais novo. Ponha no certificado os nomes pelos quais o aparelho será acessado: o IP e, se usar mDNS, o nome `.local`. Sem isso, o navegador trata o certificado como de nenhum endereço, mesmo depois de você confiar nele.

Com chave EC P-256, a recomendada:

```bash
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout web_key.pem -out web_cert.pem -days 3650 -nodes \
  -subj "/CN=simut" \
  -addext "subjectAltName=IP:192.0.2.10,DNS:simut.local"
```

Com chave RSA de 2048 bits:

```bash
openssl req -x509 -newkey rsa:2048 \
  -keyout web_key.pem -out web_cert.pem -days 3650 -nodes \
  -subj "/CN=simut" \
  -addext "subjectAltName=IP:192.0.2.10,DNS:simut.local"
```

Troque `192.0.2.10` e `simut` pelo IP e pelo nome do seu aparelho. `-days 3650` dá dez anos de validade.

O aparelho aceita:

- chave EC ou RSA; a EC precisa ser de uma curva que o firmware conheça, e a P-256 é a recomendada;
- a chave nos três formatos PEM: `PRIVATE KEY`, `EC PRIVATE KEY` e `RSA PRIVATE KEY`;
- uma cadeia de certificados de uma autoridade certificadora, com o certificado do aparelho primeiro.

A chave não pode ter senha. Para tirar a senha de uma chave:

```bash
openssl pkey -in key.pem -out plain_key.pem
```

### Instalar o par {#cap-09-instalar}

O par se instala pela rota `POST /api/tls`. Nenhuma página da interface web usa essa rota, e a página **Arquivos** recusa enviar qualquer arquivo para `/config`, onde o par fica. Há dois caminhos: o script do repositório ou o `curl`.

A rota exige uma conta de administrador completo, com todas as permissões. Outra conta recebe `403`.

**Pelo script** `tools/install_tls_cert.py`, que entra no aparelho, envia o par e, com `--reboot`, reinicia:

```bash
export SIMUT_WEB_USER=admin
read -rsp 'Senha do admin: ' SIMUT_WEB_PASS; echo; export SIMUT_WEB_PASS
python3 tools/install_tls_cert.py --host 192.0.2.10 \
  --cert web_cert.pem --key web_key.pem --reboot
```

**Pelo `curl`.** Primeiro abra uma sessão, depois envie os dois blocos PEM juntos no corpo do pedido, em qualquer ordem, com um tipo de conteúdo que não seja de formulário:

```bash
H=192.0.2.10
NONCE=$(curl -s -c jar http://$H/api/login_init | sed 's/.*"nonce":"\([^"]*\)".*/\1/')
HASH=$(printf '%s' 'sua-senha' | sha256sum | cut -d' ' -f1)
curl -s -b jar -c jar -X POST http://$H/api/login \
  --data-urlencode user=admin --data-urlencode pass=$HASH \
  --data-urlencode nonce=$NONCE
cat web_cert.pem web_key.pem | curl -s -b jar -X POST --data-binary @- \
  -H 'Content-Type: application/x-pem-file' http://$H/api/tls
```

A página de entrada envia a senha já resumida em SHA-256, e o `curl` precisa fazer o mesmo. Este exemplo vale para senhas só com caracteres ASCII. O processo de entrada está no [capítulo 26](#cap-26).

Com o par aceito, a resposta é:

```json
{"ok":true,"installed":true,"key":"EC (curve 23)","serving":"http","nextBoot":"https","rebootRequired":true,"note":"setup AP mode always serves HTTP"}
```

`key` descreve a chave: `EC (curve 23)` é a P-256, e uma chave RSA aparece como `RSA (2048 bits)`. `serving` é o protocolo em uso agora e `nextBoot`, o do próximo boot. O servidor em execução não muda: o par vale a partir do próximo reinício. Para reiniciar:

```bash
curl -s -b jar -X POST "http://$H/api/action?op=reboot"
```

Depois do reinício, abra `https://192.0.2.10`. O log de eventos registra **Par de certificados web instalado** (579) na instalação e, no boot seguinte, **Cert SSL carregado** (34) com o texto `HTTPS server (provisioned cert)` e a porta no contexto.

O aparelho confere o par antes de gravar qualquer coisa: ele deriva a chave pública da chave privada e a compara com a do certificado. Um par trocado é recusado agora, com o par anterior intacto, em vez de subir quebrado no próximo boot. A gravação usa arquivos temporários e só depois os renomeia, para que uma falha no meio não apague um par que funcionava.

Respostas de erro:

| Resposta | Motivo |
|---|---|
| `400` `empty body - send the certificate and key PEM blocks` | Corpo vazio. Confira o `Content-Type`: um corpo de formulário não chega à rota |
| `400` `no CERTIFICATE block in the body` | Falta o certificado |
| `400` `no PRIVATE KEY block in the body` | Falta a chave |
| `400` `the private key is passphrase-encrypted; decrypt it first: ...` | A chave tem senha |
| `400` `the key does not belong to this certificate` | Chave e certificado não são do mesmo par |
| `400` `certificate and key are on different curves` | Chave e certificado EC de curvas diferentes |
| `400` `this firmware's EC implementation does not know that curve` | Curva EC que o firmware não conhece. Use P-256 |
| `400` `unsupported RSA key size` | Chave RSA maior que 8192 bits |
| `403` `Forbidden — admin only` | A conta não é administrador completo |
| `413` `payload too large (8 KB)` | O corpo passou de 8.192 bytes |
| `429` `rate limited` | Menos de 5 s desde a tentativa anterior |
| `500` `could not write the pair to /config` | Falha de gravação. O par anterior continua lá |

Um par recusado registra **Certificado web inválido (HTTP)** (576) com o contexto 3.

### O que o navegador mostra {#cap-09-navegador}

Um certificado autoassinado não foi emitido por uma autoridade que o navegador conheça. No primeiro acesso, o navegador mostra uma página de aviso dizendo que a conexão não é particular ou que o certificado não é confiável. Para seguir:

1. Confira que o endereço é o do seu aparelho.
2. Abra os detalhes do aviso, geralmente em **Avançado**.
3. Escolha continuar para o endereço.

O navegador lembra a exceção por um tempo, conforme o navegador. Para não ver mais o aviso, importe o `web_cert.pem` como certificado confiável no sistema operacional ou no navegador de cada computador que acessa o aparelho. Isso só funciona se o certificado tiver o IP e o nome no campo `subjectAltName`.

::: {.figura #fig-09-aviso-certificado tipo="web" arquivo="09-aviso-certificado.png" captura="rota https://192.0.2.10/login; largura 1280; navegador Chrome sem a exceção aceita; certificado autoassinado recém-instalado; página de aviso do navegador"}
O aviso do navegador no primeiro acesso a um aparelho com certificado autoassinado.
:::

### Voltar para HTTP {#cap-09-https-off}

Para desligar o HTTPS, use o console serial ([capítulo 14](#cap-14)):

```text
system https off confirm
```

O aparelho apaga o par, registra `HTTPS disabled via serial (cert deleted)` com o código 303 e reinicia em HTTP. O comando existe também no console de emergência da release. Ele é a saída quando um par entra, mas toda negociação TLS falha e a interface fica inalcançável.

Depois de voltar para HTTP, o primeiro acesso pelo mesmo navegador pode voltar direto para a página de entrada, por causa do cookie `Secure` antigo. A página avisa e diz o que fazer ([capítulo 13](#cap-13-sessao-segura)).

### O par e a atualização de firmware {#cap-09-https-ota}

A atualização de firmware reformata o sistema de arquivos e só preserva a configuração principal (`/config/system.bin`). O par de certificados não é preservado pela atualização: depois dela, o aparelho volta a atender em HTTP.

O backup `.bkp`, que a página **Arquivos** baixa antes de atualizar, contém todos os arquivos, inclusive o par. Para voltar ao HTTPS depois de atualizar, restaure esse backup ou instale o par de novo ([capítulo 17](#cap-17)).

O comando `system format` também apaga o par. A chave privada nunca é servida pela rota de download de arquivos.

## CORS para o gestor de frota {#cap-09-cors}

O gestor de frota é uma página que roda no navegador do operador e fala com vários aparelhos ao mesmo tempo ([capítulo 27](#cap-27)). O navegador bloqueia essas chamadas, a menos que o aparelho autorize a origem da página: é o CORS.

A origem se configura pelo console serial, inclusive no console de emergência da release:

```text
system cors http://gestor.exemplo.com.br:8080
reload confirm
```

A origem vale depois do reinício. Para desligar:

```text
system cors off
reload confirm
```

Regras da origem:

- começa com `http://` ou `https://`;
- tem só o nome ou IP e, opcionalmente, `:porta`;
- não tem barra no fim, caminho nem `*`;
- tem no máximo 64 caracteres.

A origem fica em `/config/cors.txt`. No boot, o aparelho registra **CORS ligado para a origem** (577) ou, se o arquivo for inválido, **Origem CORS inválida (CORS desligado)** (578). Um integrador também pode gravar a origem pela chave `cors` da seção `sys` de `POST /api/commit_all`; ela também só vale depois do reinício.

## Referência rápida {#cap-09-referencia}

Os campos da página **Rede**, na seção `net` de `POST /api/commit_all`:

| Chave | Campo | Faixa | Fábrica | Grupo |
|---|---|---|---|---|
| `ssid` | **SSID** | 1 a 31 caracteres; vazio mantém | vazio | `net`, reinicia |
| `pass` | **Senha** | até 31 caracteres; vazio mantém | vazio | `net`, reinicia |
| `use_dhcp` | **DHCP** | `1` ou `0` | `1` | `net`, reinicia |
| `ip` | **IP Estático** | IPv4; só com DHCP desligado | `192.168.1.100` | `net`, reinicia |
| `mask` | **Máscara** | IPv4; só com DHCP desligado | `255.255.255.0` | `net`, reinicia |
| `gw` | **Gateway** | IPv4; só com DHCP desligado | `192.168.1.1` | `net`, reinicia |
| `dns_auto` | **Obter DNS automaticamente (DHCP)** | `1` ou `0` | `1` | `web`, reinicia |
| `dns1` | **DNS Primário** | IPv4 | `8.8.8.8` | `net`, reinicia |
| `dns2` | **DNS Secundário** | IPv4 ou vazio | vazio | `web`, reinicia |
| `ntp_server` | **Endereço do Servidor** | até 31 caracteres; vazio = `pool.ntp.org` | vazio | `time`, reinicia |
| `web_port` | **Porta HTTP** | 1 a 65535 | 80 | `web`, reinicia |
| `web_ka` | **Conexões persistentes (keep-alive)** | `1` ou `0` | `1` | `web`, reinicia |

Os limites de sinal que o aparelho usa:

| Limite | Efeito |
|---|---|
| −78 dBm ou abaixo | A telemetria e os envios pesados esperam o sinal melhorar ([capítulo 21](#cap-21)); a métrica `simut_wifi_connected` fica em 0 ([capítulo 24](#cap-24)). As leituras e o painel continuam |
| 0 dBm ou mais, ou abaixo de −120 dBm | Leitura impossível. Duas seguidas, com um minuto entre elas, forçam uma reconexão |
| Sem conexão | O sinal é informado como −100 dBm |

As barras de sinal do painel usam limites próprios ([capítulo 11](#cap-11)), e o ícone do LCD, os seus ([capítulo 12](#cap-12)).
