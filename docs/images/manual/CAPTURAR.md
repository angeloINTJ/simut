# Imagens a capturar para o manual

Gerado por `tools/build_manual.py` — não edite à mão. Salve cada captura
nesta pasta com o nome indicado e rode o script de novo.

10 pendentes de 245.


## Capítulo 2 — Hardware e ligações

- [ ] `02-placa.png` (Foto) — placa SIMUT v1.1 montada, vista de cima: Pico W no soquete, conector TFT-Touch com o painel ligado, bloco Alphanumeric vazio, coluna S0–S10 com uma sonda DS18B20 em S2, cigarra, conversor HLK-PM01 e a marcação 90-240Vac visível
      A placa v1.1 montada para a release: o Pico W, o conector do painel, a coluna de sensores e a entrada de rede elétrica.

## Capítulo 3 — Instalar o firmware

- [ ] `03-bootsel.png` (Foto) — mão segurando o botão BOOTSEL de um Pico W enquanto o cabo USB é ligado; o botão bem visível, ao lado do chip RP2040
      O botão BOOTSEL, segurado enquanto o cabo USB é ligado.

## Capítulo 6 — Sensores e calibração

- [ ] `06-editor-aviso.png` (Página web) — rota /config; largura 1280; sessão admin; editor do slot 1, DHT22 ativo, com o pino Data trocado para GP4, que já pertence ao slot 2 (o BMP280); a linha vermelha mostra GP4 já está em uso pelo slot 2.
      O editor avisando um conflito de GPIO antes da gravação.
- [ ] `06-procurar.png` (Página web) — rota /config; largura 1280; sessão admin; imagem release; os sensores da bancada ligados (DS18B20 no GP0 e no GP1, DHT22 no GP3 e no GP10, BMP280 no GP4/GP5); logo depois de tocar em Procurar sondas e a busca terminar; recorte dos botões e da linha de resultado
      O resultado da busca: a ROM identifica cada DS18B20; o DHT22 aparece só com o pino.

## Capítulo 11 — O painel original (Pico W)

- [ ] `11-web-ocupado.png` (Tela do painel) — screen dash; durante a exportação em CSV de um dia de histórico pela conta admin, na página Histórico e Logs
      O aviso de web ocupada: WEB ‘admin’ - toque bloqueado.
- [ ] `11-carregando.png` (Tela do painel) — tela MODE_GRAPH_LOADING; aparece por instantes ao abrir um gráfico a partir da tela inicial; transitória, pode exigir várias tentativas de captura
      A tela Carregando…, ao abrir um gráfico.

## Capítulo 13 — A interface web no dia a dia

- [ ] `13-painel-de-controle-alpha.png` (Página web) — rota /; largura 1280; sessão admin; imagem alpha, com o TFT fora e o LCD 16x2 nos GP16 a GP21; 1 sensor DHT22 ativo; telemetria desligada; bloco Tela depois de tocar em Capturar
      O Painel de Controle na alpha: o bloco Tela, numa linha própria acima dos cartões, mostra o LCD 16x2.

## Capítulo 18 — Recuperação

- [ ] `18-bootsel.png` (Foto) — um Pico W sendo ligado ao cabo USB com o polegar segurando o botão BOOTSEL; o botão em destaque, com uma seta; ao lado, a janela do gerenciador de arquivos do computador mostrando a unidade RPI-RP2
      O botão BOOTSEL do Pico W, segurado enquanto o cabo é ligado, e a unidade RPI-RP2 que aparece no computador.

## Capítulo 19 — SIMUT Air

- [ ] `19-montagem.png` (Foto) — um Air montado: Pico W com um DS18B20 e o resistor de 4,7 kΩ, a bateria e o carregador; os fios do GP16 e do GP17 identificados com etiquetas
      Um Air montado. O GP16 mostra quando ele está acordado e pode alimentar os sensores; o GP17 detecta o carregador.
- [ ] `19-painel-controle.png` (Página web) — rota /; largura 1280; sessão admin; imagem air em M0; 1 sensor DS18B20; cartão Registros Pendentes com alguns registros; sem o bloco de espelho do painel
      O Painel de Controle de um Air em M0: sem o espelho do painel, que não existe nesta imagem.
