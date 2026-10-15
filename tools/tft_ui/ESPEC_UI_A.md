# Build funcional da família A: especificação das telas

Documento de trabalho (14/10/2026), escrito no ramo de estudo `study/tft-angulo-ui-a` e trazido para
`src/ui/angulo/` e `tools/tft_ui/` na etapa S2a. Diz como a interface Ângulo da família A do plano do TFT
(`~/Documentos/simut/docs/ui_tft_angulo/entrega`) vira uma interface **funcional** no aparelho: o driver de
UI Ângulo (transitório, imagem de bancada `pico_wiretft_angulo`, com rede cabeada), sem mudar nenhuma
imagem de produto.

## 1. O que já existe

| Peça | Onde | O que faz |
|---|---|---|
| Kit Ângulo | `src/ui/angulo/Angulo.{h,cpp}`, gerados `AnguloMeta.h` e `AnguloIcones.h` | tokens (`Tema` com 17 papéis, claro e escuro), texto UTF-8 alinhado pela tinta, ícones, componentes (`botao`, `tecla`, `campo`, `pontos`, `ajuda`, `segmentado`, `chip`, `selo`, `lista`, `interruptor`, `paginador`, `rodapeAcoes`, `numero`, `progresso`, `tracejada`, `barraTitulo`, `barraTituloVoltar`, `superficie`) |
| Portes do plano | `tools/tft_ui/plano/*.cpp` | as 29 telas e os 24 teclados (T1, P1 a P3) do plano, desenhados pelo kit e iguais ao plano pixel a pixel (`python3 tools/tft_ui/host_ui.py ../esperado`: 106 de 106) — **o ponto de partida visual de cada tela** |
| Referências do plano | `entrega/referencia/telas/A_*.png`, `entrega/referencia/teclados/*.png` | as imagens que o plano desenhou |
| Contrato das telas | `src/ui/angulo/telas/Telas.h` | `Alvo`, `addAlvo`, `alvoEm`, `copiar`, `quebrar`, `VmStatus` + `desenharStatus` (barra de status com a marca) |
| Telas de referência | `src/ui/angulo/telas/Base.{h,cpp}`, amostras em `tools/tft_ui/amostras/base.cpp` | mensagem de resultado e diálogo de confirmação: **o modelo a copiar** |
| Arnês | `python3 tools/tft_ui/host_telas.py <grupo> --out <pasta>` | compila o kit, `Telas.cpp`, `src/ui/angulo/telas/<Grupo>*.cpp` e `tools/tft_ui/amostras/<grupo>.cpp`; desenha cada amostra nos dois temas; falha com glifo ou ícone ausente, texto fora da área segura (x 4..315, y 4..235), dois textos sobrepostos, texto que cruza a borda de um alvo, texto cortado com "…" que `tools/tft_ui/cortes_aceitos.txt` não aceita para aquela amostra (corte só de dado longo de propósito, com o motivo; linha cujo corte não acontece mais também falha) ou alvo fora da tela; avisa alvo menor que 24 px e alvos sobrepostos; grava PNG por quadro, PNG com os alvos e a folha `_folha.png` |
| Levantamento do código atual | do estudo, fora da árvore: `~/Documentos/simut/scratchpad/synth_20261014/inventario/*.md`; na árvore há a auditoria `docs/analysis/UI_DRIVERS.md` (os ramos de toque e os desenhos imediatos, seções 3.3 e 3.5), sem as coordenadas de cada tela | coordenadas, comportamento, eventos e dados de cada tela do painel de hoje (`dashboard_alarme_boot.md`, `grafico_tema_idioma_nucleos.md`, `configuracoes_sistema.md`, `pin_usuarios_alarmes.md`) |

Ícones disponíveis (nome, tamanho): veja `src/ui/angulo/AnguloIcones.h`. Faltando um, **não**
edite `tools/tft_ui/gerar_meta.js`: diga no relatório (o integrador acrescenta e regenera).

## 2. Arquitetura

- **Camada pura** (`src/ui/angulo/telas/`): cada tela é `desenhar<Tela>( Pintor&, const Vm<Tela>& )`
  e `alvos<Tela>( const Vm<Tela>&, Alvo* )`. Não inclui nada do firmware. É o que o arnês desenha.
- **Cola** (`src/ui/angulo/DisplayManager_UiA*.cpp`, só na build `pico_wiretft_angulo`): preenche o
  modelo a partir do estado do `DisplayManager`, pinta em faixas de 40 px quando o hash do modelo
  muda, e traduz o toque. Fase 2; não faz parte da fase 1.
- **Toque**, por modo, uma de duas formas:
  - **Tradução (T)**: a tela nova tem os mesmos elementos que a antiga; a cola troca o ponto tocado
    pelo ponto equivalente do leiaute antigo e o tratador antigo roda sem mudança (preserva toque
    longo, deslizar, repetição, bloqueios). A tela pura só precisa expor alvos com o significado certo.
  - **Controlador (C)**: o modelo de interação do plano é outro (tocar na linha abre, voltar no
    título, paginador); a cola executa a ação equivalente (mesmos eventos e estado do código atual).
  - **Bruto (B)**: o código atual trata o toque antes de qualquer gancho (calibração, sensibilidade,
    botão Pular do boot). A tela nova **tem** de pôr seus elementos dentro das zonas antigas.

## 3. Contrato de cada tela (fase 1)

1. Arquivos: `src/ui/angulo/telas/<Grupo><Algo>.h/.cpp` (o prefixo do grupo é obrigatório: o arnês
   compila `telas/<Grupo>*.cpp`) e `tools/tft_ui/amostras/<grupo>.cpp`. Não edite arquivos fora do
   seu grupo; o kit, `Telas.*`, `Base.*` e o arnês são do integrador (precisou de algo neles, diga).
2. Namespace `ua::tela`. Modelo `Vm<Tela>`: POD, só membros de tamanho fixo (`char[]`, inteiros,
   `bool`, enums, arrays), **nenhum ponteiro**; quem preenche zera com `memset` antes. Textos UTF-8.
3. Uma função de leiaute interna (`layout<Tela>( const Vm& )`) calcula todas as caixas; `desenhar` e
   `alvos` usam a mesma. Alvos: enum próprio da tela (`0` = nada), `indice` = linha, tecla, coluna.
   Lista só alvos **ativos** (um botão desabilitado não é alvo; com um popup aberto, só o popup).
4. O fundo já vem pintado com `T.fundo`; a tela pinta o resto. Cores só por papel do tema
   (`p.t( ).acento`…), nunca literal. Espaços da grade de 4 px (`ESP1`..`ESP7`, `PAD`, `Y0`, `H_STATUS`).
   Raios do kit (`RAIO_CONTROLE` 6, `RAIO_CARTAO` 12, `RAIO_TOTAL`). Sem sombra em cartão.
5. Texto (Ângulo §7, `~/Documentos/simut/ANGULO.md`): português do Brasil, caixa de sentença, botão é
   verbo + objeto quando cabe ("Salvar limites", "Excluir a conta"), erro diz o que houve e como
   resolver, sem desculpas, sem emoji, sem caixa alta para ênfase, nenhuma promessa sem número.
   Estado se escreve em palavras ("Ligado", "Desligado", "Na faixa").
6. Texto que pode não caber (nome de sensor ou conta, até 31 caracteres) se corta com "…" medindo a
   tinta (`larguraTinta`) ou quebra com `quebrar( )`; o arnês reprova texto sobreposto ou fora da área.
7. Amostras: cada estado listado na seção do grupo, com dados verossímeis (nomes de sensor como
   "Vacinas", "Geladeira 2", "Sala de coleta"; contas "admin", "maria", "joao.silva").
   **Inclua os casos extremos**: nome de 31 caracteres, lista vazia, página parcial, valores negativos,
   `--,-` de sensor em erro.
8. Pronto = `python3 tools/tft_ui/host_telas.py <grupo> --out <pasta>` com **0 failures**, avisos
   explicados no relatório, e a folha olhada quadro a quadro (abra `_folha.png` com a ferramenta Read).
9. Estilo do repositório: espaços dentro dos parênteses (`f( x )`), comentários em inglês que dizem
   **por quê**, `-Wall -Wextra -Werror` limpo. Não faça commit, push nem stash; não grave placa.

## 4. Mapa dos 31 modos

| Modo | Grupo | Tela do plano | Toque |
|---|---|---|---|
| MODE_DASHBOARD | painel | dashboard_* | T |
| MODE_GRAPH_LOADING | graficos | carregando_lendo | — |
| MODE_GRAPH_VIEW | graficos | historico_24h/7d | C |
| MODE_GRAPH_DETAIL | graficos | detalhe_temp | C |
| MODE_AUTH (PIN) | identidade | P1, P2, P3 | T |
| MODE_SETTINGS_MAIN | menus | configuracoes_p1/p2 | C |
| MODE_SETTINGS_THEMES | menus | temas_lista | C |
| MODE_SETTINGS_ALARMS | menus | alarmes_lista | C |
| MODE_SETTINGS_ALARM_EDIT | ajustes | alarme_editar_editar | C |
| MODE_SETTINGS_LANG | menus | idioma_lista | C |
| MODE_SETTINGS_PASSWORD (texto) | identidade | T1 | C |
| MODE_SETTINGS_TOUCH_CAL | ajustes | calibrar_p1 | B |
| MODE_SETTINGS_TOUCH_SENS | ajustes | sensibilidade_medindo | B |
| MODE_SETTINGS_SOUNDS | ajustes | sons_p1/p2 | C |
| MODE_SETTINGS_LICENSE | menus | licenca_p1 | C |
| MODE_SETTINGS_STATUS | menus | status_sistema/rede | C |
| MODE_SETTINGS_DISPLAY_OFFSET | ajustes | alinhar_ajuste | T |
| MODE_ALARM_ACTION | painel | alarme_acao_acoes | T |
| MODE_CALENDAR | graficos | calendario_out | C |
| MODE_CONFIRM_MUTE_ALL | base (pronto) | — | T |
| MODE_SETTINGS_ALARM_SENSOR | menus | — (novo) | C |
| MODE_SETTINGS_MAINT | ajustes | — (novo) | C |
| MODE_SETTINGS_USERS | identidade | — (novo) | C |
| MODE_SETTINGS_USER_EDIT | identidade | — (novo) | C |
| MODE_SETTINGS_USER_CONFIRM_DEL | base (pronto) | — | T |
| MODE_PANEL_MESSAGE | base (pronto) | — | T |
| MODE_AUTH_USER | identidade | — (novo) | C |
| MODE_SETTINGS_PIN_POLICY | ajustes | — (novo) | C |
| MODE_CONFIRM_AP | base (pronto) | — | T (não existe na build cabeada) |
| MODE_SET_CLOCK | ajustes | — (novo) | T |
| MODE_OTA_UPDATE | graficos | — (novo) | B (toque em qualquer ponto) |
| tela de boot (`isBooting`) | painel | boot_wifi | B |

As 12 telas sem desenho no plano seguem a linguagem da família A: `barraTituloVoltar` no topo,
listas no cartão (`lista`), `paginador` ou `rodapeAcoes` no pé (y 196..235), conteúdo de `Y0` (36) a 188.

## 5. Temas

Três escolhas no aparelho: **Claro**, **Escuro** e **Automático (escuro das 19 às 7)**. As telas não
sabem qual está em uso: desenham com `p.t( )`. A tela de temas mostra as três (com a marca na atual).

## 6. Grupos

### 6.1 painel — `Painel*.cpp`, `amostras/painel.cpp`

Leia `inventario/dashboard_alarme_boot.md` inteiro e `tools/tft_ui/plano/PlanoTelasPainel.cpp`.

**Painel principal (`VmPainel`).** Mantém os elementos do painel de hoje, com o desenho do plano:
- Barra de status (`VmStatus`, `desenharStatus`, pronta): hora (a provisória sai em alerta), aviso no
  lugar da hora ("Silenciado · 87 s" em alerta; "maria entrou pela web" neutro), rede e pendentes.
- **Cartão de cima** = painel superior de hoje (fixado num sensor ou seguindo a seleção): nome do
  sensor, ícone de alfinete (`PIN` 16) quando fixado, temperatura e o segundo valor do tipo
  (umidade do DHT22; pressão do BMP280/BME280; nada no DS18B20). Modo mín./máx.: mostra mínimo e
  máximo de temperatura (e de umidade quando houver) e o botão **Gráfico** (ícone `CHART_LINE`) à direita.
- **Cartão de baixo** = sensor selecionado: nome, selo de situação (Na faixa / No limite / Acima /
  Abaixo / Erro no sensor / Silenciado), valor grande, "mín · máx" e "faixa lo a hi °C" (como o plano);
  segundo valor do tipo quando houver. Modo mín./máx.: como o de cima, com o botão Gráfico.
- **Alarme**: duas fases que a cola alterna a cada 600 ms. Fase apagada = desenho `dashboard_alarme`
  (selo de perigo, ponto vermelho no botão do sensor). Fase acesa = `dashboard_vermelha` (cartão cheio
  em `perigo`, botão cheio). Silenciado = `dashboard_silenciado` (borda de perigo, sem piscar).
  Sensor em erro: valor `--,-`, selo "Erro no sensor"; pisca como o alarme.
- **Navegação** (y 196..235): até 5 posições, na regra de hoje (`buildDashLayout`): um botão por
  sensor ativo ("S0".."S15", índice do slot), sem o sensor fixado em cima, e a engrenagem (`SETTINGS`);
  com mais de 5, 4 por página + o botão de página ("1/3"), engrenagem só na última página; posição sem
  botão fica vazia. Estados por botão: normal, selecionado (acento), em alarme (ponto ou cheio na fase
  acesa); botão de página com alarme em outra página (borda ou cheio de perigo).
- **Web ocupada**: o cartão flutuante do plano (`dashboard_ocupado`) com o nome da conta.
- Alvos: `CARTAO_CIMA`, `GRAFICO_CIMA` (só no modo mín./máx.), `CARTAO_BAIXO`, `GRAFICO_BAIXO`,
  `NAV` com `indice` = posição 0..4 (só as posições com botão). Tradução: a cola leva cada alvo ao
  ponto do leiaute antigo.
- Amostras: DHT22 normal; DS18B20; BME280 com pressão; cima fixado; mín./máx. em cima; mín./máx. em
  baixo; alarme fase apagada e acesa; silenciado; erro no sensor; aviso de web; web ocupada; 9 sensores
  (página 1 e página 3 com a engrenagem); nenhum sensor ativo; nome de 31 caracteres; temperatura
  negativa (−18,5 °C); hora provisória; pendentes 12 e 1,2k com falha de envio; cabo sem link.

**Ação do alarme (`VmAcaoAlarme`).** Desenho `alarme_acao_acoes`: faixa de perigo com o nome e o que
houve ("Vacinas acima do limite", "abaixo do limite", "Erro no sensor"), a linha "8,7 °C agora · máx 8,0 °C"
(sem "desde": o aparelho não guarda a hora de início), e os três botões: **Silenciar por 120 s**
(primário), **Ver mínimo e máximo** (secundário), **Desativar os alarmes** (destrutivo; o texto diz que
pede o PIN). Alvos `SILENCIAR`, `MINMAX`, `DESATIVAR`. Amostras: acima, abaixo, erro, nome longo.

**Boot (`VmBoot`).** Desenho `boot_wifi`: marca, subtítulo "Monitor de temperatura e umidade", o passo
atual (última linha do log de boot, já em texto) e até 4 passos anteriores em `apoio`, versão e MAC no
pé; com `apProgressPct >= 0`, a barra de progresso "Segure para o modo de configuração" (0..100);
com `showSkipButton`, o botão **Pular** que **tem de ficar dentro de x 80..240, y 191..239** (toque bruto).
Alvo `PULAR`. Amostras: início, passo de rede, barra de AP 40 %, botão Pular, textos longos.

### 6.2 menus — `Menus*.cpp`, `amostras/menus.cpp`

Leia `inventario/configuracoes_sistema.md` (§1, 2, 3, 5, 6) e `inventario/pin_usuarios_alarmes.md`
(§9, 11), e `tools/tft_ui/plano/PlanoTelasConfig.cpp`. Todas usam `barraTituloVoltar` (alvo `VOLTAR`),
`lista( )` com linhas de 40 px e 4 linhas por página, e `paginador` quando há mais de uma página
(alvos `PAG_ANTERIOR`, `PAG_PROXIMA`, só quando a seta está ativa). A linha de duas linhas de texto (a
lista de alarmes, o menu de um sensor e o Tema; `desenharDuasLinhas( )`) tem 48 px (`LISTA_H_FILA_2`) e
são 3 por página (`LISTA_FILAS_2`): no piso de 9 pt, duas linhas de maiúsculas de 14 px em 40 px deixavam
4 px entre elas e os descendentes do nome tocavam a segunda linha. Tocar numa linha = abrir/aplicar
(alvo `LINHA`, `indice` = posição no total, não na página).

- **Configurações (`VmMenu`)**: título "Configurações"; até 13 itens, cada um com ícone e chevron, na
  ordem e com os rótulos: Limites de alarme (BELL), Sons (VOLUME_2), Status do sistema (ACTIVITY),
  Data e hora (CLOCK), Alterar meu PIN (KEY_ROUND), Usuários (USERS), Segurança do PIN (SHIELD),
  Modo de configuração (ROUTER; não existe na build cabeada), Idioma (LANGUAGES), Tema (PALETTE),
  Calibrar o toque (CROSSHAIR), Alinhar a tela (MOVE), Licença (FILE_TEXT). O modelo traz os itens já
  filtrados pela permissão da conta e o nome da conta (mostre "Conta: maria" em `apoio` sob a lista ou
  no título, sem colidir). Amostras: admin completo págs. 1 a 4; conta só com bits de painel (4 itens).
- **Tema (`VmTemas`)**: três linhas de 48 px: Claro (SUN), Escuro (MOON) e Automático (SUN_MOON), com a
  regra "escuro das 19 às 7" na segunda linha do Automático, em `apoio`; marca na atual, e a nota "A cor
  de destaque muda com o tema." Amostras: cada uma marcada.
- **Idioma (`VmIdioma`)**: "English" e o pacote carregado ("Português (Brasil)"); só "English" quando não
  há pacote. Marca no atual. Nota em `apoio`: "Esta build mostra o painel em português." (desvio
  registrado: os textos das telas novas são fixos em pt-BR). Amostras: dois idiomas; só English.
- **Limites de alarme (`VmAlarmes`)**: uma linha de 48 px por sensor ativo, 3 por página (o paginador
  aparece a partir do quarto sensor): na primeira linha o nome e um selo ("Ligado" ou "Desligado", os
  alarmes do sensor; "Manutenção" toma o lugar deles quando há janela aberta), na segunda a faixa de temperatura ("2,0 a 8,0 °C")
  com a largura toda; chevron. Vazio: "Nenhum sensor ativo. Ative um sensor pela web." Amostras: 3 sensores; 6 (2 páginas);
  vazio; nome longo.
- **Um sensor (`VmSensorAlarme`)**: título = nome do sensor (cortado); linhas: **Limites** (chevron),
  **Alarmes** (interruptor ligado/desligado; tocar alterna — o aparelho grava na hora, sem confirmação),
  **Manutenção** (linha de 48 px: o estado, "1 h 30 min restantes" ou "Desligada", na segunda linha, sob
  o rótulo, em âmbar com a janela aberta; chevron) e a nota "Ligar ou desligar alarmes vale na hora."
  Sem a permissão daquela linha:
  linha desabilitada com cadeado (LOCK 20) e não é alvo. Amostras: tudo liberado; alarmes desligados;
  manutenção aberta; conta sem permissão de bloqueio.
- **Status do sistema (`VmStatusSistema`)**: 4 páginas (Sistema, Rede, Sensores, Telemetria), título
  "Status · Sistema" etc., até 6 linhas de 24 px chave/valor como o plano; valores do levantamento §5
  (memória livre em vermelho abaixo de 20 kB; NTP "Sincronizado"/"Não sincronizado"; cabo
  "Conectado"/"Sem cabo"). Página de sensores: "Ativos: 4 de 11" e até 5 sensores com o estado; sem sensor ativo, a nota
  "Ative um sensor pela web."
  Amostras: as 4 páginas; memória baixa; sem rede.
- **Licença (`VmLicenca`)**: as linhas da página já quebradas pela cola (`quebrar` com `tipo::apoio`
  na largura do cartão), 6 por página, 18 px entre as linhas (o plano tem 7, a 16 px: no piso de 9 pt a
  sétima chegaria ao paginador), barra de rolagem proporcional, a nota "O texto legal fica no original
  (inglês)." e o paginador "n de m". O índice guarda até 192 linhas: 32 páginas. Amostras: a primeira
  página, uma do meio, a última (cheia), uma linha de traço e um parágrafo longo.

### 6.3 ajustes — `Ajustes*.cpp`, `amostras/ajustes.cpp`

Leia `inventario/configuracoes_sistema.md` (§4, 7, 8, 9, 10) e `inventario/pin_usuarios_alarmes.md`
(§4, 10, 12), e `tools/tft_ui/plano/PlanoTelasConfig.cpp`.

- **Limites de um sensor (`VmLimites`)**: título = o nome do sensor ("Vacinas"), sem sufixo (com " · limites"
  um nome de 15 letras era cortado no piso de 9 pt; a tela se explica nos rótulos "Temperatura mínima" e
  "Temperatura máxima" e no rodapé); uma página por grandeza
  (Temperatura, Umidade, Pressão) com dois "steppers" do plano (mínimo e máximo, com unidade), foco
  (borda de acento) no que foi tocado por último, paginador entre grandezas e o rodapé
  **Salvar limites** / Cancelar. Alvos: `MENOS`/`MAIS` com `indice` = linha (0 mín., 1 máx.), `PAG_*`,
  `SALVAR`, `CANCELAR`. Amostras: temperatura; umidade; pressão; negativo; 3 páginas.
- **Manutenção (`VmManutencao`)**: título "Manutenção · Vacinas". Fechada: dois steppers (Horas 0..720;
  Minutos de 5 em 5), o texto do que acontece ("Os alarmes deste sensor ficam suspensos até o fim.") e
  o rodapé **Iniciar manutenção**. Aberta: o tempo restante grande ("1 h 29 min") e **Encerrar
  manutenção** (destrutivo). Sem permissão: botão desabilitado e a linha "Sua conta não pode abrir
  manutenção." Amostras: fechada 1 h 0 min; aberta; sem permissão.
- **Sons (`VmSons`)**: página 1: interruptores Toque na tela, Confirmação, Erro, Alarme; página 2:
  Atenção, Acessos pela web, **Silenciar tudo** (interruptor; ligar pede confirmação); página 3: dois
  controles deslizantes do plano (Volume do sistema, Volume do alarme) com **−** e **+** (passo 10 %).
  Cada som ligado mostra a melodia como valor e um chevron (tocar no texto abre a escolha da melodia;
  tocar no interruptor liga ou desliga). Rodapé **Salvar sons** / Cancelar na pág. 1 a 3 (o paginador
  vai na linha acima do rodapé ou o rodapé leva as setas: decida e justifique). Alvos: `INTERRUPTOR`,
  `LINHA`, `MENOS`/`MAIS` (indice 0 sistema, 1 alarme), `PAG_*`, `SALVAR`, `CANCELAR`.
- **Melodia (`VmMelodia`)**: título "Melodia · Alarme"; 6 linhas (nomes do levantamento §4.2,
  traduzidos: Clique, Bolha, Tique, Estalo, Gota, Gorjeio; Subida, Fanfarra, Carrilhão, Triunfo, Brilho,
  Resolução; Descida, Zumbido, Grave, Áspero, Queda, Bip; Bipe duplo, Sirene, Rápido, Pulso, Crescente,
  Staccato; Aviso, Sino, Pulso, Carrilhão grave, Subida, Toque suave), marca na escolhida, rodapé
  **Usar esta melodia** / Cancelar. Tocar numa linha escolhe e toca a prévia.
- **Segurança do PIN (`VmPoliticaPin`)**: título "Segurança do PIN"; **Tamanho mínimo** (stepper 4..teto),
  **Teclado** (segmentado: "Ordenado", "6 cartões", "4 cartões" — com 0-9 A-Z: "Ordenado", "18 cartões",
  "12 cartões"), **Caracteres** (segmentado "0-9" / "0-9 e A-Z"), a linha de ajuda com o teto
  ("Com 4 cartões, o PIN vai até 8 dígitos.") e o rodapé **Salvar** / Cancelar. Amostras: fábrica
  (4, 4 cartões, 0-9); ordenado com A-Z.
- **Data e hora (`VmRelogio`)**: título "Data e hora"; cinco colunas dia / mês / ano  hora : minuto com
  botões CHEVRON_UP e CHEVRON_DOWN (alvos `MAIS`/`MENOS`, `indice` = coluna 0..4) e o valor no meio em
  `dadoGrande`; no boot, a dica "Sem rede: os registros usam esta hora." em alerta e o botão esquerdo
  **Pular** (fora do boot, **Voltar**); direito **Salvar hora**. Amostras: menu; boot.
- **Alinhar a tela (`VmAlinhar`)**: desenho `alinhar_ajuste`, com a moldura de referência **nas bordas
  da área segura** (x 4..315, y 4..235, 1 px, `acento`) para o alinhamento ter sentido; as 4 setas;
  "x +1 · y 0"; rodapé **Salvar** / **Redefinir**; voltar no título. Aviso em `apoio`: "Salvar refaz a
  calibração do toque." Alvos `CIMA`, `BAIXO`, `ESQUERDA`, `DIREITA`, `REDEFINIR`, `SALVAR`, `VOLTAR`.
- **Sensibilidade (`VmSensibilidade`)**: desenho `sensibilidade_medindo` adaptado: o aparelho conclui
  sozinho (sem botão salvar). Texto "Toque na tela várias vezes, como faria no dia a dia.", a contagem
  "Toques: 7", a barra vertical de estabilidade (0..100 %, verde a partir de 85 %), o limiar atual, e
  **Cancelar** (discreto) que **tem de ficar em x 0..124, y 196..239** (toque bruto). Concluído:
  "Pronto. Agora a posição do toque." Amostras: começo, metade, concluído.
- **Calibrar o toque (`VmCalibrar`)**: desenho `calibrar_p1`; a mira **tem de ficar em (20,20),
  (300,20), (20,220) e (300,220)** conforme o passo (0..3, em duas voltas: "Ponto 2 de 4 · volta 1 de 2");
  segurando, a mira fica `positivo`; **sem botão Cancelar** (qualquer toque é captura). O resultado usa
  `desenharMensagem` (base): "Calibração concluída." ou "Toques imprecisos. Toque de novo, devagar, no
  centro de cada mira." Amostras: os 4 pontos, segurando, volta 2.

### 6.4 identidade — `Identidade*.cpp`, `amostras/identidade.cpp`

Leia `inventario/pin_usuarios_alarmes.md` inteiro e `tools/tft_ui/plano/PlanoTeclados.cpp`.

- **Quem está no painel (`VmQuem`)**: título "Quem está no painel?", voltar; linhas com o nome de cada
  conta que tem PIN (admin incluído); conta travada: linha desabilitada com o selo "Bloqueada" e não é
  alvo. Vazio: "Nenhuma conta tem PIN. Defina um PIN pela web." Paginador com mais de 4. Amostras:
  3 contas; 1 travada; 9 contas; vazio.
- **PIN (`VmPin`)**: o desenho P1/P2/P3 do plano com o modelo de hoje (levantamento §3):
  - Cabeçalho do plano (título, **Cancelar** na barra, campo com os lugares, ajuda/erro/alerta). Título
    por propósito: "PIN de maria" (identificar), "Novo PIN" / "Confirme o novo PIN" (definir).
    Lugares: `max( digitados, mínimo )`, até o teto do teclado (8, 12 ou 16).
  - Leiautes, sempre **na ordem das teclas de hoje** (tecla k nova = tecla k antiga; a cola traduz):
    0-9 com 4 cartões (P1, 2×2), 0-9 com 6 cartões (P3, 3×2), 0-9 ordenado (P2, 3×4: 1..9, 0 na
    célula 10, apagar na célula 11, **Entrar** na célula 9), 0-9 A-Z com 12 cartões (4×3) e com 18
    cartões (6×3), e 0-9 A-Z ordenado (9 grupos 3×3 "0-9", "ABC" … "WXYZ" com o popup do grupo).
    Os cartões mostram os glifos que o aparelho sorteou (com os enchimentos `!@#$%&*+-=?~`, todos com a
    mesma tinta: um cartão não pode denunciar quantos dígitos reais tem).
  - **Apagar** e **Entrar** que o plano não tem: nos cartões, uma fileira no pé (Apagar secundário,
    Entrar primário, 32 px de altura), com os cartões encolhidos para caber; no ordenado, nas células
    9 e 11. Alvos: `TECLA` (indice k), `POPUP` (indice do caractere no popup), `APAGAR`, `ENTRAR`,
    `CANCELAR`.
  - Estados: vazio; digitando (n pontos); erro ("PIN incorreto. Tente de novo." / "PIN curto: use 6
    dígitos ou mais." / "Os PINs não batem. Comece de novo."); a linha sob o campo é de uma só fileira de 296 px
    (as teclas começam 28 px abaixo do campo): "Toque no cartão com cada dígito." (ou "caractere"),
    "Escolha de 6 a 12 caracteres." / "Digite o mesmo PIN de novo.", e para o PIN de outra conta o nome vem
    primeiro, com o espaço que o resto deixa: "joao.silva: 6 a 12 caracteres.", "joao.silva: digite o mesmo
    PIN."; aguardando ("Conferindo…", teclas
    desabilitadas, Cancelar ativo); espera ("Muitas tentativas. Aguarde 15 s.", teclas desabilitadas);
    bloqueio da conta e do painel ("Painel bloqueado. Reinicie o aparelho para liberar.", tela em perigo,
    só Cancelar).
  - Amostras: cada leiaute vazio e digitando; popup do grupo "PQRS" e do "0-9"; erro; aguardando;
    espera; bloqueio; definir fase 1 e fase 2.
- **Teclado de texto (`VmTexto`)**: o T1 do plano funcional: título (ex. "Nome da conta"), o campo com o
  texto e o cursor, contador "5 de 15", as 4 camadas (abc, ABC, 123, #+=), Shift (uma letra) e Caps
  (travado), apagar, espaço, **Salvar**/OK e **Cancelar** na barra. Alvos por tecla (`indice` = posição
  na camada) e os de função. Amostras: vazio; digitando; Shift; Caps; 123; #+=; cheio (15 de 15);
  as quatro recusas do nome da conta, numa fileira de 296 px (`TXT_ERRO_*`): "Digite um nome, não só
  espaços.", "Tire as aspas e a barra invertida.", "O nome admin é reservado. Use outro." e "Esse nome já
  existe. Use outro.".
- **Usuários (`VmUsuarios`)**: título "Usuários", voltar e a ação **Nova conta** na barra
  (desabilitada sem permissão de gerir contas); linhas de 48 px, 3 por página (`ident::LINHAS_2`; o
  paginador aparece a partir da quarta conta): na primeira linha o nome e o selo "Sem PIN" (alerta) quando
  não há PIN, na segunda as permissões resumidas ("Limites · Alarmes · Manut." ou "Só leitura") com a
  largura toda; chevron. "Quem está no painel?" segue com linhas de uma linha, 4 por página. O admin não
  aparece. Vazio: "Só o admin existe. Toque em Nova conta para criar outra."
  Amostras: 3 contas; 7 (3 páginas); a pior linha (nome de 15 letras, sem PIN, as três permissões); vazio;
  sem permissão.
- **Conta (`VmConta`)**: título = nome; interruptores **Editar limites**, **Bloquear alarmes**,
  **Manutenção**; linhas **Definir PIN** (chevron) e **Excluir a conta** (texto em perigo) só para conta
  existente; rodapé **Salvar** (existente) ou **Seguir** (nova) / Cancelar. Amostras: existente; nova.

### 6.5 graficos — `Graficos*.cpp`, `amostras/graficos.cpp`

Leia `inventario/grafico_tema_idioma_nucleos.md` (Parte 1) e `inventario/configuracoes_sistema.md`
§11, e `tools/tft_ui/plano/PlanoTelasPainel.cpp` (histórico, detalhe, calendário).

- **Carregando (`VmCarregando`)**: título com o sensor e o período, "Lendo o histórico…" (sem
  porcentagem: o aparelho não tem esse número). Sem alvos.
- **Gráfico (`VmGrafico`)**: desenho `historico_24h`: título "Vacinas · 24 h" com **Dia** (calendário)
  e **Voltar**; as estatísticas, 18 px entre as linhas (na primeira, máx. e mín. com a hora e a unidade: "máx 6,0 °C 12:10  mín 3,0 °C 22:57"; na segunda, "média 4,4  σ 0,7  Δ -0,7  agora 5,1", sem unidade); o gráfico com a banda
  mín./máx. de cada balde, a média em 2 px, lacunas onde o balde está vazio, a linha tracejada da
  média, o segundo valor (umidade ou pressão) quando houver, com escala própria à direita; eixo de
  tempo (HH:MM até 24 h, DD/MM em 7 d); "Sem dados neste período." quando vazio. Pé: os 5 períodos
  (1 h, 6 h, 12 h, 24 h, 7 d; alvo `PERIODO` indice 0..4) e a navegação no tempo **‹ anterior** e
  **› seguinte** (alvos `ANTERIOR`, `SEGUINTE`; seguinte desabilitado quando já está no agora) —
  encaixe os 7 controles no pé e justifique. Tocar no gráfico abre o detalhe (alvo `AREA`).
  Até 200 baldes; o modelo guarda os baldes em inteiros (décimos), não em float.
  Amostras: 24 h com umidade; 7 d; 1 h com lacunas; sem dados; navegação no passado.
- **Detalhe (`VmDetalhe`)**: desenho `detalhe_temp` por grandeza (paginador entre Temperatura, Umidade,
  Pressão): seis cartões de 96 x 72, textos a 8 px da borda, o valor em fonte de display com a unidade em
  `apoio` (4 px de tinta entre os dois, 5 depois de um "1", cuja haste fica rente ao "h") e uma linha sob ele: Máximo e Mínimo
  com a hora nas janelas de dia e com a data na de 7 d, Média com "n leituras" ("n = 1440" quando a frase
  não cabe), Desvio σ com "dispersão", Variação com "início→fim", Agora com a hora ("Fim", com a data,
  numa janela passada). Voltar volta ao gráfico. Amostras: temperatura; umidade; sem dados.
- **Calendário (`VmCalendario`)**: desenho `calendario_out`: mês e ano, ‹ ›, Voltar, os dias, hoje em
  acento, ponto nos dias com histórico, **Hoje**. Só dias com dados são alvos (`DIA`, indice = dia).
  Amostras: outubro 2026 com hoje; mês sem dados; fevereiro.
- **Atualização (`VmOta`)**: tela nova: título "Atualização do firmware"; por fase: Recebendo (barra +
  "%"), Conferindo a assinatura (SHIELD_CHECK), Imagem conferida, Instalando (REFRESH_CW; "O aparelho
  reinicia sozinho em cerca de 30 s."), Recusada (CIRCLE_X + motivo + "Continua na versão atual."),
  Interrompida; nas fases em andamento o aviso "Não desligue o aparelho." (alerta, ícone POWER).
  Recusada/Interrompida: "Toque na tela para voltar." Sem alvos (a cola trata o toque).
  Amostras: cada fase; recebendo 42 %; os 7 motivos de recusa (levantamento §11).

## 7. Fase 2: a cola com o firmware

Cada grupo troca o seu `src/ui/angulo/DisplayManager_UiA_<Grupo>.cpp` (hoje um esboço que não reivindica
nenhum modo) por duas funções membro do `DisplayManager`:

```cpp
bool DisplayManager::uaMontar<Grupo>( UaQuadro &q );                        // o quadro do modo atual
UaToque DisplayManager::uaToque<Grupo>( const ua::Alvo *a, int16_t &x, int16_t &y );   // o toque nele
```

Modelo pronto para copiar: `src/ui/angulo/DisplayManager_UiA.cpp`, funções `uaMontarBase` e `uaToqueBase`.

**Montar.** `switch ( _uiMode )` nos modos do grupo; `VmX &vm = uaVm<VmX>( )` (zerado, no buffer comum de
4 kB), preencher a partir do estado (os mesmos membros que a função de desenho antiga lê, com as mesmas
travas), `uaUsar<VmX, desenharX, alvosX>( q, vm )`, `return true`; nos outros modos, `return false`.
Roda no fim de cada volta do Core 1 quando alguma função de desenho antiga pediu (`UA_DESVIO`) e, no
mínimo, a cada 250 ms; o quadro só é pintado se o hash do modelo mudou. Então: nada de efeito colateral
além dos avisos que a função de desenho antiga dava ao Core 0 (`_loadingDrawn = true`, `_statusLastDraw =
millis( )`), e nada de `millis( )` cru dentro do modelo (um relógio que muda a cada volta repinta à toa:
guarde segundos inteiros).

**Tocar.** `a` é o alvo do quadro que está no vidro (ou `nullptr`, toque fora de qualquer alvo). Por modo:
- **Tradução (T):** escreva em `x, y` o centro do elemento equivalente no leiaute antigo e devolva
  `UaToque::TRADUZIDO`; o ramo antigo de `handleTouch` roda com esse ponto. Com `a == nullptr`, devolva
  `TRATADO` (zona morta). O toque longo, a repetição e o deslizar continuam funcionando porque o mesmo alvo
  dá o mesmo ponto antigo a cada amostra.
- **Controlador (C):** faça o que o ramo antigo faz para o mesmo pedido — as mesmas funções (`show*`,
  `pushUiEvent`, `forceDashboard`), os mesmos campos de estado, as mesmas checagens de permissão da tela —
  e devolva `TRATADO`. Use os filtros antigos para o toque valer uma vez (`acceptTouch( zona )`), repetir
  segurando (`acceptHoldTouch( zona )`, a cada 300 ms) ou deslizar (`acceptSlideTouch( zona )`), com zonas
  próprias do modo. Cite em comentário o arquivo:linha do ramo antigo que cada ação reproduz.
- **Bruto (B):** devolva `TRADUZIDO` sem mexer em `x, y` (calibração e sensibilidade nem chegam aqui).
- Estado que só a tela nova tem (a página de uma lista que o código antigo não paginava, a camada do
  teclado) fica em variáveis `static` do arquivo do grupo, zeradas na entrada no modo (guarde o modo da
  volta anterior e compare). Depois de mudar esse estado, `uaSujo( )`.

**Construir.** Cada grupo trabalha na sua worktree (`wt_<grupo>`, num ramo próprio saído do ramo do driver de UI
Ângulo; no estudo eram `study/tft-angulo-ui-a-<grupo>`) e
prova com `pio run -e pico_wiretft_angulo` (a primeira compilação baixa as bibliotecas; -Werror vale em
`src/`). Os arquivos de tela do grupo entram no `build_src_filter` pelo padrão `+<ui/angulo/telas/*.cpp>`.
Não grave placa: a bancada é do integrador, que roda o roteiro de toque em cada tela nos dois temas.

**Regras que não podem quebrar.** Nenhum arquivo fora de `src/ui/angulo/` muda nesta fase (o que precisar
fora, peça). As imagens de produto não podem mudar nem um byte. Nunca grave a política de PIN pela tela no
aparelho da bancada (marca todas as contas para trocar o PIN); a tela pode ser aberta, alterada e
cancelada.
