# Primeiro boot e primeira configuração {#cap-04}

Este capítulo leva um aparelho recém-gravado do primeiro boot até o uso: a senha inicial, a entrada na rede, a primeira entrada na web, o idioma, a hora, o primeiro sensor e o PIN do painel. É para quem instala. Cada passo é curto e aponta para o capítulo que traz os detalhes.

## O caminho em resumo {#cap-04-resumo}

1. Abra o console USB e ligue o aparelho: anote a senha do administrador ([A senha inicial](#cap-04-senha)). [release]{.img} No fim do boot, o painel pergunta a data e a hora ([O que o boot mostra](#cap-04-boot)).
2. Ponha o aparelho na rede: abra o ponto de acesso de configuração com o comando `ap` ou pelo menu do painel, ou grave a rede pelo console ([Pôr o aparelho na rede](#cap-04-rede)).
3. Entre na interface web e troque a senha ([A primeira entrada na web](#cap-04-web)).
4. Instale o pacote de idioma e confira o fuso e a hora ([Idioma, fuso e hora](#cap-04-idioma)).
5. Configure o primeiro sensor ([O primeiro sensor](#cap-04-sensor)).
6. [release]{.img} Troque o PIN de fábrica do painel ([O PIN do painel](#cap-04-pin)).
7. Confira a lista final ([Pronto para usar](#cap-04-pronto)).

::: {.figura #fig-04-fluxo tipo="diagrama" arquivo="04-fluxo.png" captura="fluxograma do primeiro boot: 'liga com o console USB aberto' → 'anota a senha do admin' → na release, 'o painel pergunta a data e a hora: SALVAR ou PULAR'; depois dois ramos: release e alpha 'ap no console (na release, também o item 12 do menu) → AP simut_SETUP → 192.168.4.1 → troca de senha → Rede → Salvar e reiniciar'; Air 'console: enable, configure terminal, system ssid, system pass, reload confirm'; os ramos se juntam em 'aparelho na rede' → 'pacote de idioma e reinício' → 'fuso e hora' → 'primeiro sensor' → 'PIN do painel (release)' → 'pronto para usar'"}
O primeiro boot em um só desenho: a senha, a hora, a rede, a web, o idioma, os sensores e o PIN.
:::

## Antes de ligar {#cap-04-antes}

Tenha à mão:

- o aparelho com o firmware gravado ([capítulo 3](#cap-03)) e a montagem conferida ([capítulo 2](#cap-02-conferencia));
- um computador com um programa de comunicação serial, como o `picocom` ou o PuTTY, e o cabo USB. O console usa 115200 baud, 8N1 ([capítulo 14](#cap-14-usb));
- [release]{.img} [alpha]{.img} um celular ou computador com Wi-Fi, para entrar no ponto de acesso de configuração;
- o nome e a senha da rede Wi-Fi do local. O aparelho só usa redes de 2,4 GHz, e o nome e a senha têm até 31 caracteres ([capítulo 9](#cap-09-wifi)).

## O que o boot mostra {#cap-04-boot}

Um aparelho novo não tem rede configurada, e nenhuma imagem abre o ponto de acesso de configuração sozinha: ele abre quando você pede ([Pôr o aparelho na rede](#cap-04-rede)). Sem rede também não há NTP, e o relógio em uso é o provisório, que parte de uma data gravada no firmware ([capítulo 10](#cap-10-provisorio)). Por isso, no fim do boot, um aparelho sem rede configurada pede a data e a hora: a release, no painel; todas as imagens, numa linha do console USB:

```text
Sem rede, relogio provisorio: conf time AAAA-MM-DD HH:MM:SS
```

O comando que a linha cita funciona no console de todas as imagens, pelo USB ou pelo Bluetooth ([capítulo 10](#cap-10-manual)). Sem rede configurada, o boot não espera a rede: segue na hora, e a pergunta aparece logo no fim dele. O aparelho mede e grava o histórico com ou sem rede.

::: atencao
**Da v2.7.1 à v2.8.0, um aparelho novo não media.** [release]{.img} [alpha]{.img} Nessas versões, um aparelho sem rede configurada ligava direto no ponto de acesso de configuração e, com ele aberto, não lia os sensores nem gravava o histórico, e esse ponto de acesso não fechava sozinho. Atualize o firmware antes de instalar um aparelho que vai funcionar sem Wi-Fi ([capítulo 9](#cap-09-ap-aberto)).
:::

### No painel

[release]{.img} Depois de ligar, o painel leva alguns segundos para mostrar as etapas do boot: cerca de 7,5 s, medidos em bancada em 22/09/2026, durante o desenvolvimento da v2.7.1. Em seguida aparecem `SIMUT`, a versão e a caixa com as etapas ([capítulo 11](#cap-11-boot)). Sem rede configurada, a caixa não espera o roteador: mostra **Conexão Ignorada pelo Usuário.** e segue.

No fim do boot, num aparelho sem rede configurada, o painel abre a tela **Data e hora** em vez da tela inicial:

- cinco colunas, `dd / mm / aaaa  hh : mm`, que começam na data e na hora do relógio provisório;
- acima de cada valor, uma seta que soma 1; abaixo, uma seta que subtrai 1. Segurada, a seta repete a cada 300 ms;
- a linha **Sem Wi-Fi, os dados levam esta hora.**, em âmbar;
- os botões **PULAR** e **SALVAR**.

Ajuste a data e a hora do local e toque em **SALVAR**: o relógio passa a valer na hora, e o painel vai à tela inicial. **PULAR** vai à tela inicial sem mudar o relógio. Sem nenhum toque por 30 s, o painel faz o mesmo que **PULAR**: um aparelho que volta de uma falta de energia sem ninguém por perto mostra as leituras, não uma pergunta.

A pergunta não pede conta nem PIN, e volta a cada boot enquanto o aparelho não tiver rede: o acerto à mão não sobrevive a um reinício. Enquanto o relógio for o provisório, a barra de cima da tela inicial mostra a data e a hora em âmbar, com `?` no lugar do `-`. Tudo isso está no [capítulo 10](#cap-10-painel).

::: {.figura #fig-04-painel-data-hora tipo="tft" arquivo="04-painel-data-hora.png" captura="release recém-gravada, sem rede configurada, logo depois do boot; tela Data e hora com as cinco colunas e as setas, a linha Sem Wi-Fi, os dados levam esta hora. em âmbar e os botões PULAR e SALVAR; sem rede, GET /api/screenshot não alcança o aparelho: use foto da tela, antes dos 30 s sem toque que levam o painel à tela inicial"}
A pergunta do fim do boot num aparelho sem rede: a data e a hora do relógio provisório, prontas para acertar.
:::

### No LCD

[alpha]{.img} O LCD mostra `SIMUT 2.11.0` e `Inicializando` e, depois, uma barra de progresso ([capítulo 12](#cap-12-boot)).

Num aparelho novo, o boot não espera a rede: no fim dele, o LCD mostra `Sem WiFi` e `Offline` por 3 s e passa às leituras ([capítulo 12](#cap-12-rede)):

```text
 Sem WiFi
    Offline
```

O LCD não mostra a data e a hora, e a pergunta sai só no console. No alpha, o console é o jeito de respondê-la: digite o comando que a linha cita, com a data e a hora locais, pelo console USB ou pelo Bluetooth:

```text
SIMUT> conf time 2026-10-01 14:30:00
OK: Hora aplicada (imediato, nao persiste em reboot)
```

A interface web também acerta o relógio, por um ponto de acesso aberto com `ap` ([capítulo 10](#cap-10-manual)).

### No Air

[air]{.img} O Air não tem tela. Ao ligar, ele fica acordado (M0), com o console completo pelo USB e pelo Bluetooth e com a interface web. Como as outras imagens, ele não abre o ponto de acesso sozinho. Sem rede configurada, ele escreve no console a mesma linha do relógio provisório, e o comando `time` acerta o relógio ([capítulo 10](#cap-10-manual)). Sem nenhuma atividade no console ou na web por 5 min, ele começa a hibernar ([capítulo 19](#cap-19)).

## A senha inicial do administrador {#cap-04-senha}

Na primeira partida, o aparelho cria a conta `admin` com uma senha aleatória de 8 caracteres, feita de letras maiúsculas e algarismos, sem `O`, `0`, `I` e `1`. Ele mostra a senha **uma única vez**, no console USB:

```text
==============================================
 SEC-003: FACTORY DEFAULTS ATIVADO
 Senha ADMIN inicial: R8TNC3VH
 Trocar no primeiro login (forcado).
==============================================
```

A senha do exemplo é ilustrativa. A moldura sai sempre em português, no meio das linhas de diagnóstico do boot, que começam com `[DBG]`.

- **Nenhuma tela mostra a senha.** O painel, o LCD e a interface web não a exibem. Só o console USB.
- **Ela não volta a aparecer.** O aparelho não guarda a senha em texto: depois do primeiro reinício, a moldura não sai mais, e a senha continua valendo.
- **Abra o programa serial cedo.** O aparelho espera 1 s pelo USB antes de escrever no console. Abra a porta assim que ela aparecer no computador.

Se você não viu a senha, gere outra pelo cabo USB ([capítulo 14](#cap-14-admin-reset)):

```text
SIMUT> system admin reset confirm
Senha admin resetada. Nova senha (unica vez):
 K4XW7PQA
Trocar no 1o login via web (forcado).
```

[air]{.img} No Air, entre antes no modo privilegiado com `enable`. O comando não funciona pelo Bluetooth.

A conta `viewer`, a outra conta de fábrica, não tem senha conhecida. Gere uma senha para ela ou exclua a conta ([capítulo 8](#cap-08-fabrica)).

## Pôr o aparelho na rede {#cap-04-rede}

### Pelo ponto de acesso de configuração

[release]{.img} [alpha]{.img} O ponto de acesso é a rede Wi-Fi que o aparelho cria para você configurá-lo. Ele só abre quando você pede. Os detalhes estão no [capítulo 9](#cap-09-ap).

1. Abra o ponto de acesso. No console USB, que você já abriu para a senha, digite `ap`. A resposta traz a chave, e o console mostra também o nome da rede:

   ```text
   SIMUT> ap

   [AP] SSID: simut_SETUP
   [AP] PSK : M7TQ4XH2RP   (WPA2)
   [AP] URL : http://192.168.4.1
   OK: Modo AP iniciado (WPA2). Senha: M7TQ4XH2RP
   Conecte-se ao AP e acesse http://192.168.4.1
   ```

   Na release, o item 8 do menu do painel, **Modo de Configuração**, faz o mesmo, com uma conta que tenha a permissão **Rede** no painel, e o painel passa a mostrar a rede e a chave ([capítulo 11](#cap-11-ap)). Na alpha, o `ap` funciona também pelo Bluetooth, e o LCD alterna o endereço, a rede e a chave ([capítulo 12](#cap-12-ap)).
2. No celular ou no computador, conecte-se à rede `simut_SETUP` com a chave.
3. Abra `http://192.168.4.1`. A página de entrada aparece.
4. Entre como `admin`, com a senha inicial.
5. Defina a senha nova na página **Bem-vindo!** ([capítulo 13](#cap-13-troca-obrigatoria)).
6. Abra a página **Rede**, toque em **Buscar**, escolha a sua rede e digite a senha dela ([capítulo 9](#cap-09-busca)).
7. Toque em **Salvar e reiniciar** na barra de topo.
8. Volte o celular ou o computador para a sua rede.

O aparelho reinicia e entra na rede. Se a senha da rede estiver errada, o aparelho fica tentando a rede pela escada de reconexão, e o ponto de acesso não volta sozinho: abra-o de novo e refaça a configuração ([capítulo 9](#cap-09-ap-quando)). Sem rede configurada, o ponto de acesso fica aberto até você gravar uma rede ou reiniciar o aparelho.

A chave do ponto de acesso é sempre a mesma para aquele aparelho, mesmo depois de um reset de fábrica. Anote-a: ela serve toda vez que o aparelho precisar do ponto de acesso.

Com o ponto de acesso aberto, o painel da release passa ao terminal da tela de boot e mostra o nome real da rede, `simut_SETUP` de fábrica, `PSK` seguido da chave, **Acesse no celular: 192.168.4.1** e **AP Ativo! Reinicie a placa para sair.** O LCD do alpha alterna três páginas, com o endereço, a rede e a chave. O aparelho continua medindo com o ponto de acesso aberto ([capítulo 9](#cap-09-ap-aberto)).

::: {.figura #fig-04-painel-ap tipo="tft" arquivo="04-painel-ap.png" captura="release recém-gravada, sem rede configurada; ponto de acesso aberto pelo menu (CFG, admin, PIN, item 8 Modo de Configuração, Confirmar) ou pelo comando ap no console USB; a caixa termina com simut_SETUP, PSK e a chave, Acesse no celular: 192.168.4.1 e AP Ativo! Reinicie a placa para sair.; capturar com GET /api/screenshot pelo próprio AP, com sessão admin depois da troca de senha; se a captura não funcionar no AP, use foto da tela; borre a chave antes de publicar"}
O painel com o ponto de acesso aberto: a rede, a chave e o aviso de AP ativo ficam na tela.
:::

::: {.figura #fig-04-lcd-ap tipo="lcd" arquivo="04-lcd-ap.png" captura="alpha recém-gravada, sem rede configurada; ponto de acesso aberto pelo comando ap no console USB, com o aparelho já nas leituras; página Rede: 2 de 3 com simut_SETUP na linha de baixo"}
O LCD de uma alpha com o ponto de acesso aberto pelo comando `ap`: a página com o nome da rede.
:::

### Pelo console

Sem celular à mão, grave a rede pelo console USB.

[release]{.img} [alpha]{.img} No console de emergência:

```text
SIMUT> system ssid MinhaRede
OK: SSID salvo. Use 'reload confirm' para reconectar.
SIMUT> system pass senha-da-rede
OK: Senha salva. Use 'reload confirm' para reconectar.
SIMUT> reload confirm
```

[air]{.img} No console completo do Air:

```text
SIMUT> enable
SIMUT# configure terminal
SIMUT(config)# system ssid MinhaRede
SIMUT(config)# system pass senha-da-rede
SIMUT(config)# end
SIMUT# reload confirm
```

::: atencao
**Sem espaços no console.** O console grava só até o primeiro espaço: `system ssid Minha Rede` grava `Minha`. Uma rede ou uma senha com espaço se grava pela página **Rede**, pelo ponto de acesso. No Air, use `enable` e `ap` e siga os passos do ponto de acesso.
:::

[air]{.img} O Air mede e grava o histórico mesmo sem rede. A rede serve para a telemetria e para a hora certa ([capítulo 19](#cap-19)).

## Encontrar o endereço do aparelho {#cap-04-endereco}

Na sua rede, o aparelho recebe um endereço IP do roteador. Para descobri-lo:

| Onde | Como |
|---|---|
| Console USB, todas as imagens | `show net status`: a linha `IP:` ([capítulo 14](#cap-14-show-net)) |
| [alpha]{.img} LCD | `Conectado!` e o endereço, por 3 s, no fim do boot ([capítulo 12](#cap-12-rede)) |
| [release]{.img} Nome na rede | `http://simut.local`, com o nome do aparelho seguido de `.local` ([capítulo 9](#cap-09-mdns)) |
| [release]{.img} Painel | **Configurações > Status do Sistema**, página 2, linha `IP` ([capítulo 11](#cap-11-status)) |
| Roteador | A lista de aparelhos conectados do roteador |

Com o NTP ligado e sem servidor de hora alcançável, o nome `.local` não responde ([Idioma, fuso e hora](#cap-04-idioma)).

## A primeira entrada na web {#cap-04-web}

1. No navegador, abra o endereço do aparelho, como `http://192.0.2.10`.
2. Entre como `admin`.
3. Se você ainda não trocou a senha inicial, a página **Bem-vindo!** pede a senha nova: pelo menos 8 caracteres, com letra, algarismo e símbolo ([capítulo 13](#cap-13-troca-obrigatoria)).

Sem pacote de idioma, a interface aparece em inglês. O resto da interface está no [capítulo 13](#cap-13).

## Idioma, fuso e hora {#cap-04-idioma}

1. **Idioma.** Envie `language_pt-BR.lng` para a pasta `/lang`, pela página **Arquivos**, e reinicie o aparelho ([capítulo 3](#cap-03-idioma)). O idioma de fábrica do aparelho é o português: depois do reinício com o pacote na pasta, o painel e a interface web ficam em português. O console já responde em português. [release]{.img} O idioma do painel muda em **Configurações > Idioma** ([capítulo 11](#cap-11-idioma)); o da web, no seletor de cada navegador ([capítulo 13](#cap-13-idioma)).
2. **Fuso.** O de fábrica é −3, o de Brasília. Para outro, mude o **Fuso Horário** na página **Configurações**, seção **Identidade**, e toque em **Salvar e reiniciar** ([capítulo 10](#cap-10-fuso)).
3. **Hora.** O NTP vem ligado e acerta o relógio pela internet. Numa rede sem acesso à internet, aponte o aparelho para um servidor de hora da própria rede ou acerte o relógio à mão: sem o primeiro acerto, o aparelho não termina de entrar na rede, e a telemetria espera ([capítulo 10](#cap-10-sem-ntp)). Num aparelho que vai funcionar sem Wi-Fi, acerte a hora à mão depois de cada boot: em qualquer imagem, com o comando `time` no console; na release, também pela pergunta do boot ou pelo item 13 do menu, **Data e hora** ([capítulo 10](#cap-10-manual)).

## O primeiro sensor {#cap-04-sensor}

1. Abra **Configurações** e role até **Sensores e GPIO**.
2. Toque em **+ Adicionar slot de sensor**.
3. Escolha o tipo, o GPIO de cada pino, o **ID de Hardware** e o **Nome**, e ligue **Ativo**.
4. Toque em **Concluído** e depois em **Salvar e reiniciar**.

O passo a passo completo, com a busca de sondas e a calibração, está no [capítulo 6](#cap-06-adicionar). O sensor aparece no **Painel de Controle** depois das primeiras leituras.

[release]{.img} O mínimo e o máximo do cartão no painel dependem de o número do slot coincidir com o do GPIO. Veja a atenção de [Os cartões](#cap-11-cartoes) antes de escolher os pinos.

[air]{.img} No Air, configure os sensores pela web com o aparelho acordado ou pelos comandos `sensor` do console ([capítulo 14](#cap-14-sensor)).

## O PIN do painel {#cap-04-pin}

[release]{.img} O painel não usa senha: cada pessoa se identifica com a conta e um PIN. A conta `admin` sai de fábrica com o PIN `1234`, que precisa ser trocado no primeiro uso ([capítulo 8](#cap-08-pin-fabrica)).

1. Na tela inicial, toque em **CFG**, na barra de botões de baixo, logo depois do último sensor ([capítulo 11](#cap-11-rodape)).
2. Na tela **Quem está usando o painel?**, toque em `admin` e depois em **ENTRAR**.
3. Digite `1234` e toque em **ENTRAR**.
4. O painel abre a tela **Novo PIN**. Digite o PIN novo e toque em **ENTRAR**.

::: {.figura #fig-04-novo-pin tipo="tft" arquivo="04-novo-pin.png" captura="release com configuração de fábrica; tela inicial → botão de configurações → admin → PIN 1234 → ENTRAR; tela Novo PIN aberta, antes de digitar"}
A troca obrigatória do PIN de fábrica: a tela Novo PIN abre direto depois do 1234.
:::

O tamanho mínimo e os caracteres do PIN seguem a política de PIN ([capítulo 8](#cap-08-politica)). Para cada operador, crie uma conta com PIN e só as permissões de que ele precisa ([capítulo 8](#cap-08-criar)).

## Pronto para usar {#cap-04-pronto}

Confira antes de entregar o aparelho:

1. A senha do `admin` foi trocada e está guardada em lugar seguro.
2. A conta `viewer` tem senha nova ou foi excluída ([capítulo 8](#cap-08-fabrica)).
3. O aparelho está na rede, e você sabe o endereço dele ([Encontrar o endereço](#cap-04-endereco)).
4. A chave do ponto de acesso está anotada ([capítulo 9](#cap-09-ap)).
5. A hora está certa e o fuso é o do local ([capítulo 10](#cap-10)). [release]{.img} No painel, a data e a hora da barra de cima não têm o `?` do relógio provisório.
6. Há um só pacote de idioma na pasta `/lang` ([capítulo 3](#cap-03-idioma)).
7. Cada sensor aparece no **Painel de Controle** com leituras plausíveis ([capítulo 6](#cap-06)).
8. Os limites de alarme de cada sensor estão definidos ([capítulo 7](#cap-07)).
9. [release]{.img} O PIN do `admin` não é mais `1234`, e cada operador tem a sua conta com PIN ([capítulo 8](#cap-08)).
10. A telemetria e a linha de alarmes estão configuradas, se houver servidor ([capítulo 21](#cap-21), [capítulo 22](#cap-22)).
11. Um backup da configuração foi baixado ([capítulo 17](#cap-17-backup)).
12. [air]{.img} O ciclo de hibernação foi conferido ([capítulo 19](#cap-19)).
