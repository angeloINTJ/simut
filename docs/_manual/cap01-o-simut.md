# O SIMUT em uma página {#cap-01}

Este capítulo diz o que o SIMUT faz, para quem ele serve e o que ele não é, mostra os três aparelhos lado a lado e ensina a ler o resto do manual. É para todo leitor, antes de qualquer outro capítulo.

## O que o SIMUT faz {#cap-01-o-que}

O SIMUT é um firmware para o Raspberry Pi Pico W que transforma a placa num monitor de temperatura, umidade e pressão. Um aparelho SIMUT:

- **lê até 16 sensores** dos tipos DS18B20, DHT22, BME280 e BMP280, ligados aos pinos GP0 a GP15 ([capítulo 2](#cap-02), [capítulo 6](#cap-06));
- **grava o histórico no próprio aparelho**, um registro por intervalo, a partir de 1 min. Com 11 grandezas e um registro por minuto, cabem cerca de 116 dias (medido em 31/07/2026; [capítulo 15](#cap-15));
- **mostra as leituras** no painel de toque ou no LCD, conforme a imagem, e na interface web, que o próprio aparelho serve ([capítulo 11](#cap-11), [capítulo 12](#cap-12), [capítulo 13](#cap-13));
- **dispara alarmes** por limite, com som, tela e aviso ao servidor, e aceita janelas de manutenção ([capítulo 7](#cap-07));
- **envia as medições** a um servidor seu por HTTP, HTTPS, MQTT ou MQTTS, e os alarmes por uma segunda linha, separada ([capítulo 21](#cap-21), [capítulo 22](#cap-22));
- **conversa com outros sistemas** pelo Home Assistant, pelo Prometheus, pelo syslog e por uma API REST ([capítulo 20](#cap-20), [capítulo 26](#cap-26));
- **controla quem faz o quê**, com até 32 contas e 13 permissões, senha na web e PIN no painel ([capítulo 8](#cap-08));
- **se atualiza pela rede**, pela página **Arquivos** ([capítulo 17](#cap-17)).

## Para quem {#cap-01-para-quem}

O manual fala com quatro leitores:

| Leitor | O que faz com o aparelho |
|---|---|
| Quem instala | Monta o hardware, grava o firmware, põe o aparelho na rede e liga os sensores |
| Quem opera | Acompanha as leituras, atende os alarmes, abre janelas de manutenção e consulta o histórico |
| Quem integra | Recebe a telemetria e a linha de alarmes num servidor e automatiza pela API |
| A equipe de TI | Cuida da rede, das contas, do HTTPS, das atualizações e da segurança |

Um mesmo profissional pode acumular mais de uma dessas funções. O [roteiro de leitura](#cap-01-roteiros) no fim deste capítulo indica os capítulos de cada um.

## O que o SIMUT não é {#cap-01-nao-e}

- **Não é um instrumento metrológico certificado.** Antes de usá-lo onde a norma exige instrumento certificado, compare-o com uma referência sua. A calibração por sensor está no [capítulo 6](#cap-06-calibracao).
- **Não aciona equipamentos.** O aparelho não tem saídas para relés ou atuadores. Um alarme sai como som, como aviso na tela e como mensagem ao seu servidor.
- **Não depende de nuvem.** A interface web sai do próprio aparelho e funciona numa rede local sem internet. As medições só saem para um servidor que você configurar: de fábrica, a telemetria e a linha de alarmes estão desligadas. O que o aparelho procura na rede de fábrica é a hora certa, por NTP ([capítulo 10](#cap-10-ntp)).
- **Não mede enquanto espera ser configurado.** [release]{.img} [alpha]{.img} Na v2.7.1, enquanto o ponto de acesso de configuração está aberto, o aparelho não lê os sensores nem grava o histórico ([capítulo 9](#cap-09-ap-aberto)). Um aparelho novo liga já no ponto de acesso e só começa a medir depois de entrar numa rede ([capítulo 4](#cap-04)).

## Os três aparelhos {#cap-01-aparelhos}

O mesmo código gera três imagens de firmware publicadas, uma para cada montagem de hardware:

- **release:** o monitor com painel de toque colorido de 320×240;
- **alpha:** o monitor com LCD de 16×2 caracteres e console pelo Bluetooth;
- **Air (SIMUT Air):** o registrador a bateria, sem tela, que hiberna entre as leituras. É experimental.

| Recurso | release | alpha | Air |
|---|---|---|---|
| Tela | Painel TFT com toque | LCD 16×2 | Nenhuma |
| Cigarra (buzzer) | Sim | Sim | Não |
| Console pelo USB | De emergência | De emergência | Completo |
| Console pelo Bluetooth | Não | Sim | Sim, acordado |
| Interface web e API REST | Sim | Sim | Só acordado (M0) |
| HTTPS na interface web | Sim, com certificado | Não | Não |
| Endereço `<nome>.local` (mDNS) | Sim | Não | Não |
| Ponto de acesso de configuração | Abre sozinho ou a pedido | Abre sozinho ou a pedido | Só pelo comando `ap` |
| Contas e PIN no painel | Sim | Não | Não |
| Gráfico, calendário e temas na tela | Sim | Não | Não |
| Espelho do painel na web | Sim | Não | Não |
| Hibernação entre leituras | Não | Não | Sim |
| Telemetria e linha de alarmes | Sim | Sim | Sim, nos despertares de transmissão |
| Home Assistant, Prometheus e syslog | Sim | Sim | Com limites ([capítulo 20](#cap-20-imagens)) |
| Atualização pela web | Sim | Sim | Só acordado |
| Situação | Estável | Publicada; a saída do LCD não foi testada em bancada | Experimental |

Os 16 slots de sensor, os quatro tipos de sensor, o histórico, os alarmes, as contas e as permissões são iguais nas três imagens. As diferenças de cada recurso estão no capítulo dele: o ponto de acesso no [capítulo 9](#cap-09-ap), o console no [capítulo 14](#cap-14-perfis), o PIN no [capítulo 8](#cap-08-imagens), o Air no [capítulo 19](#cap-19).

::: atencao
**Cada imagem serve a um hardware.** Uma imagem gravada no hardware errado não encontra a tela dela. Na atualização pela web, o aparelho recusa uma imagem feita para outro hardware; na gravação pelo USB, ele aceita qualquer uma. A escolha da imagem está no [capítulo 3](#cap-03-escolher).
:::

### As imagens de bancada {#cap-01-bancada}

O código gera também três imagens para testes automáticos: `pico_w_test`, `pico_w_test_https` e `pico_w_asserts`. Elas não são publicadas e não servem para uso: as duas primeiras trazem o console completo e comandos de teste, e a terceira existe para detectar erros de concorrência. Use sempre uma das três imagens publicadas.

## Como ele funciona por dentro {#cap-01-dentro}

O RP2040, o processador do Pico W, tem dois núcleos:

- **o Core 0** faz quase tudo: lê os sensores, avalia os alarmes, grava o histórico, cuida da rede, atende a interface web e o console e envia a telemetria;
- **o Core 1** cuida só da tela. Na release, ele desenha o painel e lê o toque; na alpha, ele escreve no LCD; no Air, que não tem tela, ele não é usado.

A memória flash do Pico W tem 2 MB, divididos em duas metades:

| Área | Tamanho | O que guarda |
|---|---|---|
| Programa | 1.020 KiB | O firmware, com a interface web inteira embutida |
| Sistema de arquivos (LittleFS) | 1.024 KiB | A configuração e as contas, o histórico, o log de eventos, a calibração, os pacotes de idioma, os temas do painel e os certificados |

O que fica só na memória RAM se perde num reinício ou numa falta de energia:

- o bloco aberto do histórico, com os registros ainda não selados. O aparelho grava uma cópia dele depois de cada registro e a recupera no boot, então uma falta de energia perde no máximo o último registro ([capítulo 15](#cap-15-energia));
- a fila da linha de alarmes ([capítulo 22](#cap-22-fila));
- a senha inicial do administrador, que o aparelho mostra uma única vez ([capítulo 4](#cap-04-senha)).

O mapa completo do sistema de arquivos está no [capítulo 17](#cap-17-conteudo).

::: {.figura #fig-01-arquitetura tipo="diagrama" arquivo="01-arquitetura.png" captura="diagrama de blocos do aparelho: à esquerda, os sensores (DS18B20, DHT22, BME280/BMP280) ligados a GP0–GP15; no centro, o RP2040 com dois blocos, Core 0 (sensores, alarmes, histórico, rede, web, console, telemetria) e Core 1 (painel TFT e toque na release; LCD na alpha; sem uso no Air); abaixo, a flash de 2 MB em duas faixas, 'programa 1 MB' e 'LittleFS 1 MB' com as pastas /config, /history, /lang, /themes e o log de eventos; à direita, as saídas: navegador (interface web), servidor de telemetria (HTTP/MQTT), linha de alarmes, Home Assistant, Prometheus e syslog; embaixo à direita, o cabo USB e o Bluetooth levando ao console"}
O aparelho em blocos: os sensores entram pelo Core 0, a tela fica com o Core 1, e a flash se divide entre o programa e o sistema de arquivos.
:::

## Versões {#cap-01-versoes}

Este manual descreve a **v2.7.1**, de 22/09/2026, a versão publicada mais recente. A linha 2.7 saiu do beta com a v2.7.0.

Três mudanças já estão no código-fonte (`main`) e ainda não saíram numa versão publicada. O manual as descreve com o selo [main]{.img}:

- [main]{.img} o relógio do Air atravessa o sono com erro de ±0,09 s ([capítulo 10](#cap-10-air));
- [main]{.img} a telemetria monta o envio um registro inteiro por vez ([capítulo 21](#cap-21-registro-inteiro));
- [main]{.img} o LCD da alpha mostra os registros pendentes (`N` ou `Nk`) e um ícone de Wi-Fi que cresce da esquerda para a direita ([capítulo 12](#cap-12-pendentes)).

Um aparelho com a v2.7.1 não tem esses três itens. Como conferir a versão do seu aparelho está no [capítulo 17](#cap-17-ota-conferir), e as versões anteriores estão no apêndice [Histórico de versões](#ap-c).

## Como ler este manual {#cap-01-como-ler}

### Selos de imagem

Uma seção que vale só para algumas imagens começa com selos:

| Selo | Significado |
|---|---|
| [release]{.img} | Vale para a imagem com painel de toque |
| [alpha]{.img} | Vale para a imagem com LCD |
| [air]{.img} | Vale para o SIMUT Air |
| [main]{.img} | Está no código-fonte, mas ainda não numa versão publicada |

Uma seção sem selo vale para as três imagens.

### Selos de permissão

Uma ação que exige permissão mostra o nome técnico da permissão num selo, como [PERM_NET_CONFIG]{.perm}, e o nome que a página **Usuários** usa no texto, como **Rede**. As 13 permissões estão no [capítulo 8](#cap-08-permissoes).

### Faixas

::: nota
**Nota.** Explica o porquê de um comportamento ou dá um detalhe útil.
:::

::: atencao
**Atenção.** Avisa de algo que pode dar errado ou surpreender.
:::

::: perigo
**Perigo.** Avisa de perda de dados, de um aparelho que deixa de ligar ou de risco à segurança.
:::

### Figuras

As figuras mostram telas do painel, fotos do LCD, páginas da interface web, fotos do hardware e diagramas. Onde uma figura ainda não foi capturada, o manual mostra uma caixa com a descrição do que ela vai mostrar e em que estado.

### Textos da interface

Os rótulos aparecem em **negrito**, como o aparelho os mostra com o pacote de idioma pt-BR. Na primeira vez, o rótulo em inglês vem entre parênteses e em itálico, como **Buscar redes** (*Scan*): é o texto de um aparelho sem pacote de idioma. Comandos, campos e respostas do aparelho aparecem em `fonte fixa`, exatamente como o aparelho os escreve.

Os exemplos usam nomes de documentação: `simut` como nome do aparelho, `MinhaRede` como rede Wi-Fi, `192.0.2.10` como endereço e `coletor.exemplo.com.br` como servidor. Troque pelos seus.

### Roteiro de leitura {#cap-01-roteiros}

| Leitor | Leia nesta ordem |
|---|---|
| Quem instala | [2](#cap-02) Hardware, [3](#cap-03) Firmware, [4](#cap-04) Primeiro boot, [9](#cap-09) Rede, [6](#cap-06) Sensores, [10](#cap-10) Data e hora, [18](#cap-18) Recuperação |
| Quem opera | [11](#cap-11) Painel ou [12](#cap-12) LCD, [13](#cap-13) Interface web, [7](#cap-07) Alarmes, [15](#cap-15) Histórico, [16](#cap-16) Log de eventos |
| Quem administra o aparelho | [5](#cap-05) Configuração pela web, [8](#cap-08) Contas, [17](#cap-17) Atualização e backup, [14](#cap-14) Console |
| Quem integra | [20](#cap-20) Visão geral, [21](#cap-21) Telemetria, [22](#cap-22) Linha de alarmes, [23](#cap-23) Home Assistant, [24](#cap-24) Prometheus, [25](#cap-25) Syslog, [26](#cap-26) API REST, [27](#cap-27) Frota |
| A equipe de TI | [9](#cap-09) Rede e HTTPS, [29](#cap-29) Segurança, [8](#cap-08) Contas, [17](#cap-17) Atualização, [20](#cap-20-rede) Portas, [27](#cap-27) Frota |
| Quem usa o Air | [19](#cap-19) SIMUT Air, depois o roteiro da sua função |

Os limites do aparelho estão no [capítulo 28](#cap-28), os problemas comuns no [capítulo 30](#cap-30) e os termos no [capítulo 31](#cap-31).
