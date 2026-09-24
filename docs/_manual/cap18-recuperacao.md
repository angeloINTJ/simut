# Recuperação {#cap-18}

Este capítulo diz o que fazer quando o aparelho não responde como deveria: a interface web não abre, ninguém sabe a senha, o painel travou, a rede de configuração não funciona, a imagem é a errada, o firmware ficou pela metade ou o sistema de arquivos corrompeu. Cada seção começa pelo sintoma. É para quem está diante do aparelho com um computador e um cabo USB.

## Antes de precisar {#cap-18-antes}

Quatro coisas tornam qualquer recuperação curta. Tenha-as antes de instalar o aparelho:

- **Um backup recente,** o `.bkp` da página **Arquivos** ([capítulo 17](#cap-17-backup)).
- **O `.uf2` da versão instalada,** publicado junto de cada versão como `simut_v<versão>_release.uf2`, `_alpha.uf2` ou `_air.uf2`. Guarde também o da versão anterior.
- **A chave da rede de configuração,** que é própria de cada aparelho ([capítulo 9](#cap-09-ap)).
- **Um cabo USB de dados e um programa de terminal serial** no computador ([capítulo 14](#cap-14-usb)).

O console USB é o caminho de maior confiança. Os comandos que devolvem o acesso ao aparelho, `system admin reset`, `system factory`, `system format` e `system https off`, só funcionam por ele: o Bluetooth os recusa ([capítulo 29](#cap-29-caminhos)). No Air, que tem o console completo, entre antes no modo privilegiado com `enable`.

## Por sintoma {#cap-18-sintomas}

| Sintoma | Seção |
|---|---|
| O navegador não abre a interface web | [A interface web não abre](#cap-18-sem-web) |
| Ninguém sabe a senha do administrador | [Senha do administrador esquecida](#cap-18-senha) |
| Um PIN esquecido, ou o painel mostra **ACESSO BLOQUEADO** | [PIN esquecido e painel bloqueado](#cap-18-pin) |
| A rede `<nome>_SETUP` não aparece, não aceita a conexão ou não sai do ar | [A rede de configuração](#cap-18-ap) |
| O aparelho voltou sem rede, sem contas e com outra senha | [Voltou com a configuração de fábrica](#cap-18-fabrica) |
| O visor não acende depois de gravar pelo USB | [Imagem errada](#cap-18-imagem) |
| O aparelho não liga depois de uma atualização | [Firmware pela metade](#cap-18-firmware) |
| Erros de gravação, arquivos que somem, histórico que não grava | [Sistema de arquivos corrompido](#cap-18-sistema-arquivos) |
| [air]{.img} O Air não acorda ou não aparece | [SIMUT Air](#cap-18-air) |

## A interface web não abre {#cap-18-sem-web}

Siga a lista na ordem. Cada item descarta uma causa.

1. **[air]{.img} O Air está hibernando?** Em M1, o Air não tem servidor web. Ligue o carregador, desligue e ligue a alimentação, ou rode `air stop` pelo console USB durante um despertar ([capítulo 19](#cap-19-modos)).
2. **O aparelho está na rede?** Pelo console USB, rode `show net status`. Sem IP, o aparelho não entrou na rede: veja [A rede de configuração](#cap-18-ap). Com IP, use exatamente esse endereço no navegador.
3. **HTTP ou HTTPS?** Com um par de certificados instalado, o aparelho atende em `https://`. Tente os dois ([capítulo 9](#cap-09-https)).
4. **O HTTPS não responde em nenhum dos dois?** Um par em que o certificado e a chave não combinam deixa o servidor web mudo nas duas portas. Pelo console USB, rode `system https off confirm`. O aparelho apaga o par e reinicia em HTTP.
5. **A página abre, mas a entrada falha?** Veja a mensagem ([capítulo 13](#cap-13-bloqueio)):
   - **Bloqueado por Ns. Tentativas excessivas.**: espere a contagem terminar;
   - **Limite de sessões atingido. Tente mais tarde.**: três contas já têm sessão; espere 15 min sem uso de uma delas, ou reinicie o aparelho;
   - **Usuário ou senha incorretos.**: veja [Senha do administrador esquecida](#cap-18-senha).
6. **Entrou, e a página mostra só `Access Denied`?** A conta não tem a permissão **Painel** ([capítulo 13](#cap-13-permissoes)).
7. **As páginas abrem, mas o backup ou a atualização caem no meio?** Alguns roteadores cortam transferências longas na porta 80. Use uma porta alternativa do servidor web ([capítulo 9](#cap-09-servidor-web)).

## Senha do administrador esquecida {#cap-18-senha}

A senha da web do administrador só se recupera pelo console USB. Ninguém a lê de volta: o aparelho gera outra.

1. Ligue o computador ao aparelho pelo cabo USB e abra o console ([capítulo 14](#cap-14-usb)).
2. [air]{.img} No Air, rode `enable`.
3. Rode `system admin reset confirm`.
4. Anote a senha de 8 caracteres que aparece depois de `Nova senha (unica vez):`. Ela não aparece de novo.
5. Entre na interface web com a conta `admin` e essa senha.
6. Troque a senha na tela que abre ([capítulo 13](#cap-13-troca-obrigatoria)).

A senha nova é gravada na hora e vale também depois de reiniciar. O comando não mexe nas outras contas, nas permissões nem no PIN do painel ([capítulo 17](#cap-17-admin-reset)).

::: nota
**Pelo Bluetooth não funciona.** Quem entra pelo Bluetooth já digitou a senha do administrador, então não tem o que recuperar; o comando pelo rádio só serviria a um intruso. O aparelho responde `ERROR: Comando so pela USB (cabo serial).` e registra a tentativa.
:::

## PIN esquecido e painel bloqueado {#cap-18-pin}

[release]{.img}

| Situação | O que fazer |
|---|---|
| Uma pessoa esqueceu o PIN | Um administrador define outro pela página **Usuários**, botão **PIN** ([capítulo 8](#cap-08-pin-web)) |
| O `admin` esqueceu o próprio PIN | O `admin` define outro pela mesma página. Sem a senha da web, recupere-a antes ([Senha do administrador esquecida](#cap-18-senha)) |
| Uma conta errou o PIN seis vezes e ficou bloqueada no painel | Reinicie o aparelho, ou identifique-se com outra conta |
| O painel mostra **ACESSO BLOQUEADO** e **Reinicialização requerida** | Vinte tentativas erradas somadas travaram o painel. Reinicie o aparelho |

Para reiniciar sem tocar no painel:

- desligue e ligue a alimentação;
- pelo console USB, rode `reload confirm`;
- pela API, com a operação de reinício de `/api/action` ([capítulo 26](#cap-26-acoes)).

As contagens de erro ficam só na memória, e o reinício zera todas. A interface web não é afetada por nenhum bloqueio do painel. Os detalhes dos bloqueios estão no [capítulo 8](#cap-08-bloqueios).

## A rede de configuração {#cap-18-ap}

[release]{.img} [alpha]{.img}

O que é a rede `<nome>_SETUP`, quando ela abre e como sair dela estão no [capítulo 9](#cap-09-ap). Esta seção trata dos problemas.

### O aparelho não entra na rede e não abre o AP {#cap-18-ap-nao-abre}

Force a abertura:

- pelo console USB ou Bluetooth, com o comando `ap`. A resposta traz a chave: `OK: Modo AP iniciado (WPA2). Senha: ...`;
- [release]{.img} pelo painel, no menu de configurações, com a permissão **Rede** no painel ([capítulo 11](#cap-11));
- [release]{.img} segurando a tela durante o boot, quando o painel pede **Mantenha a tela pressionada: Modo AP...** ([capítulo 4](#cap-04)).

Se o console mostrar `[AP] FAILED to start`, o AP não subiu, e o log registra **AP iniciado** (15) com contexto −1. A causa mais comum é um nome de aparelho longo demais: o nome da rede é o nome do aparelho com `_SETUP`, e o total não pode passar de 32 caracteres. Encurte o nome para até 26 caracteres ([capítulo 9](#cap-09-ap)).

### O celular vê a rede, mas não conecta {#cap-18-ap-nao-conecta}

1. **Confira a chave.** Ela tem 10 caracteres, maiúsculas e algarismos, sem `O`, `0`, `I` e `1`. Leia-a no console USB, na linha `[AP] PSK :`, na resposta do comando `ap`, na tela de boot do painel ou no LCD do alpha.
2. **Confira a versão.** Até a v2.7.0, um AP aberto enquanto o aparelho procurava uma rede inexistente ficava visível e não aceitava conexões. A v2.7.1 corrigiu isso. Numa versão anterior, desligue e ligue o aparelho e abra o AP de novo.
3. **Abra o endereço certo.** Com o celular na rede, abra `http://192.168.4.1`. Um AP aberto com o aparelho em operação mantém o protocolo do servidor web: com HTTPS instalado, use `https://192.168.4.1` ([capítulo 9](#cap-09-ap-aberto)).

### O aparelho volta sempre para o AP {#cap-18-ap-volta}

A rede configurada está errada ou fora de alcance. Um aparelho que nunca obteve IP desde que ligou abre o AP na primeira dormência da escada de reconexão, cerca de 6 a 7 min depois de ligar ([capítulo 9](#cap-09-ap-quando)).

Corrija a rede por um destes caminhos:

- **Pelo AP:** conecte-se à rede `<nome>_SETUP`, abra `http://192.168.4.1`, entre com uma conta que tenha a permissão **Rede** e grave a rede certa na página **Rede** ([capítulo 9](#cap-09-busca)).
- **Pelo console USB, na release e no alpha:**

  ```text
  SIMUT> system ssid MinhaRede
  SIMUT> system pass <senha do Wi-Fi>
  SIMUT> reload confirm
  ```

  Os dois primeiros comandos gravam na hora. O nome e a senha têm até 31 caracteres.

### O AP não sai do ar {#cap-18-ap-preso}

Um AP aberto no boot, por falta de rede configurada ou pelo gesto no painel, fica aberto até alguém gravar uma rede ou reiniciar o aparelho. O painel mostra **AP Ativo! Reinicie a placa para sair.** Grave a rede pelo AP ou pelo console e reinicie.

Um AP aberto com o aparelho em operação fecha sozinho depois de 15 min e o aparelho reinicia para tentar a rede de novo ([capítulo 9](#cap-09-ap-aberto)).

[air]{.img} O Air não abre o AP sozinho. Configure a rede dele pelo console ([SIMUT Air](#cap-18-air)).

## Voltou com a configuração de fábrica {#cap-18-fabrica}

Sintoma: o aparelho liga sem rede Wi-Fi, abre a rede de configuração, e a senha do administrador não funciona. Na release e no alpha, o AP abre sozinho, porque não há rede configurada.

Causas possíveis:

- uma atualização interrompida no envio da imagem, ou recusada, seguida de um reinício ([capítulo 17](#cap-17-energia));
- o sistema de arquivos não montou no boot, e o aparelho o reformatou;
- alguém rodou `system factory` ou `system format` ([capítulo 17](#cap-17-fabrica));
- o firmware foi gravado depois de apagar a flash inteira.

Para recuperar:

1. **Pegue a senha do administrador pelo console USB.** No boot que cria a configuração de fábrica, o console mostra a senha inicial numa moldura:

   ```text
   ==============================================
    SEC-003: FACTORY DEFAULTS ATIVADO
    Senha ADMIN inicial: <8 caracteres>
    Trocar no primeiro login (forcado).
   ==============================================
   ```

   Se você não viu a moldura, rode `system admin reset confirm` ([Senha do administrador esquecida](#cap-18-senha)).
2. **Configure a rede.** Pelo AP ou pelo console ([O aparelho volta sempre para o AP](#cap-18-ap-volta)).
3. **Entre na interface web** com `admin` e a senha, e troque a senha.
4. **Restaure o último backup** ([capítulo 17](#cap-17-restauracao)). Ele devolve a configuração, as contas, o histórico e os demais arquivos da data do backup.

Sem backup, configure o aparelho de novo a partir do [capítulo 4](#cap-04).

## Imagem errada {#cap-18-imagem}

Cada placa usa uma imagem: `release` para o painel de toque, `alpha` para o LCD 16×2, `air` para o SIMUT Air.

- **Pela página Arquivos,** o aparelho recusa uma imagem de outra variante com `v=7` ([capítulo 17](#cap-17-ota-conferencias)). A recusa reformata o sistema de arquivos: restaure o backup antes de reiniciar.
- **Pelo USB,** o modo BOOTSEL grava qualquer `.uf2`. Uma imagem de outra variante não aciona o visor da sua placa, e a imagem do Air hiberna depois de 5 min sem uso.

Para saber qual imagem está no aparelho, consulte `/api/status` (campo `sys.env`) ou `/api/perms` (campo `env`).

Para corrigir, grave o `.uf2` certo pelo BOOTSEL ([Gravar pelo BOOTSEL](#cap-18-bootsel)). A gravação de um `.uf2` substitui só o firmware: a configuração e os arquivos continuam lá.

## Firmware pela metade {#cap-18-firmware}

Sintoma: depois de uma atualização, o aparelho não volta. O console USB não aparece no computador, a rede não responde, o painel fica apagado.

A causa é uma interrupção nos cerca de 25 s da aplicação, quando a imagem nova é copiada sobre a antiga ([capítulo 17](#cap-17-energia)). O espaço do firmware ficou pela metade, e não há o que executar. O modo BOOTSEL fica na ROM do chip e não pode ser apagado: o aparelho sempre se recupera por ele, mas é preciso estar diante dele com um cabo USB.

Grave pelo BOOTSEL o `.uf2` da versão que você estava instalando ([Gravar pelo BOOTSEL](#cap-18-bootsel)).

::: nota
**A configuração costuma voltar.** Antes da aplicação, o aparelho guardou uma cópia da configuração numa área que a gravação pelo BOOTSEL não toca. No primeiro boot depois da gravação, o aparelho a encontra e a devolve. Numa campanha de testes em 11/09/2026, um aparelho travado dessa forma voltou com a configuração intacta depois de gravado pelo BOOTSEL. O histórico e os outros arquivos já tinham sido apagados pela atualização: restaure o backup.
:::

## Gravar pelo BOOTSEL {#cap-18-bootsel}

O BOOTSEL é o modo de gravação da ROM do RP2040. Ele não depende do firmware instalado.

### Entrar no BOOTSEL {#cap-18-bootsel-entrar}

**Pelo botão,** sempre funciona:

1. Desligue o cabo USB do Pico W.
2. Segure o botão **BOOTSEL**, o único botão do Pico W.
3. Ligue o cabo USB com o botão segurado.
4. Solte o botão.

O computador mostra uma unidade de disco chamada `RPI-RP2`. No Linux, `lsusb` lista a placa como `2e8a:0003`.

::: {.figura #fig-18-bootsel tipo="foto" arquivo="18-bootsel.png" captura="um Pico W sendo ligado ao cabo USB com o polegar segurando o botão BOOTSEL; o botão em destaque, com uma seta; ao lado, a janela do gerenciador de arquivos do computador mostrando a unidade RPI-RP2"}
O botão BOOTSEL do Pico W, segurado enquanto o cabo é ligado, e a unidade RPI-RP2 que aparece no computador.
:::

**Pela porta serial a 1200 bps,** sem abrir a caixa: funciona quando o firmware roda e o console USB aparece no computador, mas o aparelho não responde direito. Abra a porta serial a 1200 bps e feche-a: o aparelho reinicia em BOOTSEL.

```python
import serial, time
s = serial.Serial('/dev/ttyACM0', 1200, timeout=0.5)
time.sleep(0.1)
s.close()
```

Troque `/dev/ttyACM0` pela porta do aparelho no seu computador. Esse caminho não funciona com o firmware pela metade, nem com o Air dormindo, porque nos dois casos o console USB não existe.

### Gravar {#cap-18-bootsel-gravar}

**Arrastando o arquivo:** copie o `.uf2` para a unidade `RPI-RP2`. O Pico W grava e reinicia sozinho.

**Com o `picotool`:**

```bash
picotool info
picotool load -x simut_v2.7.1_release.uf2
```

O `picotool info` confirma que o aparelho está em BOOTSEL. O `-x` reinicia o aparelho depois de gravar. Espere cerca de 30 s e confira a versão com `show system info` no console.

### Apagar a flash inteira {#cap-18-bootsel-apagar}

Se, depois de gravar, o aparelho se comportar de forma estranha, apague a flash inteira e grave de novo:

```bash
picotool erase
picotool load -x simut_v2.7.1_release.uf2
```

O `picotool erase` só funciona em BOOTSEL. Ele apaga o firmware, a configuração, a cópia guardada pela atualização e todos os arquivos. O aparelho volta como novo: siga [Voltou com a configuração de fábrica](#cap-18-fabrica).

::: atencao
**Guarde o `.uf2` da versão anterior.** Não apague a imagem anterior antes de confirmar que a nova funciona no seu aparelho. Ela é a sua volta.
:::

## Sistema de arquivos corrompido {#cap-18-sistema-arquivos}

O aparelho se protege sozinho de boa parte das corrupções:

| O que corrompeu | O que o aparelho faz |
|---|---|
| O arquivo da configuração | Usa a cópia de segurança `/config/system.bak`. Com as duas corrompidas, volta à configuração de fábrica |
| Um bloco do histórico | Pula aquele bloco: perde-se no máximo o bloco, e não o dia ([capítulo 15](#cap-15-blocos)) |
| A cópia do bloco aberto, `.wip` | Descarta a cópia no boot ([capítulo 15](#cap-15-energia)) |
| O sistema de arquivos inteiro, que não monta no boot | Reformata e volta à configuração de fábrica ([Voltou com a configuração de fábrica](#cap-18-fabrica)) |

Se o aparelho funciona, mas o log mostra **Falha no storage** (20) ou **Falha em escrever histórico** (560) repetidos, ou arquivos somem e voltam:

1. Baixe o que puder pela página **Arquivos**, a começar pelo histórico, e faça um backup se a página deixar ([capítulo 17](#cap-17-backup)).
2. Pelo console USB, rode `system format confirm`. O aparelho reformata o sistema de arquivos e reinicia com a configuração de fábrica.
3. Siga [Voltou com a configuração de fábrica](#cap-18-fabrica): pegue a senha, configure a rede e restaure o backup.

Se a formatação não resolver, apague a flash inteira pelo BOOTSEL e grave o firmware de novo ([Apagar a flash inteira](#cap-18-bootsel-apagar)).

::: atencao
**Um backup feito com o sistema de arquivos já corrompido leva a corrupção junto.** Prefira o último backup feito com o aparelho saudável.
:::

## SIMUT Air {#cap-18-air}

[air]{.img}

O Air passa a maior parte do tempo dormindo, e dormindo ele some do USB. Para qualquer recuperação, traga-o para M0, o modo acordado ([capítulo 19](#cap-19-modos)):

- **Com o carregador:** ligue o carregador no pino configurado, GP17 de fábrica. O Air sobe em M0 no despertar seguinte e fica acordado enquanto o carregador estiver ligado.
- **Desligando e ligando:** um boot pela alimentação é um boot limpo, e o Air fica em M0 pelo tempo de ociosidade inteiro, 300 s de fábrica.
- **Pelo console USB, durante um despertar:** rode `air stop` enquanto o console aparece, nos segundos em que o Air está acordado. Pelo Bluetooth, `air stop` não funciona em M1.

Configurar a rede do Air pelo console completo:

```text
SIMUT> enable
SIMUT# configure terminal
SIMUT(config)# wifi ssid MinhaRede
SIMUT(config)# wifi pass <senha do Wi-Fi>
SIMUT(config)# end
SIMUT# reload confirm
```

Os comandos `wifi ssid` e `wifi pass` gravam na hora. O modo de configuração do console está no [capítulo 14](#cap-14-modos).

**O Air não acordou.** Um único sono nunca terminou num teste longo em 10/09/2026, depois de 119 ciclos normais. Desde então, um vigia de hardware cobre o despertar e reinicia o aparelho se ele travar ali. Se ainda assim o Air ficar mudo por mais que um período de despertar, desligue e ligue a alimentação. No boot seguinte, o log registra **Wake anterior do Air não completou** (414) quando o despertar anterior não chegou ao fim, e **Boot frio, não veio da hibernação** (412). Guarde esse log e relate o caso ([capítulo 19](#cap-19-limitacoes)).

**O Air fica acordado e não hiberna.** Veja [capítulo 19](#cap-19-problemas).
