# Espelho do painel por delta — estudo

**Estado:** Living · **Medido em:** 2026-09-18, rig 192.168.3.24, `pico_w_test`
da v2.4.8-beta · **Pergunta:** vale mandar só os blocos que mudaram?

A resposta curta é que vale, mas **não pelo motivo que a pergunta sugere**, e
hoje não cabe na imagem. As duas coisas estão medidas abaixo.

---

## 1. Onde o tempo vai hoje

Um quadro do `/api/screen_stream` custa **1,53 s** no ferro. A conta, medida em
quatro caminhos independentes e fechando dentro de 7%:

| parcela | por quadro | fração |
|---|---:|---:|
| ler o painel (240 linhas × 5,79 ms) | 1.390 ms | **91%** |
| pausas do Core 1 (30 × ~3,7 ms) | 111 ms | 7% |
| rede (≈9 kB a 221 KB/s) | 43 ms | 3% |

**O payload já é 3% do problema.** Mandar menos bytes — que é o que "só os
blocos que mudaram" parece prometer — mexe nesses 3%. Se a leitura continuar
sendo do quadro inteiro, comprimir melhor ou mandar menos não compra nada:
o teto continua sendo 1,4 s de SPI a 2 MHz.

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

## 8. A alavanca independente, que continua valendo

A leitura custa **5,79 ms por linha contra 3,84 ms de clock puro a 2 MHz** — 51%
de sobrecarga, ~2 µs por byte, que é o custo da chamada `SPI.transfer` byte a
byte do `readRow`. Trocar o laço por transferência em bloco ataca os mesmos 91%
e é ortogonal a este estudo: melhora o espelho, o delta e a captura forense ao
mesmo tempo. Deve vir antes, porque é menor e não depende de folga de flash.
