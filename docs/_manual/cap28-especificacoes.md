# Especificações e limites {#cap-28}

Este capítulo reúne em tabelas os números do SIMUT: faixas, valores de fábrica, capacidades, tempos e tamanhos. É a página de consulta de quem instala, configura e integra o aparelho, e cada linha aponta para o capítulo que explica o assunto.

## Como ler as tabelas {#cap-28-como-ler}

- Os valores são os da v2.7.2.
- Quando um número muda de uma imagem para outra, a célula traz o selo da imagem: [release]{.img}, [alpha]{.img} ou [air]{.img}.
- Uma medição traz a data e a versão em que foi feita. Os demais números vêm do código.
- A coluna **Onde** leva ao capítulo que explica o item. Lá estão o efeito de cada valor e o que fazer quando um limite é atingido.

## Plataforma {#cap-28-plataforma}

| Item | Valor | Onde |
|---|---|---|
| Placa | Raspberry Pi Pico W. O Pico sem W não serve: o firmware depende do rádio | [cap. 2](#cap-02-pico) |
| Processador | RP2040, dois núcleos: o Core 0 mede, grava e atende a rede; o Core 1 cuida da tela | [cap. 1](#cap-01-dentro) |
| Rádio | CYW43, para o Wi-Fi e o Bluetooth | [cap. 2](#cap-02-pico) |
| Memória flash | 2 MB | [cap. 1](#cap-01-dentro) |
| Espaço do programa | 1.020 KiB, um único espaço, sem cópia da versão anterior | [cap. 17](#cap-17-ota-antes) |
| Sistema de arquivos (LittleFS) | 1 MiB | [cap. 15](#cap-15-capacidade) |
| Dados da atualização | 4 KiB no fim da flash | [cap. 17](#cap-17-ota) |
| Maior imagem aceita pela atualização web | 1.040.384 B (1.016 KiB) | [cap. 17](#cap-17-ota-conferencias) |
| Menor imagem aceita pela atualização web | 100 KiB | [cap. 17](#cap-17-ota-conferencias) |
| Wi-Fi | Só 2,4 GHz. Uma rede só de 5 GHz não aparece na busca | [cap. 9](#cap-09-wifi) |
| Segurança do Wi-Fi do AP de configuração | WPA2 | [cap. 9](#cap-09-ap) |

::: {.figura #fig-28-flash tipo="diagrama" arquivo="28-flash.png" captura="barra horizontal dos 2 MB da flash do Pico W, em escala: 1.020 KiB do programa (com a marca do teto de 1.016 KiB da atualização web), 1.024 KiB do sistema de arquivos LittleFS e 4 KiB dos dados da atualização no fim; anotar que a imagem nova é recebida na área do sistema de arquivos"}
Legenda: a flash de 2 MB. O teto da atualização web fica 4 KiB abaixo do fim do espaço do programa.
:::

::: nota
**Valores elétricos e ambientais.** Este manual não define tensões, correntes nem faixas de temperatura e umidade de operação. Os da placa estão na folha de dados (*datasheet*) do Raspberry Pi Pico W, publicada pela Raspberry Pi; os de cada sensor, na folha de dados do sensor. O SIMUT também não é um instrumento metrológico certificado: valide as leituras contra a sua referência ([capítulo 29](#cap-29-limites)).
:::

### Recursos por imagem {#cap-28-imagens}

| Recurso | release | alpha | Air | Onde |
|---|---|---|---|---|
| Tela | Painel TFT 320 × 240 com toque | LCD 16×2 | Nenhuma | [cap. 1](#cap-01-aparelhos) |
| Cigarra (buzzer) | Sim | Sim | Não | [cap. 7](#cap-07-sons) |
| Bluetooth (console) | Não | Sim | Sim, acordado (M0) | [cap. 14](#cap-14-perfis) |
| HTTPS na interface web | Sim, com certificado | Não | Não | [cap. 9](#cap-09-https) |
| Nome `<nome>.local` (mDNS) | Sim | Não | Não | [cap. 9](#cap-09-mdns) |
| Console serial | De emergência, 14 comandos | De emergência, 14 comandos | Completo | [cap. 14](#cap-14-perfis) |
| Rotas da API | 62 | 56 | 56, só acordado (M0) | [cap. 26](#cap-26-imagens) |
| AP de configuração | Abre sozinho ou a pedido | Abre sozinho ou a pedido | Só pelo comando `ap` | [cap. 9](#cap-09-ap-quando) |
| PIN e contas no painel | Sim | Não | Não | [cap. 8](#cap-08-imagens) |
| Hibernação entre leituras | Não | Não | Sim | [cap. 19](#cap-19-ciclo) |

### Pinos {#cap-28-pinos}

| Uso | release | alpha | Air | Onde |
|---|---|---|---|---|
| Sensores | GP0 a GP15 | GP0 a GP15 | GP0 a GP15 | [cap. 6](#cap-06-slots) |
| Tela | SPI0: MISO GP16, SCK GP18, MOSI GP19; CS GP28, DC GP27, RST GP26 | LCD em 4 bits: RS GP16, E GP17, D4 a D7 em GP18 a GP21 | — | [cap. 2](#cap-02-mapa) |
| Toque | CS GP17, IRQ GP20, no mesmo SPI0 | — | — | [cap. 2](#cap-02-tft) |
| Cigarra | GP22 | GP22 | — | [cap. 2](#cap-02-cigarra) |
| Alimentação dos sensores e sinal de acordado | — | — | GP16 | [cap. 19](#cap-19-gp16) |
| Detecção do carregador | — | — | GP17 de fábrica; aceita GP0 a GP22 e GP26 a GP28 | [cap. 19](#cap-19-carregador) |
| Rádio do Pico W | GP23, GP24, GP25, GP29 | GP23, GP24, GP25, GP29 | GP23, GP24, GP25, GP29 | [cap. 2](#cap-02-mapa) |

## Sensores {#cap-28-sensores}

| Item | Valor | Onde |
|---|---|---|
| Slots | 16, numerados de 0 a 15 | [cap. 6](#cap-06-slots) |
| GPIOs por slot | De 1 a 4, conforme o tipo | [cap. 6](#cap-06-slots) |
| GPIOs aceitos | GP0 a GP15, em todas as imagens | [cap. 6](#cap-06-editor) |
| **Nome** do slot | Até 31 caracteres, sem aspas, barra invertida nem caracteres de controle | [cap. 6](#cap-06-editor) |
| **ID de Hardware** | De 1 a 15 caracteres: letras sem acento, dígitos, `_` e `-` | [cap. 6](#cap-06-editor) |
| Sensores I²C (BME280 e BMP280) | Um por par SDA/SCL, no endereço `0x76` ou `0x77`: a gravação recusa dois slots ativos no mesmo GPIO | [cap. 2](#cap-02-i2c) |
| Sensores I²C nos pares do periférico | No máximo um no I2C0 e um no I2C1; outros pares são lidos pelo PIO | [cap. 2](#cap-02-i2c) |
| Pull-up externo | 4,7 kΩ no dado do DS18B20; 4,7 kΩ em SDA e SCL do BME280 e do BMP280, se o módulo não tiver | [cap. 2](#cap-02-sensores) |
| **Procurar sondas** | Resultado em até 10 s; sensores I²C só em GP4 (SDA) e GP5 (SCL) | [cap. 6](#cap-06-procurar) |
| **Resolução DS18B20** | 9 a 12 bits; fábrica 12 | [cap. 6](#cap-06-resolucao) |
| Espera da conversão do DS18B20 | Sempre 750 ms, qualquer que seja a resolução | [cap. 6](#cap-06-resolucao) |
| **Amostra (ms)** | 1000 a 60000; fábrica 2000. Sem efeito na v2.7.1 | [cap. 6](#cap-06-amostra) |

### Tipos de sensor {#cap-28-tipos}

| Tipo | Pinos | Grandezas | Pausa entre leituras | O que o aparelho recusa | Onde |
|---|---|---|---|---|---|
| DS18B20 | 1 | Temperatura | 1 s, mais 750 ms de conversão | CRC errado; temperatura fora de −50 a 150 °C; outra sonda no pino | [cap. 6](#cap-06-validacao) |
| DHT22 | 1 | Temperatura e umidade | 2 s | Soma de verificação errada; sem resposta em 150 ms | [cap. 6](#cap-06-validacao) |
| BME280 | 2 | Temperatura, umidade e pressão | 5 s | Temperatura fora de −40 a 85 °C; umidade fora de 0 a 100 % e pressão fora de 300 a 1.100 hPa, só naquela grandeza | [cap. 6](#cap-06-validacao) |
| BMP280 | 2 | Temperatura e pressão | 5 s | Como o BME280 | [cap. 6](#cap-06-validacao) |

[air]{.img} No Air, a leitura acontece a cada despertar, sem pausa entre as leituras ([capítulo 19](#cap-19-ciclo)).

### Leitura, erro e calibração {#cap-28-leitura}

| Item | Valor | Onde |
|---|---|---|
| Média | Últimas 10 leituras; descarta as 2 menores e as 2 maiores e tira a média das 6 restantes | [cap. 6](#cap-06-intervalos) |
| Entrada em erro | 3 falhas seguidas | [cap. 6](#cap-06-validacao) |
| Saída do erro | 5 leituras boas seguidas | [cap. 6](#cap-06-validacao) |
| Conferência da sonda DS18B20 | A cada 5 leituras; em quarentena, releitura da ROM a cada 10 ciclos | [cap. 6](#cap-06-validacao) |
| Pontos de calibração | Até 5 por grandeza; 1 ponto soma um offset | [cap. 6](#cap-06-calibracao) |
| Interpolação **Suave** | Exige 3 pontos ou mais; com menos, vale a **Reta** | [cap. 6](#cap-06-interpolacao) |
| Gravação da calibração | Uma a cada 5 s, com o relógio acertado por NTP | [cap. 6](#cap-06-requisitos) |

### Grandezas e limites de alarme {#cap-28-grandezas}

| Grandeza | Chave | Unidade | Faixa aceita | Limites de fábrica | Onde |
|---|---|---|---|---|---|
| Temperatura | `temp` | °C | −327,68 a 327,66 | 0 a 40 | [cap. 6](#cap-06-grandezas) |
| Umidade | `hum` | % | 0 a 102,3 | 20 a 80 | [cap. 6](#cap-06-grandezas) |
| Pressão | `press` | hPa | 0 a 1.638,3 | A faixa inteira | [cap. 6](#cap-06-grandezas) |
| Luminosidade | `lux` | lx | 0 a 167.772,15 | A faixa inteira | [cap. 6](#cap-06-grandezas) |

A luminosidade existe na tabela de grandezas, mas nenhum dos quatro tipos de sensor a informa na v2.7.1. Os limites de alarme mudam em passos de 0,1, e um mínimo igual ou maior que o máximo faz o aparelho subir o máximo para o mínimo mais 0,1 ([capítulo 7](#cap-07-limites)).

## Histórico {#cap-28-historico}

| Item | Valor | Onde |
|---|---|---|
| **Intervalo Histórico (min)** | 1 a 1440; fábrica 1 | [cap. 6](#cap-06-intervalo-historico) |
| Arquivo | Um por dia, `/history/AAAAMMDD.h5`, no fuso do aparelho | [cap. 15](#cap-15-guardado) |
| Registros por bloco selado | Até 60. Com o intervalo de fábrica, 1 h | [cap. 15](#cap-15-blocos) |
| Selagem antecipada | Na virada do dia e numa correção do relógio por NTP | [cap. 15](#cap-15-blocos) |
| Bloco aberto | Na RAM, copiado para `/history/.wip` a cada registro; com o aparelho ocupado, nova tentativa em 2 s | [cap. 15](#cap-15-blocos) |
| Perda numa falta de energia | No máximo o último registro | [cap. 15](#cap-15-energia) |
| Espaço por registro | 5,38 B com 11 canais a 1 registro por minuto, cerca de 7,6 KiB por dia (arquivos de bancada V5, 31/07/2026) | [cap. 15](#cap-15-capacidade) |
| Retenção estimada | Cerca de 116 dias nesse caso; varia com os canais, o intervalo e a variação dos valores | [cap. 15](#cap-15-capacidade) |
| Limpeza automática | Acima de 86 % do sistema de arquivos, apaga os dias mais antigos; confere a cada minuto | [cap. 15](#cap-15-capacidade) |
| Ritmo da limpeza | 2 arquivos por vez; se ainda acima, 1 a cada 15 s. [air]{.img} Até 4 por despertar | [cap. 15](#cap-15-capacidade) |
| Arquivo do dia corrente | Nunca é apagado pela limpeza | [cap. 15](#cap-15-capacidade) |
| Lacuna no gráfico | Registros separados por mais de 3,5 vezes o **Intervalo Histórico**, com o mínimo de 90 s | [cap. 15](#cap-15-grafico) |
| Baldes do gráfico | Um por pixel de largura, de 300 a 1200; até 1,6 registro por balde, cada registro aparece como é | [cap. 15](#cap-15-baldes) |
| Períodos do gráfico | `1h`, `6h`, `24h`, `7d`, `1M` (30 dias), `1A` (365 dias), `MAX` | [cap. 15](#cap-15-periodo) |
| Exportação pela API | Até 31 dias por pedido, em `.simx` | [cap. 26](#cap-26-historico) |

## Log de eventos {#cap-28-log}

| Item | Valor | Onde |
|---|---|---|
| Tamanho de um registro | 12 bytes | [cap. 16](#cap-16-o-que) |
| Arquivos | `/system.blog` e `/system.old.blog`, de 800 registros cada | [cap. 16](#cap-16-guarda) |
| Registros guardados | Entre 800 e 1.600, os mais recentes | [cap. 16](#cap-16-guarda) |
| Códigos de evento | 155 | [ap. B](#ap-b) |
| Tempo ligado gravado no registro | Até cerca de 194 dias | [cap. 16](#cap-16-o-que) |
| Registros segurados na RAM durante toque ou operação pesada | Até 32; o excedente se perde | [cap. 16](#cap-16-ram) |
| Filtro por transição | Vai para a flash o primeiro sucesso de cada família depois do boot, a primeira falha, o primeiro sucesso depois de uma falha e um registro por família a cada hora | [cap. 16](#cap-16-transicao) |
| Resumo do que ficou de fora | Um registro **Registros de rotina suprimidos** (5) por hora | [cap. 16](#cap-16-transicao) |
| [air]{.img} Despertar do ciclo | Um registro por ciclo, **Snapshot de histórico gravado** (567) | [cap. 16](#cap-16-air) |
| Exportação pela API | Até 31 dias por pedido, em `.simx` | [cap. 26](#cap-26-logs) |
| Ritmo de `GET /api/logs` | Um pedido a cada 200 ms por endereço | [cap. 26](#cap-26-ritmo) |

O filtro vale só para a flash: o console com `debug on` e o syslog recebem todos os eventos.

## Contas e segurança {#cap-28-contas}

| Item | Valor | Onde |
|---|---|---|
| Contas | Até 32; a conta 0 é a do `admin` | [cap. 8](#cap-08-modelo) |
| Nome da conta | De 1 a 15 caracteres, único sem diferenciar maiúsculas de minúsculas | [cap. 8](#cap-08-modelo) |
| Permissões | 13, uma por bit; `perms` até 8191 | [cap. 8](#cap-08-permissoes) |
| Senha inicial do `admin` | 8 caracteres aleatórios, mostrados uma vez no console USB | [cap. 8](#cap-08-fabrica) |
| Senha nova, pela página | Pelo menos 8 caracteres, com letra, dígito e símbolo | [cap. 13](#cap-13-trocar-senha) |
| Senha nova, conferida pelo aparelho | [release]{.img} Só em HTTPS: pelo menos 8 caracteres, com letra e dígito | [cap. 8](#cap-08-senhas) |
| Resumo da senha | HMAC-SHA256, 5.000 rodadas, sal aleatório de 8 bytes por conta | [cap. 8](#cap-08-hash) |
| Sessões web | 3 ao todo, 1 por conta | [cap. 26](#cap-26-sessoes) |
| Ociosidade da sessão web | 15 min sem pedido autenticado; um reinício encerra todas | [cap. 26](#cap-26-sessoes) |
| Código de uso único da entrada | 32 algarismos hexadecimais; vale 60 s e uma tentativa | [cap. 26](#cap-26-referencia) |
| Token de sessão | 32 algarismos hexadecimais, em cookie ou `Authorization: Bearer` | [cap. 26](#cap-26-token) |

### Bloqueios {#cap-28-bloqueios}

| Canal | Regra | Onde |
|---|---|---|
| Web e `/metrics` | Por endereço IP: 2 s no primeiro erro, dobrando até 300 s a partir do nono | [cap. 26](#cap-26-bloqueio) |
| Tabela de bloqueios web | 8 endereços; a contagem de erros para em 12. Com os 8 bloqueados, um endereço novo recebe `429` | [cap. 26](#cap-26-bloqueio) |
| [release]{.img} PIN do painel, por conta | 3ª tentativa errada: 5 s; 4ª: 15 s; 5ª: 60 s; 6ª: conta bloqueada no painel até reiniciar | [cap. 8](#cap-08-bloqueios) |
| [release]{.img} Painel inteiro | Travado depois de 20 tentativas erradas somadas entre todas as contas, até reiniciar | [cap. 8](#cap-08-bloqueios) |
| [alpha]{.img} [air]{.img} Bluetooth | 2 s no primeiro erro, dobrando até 300 s a partir do nono; vale também depois de reconectar | [cap. 14](#cap-14-bt-bloqueio) |
| [alpha]{.img} [air]{.img} Senha do Bluetooth | A do `admin` da web, até 64 caracteres | [cap. 14](#cap-14-bt-entrar) |
| [alpha]{.img} [air]{.img} Sessão Bluetooth | Termina com 5 min sem nada digitado | [cap. 14](#cap-14-bt-sessao) |
| [alpha]{.img} [air]{.img} Descoberta do Bluetooth | Visível nas buscas por 5 min depois que o Bluetooth liga, no boot | [cap. 14](#cap-14-parear) |

### PIN do painel {#cap-28-pin}

[release]{.img}

| Item | Valor | Onde |
|---|---|---|
| Caracteres por tecla | 1, 2 ou 3; fábrica 3 | [cap. 8](#cap-08-politica) |
| Alfabeto | `0-9` ou `0-9 A-Z`; fábrica `0-9` | [cap. 8](#cap-08-politica) |
| Tamanho mínimo | De 4 até o teto; fábrica 4 | [cap. 8](#cap-08-politica) |
| Teto do tamanho | 8 com 3 caracteres por tecla, 12 com 2, 16 com 1 | [cap. 8](#cap-08-politica) |
| PIN pela web ou pela API | Sempre de 4 a 8 dígitos | [cap. 8](#cap-08-pin-web) |
| PIN de fábrica do `admin` | `1234`, com troca obrigatória | [cap. 8](#cap-08-pin-fabrica) |

### Rotas, ritmo e tamanhos da API {#cap-28-api}

| Item | Valor | Onde |
|---|---|---|
| Rotas | [release]{.img} 62, das quais 52 exigem permissão ou sessão e 10 são públicas. [alpha]{.img} [air]{.img} 56 | [cap. 26](#cap-26-imagens) |
| Pedidos simultâneos | Um de cada vez; os outros esperam na fila | [cap. 26](#cap-26-um-por-vez) |
| Intervalo mínimo por endereço | 200 ms em `GET /api/logs` e `GET /api/ls`; 5 s em `POST /api/calib` e `POST /api/tls` | [cap. 26](#cap-26-ritmo) |
| Campo `_payload` de `POST /api/commit_all` | Até 6.144 bytes | [cap. 26](#cap-26-referencia) |
| Corpo de `POST /api/calib` e `POST /api/tls` | Até 8.192 bytes | [cap. 26](#cap-26-referencia) |
| Janela de manutenção | Até 30 dias (2.592.000 s); um pedido maior é cortado | [cap. 7](#cap-07-manutencao) |
| [release]{.img} Janela de manutenção pelo painel | De 5 min a 720 h, com os minutos em passos de 5 | [cap. 7](#cap-07-manutencao-painel) |
| `sys.uptime` | Volta a zero a cada 49,7 dias | [cap. 27](#cap-27-status) |
| Tempo de espera do cliente, sugerido | 10 s em leituras; 30 s em exportações, listagens e histórico; 60 s em envio de arquivo e atualização | [cap. 26](#cap-26-um-por-vez) |

## Rede {#cap-28-rede}

### Nomes e senhas {#cap-28-nomes}

| Campo | Limite | Fábrica | Onde |
|---|---|---|---|
| **Nome** do aparelho | 1 a 31 caracteres; use até 26, porque o AP acrescenta `_SETUP` e o nome de rede tem no máximo 32 | `simut` | [cap. 9](#cap-09-ap) |
| **SSID** | 1 a 31 caracteres | Vazio | [cap. 9](#cap-09-wifi) |
| **Senha** do Wi-Fi | Até 31 caracteres; o WPA2 aceita até 63, mas o aparelho recusa uma senha mais longa | Vazio | [cap. 9](#cap-09-wifi) |
| Servidor NTP | Até 31 caracteres; vazio usa `pool.ntp.org` | Vazio | [cap. 9](#cap-09-ntp) |
| Linha do console serial | Até 256 caracteres; uma linha maior é descartada | — | [cap. 14](#cap-14-usb) |

::: atencao
**SSID ou senha com espaço, só pela web.** No console, cada argumento termina no primeiro espaço: `system ssid Minha Rede` grava só `Minha` ([capítulo 14](#cap-14-wifi)). Uma rede cujo nome ou senha tenha espaço só pode ser configurada pela página **Rede** ([capítulo 9](#cap-09-wifi)).
:::

### Wi-Fi e sinal {#cap-28-wifi}

| Item | Valor | Onde |
|---|---|---|
| Espera pela rede no boot | Até 30 s; depois o aparelho segue sem rede | [cap. 10](#cap-10-sem-ntp) |
| Busca de redes | Até 12 redes na lista; prazo de 15 s no aparelho e cerca de 18 s na página | [cap. 9](#cap-09-busca) |
| Sinal fraco | Em −78 dBm ou abaixo, a telemetria, a linha de alarmes e os envios pesados esperam | [cap. 9](#cap-09-referencia) |
| Sinal impossível | 0 dBm ou mais, ou abaixo de −120 dBm, em duas leituras seguidas, com 1 min entre elas: o aparelho reconecta | [cap. 9](#cap-09-vigilancia) |
| Sinal sem conexão | Informado como −100 dBm | [cap. 9](#cap-09-referencia) |

### Escada de reconexão {#cap-28-escada}

| Item | Valor | Onde |
|---|---|---|
| Uma tentativa de entrar na rede | Até 20 s | [cap. 9](#cap-09-degraus) |
| Uma busca antes da tentativa | Até 15 s | [cap. 9](#cap-09-degraus) |
| Esperas entre tentativas | 5, 10, 20, 40 e 80 s | [cap. 9](#cap-09-degraus) |
| Dormência | 10 min antes de cada busca, três vezes; depois a escada recomeça | [cap. 9](#cap-09-degraus) |
| AP pela escada, aparelho que nunca obteve IP desde que ligou | Na primeira dormência, cerca de 6 a 7 min depois de ligar (medido em bancada em 22/09/2026, desenvolvimento da v2.7.1) | [cap. 9](#cap-09-ap-quando) |
| AP pela escada, aparelho que já obteve IP | Depois de uma volta inteira, cerca de 68 min (pela aritmética da escada) | [cap. 9](#cap-09-ap-quando) |

### AP de configuração {#cap-28-ap}

| Item | Valor | Onde |
|---|---|---|
| Abertura sozinha | [release]{.img} [alpha]{.img} No boot sem rede configurada e pela escada de reconexão. [air]{.img} Nunca; só pelo comando `ap` | [cap. 9](#cap-09-ap-quando) |
| Nome da rede | `<nome>_SETUP` | [cap. 9](#cap-09-ap) |
| Chave | 10 caracteres, letras maiúsculas e algarismos sem `O`, `0`, `I` e `1`; sempre a mesma naquele aparelho, mesmo depois de um reset de fábrica | [cap. 9](#cap-09-ap) |
| Endereço do aparelho | `http://192.168.4.1` | [cap. 9](#cap-09-ap) |
| AP aberto em operação, com rede configurada | Dura 15 min; depois o aparelho reinicia para tentar a rede | [cap. 9](#cap-09-ap-aberto) |
| AP aberto no boot, pelo gesto ou sem rede configurada | Fica aberto até alguém gravar uma rede ou reiniciar o aparelho | [cap. 9](#cap-09-ap-aberto) |
| Medição com o AP aberto | Nenhuma: não lê sensores, não confere alarmes, não grava histórico nem envia telemetria | [cap. 9](#cap-09-ap-aberto) |

::: perigo
**Com o AP de configuração aberto, a v2.7.1 não mede.** Um aparelho sem rede configurada fica no AP desde o boot, e um que perde a rede por mais de cerca de 68 min passa a alternar 15 min no AP com cerca de 6 a 7 min medindo. O que não foi medido não é recuperado ([capítulo 9](#cap-09-ap-aberto)).
:::

### Data e hora {#cap-28-hora}

| Item | Valor | Onde |
|---|---|---|
| Esperas entre pedidos ao NTP | 20 s, 1 min, 3 min, 9 min e, daí em diante, 15 min | [cap. 10](#cap-10-tentativas) |
| Troca para `pool.ntp.org` | Depois de três esperas sem resposta, cerca de 4 min 20 s depois do primeiro pedido; vale até o próximo reinício | [cap. 10](#cap-10-tentativas) |
| Correção do relógio no log de eventos | Acima de 5 s (evento 408); acima de 1 h, como aviso | [cap. 10](#cap-10-log) |
| **Fuso Horário** | −12 a +14, em horas inteiras, sem horário de verão; fábrica −3 | [cap. 10](#cap-10-fuso) |

### Servidor web, HTTPS e CORS {#cap-28-servidor-web}

| Item | Valor | Onde |
|---|---|---|
| **Porta HTTP** | 1 a 65535; fábrica 80 | [cap. 9](#cap-09-servidor-web) |
| Conexões persistentes (keep-alive) | Ligadas de fábrica | [cap. 9](#cap-09-servidor-web) |
| [release]{.img} Porta HTTPS | 443 quando a **Porta HTTP** é 80; senão, a porta configurada. O aparelho nunca serve HTTP e HTTPS ao mesmo tempo | [cap. 9](#cap-09-https-comportamento) |
| [release]{.img} TLS da interface web | Só TLS 1.2, troca de chaves ECDHE, cifra AES-GCM de 128 ou 256 bits | [cap. 9](#cap-09-https-comportamento) |
| [release]{.img} Clientes TLS ao mesmo tempo | 1 | [cap. 9](#cap-09-https-comportamento) |
| [release]{.img} Par de certificado e chave | Até 8 KB; um par ausente, vazio, maior ou ilegível faz o aparelho subir em HTTP | [cap. 9](#cap-09-https-comportamento) |
| [release]{.img} mDNS | `<nome>.local` e o serviço `_simut._tcp`, com `uid`, `ver`, `env` e `tls` | [cap. 9](#cap-09-mdns) |
| Origem CORS | Até 64 caracteres, `http://` ou `https://`, sem caminho nem barra no fim | [cap. 9](#cap-09-cors) |
| Validade da resposta de verificação (*preflight*) | `Access-Control-Max-Age: 600` (10 min) | [cap. 26](#cap-26-cors) |

## Telemetria {#cap-28-telemetria}

| Item | Valor | Onde |
|---|---|---|
| Transportes | HTTP, HTTPS, MQTT e MQTTS | [cap. 21](#cap-21-transporte) |
| **Lote mínimo (registros)** (`t_int`) | 0 a 20.000; fábrica 0, que desliga a telemetria | [cap. 21](#cap-21-lotes) |
| **Lote máximo (registros)** (`t_bat`) | 1 a 250; fábrica 10 | [cap. 21](#cap-21-lotes) |
| Limite do lote pela memória | Reserva 12 KB sem TLS ou 32 KB com TLS e conta 350 bytes por registro no JSON e no **Dinâmico**, 160 no CSV | [cap. 21](#cap-21-tamanho) |
| Ajuste automático do lote | Cai pela metade a cada falha, até 10; cresce 50 % a cada sucesso, até o **Lote máximo** | [cap. 21](#cap-21-tamanho) |
| Memória livre para enviar | 24 KB com TLS, 14 KB sem TLS; a falta conta como falha | [cap. 21](#cap-21-quando) |
| Primeiro envio depois de ligar | Pelo menos 8 s depois | [cap. 21](#cap-21-quando) |
| Resposta do coletor HTTP | 4 s; só 200 a 299 é entrega | [cap. 21](#cap-21-entrega) |
| Negociação TLS no HTTPS | 15 s | [cap. 21](#cap-21-tls) |
| Espera depois de uma falha | 5 s, dobrando até 300 s, com variação de 25 % para mais ou para menos | [cap. 21](#cap-21-falhas) |
| Pausa entre lotes | Nenhuma com ciclo de até 0,4 s sem TLS ou 2,5 s com TLS; senão, igual ao ciclo; teto de 10 s | [cap. 21](#cap-21-ritmo) |
| Janela de envio | 30 dias antes do registro mais novo | [cap. 21](#cap-21-filas) |
| Contador de pendentes | Para de contar em 65.535 | [cap. 21](#cap-21-filas) |
| Gravação do cursor na flash | No máximo a cada 5 s | [cap. 21](#cap-21-duplicatas) |
| **IP Servidor** | Até 63 caracteres | [cap. 21](#cap-21-transporte) |
| **Endpoint** | Até 31 caracteres; fábrica `/api.php` | [cap. 21](#cap-21-transporte) |
| **API Key** | Até 63 caracteres | [cap. 21](#cap-21-transporte) |
| Formatos | JSON, CSV com 34 colunas e **Dinâmico** | [cap. 21](#cap-21-json) |
| Modelos do **Dinâmico** | **1. Global** até 255 caracteres; **2. Linha** até 511; **3. Separador** até 7 | [cap. 21](#cap-21-construtor) |
| Montagem do corpo | Um registro inteiro por vez; o cursor avança só até o último registro enviado | [cap. 21](#cap-21-registro-inteiro) |

### MQTT e TLS da telemetria {#cap-28-mqtt}

| Item | Valor | Onde |
|---|---|---|
| **Tópico** | Até 63 caracteres; fábrica `simut/data` | [cap. 21](#cap-21-campos-mqtt) |
| **Client ID** | Até 23 caracteres; vazio usa `simut_` e o fim do MAC | [cap. 21](#cap-21-campos-mqtt) |
| **Usuário** e **Senha** | Até 31 caracteres cada | [cap. 21](#cap-21-campos-mqtt) |
| **QoS** | Só 0 | [cap. 21](#cap-21-campos-mqtt) |
| **Keep-Alive** | 10 a 300 s; fábrica 60 s | [cap. 21](#cap-21-campos-mqtt) |
| Mensagens por lote | Até 5 registros: uma por registro. Mais de 5: uma com o lote. Corpo acima de cerca de 8 KB: uma por registro | [cap. 21](#cap-21-mensagens) |
| Conexão ao broker | No máximo uma tentativa a cada 5 s; 4 s por operação | [cap. 21](#cap-21-conexao-mqtt) |
| Entrega no MQTT | Mensagens escritas e conexão viva 60 ms depois | [cap. 21](#cap-21-entrega) |
| Versão e cifras TLS | Só TLS 1.2; ECDHE-ECDSA ou ECDHE-RSA com AES-128-GCM ou AES-256-GCM | [cap. 21](#cap-21-tls) |
| Fragmento TLS | Até 4.096 bytes (*Maximum Fragment Length*) | [cap. 21](#cap-21-tls) |
| `/cert.pem` | Até 16 KB; maior é recusado e a conexão segue sem validar o servidor | [cap. 21](#cap-21-certpem) |

## Linha de alarmes {#cap-28-linha}

| Item | Valor | Onde |
|---|---|---|
| **Tamanho da fila (RAM)** (`a_qmax`) | 1 a 64; fábrica 32 | [cap. 22](#cap-22-fila) |
| Fila cheia | Recusa o evento novo e guarda os antigos (evento 553) | [cap. 22](#cap-22-fila) |
| Reinício | A fila está na RAM: o que não foi confirmado se perde | [cap. 22](#cap-22-fila) |
| Registros por envio HTTP | A fila inteira, até 64 | [cap. 22](#cap-22-http) |
| Nova tentativa | A cada 15 s, sem espera crescente, e a cada evento novo | [cap. 22](#cap-22-http) |
| Resposta do coletor HTTP | 4 s | [cap. 22](#cap-22-http) |
| Confirmação pelo MQTT | Até 64 números `seq` por mensagem | [cap. 22](#cap-22-mqtt) |
| Espaço de uma mensagem MQTT | 2.048 bytes, cerca de 120 bytes por registro no modelo de fábrica. Com MQTT, use uma fila de até 16 | [cap. 22](#cap-22-mqtt) |
| Confirmações MQTT | Só são lidas com a telemetria ligada (**Lote mínimo** acima de 0) | [cap. 22](#cap-22-mqtt) |
| `seq` | De 1 a 65.535; volta a 1 e recomeça a cada reinício | [cap. 22](#cap-22-registro) |
| **Caminho HTTP** | Até 31 caracteres; vazio usa o **Endpoint** com `/alarm` | [cap. 22](#cap-22-campos) |
| Modelos | **1. Global** até 255 caracteres; **2. Linha** até 511; **3. Separador** até 7 | [cap. 22](#cap-22-campos) |
| [air]{.img} Air | Alarmes só acordado (M0); a fila não atravessa o sono | [cap. 22](#cap-22-fila) |

## Syslog, Prometheus e Home Assistant {#cap-28-outras-integracoes}

| Item | Valor | Onde |
|---|---|---|
| Syslog: formato | RFC 5424 sobre UDP, *facility* `local0` | [cap. 25](#cap-25) |
| Syslog: **Porta UDP** | 1 a 65535; fábrica 514 | [cap. 5](#cap-05-syslog) |
| Syslog: **Nível Mínimo** | 0 (Debug) a 4 (Fatal); fábrica 1 (Info) | [cap. 5](#cap-05-syslog) |
| Syslog: linha | Até 256 bytes por datagrama | [cap. 25](#cap-25) |
| Syslog: fila | 8 linhas; cheia, descarta a mais antiga. Sem Wi-Fi, as linhas esperam nela | [cap. 25](#cap-25) |
| Prometheus: rota | `GET /metrics`, formato de texto 0.0.4, 45 famílias de métricas | [cap. 24](#cap-24-metricas) |
| Prometheus: acesso | HTTP Basic, com senha de até 128 caracteres, ou a sessão da web; a conta precisa da permissão **Painel**. `401` sem conta e senha válidas, `403` sem a permissão | [cap. 24](#cap-24-autenticacao) |
| Prometheus: senha errada | Conta no mesmo bloqueio por IP da entrada web; bloqueado, recebe `429` | [cap. 24](#cap-24-bloqueio) |
| Prometheus: intervalo de coleta | `scrape_interval` de 60 s, nunca menos de 15 s. Cada leitura ocupa o aparelho por cerca de 0,69 s (v2.2.13-beta, 19/08/2026) | [cap. 24](#cap-24-intervalo) |
| Home Assistant: descoberta | Pelo MQTT, uma mensagem retida por medição em `homeassistant/sensor/<nó>/<objeto>/config` | [cap. 23](#cap-23-topicos) |
| Home Assistant: requisitos | Formato JSON; lotes de até 5 registros, para que o Home Assistant leia os valores | [cap. 23](#cap-23-requisitos) |

## Arquivos, backup e atualização {#cap-28-arquivos}

| Item | Valor | Onde |
|---|---|---|
| Envio de arquivo | Recusado se for maior que o espaço livre | [cap. 17](#cap-17-enviar) |
| Nome de arquivo | Até 64 caracteres, sem `..`, `%`, caracteres de controle, barra vertical nem `\ " : < > ? *` | [cap. 29](#cap-29-abuso) |
| Pasta `/config` | Nenhum envio, exclusão nem criação | [cap. 29](#cap-29-abuso) |
| Backup | Um arquivo `.bkp`, só para o administrador completo | [cap. 17](#cap-17-backup) |
| Proteções do backup | Cabeçalho de 40 bytes com CRC32 próprio, CRC32 do conteúdo e a identidade do chip | [cap. 17](#cap-17-backup) |
| Restauração | Só no mesmo aparelho; no máximo 200 arquivos, com até 4 KiB de nomes somados | [cap. 17](#cap-17-restauracao-recusa) |
| Atualização: tamanho da imagem | De 100 KiB a 1.016 KiB (1.040.384 B) | [cap. 17](#cap-17-ota-conferencias) |
| Atualização: outras conferências | Início de imagem válido para o RP2040 nos primeiros 256 bytes; etiqueta da variante igual à do aparelho | [cap. 17](#cap-17-ota-conferencias) |
| [release]{.img} Captura do painel | BMP de 320 × 240 pixels, 230.454 bytes | [cap. 13](#cap-13-captura) |

## SIMUT Air {#cap-28-air}

[air]{.img}

| Item | Valor | Onde |
|---|---|---|
| Ociosidade até hibernar (`air idle`) | 10 a 65535 s; fábrica 300 s | [cap. 19](#cap-19-ociosidade) |
| Renovações antes de entrar na web | 3 por boot | [cap. 19](#cap-19-ociosidade) |
| Período do ciclo | O **Intervalo Histórico** | [cap. 19](#cap-19-ciclo) |
| Sono | O intervalo menos o tempo acordado; no mínimo 5 s, no máximo 1 s a menos que 24 h | [cap. 19](#cap-19-24h) |
| Fases do despertar | Aquecimento de 0,4 s; amostragem de até 30 s; conexão de até 30 s | [cap. 19](#cap-19-ciclo) |
| Rádio | Liga quando os registros pendentes chegam ao **Lote mínimo**; com o **Lote mínimo** em 0, nunca liga no ciclo | [cap. 19](#cap-19-telemetria) |
| Depois de um envio que falha | 5 despertares sem rádio | [cap. 19](#cap-19-telemetria) |
| Rede que não aparece | Duas tentativas por despertar | [cap. 19](#cap-19-telemetria) |
| Despertar de leitura | 9,31 s, dos quais 6,83 s são a conversão do DS18B20 (v2.4.1-beta, 08/09/2026) | [cap. 19](#cap-19-tempos) |
| Despertar de telemetria | Cerca de 12,7 s (v2.4.1-beta, 08/09/2026) | [cap. 19](#cap-19-tempos) |
| Período real | 60,5 a 60,6 s para 60 s configurados (v2.4.1-beta, 08/09/2026) | [cap. 19](#cap-19-tempos) |
| Consumo | Cerca de 8,2 mA de média e 17 dias numa 18650 de 3.400 mAh. É uma conta sobre os tempos, não uma medição de corrente | [cap. 19](#cap-19-tempos) |
| Lote com `t_bat=250` | Cerca de 190 registros, limitado pela memória (Air em M0, v2.7.1, 23/09/2026) | [cap. 19](#cap-19-telemetria) |
| Relógio através do sono | Na v2.7.1, perde até cerca de 1 s por despertar até o próximo acerto por NTP | [cap. 19](#cap-19-relogio) |
| Relógio através do sono | Cerca de ±0,09 s por despertar, sem acúmulo (−0,085 a +0,030 s em 10 despertares, bancada, 23/09/2026) | [cap. 10](#cap-10-air) |

::: atencao
**O Air é experimental.** Os tempos acima foram medidos em bancada, com o **Intervalo Histórico** de 1 min e um DS18B20 a 12 bits. O único teste longo do ciclo parou depois de 119 ciclos, em 10/09/2026 ([capítulo 19](#cap-19-status)).
:::

## Tempos que o operador percebe {#cap-28-tempos}

| Situação | Tempo | Onde |
|---|---|---|
| Boot sem rede | O aparelho espera a rede por até 30 s e segue sem ela; o painel mostra **Timeout de rede. Iniciando Offline...** | [cap. 11](#cap-11-boot) |
| [release]{.img} Um toque no painel ocupa o aparelho | 5 s; nesse intervalo, gravações, capturas e exportações respondem `503` com `Retry-After` de 5 s nas gravações e 3 s nas leituras | [cap. 26](#cap-26-ocupado) |
| [release]{.img} Console durante um toque | Guarda até 2 comandos e os executa depois dos 5 s | [cap. 14](#cap-14-usb) |
| [release]{.img} Volta do painel à tela inicial | 30 s sem toque | [cap. 11](#cap-11-ocioso) |
| [release]{.img} Aviso `Web: <conta>` no painel | 5 s depois de uma entrada na web | [cap. 11](#cap-11-barra) |
| [release]{.img} **Silenciar 120s** | 120 s | [cap. 7](#cap-07-silenciar) |
| [alpha]{.img} Troca de grandeza no LCD | A cada 3 s; a tela atualiza a cada 0,5 s | [cap. 12](#cap-12-leituras) |
| **Painel de Controle** da web | Consulta o aparelho a cada 3 s | [cap. 13](#cap-13-painel-de-controle) |
| Página depois de **Salvar e reiniciar** | Recarrega sozinha cerca de 12 s depois | [cap. 13](#cap-13-sessoes) |
| Página depois de mudar a **Porta HTTP** | Abre o endereço novo cerca de 15 s depois | [cap. 9](#cap-09-servidor-web) |
| Busca de redes | Cerca de 1 s | [cap. 9](#cap-09-busca) |
| Restauração de backup | A página avisa que o aparelho está fora do ar depois de 90 s | [cap. 17](#cap-17-restauracao) |
| Atualização: envio e conferência | 34,9 a 35,9 s (v2.7.0, 22/09/2026, 6 atualizações) | [cap. 17](#cap-17-ota-tempos) |
| Atualização: da aplicação até o aparelho voltar | 52 a 56 s (v2.7.0, 22/09/2026, 6 de 6 bem-sucedidas) | [cap. 17](#cap-17-ota-tempos) |
| Atualização: cópia da imagem | Cerca de 25 s em que uma falta de energia exige BOOTSEL e cabo USB | [cap. 17](#cap-17-energia) |
| [release]{.img} Quadro do espelho do painel | 213 ms dentro do aparelho (v2.5.0-beta, 19/09/2026) | [cap. 13](#cap-13-ao-vivo) |
| [release]{.img} Captura do painel | 1,69 s (v2.4.10-beta, 18/09/2026) | [cap. 13](#cap-13-captura) |
