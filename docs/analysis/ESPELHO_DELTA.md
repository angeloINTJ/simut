# Espelho do painel por delta — estudo

**Estado:** Living · **Medido em:** 2026-09-18 (§1–§9) e 2026-09-19 (§10, §11),
rig 192.168.3.24, `pico_w_test` · **Pergunta:** vale mandar só os blocos que
mudaram?

> **§10 mede o que este documento vinha estimando.** A §9.4 dizia que as pausas
> do Core 1 eram 23–29% do quadro; medidas diretamente no aparelho, são 14%, e a
> rede é 14% e não 7%. As duas contas antigas eram aritmética sobre um total de
> quadro menos uma leitura modelada. A alavanca que a §9.4 apontou existe mesmo,
> mas é menor do que ela prometia — e foi puxada: **643,5 → 565,0 ms**, com as
> duas distribuições disjuntas em 20 quadros por braço.
>
> **§11 varreu o resto do caminho** (clock de leitura, rede, conversão, codec) e
> chegou a **613,2 → 212,8 ms, −65,3%**, com o payload 40% menor. O quadro agora
> é limitado pelo **barramento SPI**, e a partir daqui só encurta lendo menos
> pixels.

A resposta curta é que vale, mas **não pelo motivo que a pergunta sugere**, e
hoje não cabe na imagem. As duas coisas estão medidas abaixo.

---

## 1. Onde o tempo vai hoje

> **Atualizado em 18/09, depois que o estudo virou conserto.** A seção 9 mede de
> novo o que este documento chamava de teto: o clock de leitura era 2 MHz por
> convenção e não por exigência do painel, e a "transferência em bloco" do
> framework é um laço byte a byte. Corrigidas as duas, o quadro caiu de 1,53 s
> para 0,57 s e a leitura deixou de ser 91% da conta. As proporções abaixo são as
> de antes disso; as conclusões sobre o delta continuam valendo, com margem menor.

Um quadro do `/api/screen_stream` custava **1,53 s** no ferro. A conta, medida em
quatro caminhos independentes e fechando dentro de 7%:

| parcela | por quadro | fração |
|---|---:|---:|
| ler o painel (240 linhas × 5,79 ms) | 1.390 ms | **91%** |
| pausas do Core 1 (30 × ~3,7 ms) | 111 ms | 7% |
| rede (≈9 kB a 221 KB/s) | 43 ms | 3% |

**O payload já é 3% do problema.** Mandar menos bytes — que é o que "só os
blocos que mudaram" parece prometer — mexe nesses 3%. Se a leitura continuar
sendo do quadro inteiro, comprimir melhor ou mandar menos não compra nada.

O ganho real de um sistema de blocos é outro: **não LER o que não mudou.**

---

## 2. Quanto a tela muda de verdade

Medido no dashboard vivo (relógio correndo, cinco sensores), com o painel
**livre entre as capturas** — a primeira tentativa capturou quadro atrás de
quadro e mediu 0 pixels alterados, porque o próprio espelho mata o Core 1 de
fome e o painel não repinta enquanto ele corre. Essa contaminação é a primeira
armadilha do assunto e vale para qualquer medição futura aqui.

12 amostras, 2 s de painel livre cada:

| | pixels alterados | % da tela |
|---|---:|---:|
| mediana | 76 | 0,10% |
| p90 | 162 | 0,21% |
| máximo | 169 | 0,22% |

Tudo numa faixa só: **linhas 8..20**, o relógio do topo. Os dígitos dos
sensores entram de vez em quando e não alargam a faixa.

### O que isso custa de leitura, por tamanho de bloco

A leitura é por linha inteira (o `readRow` varre 320 px), então o que importa
não é quantos blocos mudaram e sim **quantas linhas eles tocam**, alinhadas ao
bloco:

| bloco | blocos alterados (mediana / máx) | linhas a ler | leitura |
|---|---:|---:|---:|
| 8 px | 6 / 12 | 16 | **93 ms** |
| 16 px | 3 / 8 | 32 | 185 ms |
| 24 px | 2 / 4 | 24 | 139 ms |
| 40 px | 1 / 2 | 40 | 232 ms |
| — hoje | — | 240 | 1.390 ms |

**8 px ganha** e não por pouco: 93 ms contra 1.390 ms, **15× menos leitura**.
Blocos de 16 px saem piores que os de 24 porque a faixa do relógio cai em cima
da fronteira e suja duas bandas.

### O caso degenerado

Trocar de tela (dashboard → teclado de PIN) altera **65% dos pixels, 1.103 dos
1.200 blocos, as 240 linhas**. O delta vira o quadro inteiro, 1.390 ms — igual
a hoje. Isso é o comportamento certo: não existe ler menos do que mudou. O
sistema não tem pior caso além do que já se paga.

---

## 3. O aparelho consegue saber o que mudou sem ler o painel?

Essa é a pergunta que decide tudo. Se para descobrir o que mudou for preciso
ler, não se economiza leitura nenhuma.

**Consegue.** Todo pixel que chega ao ILI9341 precisa de uma janela de endereço
antes, e o `TftWithOffset` já intercepta exatamente isso — o cabeçalho dele diz,
palavra por palavra, que "toda primitiva de desenho — texto, retângulos,
escritas diretas de pixel — passa por `setAddrWindow`". O override é `virtual` e
já existe, porque é onde o deslocamento do painel é aplicado.

Quem chama, e com que frequência:

- **45 blits** (`blitCanvas` / `blitWindowDma` / `fillWindowDma`) — cada um já
  carrega o retângulo explícito. Custo do gancho: uma vez por blit.
- **As primitivas diretas** (32 `print`, 20 `fillRect`, 13 `drawLine`, …) descem
  para `Adafruit_SPITFT::writePixel`, que faz `setAddrWindow(x, y, 1, 1)`
  **por pixel**. O relógio, que é justamente o que muda, é desenhado assim.

Então o gancho é universal, mas é pago por pixel no caminho de texto. Marcar um
bloco é calcular dois índices e um `__atomic_fetch_or` numa palavra: ~10–15
instruções, contra os vários microssegundos que a própria janela de endereço já
gasta em SPI. **Estimativa de 2 a 5% no caminho de texto — não medido.**

### ⚠️ A armadilha que vai morder quem implementar

`readRow` também chama `setAddrWindow` — a leitura de GRAM abre janela como
qualquer escrita. Um gancho ingênuo marcaria como suja exatamente a região que o
espelho acabou de ler, e o espelho passaria a se auto-sujar em laço fechado,
lendo a tela inteira para sempre. **O gancho precisa ser suprimido durante a
leitura** (uma flag em volta do `readRow`/`read_chunk_bgr`), e um teste tem que
provar que dois quadros seguidos de tela parada devolvem "nada mudou".

---

## 4. O que isso vira em números

Com blocos de 8 px, mapa de sujeira alimentado pelo `setAddrWindow`, e o piso de
HTTP medido em **16 ms** (mediana de 10 `GET /api/perms`):

| cenário | leitura | pausas | rede | total | hoje |
|---|---:|---:|---:|---:|---:|
| **tela parada** (nada sujo) | 0 | 0 | ~0 | **~16 ms** | 1.530 ms |
| **dashboard vivo** (6 blocos) | 93 ms | ~10 ms | ~2 ms | **~120 ms** | 1.530 ms |
| **troca de tela** | 1.390 ms | 111 ms | 43 ms | **1.545 ms** | 1.530 ms |

O payload do caso típico são 6 blocos de 8×8 = 384 pixels; com o RLE de paleta
que já existe, mais o cabeçalho de posição por bloco, dá **~200 B**. A rede
deixa de aparecer na conta.

### O que muda para quem usa

Três coisas, e a segunda é a que importa:

1. **Tela parada custa um round-trip de 16 ms**, não 1,5 s. O espelho pode
   perguntar "mudou alguma coisa?" cinco vezes por segundo sem encostar no
   painel — hoje ele paga 1,5 s de SPI para descobrir que nada mudou.
2. **O resultado de um toque aparece em ~200 ms**, não em ~2,3 s. Hoje o toque
   espera o quadro em voo terminar, mais 600 ms de silêncio para o painel
   repintar, mais uma captura inteira. Com a leitura curta o Core 1 quase não é
   interrompido, e os 600 ms de `TOUCH_SETTLE` provavelmente caem junto — eles
   existem porque a captura de 1,4 s rouba o core que consome o UiEvent.
3. **A taxa sustentável sai de 0,65 para ~3–5 fps**, limitada não pela rede mas
   pelo quanto de painel se quer travar: a 8 fps o painel ficaria bloqueado 77%
   do tempo e voltaria a não repintar. O ritmo passa a ser uma escolha, e não um
   teto de hardware.

---

## 5. Desenho proposto

**Mapa de sujeira.** 1.200 blocos de 8×8 = 150 B de BSS, 38 palavras de 32 bits.
Core 1 marca com `__atomic_fetch_or`; o handler do Core 0 faz troca-e-limpa da
palavra inteira no começo da captura. O que sujar durante a leitura fica para o
quadro seguinte, que é o comportamento correto.

**Protocolo.** `GET /api/screen_stream?d=1` devolve o mesmo formato de hoje
(`src/ScreenRle.h`) com um cabeçalho de bloco em vez de faixa: `x:u8, y:u8` em
unidades de bloco, `enc:u8`, `len:u16`. Sem blocos sujos, devolve só o cabeçalho
de quadro — 9 bytes. O cliente pinta os blocos por cima do canvas que já tem.

**Quadro-chave.** O cliente pede quadro inteiro ao abrir e a cada N segundos. É
o que fecha o buraco de um retângulo sujo perdido (a biblioteca tem caminhos que
podem desvirtualizar o `setAddrWindow` — é por isso que o `setOffsetBypass`
existe). Com N = 30 s o custo amortizado é 1,4 s / 30 s = **4,6%**.

**Entrega garantida.** Limpar o bit no começo e perder a conexão no meio deixa o
bloco desatualizado para sempre. O handler precisa restaurar os bits se o envio
falhar — ou limpar só depois do último `safeSend` bem-sucedido.

---

## 6. 🔴 O que impede isso hoje

**A folga de OTA da imagem de release é 484 B.**

Estimativa do custo: mapa + gancho + montagem por bloco + protocolo, algo entre
600 e 1.200 B de firmware, mais ~1 kB de página comprimida no cliente.
**Entre 1,5 e 2 kB contra 484 B disponíveis** — não cabe, e o portão novo do
`check_flash_budget.py` vai reprovar a imagem antes do CI terminar.

Precisa vir antes:

| candidato | libera | custo |
|---|---:|---|
| `custom_fs_pages = HIST_PAGE` | ~28 kB | serve a página do LittleFS; o `build_webui_gz.py` **recusa** isso nos ambientes que embarcam, e com razão: o apply do OTA zera o LittleFS e a página sumiria depois de cada atualização |
| CFG_PAGE pelo mesmo caminho | ~12 kB | mesma objeção |
| dieta de strings/JS nas 13 páginas | ? | não medido |

A saída barata não existe: as duas maiores são páginas que o OTA apagaria. Ou se
resolve o problema de as páginas sobreviverem ao OTA — o que é um projeto em si —
ou se corta de dentro do firmware.

---

## 7. O que NÃO vale a pena

- **Comprimir melhor.** A rede já é 3% do quadro. Um codec duas vezes melhor
  ganha 1,5% do tempo.
- **Aumentar a faixa para 16 ou 40 linhas.** Medido: as pausas do Core 1 custam
  ~3,7 ms cada, e faixas de 16 linhas compraram 3,6% num A/B no ferro. O teto é
  a leitura, não a geometria da faixa.
- **Espelhar o canvas em vez de ler a GRAM.** Elimina a leitura inteira, mas
  passa a mostrar o que o firmware *pretendia* desenhar, não o que o painel
  mostra — e o `/api/screenshot` existe justamente para provar que a fiação
  aguenta 62,5 MHz. Uma sombra parcial do framebuffer também não cabe: 153.600 B
  contra ~63 kB de heap livre.

## 8. A alavanca independente — FEITA, e valeu 2,5×

Era a recomendação deste estudo e foi executada no mesmo dia. Está na seção 9.

---

## 9. O que a leitura realmente custava (18/09)

O documento dizia "5,79 ms por linha" como se fosse física. Não era: eram duas
escolhas de software, e as duas estavam erradas.

### 9.1 O clock de leitura era 2 MHz por convenção

O `readRow` abria a transação com `SPISettings(2000000, ...)` e nada, em lugar
nenhum, dizia por quê. Medido no ferro numa tela **provadamente parada** (o
controle é dois quadros a 2 MHz devolvendo 0 pixels diferentes — sem esse
controle o teste mede o relógio do painel, e mediu, duas vezes, antes de eu
perceber):

| clock | quadro | µs/px | pixels errados |
|---|---:|---:|---:|
| 2 MHz | 1,303 s | 15,0 | 0 |
| 4 MHz | 0,742 s | 7,7 | 0 |
| **6 MHz** | **0,550 s** | **5,2** | **0** |
| 8 MHz | 0,476 s | 4,2 | 0 |
| 12 MHz | 0,369 s | 2,8 | 0 |
| 16 MHz | 0,368 s | 2,8 | 0 |

Mais um soak de 30 quadros a 6 MHz e 30 a 12 MHz: **zero pixels errados nos
dois**. O default ficou em 6 MHz porque o ciclo de leitura serial do ILI9341 dá
~6,6 MHz e isso fica dentro dele; 12 MHz mediu igualmente limpo **neste módulo e
nesta fiação** e está a uma constante de distância (`SIMUT_TFT_READ_HZ`), com o
mesmo trato que o clock de escrita de 62,5 MHz já documenta.

### 9.2 A "transferência em bloco" do framework não é em bloco

`SPIClassRP2040::transfer(void* buf, size_t count)` é um laço que chama a
versão de **um byte**. Trocar o `readRow` por ele comprou 5,5% — e foi isso que
denunciou o problema, porque deveria ter comprado muito mais.

O caminho rápido é a sobrecarga de dois buffers com `tx = nullptr`, que cai em
`spi_read_blocking` do SDK. A diferença, medida: **~4,3 µs por pixel de puro
software**, que a 6 MHz é mais que o próprio fio.

### 9.3 Ler janela estreita funciona — e é o que o delta precisa

Quatro janelas de 80×8 por faixa, em vez de uma de 320×8: **0 pixels diferentes**
do caminho forense, e o custo de abrir 4× mais janelas foi ~0,2 ms cada. Ou
seja, **o custo da leitura é por pixel lido, não por janela aberta**. É a
premissa que faltava para a seção 2: ler só os blocos sujos custa proporcional à
área deles, e não à linha inteira que os contém.

### 9.4 O resultado

| | antes | agora |
|---|---:|---:|
| espelho, tela parada | 1,530 s | **0,571 s** |
| espelho, dashboard vivo | 1,530 s | **0,622 s** |
| `/api/screenshot` (3 leituras + voto) | 4,26 s | **1,93 s** |
| pixels diferentes entre os dois caminhos | 0 | **0** |

O orçamento do quadro mudou de dono:

| parcela | tela parada | dashboard vivo |
|---|---:|---:|
| leitura (399 ms) | 70% | 64% |
| pausas do Core 1 | 23% (129 ms) | 29% (180 ms) |
| rede | 7% | 7% |

As pausas ficaram **mais caras em tempo absoluto** (111 → 129-180 ms) justamente
porque a leitura ficou rápida: o Core 1 recebe mais tempo entre capturas, tem
mais o que pintar, e demora mais para estacionar quando o handshake pede.

### 9.5 O que isso faz com o resto do estudo

O delta por blocos continua valendo e **fica mais barato de justificar**: com a
leitura a 5,2 µs/px, os 6 blocos de 8×8 do caso típico custam **2 ms** de
leitura, não 93. O gargalo do espelho passa a ser a pausa do Core 1, que é
justamente o que menos blocos sujos também reduzem — menos leitura, menos pausa.

O que NÃO mudou é o impedimento: a folga de OTA da imagem de release, que este
próprio conserto consumiu um pouco mais (484 → **412 B**).

---

## 10. A pausa do Core 1, medida e encurtada (19/09)

A §9.4 encerrou dizendo "a próxima alavanca é a pausa do Core 1, não o fio".
Isto é a execução dessa frase. Três coisas saíram dela: a conta da §9.4 estava
errada, a pausa é quase toda **espera do park** e não handshake do SDK, e o
quadro caiu 11%.

### 10.1 O instrumento, antes de qualquer número

Nada aqui foi deduzido de `total − modelo`. O próprio aparelho cronometra cada
parcela com `timer_hw->timerawl` e as publica em `show metrics`
(`SIMUT_MIRROR_PROBE=1`, default 0 — ver `src/simut_config.h`):

- **Um único binário para todos os braços.** `?pm=<máscara>&g=<n>` escolhe a
  estratégia por requisição. Comparar duas *builds* seria medir a build — foi
  isso que a §2 já tinha aprendido, e o custo da instrumentação se cancela
  quando ela está nos dois lados.
- **Braços intercalados**, um por rodada, e não em blocos: o dashboard muda
  sozinho (relógio, dígitos) e o custo do render muda com ele, então um layout
  em blocos cobra a deriva de quem calhou de rodar durante ela.
- **Coluna de resíduo** = quadro − soma das parcelas. Fechou em 1,7% a 2,7%. Uma
  decomposição que não fecha está avisando que falta algo.
- **Controle A-contra-A em capturas adjacentes** e comparação pixel a pixel
  contra ele.

⚠️ **Duas vezes o instrumento mentiu antes do firmware, e as duas foram pegas:**

1. `PLATFORMIO_BUILD_FLAGS` apaga `.pio/build`: o primeiro `-t upload` rodou
   **sem** a variável, recompilou sem a sonda e gravou a imagem errada. A string
   de versão não discrimina — quem discriminou foi a ausência das linhas `MIR`, e
   o harness se recusou a produzir número.
2. **A tela volta ao dashboard 30 s depois do último toque** (guarda de ociosidade
   do `DisplayManager_Touch.cpp`). Uma comparação de pixels de vários minutos na
   tela `lic` passou a comparar **duas telas diferentes** e acusou 66.310 px
   errados de 76.800. O conserto é reemitir `screen <n>` antes de cada captura —
   e o sintoma é exatamente o que um defeito de leitura pareceria.

### 10.2 Onde o tempo vai de verdade

Dashboard vivo, 20 quadros intercalados, mediana:

| parcela | por quadro | fração | a §9.4 dizia |
|---|---:|---:|---:|
| ler o painel (30 × `readRect`) | 410,2 ms | 64% | 64–70% ✅ |
| **espera do park** | 107,0 ms | 17% | — |
| rede (30 escritas chunked) | 88,1 ms | 14% | 7% ❌ |
| RLE de paleta | 23,0 ms | 4% | — |
| **handshake de lockout do SDK** | 4,4 ms | 0,7% | — |
| resíduo | 9,9 ms | 2% | — |

**A pausa é 111,4 ms, não 129–180.** E dentro dela, **96% é o park** — Core 0
esperando Core 1 chegar ao topo do seu laço — contra 4% dos dois handshakes do
SDK. Isso decidiu tudo: otimizar o handshake renderia no máximo 3,6 ms.

Numa tela **parada** (`lic`) a mesma pausa custa 22,5 ms de 554,5 (4%), porque o
render que o park espera é quase nada. A pausa só é cara quando o painel tem o
que pintar — e é por isso que qualquer medição dela feita na tela de licença
subestima o problema por 4×.

### 10.3 Os braços, medidos

Máscara: `1` = park sem o lockout do SDK, `2` = pedir o park **antes** do envio
do quadrado anterior, `4` = Core 1 para de pintar durante a captura.
Dashboard vivo, 7 quadros por braço, intercalados:

| pm | estratégia | quadro | park | lock+unlk | leitura | renders do Core 1 |
|---:|---|---:|---:|---:|---:|---:|
| 0 | como estava | 655,6 ms | 123,6 | 4,2 | 411,6 | 64 |
| 1 | SOFT | 608,6 ms | 83,8 | 0,1 | 412,1 | 65 |
| 2 | PREPARK | 624,4 ms | 79,9 | 4,2 | 423,1 | **30** |
| 4 | C1IDLE | 559,1 ms | 19,4 | 3,9 | 418,4 | **0** |
| 3 | SOFT+PREPARK | 611,3 ms | 74,0 | 0,1 | 427,2 | **30** |
| 5 | **SOFT+C1IDLE** | 585,1 ms | 51,9 | 0,0 | 418,9 | **0** |
| 6 | PREPARK+C1IDLE | 558,4 ms | 16,6 | 4,2 | 428,5 | **0** |
| 7 | as três | 557,1 ms | 28,2 | 0,0 | 430,6 | **0** |

A coluna que decide não é o quadro, é a última: **o que cada braço cobra em
repintura**. PREPARK corta pela metade os renders que o Core 1 completa durante
um quadro, e C1IDLE os zera. Não é um preço abstrato — é a coisa que o espelho
existe para mostrar.

O que torna C1IDLE aceitável é o que a §2 já tinha medido: capturando quadro
atrás de quadro, o painel repinta **0 pixels** de qualquer jeito, porque o
espelho já mata o Core 1 de fome. O render que C1IDLE descarta é um que o
espelho não estava mostrando. Entre dois quadros o Core 1 pinta normalmente.

**O default ficou em `pm=5`, não em `pm=7`** — e a razão honesta não é que o
PREPARK custe repintura: **sobre o C1IDLE ele não custa nenhuma**, porque o
C1IDLE já não repinta nada. É que o ganho dele nunca se firmou. Em quatro
rodadas o `pm=7` ganhou do `pm=5` por algo entre **0 e 28 ms**, com as faixas
muito sobrepostas, e o `pm=7` é justamente o braço que mais se apoia na inflação
de leitura inexplicada da §10.4 (431 ms contra 423). Somando a isso um pedido de
park que sobrevive à pausa que o levantou — superfície de concorrência nova —
não há número que pague.

A coluna de repintura acima **importa para o PREPARK sozinho** (`pm=2`): ali ele
corta os renders de 64 para 30 sem zerá-los, e é a única configuração em que esse
custo é visível.

### 10.4 ⚠️ A leitura fica mais lenta quando a pausa encurta — e eu não sei por quê

Em **todo** braço que encurta a pausa, `readRect` demora mais: 411,9 → 419,2 ms
no default (+7,3), e até 431,6 ms no `pm=7` (+19,7). A geometria é idêntica, o
clock é o mesmo e os bytes saem iguais. Devolve-se assim cerca de 10% do ganho.

Uma hipótese foi **implementada, medida e REVERTIDA**, e vale registrar porque
era a mais convincente das duas que eu tinha:

> *O laço do park roda de XIP (uma carga atômica, uma chamada a `millis( )` e o
> retorno, por volta) durante todo o tempo em que o Core 0 está lendo, e
> `readRect` também é XIP — então o park disputaria banda de flash com a leitura
> que ele existe para proteger.* Sob o default novo isso pesaria mais, não menos:
> o Core 1 passa a ficar no laço ~420 ms por quadro em vez de 3 ms por faixa.

Levei o laço para SRAM (`__no_inline_not_in_flash_func`) e passei a ler o timer
uma vez a cada 512 voltas em vez de chamar `millis( )` sempre. Depois medi de
verdade, com o laço antigo **alcançável por máscara na mesma imagem** (`pm|8`),
12 quadros por braço:

| par | quadro, laço em SRAM | quadro, laço antigo | SRAM ganha |
|---|---:|---:|---:|
| `pm=5` | 576,2 ms (549..592) | 563,7 ms (549..602) | 49,3% dos pares |
| `pm=1` | 635,5 ms (617..664) | 624,8 ms (606..655) | 29,2% dos pares |

**Não compra nada, e no braço SOFT o laço antigo ganha mais vezes do que perde.**
A leitura melhora 4 ms no par `pm=5` (417,7 contra 421,7, SRAM ganhando 80,6% dos
pares), o que é pequeno e não sobrevive à faixa. Revertido: é um laço no caminho
crítico de concorrência, e uma mudança ali sem número é risco sem retorno.

⚠️ Antes de medir, eu já tinha escrito num comentário de código que a mudança
recuperava 18 ms — número que eu tirei de ler errado a minha própria tabela (o
429 ms era do braço `pm=7`, não do SOFT; o SOFT mediu 411,5 antes e 412,0
depois). O comentário foi apagado junto com o código. **A regra da casa pegou:
quem afirma ganho carrega a medição.**

O que também foi descartado: o laço lendo `timer_hw` a cada volta disputando a
ponte APB com a spi0 — o gate de 512 voltas não mudou nada.

A inflação, aliás, **é anterior** a qualquer mexida minha no laço: já estava lá
na primeira rodada factorial, e acompanha PREPARK e C1IDLE, não o laço.

O que sobra e não foi medido: `read + send` é praticamente constante entre os
braços (~503 ms contra 499 no baseline), o que é a forma de um **acoplamento via
lwIP** — se a leitura demora mais, a fila de envio drena nesse meio-tempo e o
`safeSend` seguinte bloqueia menos. Isso explicaria a gangorra sem que a leitura
esteja de fato mais lenta no fio; explicaria também por que a soma não se move.
**Não está provado.** Quem for medir: cronometre dentro do `readRect` separando
`spi_read_blocking` da conversão RGB565, e rode uma vez com a rede fora do laço.

### 10.5 O que foi verificado além do cronômetro

- **Pixels idênticos.** Em tela parada, `pm=0`, `1`, `5` e `7` devolvem **0 px
  diferentes de 76.800** contra uma captura baseline adjacente, e o quadro tem
  exatamente os mesmos **5.323 B** nos quatro. O controle A-contra-A deu 0.
- **O toque continua chegando ao painel.** Com `pm=5` e um laço de capturas, 5
  de 5 toques por `POST /api/touch` mudaram o painel (~4.800 px), igual ao
  `pm=0`; o controle sem toque mexe no máximo 118 px. O C1IDLE tem escape por
  **PENIRQ** (`isScreenTouched( )`, um `gpio_get`, sem SPI) porque o portão que
  faz o painel ganhar do espelho só arma depois que um toque é *detectado*, e a
  detecção mora justamente na leitura que o C1IDLE pula. ⚠️ O escape por dedo
  real **não foi testado no ferro** — a PicoHand não está ligada ao GP20. A prova
  indireta de que o PENIRQ lê ALTO sem ninguém é que o C1IDLE encurta o park:
  se disparasse à toa, o ramo nunca seria tomado.
- **Saúde do Core 1 intacta** depois de ~300 capturas: 0 kills por lockout, 0
  lockout travado, 0 kills por saúde, nenhum FTL fora dos falsos de boot que o
  flash por toque sempre deixa.
- **`_captureActive` tem prazo próprio** (15 s, o teto do handler) e um RAII na
  única chamada. Um flag desses, se vazar, é um display morto para sempre — a
  mesma falha que o `forceUnpause( )` existe para desfazer no refcount.

### 10.6 Grupo de faixas: medido e recusado

Uma pausa segurando N faixas troca handshakes por congelamento e por heap
(N × 5.120 B). Dashboard vivo:

| braço | quadro | park | pausas | heap extra |
|---|---:|---:|---:|---:|
| `pm=0 g=1` | 634,9 ms | 107,8 | 30 | — |
| `pm=0 g=2` | 596,7 ms | 65,9 | 15 | 5 kB |
| `pm=0 g=5` | 576,6 ms | 54,1 | 6 | 20 kB |
| `pm=5 g=1` | 557,0 ms | 19,5 | 30 | — |
| `pm=5 g=5` | 548,2 ms | 16,8 | 6 | 20 kB |

Agrupar **sozinho** vale 58 ms, mas sobre o default vale **8,8 ms por 20 kB de
heap** num aparelho cuja mínima histórica passa perto de 40 kB. Recusado, e o
`g=` fica só na sonda.

### 10.7 O resultado

Dashboard vivo, 20 quadros intercalados por braço, **o binário que ficaria**:

| | antes | depois |
|---|---:|---:|
| quadro (device) | 643,5 ms | **565,0 ms** (−12,2%) |
| quadro (relógio do cliente) | 666,2 ms | **583,0 ms** (−12,5%) |
| espera do park | 107,0 ms | **30,2 ms** |
| handshake de lockout | 4,4 ms | **0,0 ms** |
| taxa sustentável | 1,50 quadro/s | **1,72 quadro/s** |

A separação é **total**: as faixas não se tocam (620..665 ms contra 549..598) e
na comparação par a par o `pm=5` ganha **400 de 400**. Na tela parada, onde a
pausa já era pequena, o ganho é o esperado e pequeno: 556,2 → 543,3 ms, com os
mesmos 5.323 B e 0 px de diferença.

Custo em flash: **+896 B** no release, +1.064 B no test, +56 B no alpha e +8 B no
Air (os dois sem painel só levam os carimbos de microssegundo). Todos os seis
ambientes seguem dentro do orçamento.

### 10.8 O que sobra

A leitura voltou a ser 74% do quadro, e agora sem nada grande ao lado. As duas
alavancas que restam, em ordem de tamanho:

1. **Ler por DMA.** `readRect` bloqueia o Core 0 em `spi_read_blocking` por
   412 ms. Um DMA de recepção libera esse tempo para a rede e para o codec, o que
   também atacaria os 88,5 ms de envio, que hoje são serializados atrás da
   leitura. É a única mudança grande que sobrou.
2. **Ler só os blocos sujos** (§3 a §5), que continua barrado pelo mesmo motivo
   da §6 e agora vale menos: com a pausa resolvida, o que o delta economiza é
   leitura, e a leitura já é o alvo do item 1.

---

## 11. A varredura do caminho inteiro (19/09)

A §10 resolveu a pausa e deixou a leitura em 65% do quadro. Isto mede as outras
parcelas — clock, rede, conversão, codec — com o mesmo instrumento e a mesma
disciplina: **um binário só, braços intercalados, pixel a pixel contra referência
e contra um leitor independente**.

### 11.1 A leitura, aberta em três

O que a §10 chamava de "leitura" são três coisas, e a proporção decide tudo:

| parcela | 6 MHz | o que é |
|---|---:|---|
| **fio** (`spi_read_blocking`) | 382,0 ms | 93% |
| conversão 6-6-6 → RGB565 | 12,0 ms | 3% |
| janela de endereço + RAMRD | 14,3 ms | 3,5% |

O teórico do fio a 6 MHz é 307 ms (76.800 px × 24 bits). **O fio é a leitura** — a
conversão, que eu esperava que fosse cara, é 3%. Otimizá-la (troca de indexação
por caminhada de ponteiro) não aparece na conta.

### 11.2 O clock: os degraus do PL022, e onde parar

Varredura com cada quadro comparado pixel a pixel contra a referência de 6 MHz,
em tela parada e reafirmada a cada captura:

| pedido | fio | quadro | pixels errados |
|---|---:|---:|---:|
| 6 MHz | 387,9 ms | 574,7 ms | **0** |
| 8 MHz | 293,7 ms | 479,2 ms | **0** |
| 10 MHz | 292,9 ms | 482,6 ms | **0** |
| **12 MHz** | **201,5 ms** | **386,8 ms** | **0** |
| 16 MHz | 204,6 ms | 392,3 ms | **0** |
| 20 MHz | 201,3 ms | 388,5 ms | **0** |

O divisor do PL022 só alcança degraus: **10 cai no degrau de 8, e 16 e 20 caem no
de 12**. Ou seja, pedir 20 MHz não compra nada sobre 12 — só gasta margem num
componente cujo ciclo de leitura serial dá ~6,6 MHz no papel. **12 MHz é o último
degrau que paga**, e é onde isto para. A §9.1 já tinha medido 12 como limpo; o que
faltava era saber que 16 e 20 são o mesmo degrau.

### 11.3 A rede: 61 escritas viram 7

Cada `safeSend` paga um `setTimeout` no cliente, um `feedWatchdog` e um
`waitSendRoom` antes do primeiro byte. O quadro fazia **duas chamadas por faixa,
61 no total**, de ~230 B cada. Um buffer que acumula antes de entregar:

| buffer | quadro | envio | chamadas |
|---|---:|---:|---:|
| 0 (como era) | 393,6 ms | 117,6 ms | 61 |
| 1 kB | 327,4 ms | 71,2 ms | 23 |
| 4 kB | 313,3 ms | 72,2 ms | 5 |
| 16 kB | 311,6 ms | 74,9 ms | 2 |

⚠️ **E depois do DMA isso INVERTE** (§11.4): com a leitura sobreposta, um buffer
grande deixa o Core 0 ocioso na maior parte das faixas e depois trava o
barramento num flush longo. Medido no dashboard com DMA: 0 → 222,2 ms, 512 →
208,3, 1024 → 210,7, 2048 → 211,7, **4096 → 232,3**. O default ficou em **1 kB**,
no platô, com um terço das chamadas de 512. É o caso de manual de uma otimização
que muda de sinal quando outra entra.

### 11.4 O DMA: ler a próxima faixa enquanto esta é enviada

`spi_read_blocking` custava 184 ms onde o fio puro a 12 MHz é 154 — os outros 20%
são o *polling* de FIFO byte a byte, CPU que o Core 0 gasta olhando uma
transferência que podia delegar. Delegando, ele ganha esses 20% **e** fica livre
para os ~95 ms de conversão, codec e rede que estavam serializados atrás.

Duas canais, porque SPI é síncrono: o de TX empurra um `0xFF` constante (o painel
ignora MOSI durante o RAMRD) e o de RX captura. Buffer duplo: enquanto o DMA
enche um, o Core 0 converte, codifica e envia o outro.

| | quadro | fio (espera) | park |
|---|---:|---:|---:|
| bloqueante, 12 MHz, 4 kB | 314,0 ms | 187,5 ms | 7,7 ms |
| **DMA** | **242,5 ms** | 122,9 ms | 0,2 ms |

⚠️ **O DMA obriga o Core 1 a ficar parado o quadro inteiro**, porque o barramento
está em uso do primeiro Start ao último Finish. Isso levaria o pior `PARK` de
7 ms para **238 ms** — um quarto de segundo em que o painel não percebe um dedo,
a cada quadro de um espelho em laço. O conserto é um único ponto de escape: entre
o Finish de uma faixa e o Start da seguinte o barramento está ocioso, e é aí que
o PENIRQ é lido (um `gpio_get`, sem SPI) e o park é solto se houver dedo. Custo
medido com ninguém encostando: **`touchyields=0` e `park` de 0,2 ms**.

### 11.5 O codec: −42% de bytes, e nenhum milissegundo

Simulei quatro candidatos **offline**, sobre sete quadros reais capturados do
aparelho, com round-trip verificado. Sem risco de firmware para responder uma
pergunta de formato:

| quadro | cru RGB565 | atual (enc 1) | **nibble (enc 2)** | enc1+deflate | deflate(cru) | paleta por QUADRO |
|---|---:|---:|---:|---:|---:|---:|
| alarmes | 153.600 | 8.861 | **5.165** | 3.673 | 4.175 | 8.464 |
| dashboard | 153.600 | 10.295 | **6.200** | 4.656 | 5.225 | 9.862 |
| gráfico | 153.600 | 4.285 | **2.865** | 1.939 | 2.416 | 4.014 |
| licença | 153.600 | 18.149 | **9.758** | 5.466 | 5.738 | 17.830 |
| config | 153.600 | 10.621 | **6.152** | 4.190 | 4.699 | 10.214 |
| status | 153.600 | 13.313 | **7.335** | 4.376 | 4.827 | 12.934 |
| temas | 153.600 | 5.737 | **3.645** | 2.347 | 2.944 | 5.396 |

- **Nibble (`ENC_PAL_RLE4`): −42% na média.** Índice e tamanho dividem um byte;
  15 é escape e o byte seguinte leva `tamanho−16`. Nenhuma tela deste projeto
  passou de 11 cores num quadro, quanto mais de 16 numa faixa. **Adotado.**
- **Deflate cortaria mais**, mas custa CPU justamente onde hoje sobra folga —
  e transformaria um quadro limitado pelo barramento num limitado por CPU.
  Recusado.
- **Paleta por quadro em vez de por faixa: 4%.** Não paga a complexidade.

🔴 **E o ponto que importa: o codec NÃO encurta o quadro.** Depois do DMA o
gargalo é o barramento, e o Core 0 já termina antes da próxima faixa chegar.
Isso não é dedução — é medida: **encarecer o envio de propósito** (`?sb=0`, 61
escritas em vez de 7) mexeu no quadro **14 ms, não 70**. O que o codec compra é
banda: 40% a menos no fio, que vale para um cliente remoto, não para o relógio.

### 11.6 O que ficou, e o que foi recusado

| alavanca | ganho | veredito |
|---|---:|---|
| clock 6 → 12 MHz | −188 ms | ✅ adotado |
| pipeline por DMA | −72 ms | ✅ adotado |
| coalescer envio (1 kB) | −66 ms | ✅ adotado |
| `ENC_PAL_RLE4` | −40% de bytes | ✅ adotado (banda, não tempo) |
| conversão por ponteiro | ~0 | ⬜ inócuo, ficou |
| agrupar faixas (`g=2`) | −12,8 ms | ❌ 5 kB de heap |
| clock 16/20 MHz | 0 | ❌ mesmo degrau do PL022 |
| deflate no aparelho | bytes | ❌ CPU vira o gargalo |
| paleta por quadro | 4% de bytes | ❌ não paga |

### 11.7 O resultado

Dashboard vivo, 16 quadros por braço intercalados, **na mesma imagem** (o codec
antigo é reproduzível por `?pm=16`, senão o "antes" não seria comparável):

| | antes | depois |
|---|---:|---:|
| quadro (aparelho) | 613,2 ms | **212,8 ms** (−65,3%) |
| quadro (relógio do cliente) | 634,1 ms | **235,1 ms** (−62,9%) |
| taxa sustentável | 1,58 quadro/s | **4,25 quadro/s** |
| payload | 10.453 B | **6.271 B** (−40,0%) |

As faixas não se tocam (599..638 ms contra 200..234) e o par a par dá **100%**.

**Validação além do cronômetro:**
- **30 quadros seguidos em tela parada: 0 pixels diferentes, tamanho idêntico
  (9.758 B) nos 30.**
- **0 de 76.800 pixels de diferença contra `/api/screenshot`**, que é outro
  leitor (`read_chunk_bgr` → `readRow` → caminho bloqueante, três leituras e
  voto), 3 de 3. É o cruzamento que o controle A-contra-A **não** consegue dar.
- 5 de 5 toques chegam ao painel com o espelho em laço; controle sem toque mexe
  ≤66 px.
- Core 1 intacto: 0 kills de lockout, 0 travados, 0 por saúde. Heap livre
  60,8 kB (mínimo 60,5) com as cinco alocações do quadro em pé.
- `ENC_PAL_RLE4` tem cinco testes nativos novos, com um **decodificador escrito
  contra a especificação** e não derivado do codificador — que é a única forma de
  um round-trip provar algo sobre o formato.

### 11.8 O que sobra

O quadro é **limitado pelo barramento**: 125 ms de espera de DMA contra ~95 ms de
trabalho do Core 0. Daqui só encurta **lendo menos pixels**, que é exatamente o
projeto de blocos sujos das §3–§5 — e que agora vale mais do que quando foi
desenhado, porque não há mais nada grande ao lado dele. O impedimento da §6
mudou de forma: não é mais folga de OTA (há 66,9 kB até o teto), é a margem de
3.000 B do orçamento, que esta mudança já consumiu e repôs uma vez.
