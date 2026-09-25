# Ângulo

Guia de referência do padrão de interface para web e celular. Versão 0.1, setembro de 2026.

Guarde este arquivo na raiz de cada projeto (`ANGULO.md`) ou em `docs/`. Ele serve a quem desenha e a quem codifica. A versão navegável, com previews ao vivo, fica em <https://claude.ai/artifact/ErR2dYNRpubhB1LULPFYLe>.

> **Esta é a cópia do `simut`: o guia compartilhado, §1 a §10.** A casa canônica
> do Ângulo é o repo **`simut-rx`**, e é lá que ficam os três anexos (§11 a §13:
> o que foi corrigido em relação à v0.1, o que aquele projeto estende, e como a
> regra é feita valer por máquina).
>
> **Os valores não moram neste arquivo.** A fonte da verdade é
> `simut-rx/design/tokens.cor.json` + `tokens.comum.json`, de onde
> `design/build-tokens.mjs` (`npm run tokens`) gera `web/angulo.css` e
> `src/theme/angulo.ts`. O `simut` **consome** esses valores: o site copia o
> `angulo.css` byte a byte em `docs/assets/angulo.css`, com a face de display em
> `docs/assets/fonts/`; a UI do aparelho (`WebUI.h`, no `LANG_JS` e no
> `LOGIN_PAGE`) e o manual (`tools/build_manual.py`) carregam cópias dos tokens;
> e `tools/gen_logo.py` desenha a marca lendo as cores do `angulo.css`. Onde
> cada cópia mora, o que nunca se edita à mão e o que é conferido por máquina
> está no `AGENTS.md`, §7. O texto do §5 descreve a tradução manual da v0.1 e
> fica como referência de formato — no `simut`, mexer nas cores é mexer nos
> tokens do `simut-rx` e recopiar.

## Sumário

1. [Em uma frase](#1-em-uma-frase)
2. [As oito regras](#2-as-oito-regras)
3. [Tokens](#3-tokens)
4. [Componentes](#4-componentes)
5. [Implementação](#5-implementação)
6. [Iconografia](#6-iconografia)
7. [Microcopy](#7-microcopy)
8. [Checklist de revisão de tela](#8-checklist-de-revisão-de-tela)
9. [Direções alternativas](#9-direções-alternativas)
10. [Nomes alternativos e licenças](#10-nomes-alternativos-e-licenças)

> Os anexos §11–§13 (correções da v0.1, extensões e enforcement por máquina) ficam na cópia do `simut-rx`.

## 1. Em uma frase

Barato de manter, difícil de confundir: uma cor de destaque, duas famílias de fonte, dois raios de canto e uma grade de 4px, escritos uma vez num arquivo de tokens que serve as duas plataformas.

O que torna o sistema barato:

- A fonte de texto é a do aparelho (San Francisco, Segoe, Roboto). Custo zero, cara nativa, nada para baixar.
- A fonte de display é gratuita (Bricolage Grotesque, licença OFL) e vem em dois pesos, 500 e 600, cerca de 45 KB no total.
- Os ícones vêm de um único conjunto gratuito (Lucide ou Tabler).
- Na web, tudo são variáveis CSS: funciona sem build, sem framework, em qualquer stack.
- Um `tokens.json` gera o CSS da web e o tema do app. A paleta muda num lugar só.

## 2. As oito regras

Um layout gerado por máquina tem tiques reconhecíveis: gradiente roxo-azul, cartões idênticos com a mesma sombra cinza, emoji no lugar de ícone, uma palavra colorida no meio do título, texto que promete revolucionar alguma coisa. As regras existem para que nada feito com o Ângulo caia nesses tiques, e para que qualquer pessoa consiga decidir sozinha.

1. **Uma cor manda.** Só `acento` chama atenção; os neutros fazem todo o resto. Gradiente aparece em gráfico de dados e em mais nada: nunca em botão, título ou fundo.
2. **Duas famílias, papéis claros.** `display` carrega a personalidade em títulos; `texto` é a fonte do sistema operacional. Não se destaca uma palavra do título com outra cor ou outro peso.
3. **Linha antes de sombra.** Superfícies se separam por contraste e por `linha` de 1px. `sombra-flutuante` é só para o que flutua de verdade: menu, modal, toast. Cartão nunca tem sombra.
4. **Dois raios e o círculo.** `raio-controle` em botões, campos e chips; `raio-cartao` em cartões, modais e imagens; `raio-total` em pílulas e avatares. Um único raio em tudo entrega o kit de template.
5. **Espaço na escala.** Toda distância é um token `espaco-*` da grade de 4px. Um valor mágico no código é um bug a corrigir.
6. **Ícone de um traço só.** Um único conjunto, traço de 1,5px, tamanho 20 ou 24. Emoji nunca faz papel de ícone, e cartão nunca abre com um emoji.
7. **Microcopy que diz o que faz.** O botão nomeia a ação e o objeto: "Salvar fatura", não "Vamos lá". Nada de "revolucione", "desbloqueie", "turbine". Um erro explica o que aconteceu e como resolver, sem pedir desculpas.
8. **Todo estado existe.** Hover, foco visível, desabilitado, erro, vazio e carregando são desenhados antes de o componente ser considerado pronto.

## 3. Tokens

Os tokens são nomeados pela função, nunca pela cor: `acento` sobrevive a uma troca de paleta, `verde-600` não.

### 3.1 Cor

Dois temas, `claro` (padrão) e `escuro`. Todos os pares texto/fundo da tabela de combinações passam de 4,5:1 nos dois temas — e isso é **medido no build**, não afirmado: `design/build-tokens.mjs` confere os 52 pares e se recusa a gerar se algum cair abaixo. Na v0.1 dois caíam (§11, na cópia do `simut-rx`).

| Token | Claro | Escuro | Uso |
| --- | --- | --- | --- |
| `fundo` | `#f6f6f4` | `#161513` | Fundo da página e das telas. Neutro morno, nunca branco nem preto puros. |
| `superficie` | `#ffffff` | `#201e1b` | Cartões, campos, menus e folhas. |
| `superficie-2` | `#ecebe6` | `#2a2723` | Faixas, chips inativos, cabeçalho de tabela, fundo de código. |
| `tinta` | `#201e1a` | `#ebe7df` | Texto principal e ícones. |
| `tinta-2` | `#5f5b54` | `#a39c90` | Texto de apoio, metadados, rótulos, ícones secundários. |
| `linha` | `#dcdad3` | `#383430` | Divisórias e contorno de cartão. Decorativa: fica abaixo de 3:1 de propósito. |
| `linha-forte` | `#827e76` | `#78716a` | Borda de campos, caixas de seleção e botão secundário. Passa de 3:1. |
| `acento` | `#1f6355` | `#5fb39a` | A única cor de destaque: botão primário, link, aba ativa, anel de foco. |
| `acento-forte` | `#174d42` | `#7cc7b2` | Hover e pressionado do que usa `acento`. |
| `acento-tinta` | `#f1faf6` | `#0e211b` | Texto e ícone sobre `acento` e `acento-forte`. |
| `positivo` | `#20784e` | `#6fbe8e` | Sucesso: texto de selo, check, mensagem de êxito. **Corrigido — ver §11 na cópia do `simut-rx`.** |
| `positivo-suave` | `#e1f0e7` | `#24352b` | Fundo de selo e faixa de sucesso, com texto em `positivo`. |
| `alerta` | `#8a6116` | `#d9a84e` | Atenção e pendência: texto de selo e ícone. |
| `alerta-suave` | `#f3ead2` | `#38301c` | Fundo de selo e faixa de atenção, com texto em `alerta`. |
| `perigo` | `#b3382e` | `#e07862` | Erro e exclusão: mensagem, borda de campo inválido, botão destrutivo. |
| `perigo-suave` | `#f7e3e0` | `#382220` | Fundo de selo e faixa de erro, com texto em `perigo`. |
| `perigo-tinta` | `#fff5f3` | `#2b100c` | Texto sobre o botão destrutivo. |

Combinações permitidas (o que vai sobre o quê):

| Sobre este fundo | Use este texto |
| --- | --- |
| `fundo`, `superficie`, `superficie-2` | `tinta` para o principal, `tinta-2` para o apoio |
| `acento`, `acento-forte` | `acento-tinta` |
| `positivo-suave`, `alerta-suave`, `perigo-suave` | `positivo`, `alerta`, `perigo`, respectivamente |
| `perigo` (botão destrutivo) | `perigo-tinta` |

O tema escuro não é o claro invertido: o fundo é um grafite morno, o `acento` clareia para manter contraste e os fundos suaves são misturas com o próprio fundo, não transparências.

### 3.2 Tipografia

Famílias:

| Família | Pilha | Papel |
| --- | --- | --- |
| `display` | `"Bricolage Grotesque", "Segoe UI", system-ui, sans-serif` | Títulos. Embutida no projeto, pesos 500 e 600. |
| `texto` | `system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif` | Todo o resto. É a fonte do aparelho. |
| `mono` | `ui-monospace, "SF Mono", "Cascadia Mono", Menlo, Consolas, monospace` | Números em coluna e código. |

Escala, sete estilos:

| Estilo | Família | Tamanho / linha | Peso | Espaçamento | Uso |
| --- | --- | --- | --- | --- | --- |
| `display` | display | 34 / 38 | 600 | -0.02em | Um por tela, no máximo: título de página ou número principal. |
| `titulo` | display | 24 / 30 | 600 | -0.01em | Título de seção e de modal. |
| `subtitulo` | display | 18 / 24 | 600 | -0.005em | Título de cartão e de grupo de lista. |
| `corpo` | texto | 16 / 24 | 400 | 0 | Texto corrido e itens de lista. Linha com até 70 caracteres. |
| `rotulo` | texto | 13 / 16 | 600 | 0 | Rótulo de campo, cabeçalho de tabela, nome de aba. Em `tinta-2`, sem caixa alta. |
| `apoio` | texto | 13 / 18 | 400 | 0 | Ajuda, metadados e legendas. Em `tinta-2`. |
| `dado` | mono | 13 / 20 | 500 | 0 | Valores em coluna, códigos, identificadores. Nunca como decoração. |

Receita de hierarquia: uma tela tem no máximo um `display` ou um `titulo`; cartões usam `subtitulo`; `corpo` faz todo o resto. Botões usam `texto` em 15px, peso 500, sem estilo próprio.

### 3.3 Espaço

Grade de 4px. Um valor fora da escala é bug, não escolha.

| Token | Valor | Uso |
| --- | --- | --- |
| `espaco-1` | 4px | Entre ícone e texto. |
| `espaco-2` | 8px | Entre rótulo e campo; entre selos numa linha. |
| `espaco-3` | 12px | Padding vertical de botões e campos; entre itens de lista compacta. |
| `espaco-4` | 16px | Padding horizontal de botões; cartão compacto; margem de tela no celular. |
| `espaco-5` | 24px | Padding de cartão; entre blocos de um formulário. |
| `espaco-6` | 32px | Entre seções de uma tela; margem de página na web. |
| `espaco-7` | 48px | Respiro acima de um título de página. |

### 3.4 Raio

| Token | Valor | Uso |
| --- | --- | --- |
| `raio-controle` | 6px | Botões, campos, chips, menus. |
| `raio-cartao` | 12px | Cartões, modais, folhas, imagens. |
| `raio-total` | 999px | Pílulas, avatares, interruptores. É o círculo, não um terceiro raio. |

### 3.5 Sombra

| Token | Claro | Escuro | Uso |
| --- | --- | --- | --- |
| `sombra-flutuante` | `0 12px 32px rgba(23, 22, 20, 0.16)` | `0 16px 40px rgba(0, 0, 0, 0.5)` | Menus, modais, toasts e folhas. Cartões nunca. |

### 3.6 Foco e toque

- Anel de foco: borda de 2px em `acento` com 2px de folga (`outline-offset`), em `:focus-visible`. Aparece na navegação por teclado, nunca no clique do mouse.
- Alvo de toque: no celular, todo controle tem pelo menos 44px de altura, mesmo que o desenho pareça menor. O padding invisível completa o alvo.

## 4. Componentes

### 4.1 Botão

Ação em quatro pesos. O peso diz quão importante é a ação; a cor só muda quando ela é irreversível.

- Um primário por tela. Se duas ações merecem o primário, uma é secundária.
- Rótulo com verbo no infinitivo e objeto: "Salvar fatura", "Exportar CSV".
- Fonte `texto`, 15px, peso 500. Padding `espaco-3` × `espaco-4`. Altura mínima 40px na web, alvo de 44px no toque. Canto `raio-controle`.

| Peso | Repouso | Hover e pressionado |
| --- | --- | --- |
| Primário | fundo `acento`, texto `acento-tinta` | fundo `acento-forte` |
| Secundário | fundo `superficie`, borda `linha-forte`, texto `tinta` | fundo `superficie-2` |
| Discreto | sem fundo, texto `acento` | fundo `superficie-2` |
| Destrutivo | fundo `perigo`, texto `perigo-tinta` | opacidade 0.9 |

Desabilitado: opacidade 0.45. Carregando: rótulo em curso ("Enviando…") e botão desabilitado, nunca um spinner sozinho.

### 4.2 Campo

Rótulo, caixa, ajuda ou erro, sempre nesta ordem, separados por `espaco-2`.

- Todo campo tem rótulo visível em `rotulo`. Placeholder não substitui rótulo.
- A ajuda (`apoio`, `tinta-2`) diz o que o sistema faz com o dado. O erro ocupa o lugar da ajuda, nunca os dois juntos, e diz o que fazer.
- Caixa: fundo `superficie`, borda 1px `linha-forte`, canto `raio-controle`, padding `espaco-3`, texto em `corpo`.
- Valide ao sair do campo ou ao enviar, nunca a cada tecla.

| Estado | Borda | O que mais muda |
| --- | --- | --- |
| Repouso | `linha-forte` | |
| Foco | `acento` | anel de foco |
| Erro | `perigo` | mensagem em `perigo` no lugar da ajuda |
| Desabilitado | `linha` | fundo `superficie-2`, texto `tinta-2` |

### 4.3 Selo

Estado em uma palavra: ponto de 6px, fundo suave, texto firme.

- Fonte `texto`, 13px, peso 600. Padding `espaco-1` × `espaco-2`. Canto `raio-total`.
- Informa, não convida ao clique. Filtro clicável é chip, outro componente.
- O ponto ajuda quem não distingue cores; o estado precisa ser legível pelo texto sozinho.

| Tom | Fundo | Texto e ponto | Quando |
| --- | --- | --- | --- |
| Positivo | `positivo-suave` | `positivo` | Concluído, pago, ativo |
| Alerta | `alerta-suave` | `alerta` | Pendente, aguardando |
| Perigo | `perigo-suave` | `perigo` | Vencido, falhou, bloqueado |
| Neutro | `superficie-2` | `tinta-2` | Rascunho, arquivado |

Não existe tom "informativo" em `acento`: a cor de destaque é reservada para ação.

### 4.4 Cartão

Superfície contornada por linha, nunca por sombra. Agrupa um assunto só.

- Fundo `superficie`, borda 1px `linha`, canto `raio-cartao`, padding `espaco-5` (`espaco-4` no compacto e nas listas do celular).
- Título em `subtitulo`; metadados em `apoio`; valores em `dado` quando em coluna.
- Nada de cartão dentro de cartão; dentro, o agrupamento é feito com `linha`. O cartão abre com o título, não com ícone.

| Estado | O que muda |
| --- | --- |
| Clicável, em hover | borda `linha-forte`, cursor ponteiro, sem sombra |
| Selecionado | borda `acento` |
| Vazio | mesmo cartão, com uma frase em `corpo` dizendo o que fazer |

## 5. Implementação

### 5.1 Fonte da verdade

O `tokens.json` guarda cada token com nome, valor por tema e nota de uso. Os blocos de CSS e de React Native abaixo são a tradução manual desse arquivo: bastam para começar hoje, sem etapa de build. Quando o número de projetos crescer, o Style Dictionary (gratuito) gera os dois a partir do JSON; ele lê o formato aninhado, como neste trecho:

```json
{
  "cor": {
    "acento":       { "value": "#1f6355", "comment": "Botão primário, link, aba ativa, anel de foco." },
    "acento-tinta": { "value": "#f1faf6", "comment": "Texto sobre acento." }
  },
  "espaco": {
    "4": { "value": "16px", "comment": "Padding horizontal de botões." }
  },
  "raio": {
    "controle": { "value": "6px", "comment": "Botões, campos, chips." }
  }
}
```

Um arquivo por tema para as cores (`cor.claro.json`, `cor.escuro.json`) e um comum para o resto.

### 5.2 Web: variáveis CSS

Copie para `angulo.css` e carregue antes de qualquer outro estilo. O tema é escolhido por `data-theme` no `<html>`; sem o atributo, vale o claro.

```css
/* ==========================================================================
   Ângulo v0.1 — tokens para a web
   Tradução do tokens.json. Quando um valor mudar, mude nos dois.
   Tema: <html data-theme="claro"> (padrão) ou <html data-theme="escuro">
   ========================================================================== */

/* ---------- Fonte de display: embutida, licença OFL ---------- */
@font-face {
  font-family: "Bricolage Grotesque";
  font-weight: 500;
  font-display: swap;
  src: url("fonts/BricolageGrotesque-500.woff2") format("woff2");
}

@font-face {
  font-family: "Bricolage Grotesque";
  font-weight: 600;
  font-display: swap;
  src: url("fonts/BricolageGrotesque-600.woff2") format("woff2");
}

/* ---------- Cor: tema claro (padrão) ---------- */
:root,
[data-theme="claro"] {
  color-scheme: light;

  --fundo:            #f6f6f4;   /* página e telas */
  --superficie:       #ffffff;   /* cartões, campos, menus */
  --superficie-2:     #ecebe6;   /* faixas, chips inativos, código */
  --tinta:            #201e1a;   /* texto principal */
  --tinta-2:          #5f5b54;   /* texto de apoio */
  --linha:            #dcdad3;   /* divisórias e contorno de cartão (decorativa) */
  --linha-forte:      #827e76;   /* borda de controles interativos (3:1) */

  --acento:           #1f6355;   /* a única cor de destaque */
  --acento-forte:     #174d42;   /* hover e pressionado */
  --acento-tinta:     #f1faf6;   /* texto sobre acento */

  --positivo:         #20784e;
  --positivo-suave:   #e1f0e7;
  --alerta:           #8a6116;
  --alerta-suave:     #f3ead2;
  --perigo:           #b3382e;
  --perigo-suave:     #f7e3e0;
  --perigo-tinta:     #fff5f3;   /* texto sobre o botão destrutivo */

  --sombra-flutuante: 0 12px 32px rgba(23, 22, 20, 0.16);   /* só menu, modal, toast */
}

/* ---------- Cor: tema escuro ---------- */
[data-theme="escuro"] {
  color-scheme: dark;

  --fundo:            #161513;
  --superficie:       #201e1b;
  --superficie-2:     #2a2723;
  --tinta:            #ebe7df;
  --tinta-2:          #a39c90;
  --linha:            #383430;
  --linha-forte:      #78716a;

  --acento:           #5fb39a;   /* clareia para manter contraste sobre o grafite */
  --acento-forte:     #7cc7b2;
  --acento-tinta:     #0e211b;

  --positivo:         #6fbe8e;
  --positivo-suave:   #24352b;
  --alerta:           #d9a84e;
  --alerta-suave:     #38301c;
  --perigo:           #e07862;
  --perigo-suave:     #382220;
  --perigo-tinta:     #2b100c;

  --sombra-flutuante: 0 16px 40px rgba(0, 0, 0, 0.5);
}

/* ---------- Escalas: iguais nos dois temas ---------- */
:root {
  /* Famílias */
  --font-display: "Bricolage Grotesque", "Segoe UI", system-ui, sans-serif;
  --font-texto:   system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --font-mono:    ui-monospace, "SF Mono", "Cascadia Mono", Menlo, Consolas, monospace;

  /* Espaço: grade de 4px */
  --espaco-1: 4px;
  --espaco-2: 8px;
  --espaco-3: 12px;
  --espaco-4: 16px;
  --espaco-5: 24px;
  --espaco-6: 32px;
  --espaco-7: 48px;

  /* Raio: dois raios e o círculo */
  --raio-controle: 6px;
  --raio-cartao:   12px;
  --raio-total:    999px;
}

/* ---------- Estilos de texto: a escala inteira em sete classes ---------- */
.display   { font: 600 34px/38px var(--font-display); letter-spacing: -0.02em; }
.titulo    { font: 600 24px/30px var(--font-display); letter-spacing: -0.01em; }
.subtitulo { font: 600 18px/24px var(--font-display); letter-spacing: -0.005em; }
.corpo     { font: 400 16px/24px var(--font-texto); }
.rotulo    { font: 600 13px/16px var(--font-texto); color: var(--tinta-2); }
.apoio     { font: 400 13px/18px var(--font-texto); color: var(--tinta-2); }
.dado      { font: 500 13px/20px var(--font-mono); }

/* ---------- Base e foco ---------- */
body {
  margin: 0;
  background: var(--fundo);
  color: var(--tinta);
  font: 400 16px/24px var(--font-texto);
}

/* Anel de foco só por teclado, em todo controle, nos dois temas */
:focus-visible {
  outline: 2px solid var(--acento);
  outline-offset: 2px;
}
```

Para seguir a preferência do sistema operacional, troque o atributo no carregamento:

```js
// Aplica o tema escuro quando o aparelho pede e nada foi escolhido pelo usuário.
// A escolha manual (um interruptor na interface) grava em localStorage e vence a do aparelho.
const escolhaSalva = localStorage.getItem('angulo:tema');            // "claro" | "escuro" | null
const preferenciaEscura = matchMedia('(prefers-color-scheme: dark)').matches;

document.documentElement.dataset.theme = escolhaSalva ?? (preferenciaEscura ? 'escuro' : 'claro');
```

### 5.3 React Native: objeto de tema

Copie para `angulo.ts`. Os nomes são os mesmos do CSS; os valores de espaço e raio ficam em pontos. Registre os dois arquivos da Bricolage no projeto (`react-native-asset` ou `expo-font`) com os nomes usados em `familia`.

```ts
/**
 * Ângulo v0.1 — tema para React Native
 * Tradução do tokens.json. Quando um valor mudar, mude nos dois.
 */
import { Platform, type TextStyle } from 'react-native';

/** Papéis de cor de um tema. Nomeados pela função, nunca pela cor. */
export interface CoresAngulo {
  fundo: string;
  superficie: string;
  superficie2: string;
  tinta: string;
  tinta2: string;
  linha: string;
  linhaForte: string;
  acento: string;
  acentoForte: string;
  acentoTinta: string;
  positivo: string;
  positivoSuave: string;
  alerta: string;
  alertaSuave: string;
  perigo: string;
  perigoSuave: string;
  perigoTinta: string;
}

/** Tema claro: o padrão. */
export const coresClaro: CoresAngulo = {
  fundo:         '#f6f6f4',
  superficie:    '#ffffff',
  superficie2:   '#ecebe6',
  tinta:         '#201e1a',
  tinta2:        '#5f5b54',
  linha:         '#dcdad3',
  linhaForte:    '#827e76',
  acento:        '#1f6355',
  acentoForte:   '#174d42',
  acentoTinta:   '#f1faf6',
  positivo:      '#20784e',
  positivoSuave: '#e1f0e7',
  alerta:        '#8a6116',
  alertaSuave:   '#f3ead2',
  perigo:        '#b3382e',
  perigoSuave:   '#f7e3e0',
  perigoTinta:   '#fff5f3',
};

/** Tema escuro: grafite morno, acento mais claro para manter contraste. */
export const coresEscuro: CoresAngulo = {
  fundo:         '#161513',
  superficie:    '#201e1b',
  superficie2:   '#2a2723',
  tinta:         '#ebe7df',
  tinta2:        '#a39c90',
  linha:         '#383430',
  linhaForte:    '#78716a',
  acento:        '#5fb39a',
  acentoForte:   '#7cc7b2',
  acentoTinta:   '#0e211b',
  positivo:      '#6fbe8e',
  positivoSuave: '#24352b',
  alerta:        '#d9a84e',
  alertaSuave:   '#38301c',
  perigo:        '#e07862',
  perigoSuave:   '#382220',
  perigoTinta:   '#2b100c',
};

/** Grade de 4px, em pontos. Nenhum valor fora daqui. */
export const espaco = {
  1: 4,
  2: 8,
  3: 12,
  4: 16,
  5: 24,
  6: 32,
  7: 48,
} as const;

/** Dois raios e o círculo. */
export const raio = {
  controle: 6,
  cartao:   12,
  total:    999,
} as const;

/** Altura mínima de qualquer alvo de toque. */
export const ALVO_TOQUE = 44;

/**
 * Famílias. A de display vem embutida no app; texto e mono são as do aparelho
 * (fontFamily undefined deixa o sistema escolher).
 */
export const familia = {
  display500: 'BricolageGrotesque-500',
  display600: 'BricolageGrotesque-600',
  texto:      undefined,
  mono:       Platform.select({ ios: 'Menlo', android: 'monospace', default: 'monospace' }),
} as const;

/**
 * Escala de texto. O letterSpacing do RN é em pontos, por isso os valores
 * já vêm multiplicados pelo tamanho (-0.02em × 34 = -0.68).
 */
export const tipo: Record<
  'display' | 'titulo' | 'subtitulo' | 'corpo' | 'rotulo' | 'apoio' | 'dado',
  TextStyle
> = {
  display:   { fontFamily: familia.display600, fontSize: 34, lineHeight: 38, letterSpacing: -0.68 },
  titulo:    { fontFamily: familia.display600, fontSize: 24, lineHeight: 30, letterSpacing: -0.24 },
  subtitulo: { fontFamily: familia.display600, fontSize: 18, lineHeight: 24, letterSpacing: -0.09 },
  corpo:     { fontFamily: familia.texto,      fontSize: 16, lineHeight: 24, fontWeight: '400' },
  rotulo:    { fontFamily: familia.texto,      fontSize: 13, lineHeight: 16, fontWeight: '600' },
  apoio:     { fontFamily: familia.texto,      fontSize: 13, lineHeight: 18, fontWeight: '400' },
  dado:      { fontFamily: familia.mono,       fontSize: 13, lineHeight: 20, fontWeight: '500' },
};

/** Sombra só para o que flutua: menu, modal, toast. Cartão nunca. */
export const sombraFlutuante = {
  shadowColor:   '#171614',
  shadowOffset:  { width: 0, height: 12 },
  shadowOpacity: 0.16,
  shadowRadius:  16,
  elevation:     8,   // Android
} as const;
```

Escolha do tema pela preferência do aparelho:

```ts
import { useColorScheme } from 'react-native';
import { coresClaro, coresEscuro } from './angulo';

/** Retorna as cores do tema atual; um interruptor manual pode sobrepor esta escolha. */
export function useCores() {
  return useColorScheme() === 'dark' ? coresEscuro : coresClaro;
}
```

### 5.4 Flutter

Os mesmos valores viram um `ThemeExtension` com os dezessete papéis de cor por tema, mais constantes para espaço e raio. O Style Dictionary emite Dart a partir do mesmo JSON. Regras que não mudam: a fonte de texto é a do aparelho, a de display é a Bricolage embutida, e os alvos de toque têm 44 de altura.

## 6. Iconografia

- Um único conjunto por projeto: Lucide (licença ISC) ou Tabler (MIT). Nunca os dois misturados.
- Traço de 1,5px, tamanho 20 dentro de botões e campos, 24 em navegação e listas.
- Cor `tinta` ou `tinta-2`; em botão primário, `acento-tinta`. Ícone nunca é a única forma de dizer o que um botão faz: ou tem rótulo, ou tem `aria-label` e dica.
- Ainda não há logotipo. A marca é o nome em `display`, peso 600, espaçamento -0.02em.

## 7. Microcopy

Texto é conteúdo de design, não decoração. Voz ativa, sentença normal (sem caixa alta), verbo no infinitivo nos botões, e o mesmo nome para a mesma ação em todo o fluxo: "Publicar" gera "Publicado".

| Em vez de | Escreva |
| --- | --- |
| Enviar | Salvar fatura |
| Vamos lá! | Começar cadastro |
| Ops, algo deu errado | Não foi possível salvar. Verifique a conexão e tente de novo. |
| Campo inválido | Informe um CPF ou CNPJ válido, com todos os dígitos. |
| Nenhum item encontrado | Nenhuma fatura ainda. Crie a primeira em "Nova fatura". |
| Revolucione sua gestão | Controle suas cobranças em um lugar só. |

Erros não pedem desculpas e nunca são vagos. Telas vazias dizem o que fazer para preenchê-las.

## 8. Checklist de revisão de tela

Antes de considerar uma tela pronta, nos dois temas:

- [ ] Só um `display` ou `titulo` na tela, e só um botão primário.
- [ ] Nenhum hex fora dos tokens; nenhuma distância fora da escala `espaco-*`.
- [ ] Só os três raios; sombra apenas em menu, modal ou toast.
- [ ] Todo texto está sobre um fundo previsto na tabela de combinações.
- [ ] Todo controle mostra o anel de foco na navegação por teclado.
- [ ] Todo campo tem rótulo visível; todo erro diz o que fazer.
- [ ] Ícones de um conjunto só, sem emoji; nenhum título com uma palavra em outra cor.
- [ ] Botões com verbo e objeto; nenhuma hipérbole na tela.
- [ ] Estados vazio, carregando e erro desenhados.
- [ ] No celular, todo alvo de toque tem pelo menos 44px.

## 9. Direções alternativas

A direção deste guia é a recomendada: neutra o bastante para qualquer projeto, com assinatura na tipografia e no verde. Duas outras foram consideradas e ficam registradas para comparação.

**Editorial.** Papel frio `#f5f4ef`, tinta `#1c1a17`, acento vinho `#7c2d3a`. Display em Fraunces (serifa, gratuita); raios de 2px em controles e 6px em cartões. Para produtos de conteúdo, portfólio e marca com voz. Advertência: creme, serifa de display e terracota viraram o clichê da página gerada por máquina; esta direção só funciona se fugir dos três ao mesmo tempo.

**Oficina.** Escura por padrão: grafite morno `#191817`, superfície `#221f1c`, tinta `#ece7db`, acento âmbar `#e3a63b`. Display em IBM Plex Mono e números tabulares em tudo. Para ferramenta técnica, painel de operação e produto para desenvolvedores. O tema claro é o secundário.

## 10. Nomes alternativos e licenças

| Nome | Por quê |
| --- | --- |
| Ângulo | "Angelo" escondido à vista de todos; a unidade básica de qualquer layout. Pacotes `@angulo/tokens`, `@angulo/react`, `@angulo/native`. |
| Prumo | A ferramenta de obra que garante o alinhamento. Precisão de ofício, sotaque brasileiro. |
| Régua | Ter régua é ter padrão. Medida, consistência, exigência. |
| Traço | O traço do desenhista: o que torna um trabalho reconhecível à distância. |
| Cerne | O núcleo duro da madeira, a parte que sustenta. Bom nome para uma biblioteca-base. |

Licenças do que o sistema usa: Bricolage Grotesque, SIL Open Font License 1.1; Lucide, ISC; Tabler Icons, MIT; Style Dictionary, Apache 2.0. Todos gratuitos para uso comercial.
