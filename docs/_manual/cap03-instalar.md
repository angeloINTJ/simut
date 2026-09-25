# Instalar o firmware {#cap-03}

Este capítulo mostra quais arquivos cada versão publica, como escolher a imagem certa, como gravá-la pelo cabo USB, como pôr o pacote de idioma no aparelho e como compilar o firmware a partir do código-fonte. É para quem instala um aparelho novo ou atualiza um aparelho antigo pelo USB. A atualização pela rede, de um aparelho já em uso, está no [capítulo 17](#cap-17-ota).

## Os arquivos de cada versão {#cap-03-arquivos}

Cada versão publicada, na página de versões do projeto no GitHub (`github.com/angeloINTJ/simut/releases`), traz nove arquivos:

| Arquivo | Para que serve |
|---|---|
| `simut_v2.7.3_release.uf2` | Gravar a imagem release pelo USB |
| `simut_v2.7.3_alpha.uf2` | Gravar a imagem alpha pelo USB |
| `simut_v2.7.3_air.uf2` | Gravar a imagem do Air pelo USB |
| `simut_v2.7.3_release.bin` | Atualizar a release pela página **Arquivos** ([capítulo 17](#cap-17-ota)) |
| `simut_v2.7.3_alpha.bin` | Atualizar a alpha pela página **Arquivos** |
| `simut_v2.7.3_air.bin` | Atualizar o Air pela página **Arquivos** |
| `language_pt-BR.lng` | Pacote de idioma português do Brasil |
| `language_es-ES.lng` | Pacote de idioma espanhol |
| `manifest.json` | Tamanho, soma SHA-256 e tipo de cada imagem, para um gestor de frota ([capítulo 27](#cap-27-manifest)) |

O `.uf2` e o `.bin` de uma mesma imagem têm o mesmo firmware: o `.uf2` é o `.bin` embrulhado no formato que o Pico W aceita pelo USB. Não troque um pelo outro: a página **Arquivos** recusa o `.uf2` ([capítulo 17](#cap-17-ota-conferencias)), e a unidade do BOOTSEL ignora o `.bin`.

Os pacotes de idioma não vão dentro do firmware. Eles ficam no sistema de arquivos do aparelho e entram por outro caminho ([O pacote de idioma](#cap-03-idioma)). Os 11 temas do painel (arquivos `.thm`) não vêm na página da versão: estão na pasta `data/themes` do código-fonte, e são opcionais ([capítulo 11](#cap-11-temas)).

::: {.figura #fig-03-arquivos tipo="diagrama" arquivo="03-arquivos.png" captura="os nove arquivos de uma versão e para onde vai cada um: .uf2 → cabo USB com BOOTSEL (aparelho novo ou recuperação); .bin → página Arquivos, atualização pela rede; language_pt-BR.lng → página Arquivos, pasta /lang; manifest.json → gestor de frota; três colunas coloridas por imagem (release, alpha, air)"}
Os arquivos de uma versão e o caminho de cada um até o aparelho.
:::

## Escolher a imagem {#cap-03-escolher}

A imagem depende do hardware montado ([capítulo 2](#cap-02)):

| Hardware | Imagem |
|---|---|
| Painel TFT com toque | release |
| LCD 16×2 | alpha |
| Sem tela, a bateria, com hibernação | Air |

Na dúvida sobre um aparelho já instalado, o tipo de imagem aparece na interface de programação: no campo `sys.env` de `/api/status` e no campo `env` de `/api/perms` ([capítulo 26](#cap-26)).

O aparelho confere o tipo de imagem de dois jeitos diferentes:

- **Pela página Arquivos**, ele recusa uma imagem de outro tipo antes de aplicá-la ([capítulo 17](#cap-17-ota-conferencias)).
- **Pelo USB**, ele aceita qualquer imagem. A conferência é sua: leia o nome do arquivo antes de copiar.

Uma imagem gravada no hardware errado não encontra a tela dela: o painel ou o LCD fica apagado. Grave de novo, pelo USB, a imagem certa.

## Gravar pelo USB com o BOOTSEL {#cap-03-uf2}

Este é o caminho para um Pico W novo e o caminho que sempre funciona, porque não depende do firmware que está na placa. Você precisa do cabo USB e do arquivo `.uf2` da sua imagem.

1. Desligue o cabo USB do Pico W.
2. Aperte e segure o botão **BOOTSEL**, o único botão da placa.
3. Ligue o cabo USB ao computador, ainda segurando o botão.
4. Solte o botão. O computador mostra uma unidade de disco chamada `RPI-RP2`.
5. Copie o arquivo `.uf2` para a unidade `RPI-RP2`.
6. Espere. A unidade some sozinha, e o Pico W reinicia já com o SIMUT.

::: {.figura #fig-03-bootsel tipo="foto" arquivo="03-bootsel.png" captura="mão segurando o botão BOOTSEL de um Pico W enquanto o cabo USB é ligado; o botão bem visível, ao lado do chip RP2040"}
O botão BOOTSEL, segurado enquanto o cabo USB é ligado.
:::

A gravação pelo USB só escreve a área do programa. O sistema de arquivos fica como estava:

- **num Pico W novo**, o sistema de arquivos está vazio. No primeiro boot, o aparelho o formata, cria as pastas e começa com a configuração de fábrica ([capítulo 4](#cap-04));
- **num aparelho em uso**, a configuração, as contas, o histórico, a calibração e os pacotes de idioma continuam lá. O firmware novo converte a configuração antiga no boot, dentro dos limites de [Atualizar um aparelho antigo](#cap-03-antigo).

::: nota
**Pelo USB, o histórico fica.** A atualização pela página **Arquivos** reformata o sistema de arquivos e depende do backup para devolver o histórico ([capítulo 17](#cap-17-sobrevive)). A gravação pelo USB não mexe nele. Para um aparelho ao alcance do cabo, é o caminho que preserva tudo sem backup. Troque depois o pacote de idioma pelo da versão nova ([O pacote de idioma](#cap-03-idioma)): um pacote antigo deixa em inglês os textos que a versão nova acrescentou.
:::

### Com o picotool

O `picotool` é a ferramenta de linha de comando do Raspberry Pi para o RP2040. Com o Pico W no modo BOOTSEL (passos 1 a 4 acima):

```bash
picotool info
picotool load -x simut_v2.7.3_release.uf2
```

O `picotool info` confirma que a placa está no modo BOOTSEL. O `load -x` grava e reinicia a placa no fim.

::: perigo
**`picotool erase` apaga tudo.** O comando apaga a flash inteira, com o sistema de arquivos: configuração, contas, histórico e calibração. Ele serve para a recuperação de um aparelho que não liga mais ([capítulo 18](#cap-18)), não para uma gravação comum.
:::

### Sem apertar o botão: o toque de 1200 baud

Um aparelho com o firmware funcionando entra no modo BOOTSEL sozinho quando o computador abre a porta serial USB dele a 1200 baud e a fecha. O `pio run -t upload` faz isso automaticamente antes de gravar ([Compilar a partir do código-fonte](#cap-03-fonte)). O toque só funciona enquanto o USB do firmware responde; num aparelho travado, use o botão.

[air]{.img} O Air desliga o USB enquanto dorme, e a porta serial some do computador. Grave o Air com ele acordado, em M0 ([capítulo 19](#cap-19)), ou pelo botão BOOTSEL.

## O pacote de idioma {#cap-03-idioma}

Sem pacote de idioma, o painel e a interface web aparecem em inglês. O console serial responde em português de fábrica, com ou sem pacote ([capítulo 14](#cap-14-perfis)).

O caminho recomendado é enviar o pacote pela interface web, depois que o aparelho estiver na rede ([capítulo 4](#cap-04)):

1. Entre na interface web com uma conta que tenha as permissões **Leitura** [PERM_FILE_READ]{.perm} e **Upload** [PERM_FILE_UPLOAD]{.perm}. O `admin` tem as duas.
2. Abra a página **Arquivos** e a pasta `/lang`.
3. Toque em **Enviar** e escolha `language_pt-BR.lng`, o da mesma versão do firmware.
4. Confira se o arquivo aparece na pasta com o tamanho certo.
5. Reinicie o aparelho. O pacote só é lido no boot.

Detalhes da página **Arquivos** estão no [capítulo 17](#cap-17-enviar), e o comportamento do idioma na web, no [capítulo 13](#cap-13-idioma).

::: atencao
**Um pacote só.** Com mais de um pacote na pasta `/lang`, o aparelho carrega o primeiro em ordem alfabética e registra um aviso no log de eventos. `language_es-ES.lng` vem antes de `language_pt-BR.lng`: com os dois na pasta, o aparelho fica em espanhol. Deixe na pasta só o pacote que você quer.
:::

### O uploadfs, só num aparelho novo

Quem compila da fonte pode gravar o sistema de arquivos inteiro com a pasta `data/` do código-fonte:

```bash
pio run -e pico_w_release -t uploadfs
```

::: perigo
**O `uploadfs` reformata o sistema de arquivos.** Num aparelho em uso, ele apaga o histórico, a configuração, as contas e a calibração, sem volta. Use-o só num aparelho novo, ou não use: o envio pela página **Arquivos** faz o mesmo sem apagar nada.
:::

A pasta `data/` leva os dois pacotes de idioma e os 11 temas. Depois de um `uploadfs`, apague `language_es-ES.lng` da pasta `/lang` pela página **Arquivos**, se você quer o aparelho em português, e reinicie.

## Compilar a partir do código-fonte {#cap-03-fonte}

A maioria das instalações usa as imagens publicadas. Compile só para mudar uma opção de compilação, como a frequência do TFT ([capítulo 2](#cap-02-tft)).

Você precisa de Python 3 e do PlatformIO. Os passos abaixo repetem os da integração contínua do projeto:

```bash
git clone https://github.com/angeloINTJ/simut.git
cd simut
pip install platformio zopfli
pio pkg install -e pico_w_release
bash tools/arduino_pico_overrides/patch.sh
pio run -e pico_w_release
```

- **O `patch.sh`** aplica as correções que o projeto faz no framework arduino-pico. Sem ele, a compilação falha. Rode-o de novo sempre que o framework for reinstalado.
- **O `zopfli`** comprime as páginas web. Sem ele, a compilação usa o gzip e a imagem sai alguns kB maior.
- **O ambiente** escolhe a imagem: `pico_w_release`, `pico_w_alpha` ou `pico_w_air`. O `pio run` sem `-e` compila só a `pico_w_release`.

A imagem sai em `.pio/build/pico_w_release/`, como `firmware.uf2` e `firmware.bin`. Para gravar direto, com o Pico W ligado ao USB:

```bash
pio run -e pico_w_release -t upload
```

O PlatformIO usa o toque de 1200 baud e o `picotool` para gravar; o botão BOOTSEL também funciona.

Uma opção de compilação muda com `-D` e vale sobre o padrão do arquivo `src/simut_config.h`. Por exemplo, para escrever no TFT a 31,25 MHz em vez de 62,5 MHz:

```bash
PLATFORMIO_BUILD_FLAGS="-DSIMUT_TFT_SPI_HZ=31250000u" pio run -e pico_w_release
```

::: atencao
**Uma imagem compilada por você não é a publicada.** Ela não tem o `manifest.json` e pode diferir das imagens da página de versões. Guarde o `.uf2` da última versão publicada para voltar a ela, se preciso.
:::

## Atualizar um aparelho antigo {#cap-03-antigo}

| Versão no aparelho | Como atualizar | O que se perde |
|---|---|---|
| 2.x | Pela página **Arquivos** ([capítulo 17](#cap-17-ota)), ou pelo USB | Pela página, o que o backup não devolver; pelo USB, nada |
| 1.6.2-beta a 1.x | Pela página **Arquivos**, ou pelo USB | A configuração e o histórico antigo (veja abaixo) |
| Anterior à 1.6.2-beta | Só pelo USB, uma vez | A configuração e o histórico antigo (veja abaixo) |

As versões anteriores à 1.6.2-beta tinham um defeito na atualização pela rede: diziam que tinham aplicado a imagem sem aplicá-la. Por isso, a primeira atualização de um aparelho desses é pelo USB.

Um aparelho na versão 1.x perde duas coisas no primeiro boot da 2.7.3:

- **A configuração.** O firmware não lê configurações das versões 1.x. O aparelho começa com a configuração de fábrica e uma senha nova para o `admin`, mostrada no console USB ([capítulo 4](#cap-04-senha)). Anote a rede, as contas e os sensores antes de atualizar.
- **O histórico no formato antigo.** Os arquivos `.sim4` da pasta `/history` são apagados no primeiro boot. Baixe-os antes de atualizar e converta-os no computador, com a ferramenta `tools/history_v5.py` do código-fonte:

```bash
python3 tools/history_v5.py --convert-v4 AAAAMMDD.sim4 AAAAMMDD.h5
```

Um aparelho na versão 2.x mantém a configuração: o firmware novo a converte no boot.

## Depois de gravar {#cap-03-depois}

O primeiro boot de um aparelho novo mostra a senha do administrador uma única vez, no console USB. Deixe o console aberto antes de o aparelho reiniciar ([capítulo 4](#cap-04-senha)). Para conferir a versão gravada, veja o [capítulo 17](#cap-17-ota-conferir).
