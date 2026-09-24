# Primeiro boot e primeira configuração {#cap-04}

Este capítulo leva um aparelho recém-gravado do primeiro boot até o uso: a senha inicial, a entrada na rede, a primeira entrada na web, o idioma, a hora, o primeiro sensor e o PIN do painel. É para quem instala. Cada passo é curto e aponta para o capítulo que traz os detalhes.

## O caminho em resumo {#cap-04-resumo}

1. Abra o console USB e ligue o aparelho: anote a senha do administrador ([A senha inicial](#cap-04-senha)).
2. Ponha o aparelho na rede, pelo ponto de acesso de configuração ou pelo console ([Pôr o aparelho na rede](#cap-04-rede)).
3. Entre na interface web e troque a senha ([A primeira entrada na web](#cap-04-web)).
4. Instale o pacote de idioma e confira o fuso e a hora ([Idioma, fuso e hora](#cap-04-idioma)).
5. Configure o primeiro sensor ([O primeiro sensor](#cap-04-sensor)).
6. [release]{.img} Troque o PIN de fábrica do painel ([O PIN do painel](#cap-04-pin)).
7. Confira a lista final ([Pronto para usar](#cap-04-pronto)).

::: {.figura #fig-04-fluxo tipo="diagrama" arquivo="04-fluxo.png" captura="fluxograma do primeiro boot: 'liga com o console USB aberto' → 'anota a senha do admin'; depois dois ramos: release e alpha 'AP simut_SETUP → 192.168.4.1 → troca de senha → Rede → Salvar e reiniciar'; Air 'console: enable, configure terminal, system ssid, system pass, reload confirm'; os ramos se juntam em 'aparelho na rede, começa a medir' → 'pacote de idioma e reinício' → 'fuso e hora' → 'primeiro sensor' → 'PIN do painel (release)' → 'pronto para usar'"}
O primeiro boot em um só desenho: a senha, a rede, a web, o idioma, os sensores e o PIN.
:::

## Antes de ligar {#cap-04-antes}

Tenha à mão:

- o aparelho com o firmware gravado ([capítulo 3](#cap-03)) e a montagem conferida ([capítulo 2](#cap-02-conferencia));
- um computador com um programa de comunicação serial, como o `picocom` ou o PuTTY, e o cabo USB. O console usa 115200 baud, 8N1 ([capítulo 14](#cap-14-usb));
- [release]{.img} [alpha]{.img} um celular ou computador com Wi-Fi, para entrar no ponto de acesso de configuração;
- o nome e a senha da rede Wi-Fi do local. O aparelho só usa redes de 2,4 GHz, e o nome e a senha têm até 31 caracteres ([capítulo 9](#cap-09-wifi)).

## O que o boot mostra {#cap-04-boot}

Um aparelho novo não tem rede configurada. A release e a alpha ligam direto no ponto de acesso de configuração; o Air liga acordado e espera ordens pelo console.

::: atencao
**Sem rede, o aparelho não mede.** [release]{.img} [alpha]{.img} Enquanto o ponto de acesso de configuração está aberto, o aparelho não lê os sensores nem grava o histórico, e um ponto de acesso aberto no boot não fecha sozinho. O aparelho só começa a medir depois de entrar numa rede ([capítulo 9](#cap-09-ap-aberto)).
:::

### No painel

[release]{.img} Depois de ligar, o painel leva alguns segundos para mostrar as etapas do boot: cerca de 7,5 s, medidos em bancada em 22/09/2026, durante o desenvolvimento da v2.7.1. Em seguida aparecem `SIMUT`, a versão e a caixa com as etapas ([capítulo 11](#cap-11-boot)).

Num aparelho novo, a caixa termina assim:

- o nome real da rede do ponto de acesso, `simut_SETUP`;
- `PSK` seguido da chave da rede, de 10 caracteres;
- **AP Ativo! Reinicie a placa para sair.**

O painel fica nessa tela até o próximo reinício: a chave continua visível enquanto o ponto de acesso estiver aberto. Uma linha anterior da caixa, **Conecte à rede SIMUT_SETUP**, tem um nome fixo; a rede de verdade é a da linha que traz só o nome, `simut_SETUP` de fábrica.

::: {.figura #fig-04-painel-ap tipo="tft" arquivo="04-painel-ap.png" captura="release recém-gravada, sem rede configurada, depois do boot; a caixa de boot termina com simut_SETUP, PSK e a chave, e AP Ativo! Reinicie a placa para sair.; capturar com GET /api/screenshot pelo próprio AP, com sessão admin depois da troca de senha; se a captura não funcionar no AP, use foto da tela; borre a chave antes de publicar"}
O painel de um aparelho novo: a rede do ponto de acesso, a chave e o aviso de AP ativo ficam na tela.
:::

### No LCD

[alpha]{.img} O LCD mostra `SIMUT 2.7.2` e `Inicializando` e, depois, uma barra de progresso ([capítulo 12](#cap-12-boot)).

Num aparelho novo, pelo código da v2.7.2, o LCD fica parado em `SIMUT 2.7.2` com a barra cheia e não mostra as páginas do ponto de acesso. Esse comportamento vem da leitura do código e não foi observado em bancada, que não tem LCD. Pegue o nome da rede e a chave no console, como em [Pôr o aparelho na rede](#cap-04-rede).

```text
SIMUT 2.7.2
[##############]
```

::: {.figura #fig-04-lcd-ap tipo="lcd" arquivo="04-lcd-ap.png" captura="alpha recém-gravada, sem rede configurada, depois do boot com o ponto de acesso aberto; linha de cima SIMUT 2.7.2, linha de baixo [##############] com a barra cheia"}
O LCD de uma alpha nova com o ponto de acesso aberto: a tela de boot fica parada, e a chave está no console.
:::

### No Air

[air]{.img} O Air não tem tela. Ao ligar, ele fica acordado (M0), com o console completo pelo USB e pelo Bluetooth e com a interface web. Ele não abre o ponto de acesso sozinho. Sem nenhuma atividade no console ou na web por 5 min, ele começa a hibernar ([capítulo 19](#cap-19)).

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

[release]{.img} [alpha]{.img} O ponto de acesso é a rede Wi-Fi que o aparelho cria para você configurá-lo. Os detalhes estão no [capítulo 9](#cap-09-ap).

1. Descubra o nome da rede e a chave. O console USB mostra os dois quando o ponto de acesso abre:

   ```text
   [AP] SSID: simut_SETUP
   [AP] PSK : M7TQ4XH2RP   (WPA2)
   [AP] URL : http://192.168.4.1
   ```

   Na release, a tela de boot também os mostra. O comando `ap` do console responde com a chave (a rede é o nome do aparelho seguido de `_SETUP`); na alpha, ele funciona também pelo Bluetooth ([capítulo 14](#cap-14-ap)).
2. No celular ou no computador, conecte-se à rede `simut_SETUP` com a chave.
3. Abra `http://192.168.4.1`. A página de entrada aparece.
4. Entre como `admin`, com a senha inicial.
5. Defina a senha nova na página **Bem-vindo!** ([capítulo 13](#cap-13-troca-obrigatoria)).
6. Abra a página **Rede**, toque em **Buscar**, escolha a sua rede e digite a senha dela ([capítulo 9](#cap-09-busca)).
7. Toque em **Salvar e reiniciar** na barra de topo.
8. Volte o celular ou o computador para a sua rede.

O aparelho reinicia e entra na rede. Se a senha da rede estiver errada, o ponto de acesso volta sozinho cerca de 6 a 7 min depois do boot ([capítulo 9](#cap-09-ap-quando)).

A chave do ponto de acesso é sempre a mesma para aquele aparelho, mesmo depois de um reset de fábrica. Anote-a: ela serve toda vez que o aparelho precisar do ponto de acesso.

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
**Sem espaços no console.** O console grava só até o primeiro espaço: `system ssid Minha Rede` grava `Minha`. Uma rede ou uma senha com espaço se grava pela página **Rede**, pelo ponto de acesso. No Air, que não abre o ponto de acesso sozinho, use `enable` e `ap` e siga os passos do ponto de acesso.
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
3. **Hora.** O NTP vem ligado e acerta o relógio pela internet. Numa rede sem acesso à internet, aponte o aparelho para um servidor de hora da própria rede ou acerte o relógio à mão: sem o primeiro acerto, o aparelho não termina de entrar na rede, e a telemetria espera ([capítulo 10](#cap-10-sem-ntp)).

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
5. A hora está certa e o fuso é o do local ([capítulo 10](#cap-10)).
6. Há um só pacote de idioma na pasta `/lang` ([capítulo 3](#cap-03-idioma)).
7. Cada sensor aparece no **Painel de Controle** com leituras plausíveis ([capítulo 6](#cap-06)).
8. Os limites de alarme de cada sensor estão definidos ([capítulo 7](#cap-07)).
9. [release]{.img} O PIN do `admin` não é mais `1234`, e cada operador tem a sua conta com PIN ([capítulo 8](#cap-08)).
10. A telemetria e a linha de alarmes estão configuradas, se houver servidor ([capítulo 21](#cap-21), [capítulo 22](#cap-22)).
11. Um backup da configuração foi baixado ([capítulo 17](#cap-17-backup)).
12. [air]{.img} O ciclo de hibernação foi conferido ([capítulo 19](#cap-19)).
