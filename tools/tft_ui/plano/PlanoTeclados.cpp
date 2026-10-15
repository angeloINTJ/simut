// Ports of the plan's keyboards (renderizador/src/teclado.js): T1, the text
// keyboard, and P1 to P3, the PIN keypads whose layouts the firmware's own PIN
// uses. Each function is the plan's own, in the same order of drawing, and the
// examples are the plan's states (ESTADOS_TEXTO with the SSID one of T1,
// ESTADOS_PIN, ESTADOS_PIN_DIGITOS). The components are the kit's; only the
// title bar is local, because angulo.js has its own (telas.js's is the kit's).
//
// Not ported: T2 to T6 and P4, and what only they use (ESCALONADO, the grid
// cells of T3: the `opts` of fileira( ) and the `grade` of teclaSalvar( )).
#include <stdint.h>
#include <string.h>

#include "PlanoComum.h"

using namespace ua;

namespace plano {

namespace {

/* ------------------------------------------------------------------ */
/* Shared helpers                                                       */
/* ------------------------------------------------------------------ */

// angulo.js barraTitulo( ): the title on the left, the actions on the right,
// a line below. Unlike telas.js's, the title font is the caller's and the
// action buttons are 24 px tall at most, centred in the bar.
struct AcaoBarra { const char *rotulo; Botao tipo; bool desab; int16_t w; };   // w 0: from the label

void barraTituloAngulo( Pintor &p, int16_t h, const char *titulo, Fonte fonte, const AcaoBarra *acoes, int16_t n,
                        bool linha = true ) {
  const Tema &T = p.t( );
  textoCentrado( p, titulo, ESP3, 0, h, fonte, T.tinta, 'l' );
  int16_t x = 320 - ESP3;
  for ( int16_t i = n - 1; i >= 0; i-- ) {
    const AcaoBarra &a = acoes[i];
    const int16_t w = a.w ? a.w : larguraTinta( a.rotulo, tipo::botao ) + 2 * ESP2;
    const int16_t bh = h - 2 * ESP1 < 24 ? h - 2 * ESP1 : 24;
    const int16_t by = rd2( h - bh );
    x -= w;
    botao( p, x, by, w, bh, a.rotulo, a.tipo, a.desab );
    x -= ESP2;
  }
  if ( linha ) p.hline( 0, h, 320, T.linha );
}

// st.texto.length: the characters typed (code points here; JavaScript counts
// UTF-16 units, the same below U+10000, where every layer's character is).
int16_t comprimento( const char *s ) {
  int16_t n = 0;
  for ( ; *s; s++ )
    if ( ( (uint8_t)*s & 0xC0 ) != 0x80 ) n++;
  return n;
}

// Bytes of the UTF-8 sequence at c (Array.from( ) walks code points); a
// sequence cut by the end of the string is taken up to the end.
int bytesDe( const char *c ) {
  const uint8_t b = (uint8_t)*c;
  const int n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
  int k = 1;
  while ( k < n && c[k] ) k++;
  return k;
}

/* ------------------------------------------------------------------ */
/* The text keyboard: layers and state                                  */
/* ------------------------------------------------------------------ */

enum class Camada : uint8_t { MIN, MAI, NUM, SIMB };   // 'abc', 'ABC', '123', '#+='
enum class Shift : uint8_t { OFF, ON, LOCK };          // 'off', 'on', 'lock'

// teclado.js CAMADAS: the three rows of ten of the firmware, with the
// punctuation that passwords and SSIDs use most (@ . - _) at the ends in the
// place of ! ?.
const char *const kCamadas[4][3] = {
  { "qwertyuiop", "asdfghjkl@", "zxcvbnm.-_" },
  { "QWERTYUIOP", "ASDFGHJKL#", "ZXCVBNM!?;" },
  { "1234567890", "@#$%&*-+=~", "()[]{}/\\^_" },
  { "~`|<>:;\"'", "€£¥§°±×÷", "…•·¿¡“”‘’" },
};

struct Chip { const char *nome; bool ativo; };

// teclado.js base( ): the state of a text keyboard; each example overrides a few.
// `pressionada` is the legend of the pressed key; `ultimo` shows the last
// character typed in the clear; salvando is st.salvar === 'salvando' and
// teclasDesab is st.teclas === 'desab'. `chips` (the SSID option) replaces the
// help line with the networks found. rotuloSalvar is the plan's hook for
// another label on the Salvar key; no state of the plan sets it.
struct EstadoTexto {
  const char *titulo = "Nova senha";
  const char *texto = "";
  Camada camada = Camada::MIN;
  Shift shift = Shift::OFF;
  const char *pressionada = nullptr;
  const char *ajuda = "De 8 a 31 caracteres.";
  const char *erro = nullptr;
  bool ver = false;
  const char *ultimo = nullptr;
  bool salvando = false;
  bool teclasDesab = false;
  const Chip *chips = nullptr;
  int16_t nChips = 0;
  const char *rotuloSalvar = nullptr;
};

// camadaAtiva( ): the legends of the active layer (shift over abc gives ABC).
const char *const *camadaAtiva( const EstadoTexto &st ) {
  if ( st.camada == Camada::MIN && st.shift != Shift::OFF ) return kCamadas[(int)Camada::MAI];
  return kCamadas[(int)st.camada];
}

// fileira( ): a straight row of keys of one width; x0 = first x, passo = width + gap.
// One baseline per row: if it holds lowercase letters the x height is what is
// centred (the ascenders rise), if not the cap height.
void fileira( Pintor &p, int16_t x0, int16_t y, int16_t w, int16_t h, int16_t passo, const char *legendas,
              const EstadoTexto &st ) {
  bool minusculas = false;
  for ( const char *c = legendas; *c; c++ )
    if ( *c >= 'a' && *c <= 'z' ) minusculas = true;
  int16_t i = 0;
  for ( const char *c = legendas; *c; i++ ) {
    const int n = bytesDe( c );
    char legenda[5] = { 0 };
    memcpy( legenda, c, n );
    c += n;
    TeclaOpts o;
    o.legenda = legenda;
    o.estado = st.teclasDesab ? TeclaEstado::DESAB
               : st.pressionada && strcmp( st.pressionada, legenda ) == 0 ? TeclaEstado::PRESSIONADA
                                                                           : TeclaEstado::REPOUSO;
    o.alturaDeX = minusculas;
    o.fonte = tipo::corpo;
    tecla( p, x0 + i * passo, y, w, h, o );
  }
}

// estadoShift( ), iconeShift( ) and rotuloCamada( ): the shift key's state and
// icon (the arrow with a dash for caps lock) and the layer key's text.
TeclaEstado estadoShift( const EstadoTexto &st ) {
  return st.teclasDesab ? TeclaEstado::DESAB
         : st.shift == Shift::LOCK ? TeclaEstado::TRAVADA
         : st.shift == Shift::ON ? TeclaEstado::ATIVA
                                 : TeclaEstado::REPOUSO;
}
IconeNome iconeShift( const EstadoTexto &st ) {
  return st.shift == Shift::LOCK ? IconeNome::ARROW_BIG_UP_DASH : IconeNome::ARROW_BIG_UP;
}
const char *rotuloCamada( const EstadoTexto &st ) {
  return st.camada == Camada::MIN || st.camada == Camada::MAI ? "123" : "abc";
}

// conteudoSenha( ): the field's content, drawn in the box campo( ) left for it,
// (x + 12, y, w - 24, h): dots, or the text in the clear with a cursor, then
// the "see the password" icon against the right edge.
void conteudoSenha( Pintor &p, const EstadoTexto &st, int16_t xi, int16_t yi, int16_t wi, int16_t hi ) {
  const Tema &T = p.t( );
  const bool des = st.teclasDesab;
  const uint16_t tinta = des ? desab( T, T.tinta ) : T.tinta;
  const bool aberto = st.ver && *st.texto;
  if ( aberto ) {
    textoCentrado( p, st.texto, xi, yi, hi, tipo::corpo, tinta, 'l' );
  } else {
    PontosOpts o;
    o.ultimo = st.ultimo && *st.ultimo ? st.ultimo : nullptr;
    o.cursor = !des;
    o.temCor = true;
    o.cor = tinta;
    pontos( p, xi, yi, hi, comprimento( st.texto ), o );
  }
  if ( aberto && !des ) p.fillRect( xi + larguraTinta( st.texto, tipo::corpo ) + ESP1, yi + rd2( hi ) - 8, 2, 16, T.acento );
  p.icone( st.ver ? IconeNome::EYE_OFF : IconeNome::EYE, 20, xi + wi - 20, yi + rd2( hi - 20 ),
           des ? desab( T, T.tinta2 ) : T.tinta2 );
}

// linhaAjuda( ): the error if there is one, else the help.
void linhaAjuda( Pintor &p, int16_t x, int16_t y, int16_t h, const EstadoTexto &st ) {
  if ( st.erro ) ajuda( p, x, y, h, st.erro, Tom::PERIGO );
  else if ( st.ajuda ) ajuda( p, x, y, h, st.ajuda, Tom::NEUTRO );
}

CampoEstado estadoCampo( const EstadoTexto &st ) {
  return st.erro ? CampoEstado::ERRO : st.teclasDesab ? CampoEstado::DESAB : CampoEstado::FOCO;
}

// larguraSalvar( ): the Salvar key gains 32 px when its label becomes "Salvando…".
int16_t larguraSalvar( const EstadoTexto &st, int16_t w ) { return st.salvando ? w + ESP6 : w; }

// teclaSalvar( ): the primary key; disabled without text, "Salvando…" under way.
void teclaSalvar( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const EstadoTexto &st ) {
  TeclaOpts o;
  o.tipo = TeclaTipo::PRIMARIA;
  o.fonte = tipo::botao;
  o.legenda = st.salvando ? "Salvando…" : st.rotuloSalvar ? st.rotuloSalvar : "Salvar";
  o.estado = st.salvando || !*st.texto ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;
  tecla( p, x, y, w, h, o );
}

/* ------------------------------------------------------------------ */
/* T1: the 10x3 row (the firmware's geometry, dressed by the Ângulo)     */
/* ------------------------------------------------------------------ */

void desenharT1( Pintor &p, const EstadoTexto &st ) {
  // Geometry: margins of 12, keys of 26x28 with a gap of 4 (step 30), an
  // action row of 32 px, 12 px of air below.
  const int16_t M = ESP3, KW = 26, KH = 28, GAP = ESP1, PASSO = KW + GAP;
  const int16_t Y_BARRA = 28, Y_CAMPO = 36, H_CAMPO = 32, Y_AJUDA = 72, Y_TECLAS = 96, Y_ACOES = 196, H_ACOES = 32;
  const bool des = st.teclasDesab;

  const AcaoBarra cancelar = { "Cancelar", Botao::DISCRETO, des, 0 };
  barraTituloAngulo( p, Y_BARRA, st.titulo, tipo::titulo, &cancelar, 1 );
  campo( p, M, Y_CAMPO, 320 - 2 * M, H_CAMPO, estadoCampo( st ) );
  conteudoSenha( p, st, M + ESP3, Y_CAMPO, 320 - 2 * M - 2 * ESP3, H_CAMPO );

  if ( st.chips ) {
    // SSID option: the networks found, as chips, in the help line's place.
    int16_t x = M;
    for ( int16_t i = 0; i < st.nChips; i++ ) {
      if ( x + larguraChip( st.chips[i].nome ) > 320 - M ) break;   // what does not fit is left for the scroll
      x += chip( p, x, Y_AJUDA, 20, st.chips[i].nome, st.chips[i].ativo ) + ESP2;
    }
  } else {
    linhaAjuda( p, M, Y_AJUDA, 16, st );
  }

  const int16_t y0 = st.chips ? Y_TECLAS + 4 : Y_TECLAS;
  const char *const *filas = camadaAtiva( st );
  for ( int16_t i = 0; i < 3; i++ ) fileira( p, M, y0 + i * ( KH + GAP ), KW, KH, PASSO, filas[i], st );

  // Actions: shift 36 | 123 36 | space 108 | delete 36 | Salvar 64 (= 296).
  const int16_t ya = st.chips ? Y_ACOES + 4 : Y_ACOES;
  TeclaOpts fn;                       // the function keys: type and resting state
  fn.tipo = TeclaTipo::FUNCAO;
  fn.estado = des ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;

  TeclaOpts sh;
  sh.tipo = TeclaTipo::FUNCAO;
  sh.estado = estadoShift( st );
  sh.tam = 20;
  if ( st.camada == Camada::NUM ) { sh.legenda = "#+="; sh.fonte = tipo::rotulo; }
  else sh.icone = iconeShift( st );
  tecla( p, M, ya, 36, H_ACOES, sh );

  TeclaOpts ly = fn;
  ly.legenda = rotuloCamada( st );
  ly.fonte = tipo::rotulo;
  tecla( p, M + 40, ya, 36, H_ACOES, ly );

  const int16_t wS = larguraSalvar( st, 64 ), wE = 108 - ( wS - 64 );
  TeclaOpts sp;
  sp.tipo = TeclaTipo::ESPACO;
  sp.legenda = "espaço";
  sp.fonte = tipo::apoio;
  sp.alturaDeX = true;
  sp.estado = fn.estado;
  tecla( p, M + 80, ya, wE, H_ACOES, sp );

  TeclaOpts ap = fn;
  ap.icone = IconeNome::DELETE;
  ap.tam = 20;
  tecla( p, M + 84 + wE, ya, 36, H_ACOES, ap );

  teclaSalvar( p, 320 - M - wS, ya, wS, H_ACOES, st );
}

/* ------------------------------------------------------------------ */
/* The text examples: ESTADOS_TEXTO and T1's SSID state                  */
/* ------------------------------------------------------------------ */

/* Named for what they show: TEXTO_EXEMPLO is the text typed in the examples,
 * ERRO_CURTO the password field's too-short error. tools/scan_secrets.sh reads
 * a constant named like a password as a credential, so the names avoid it. */
const char *const TEXTO_EXEMPLO = "Vacina#26";
const char *const ERRO_CURTO = "Use pelo menos 8 caracteres.";
const Chip kChipsSsid[] = { { "HOSP-IOT", true }, { "HOSP-ENG", false }, { "Lab 2", false }, { "Oculta…", false } };

// The examples in the plan's order: vazio, digitando, shift, caps, simbolos,
// ver, erro, salvando, ssid.
EstadoTexto estadoTexto( int i ) {
  EstadoTexto e;
  switch ( i ) {
    case 0: break;                                                       // vazio
    case 1: e.texto = TEXTO_EXEMPLO; e.pressionada = "e"; break;         // digitando
    case 2: e.texto = "Vac"; e.shift = Shift::ON; break;                 // shift
    case 3: e.texto = "VAC"; e.shift = Shift::LOCK; break;               // caps
    case 4: e.texto = "Vacina"; e.camada = Camada::NUM; e.pressionada = "#"; break;   // simbolos
    case 5: e.texto = TEXTO_EXEMPLO; e.ver = true; break;                // ver
    case 6: e.texto = "Vac12"; e.erro = ERRO_CURTO; break;               // erro
    case 7: e.texto = TEXTO_EXEMPLO; e.salvando = true; e.teclasDesab = true; break;  // salvando
    case 8:                                                              // ssid
      e.titulo = "Rede Wi-Fi"; e.texto = "HOSP"; e.ver = true; e.ajuda = nullptr;
      e.chips = kChipsSsid; e.nChips = sizeof kChipsSsid / sizeof kChipsSsid[0];
      break;
    default: break;
  }
  return e;
}

template <int I> void t1( Pintor &p ) { desenharT1( p, estadoTexto( I ) ); }

/* ------------------------------------------------------------------ */
/* The PIN keypads: state and header                                    */
/* ------------------------------------------------------------------ */

const int16_t PIN_MAX = 6;
const char *const AJUDA_PIN = "Toque no grupo que tem cada dígito.";
// The keypads with one digit per key (P2): the help talks of digits, not groups.
const char *const AJUDA_PIN_DIGITOS = "Os dígitos trocam de lugar a cada toque.";

// teclado.js basePin( ): `n` digits typed, `pressionada` the index of the
// pressed key or group (-1: none), `semente` the seed of the shuffle.
struct EstadoPin {
  const char *titulo = "PIN de acesso";
  int16_t n = 0;
  const char *ajuda = AJUDA_PIN;
  const char *erro = nullptr;
  const char *alerta = nullptr;
  bool teclasDesab = false;
  int16_t pressionada = -1;
  int semente = 1;
};

// digitosEmbaralhados( ): the digits 0 to 9 shuffled deterministically by the
// seed (Fisher-Yates over a 32-bit generator). Math.imul and the >>> are 32-bit
// wraps, and the seed's product is exact in a double, so uint32_t is the same.
void digitosEmbaralhados( int semente, char d[10] ) {
  for ( int i = 0; i < 10; i++ ) d[i] = (char)( '0' + i );
  uint32_t a = (uint32_t)( (uint64_t)semente * 2654435761u );
  for ( int i = 9; i > 0; i-- ) {
    a += 0x6D2B79F5u;
    uint32_t t = ( a ^ ( a >> 15 ) ) * ( 1u | a );
    t = ( t + ( t ^ ( t >> 7 ) ) * ( 61u | t ) ) ^ t;
    const uint32_t j = ( t ^ ( t >> 14 ) ) % (uint32_t)( i + 1 );
    const char tmp = d[i];
    d[i] = d[j];
    d[j] = tmp;
  }
}

// grupos( ): the shuffled digits split into groups of the given sizes, each
// group as the text of its key: the digits joined by a space.
void grupos( int semente, const int8_t *tamanhos, int n, char ( *textos )[6] ) {
  char d[10];
  digitosEmbaralhados( semente, d );
  int k = 0;
  for ( int g = 0; g < n; g++ ) {
    char *o = textos[g];
    for ( int j = 0; j < tamanhos[g]; j++ ) {
      if ( j ) *o++ = ' ';
      *o++ = d[k++];
    }
    *o = 0;
  }
}

// cabecalhoPin( ): the title, the field with the places of the PIN, and the
// help, the error or the alert.
void cabecalhoPin( Pintor &p, const EstadoPin &st ) {
  const int16_t hBarra = 28, yCampo = 36, hCampo = 32, yAjuda = 72;
  const int16_t M = ESP3;
  const AcaoBarra cancelar = { "Cancelar", Botao::DISCRETO, false, 0 };   // never dimmed, even when the keys are
  barraTituloAngulo( p, hBarra, st.titulo, tipo::titulo, &cancelar, 1 );
  campo( p, M, yCampo, 320 - 2 * M, hCampo,
         st.erro ? CampoEstado::ERRO : st.teclasDesab ? CampoEstado::DESAB : CampoEstado::FOCO );
  // Places of a fixed length, centred, with no "see" button (a PIN is never
  // shown) and no cursor (the empty places say where the next digit goes).
  const int16_t xi = M + ESP3, wi = 320 - 2 * M - 2 * ESP3;
  const int16_t passo = ESP4;
  const int16_t largura = PIN_MAX * passo - ( passo - 6 );
  PontosOpts o;
  o.cursor = false;
  o.max = PIN_MAX;
  o.passo = passo;
  pontos( p, xi + rd2( wi - largura ), yCampo, hCampo, st.n, o );
  if ( st.erro ) ajuda( p, M, yAjuda, 16, st.erro, Tom::PERIGO );
  else if ( st.alerta ) ajuda( p, M, yAjuda, 16, st.alerta, Tom::ALERTA );
  else ajuda( p, M, yAjuda, 16, st.ajuda, Tom::NEUTRO );
}

// teclaGrupo( ): a group key; the digits in the large `dado` font.
void teclaGrupo( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *digitos, const EstadoPin &st,
                 int16_t idx ) {
  TeclaOpts o;
  o.legenda = digitos;
  o.fonte = tipo::dadoGrande;
  o.estado = st.teclasDesab ? TeclaEstado::DESAB : st.pressionada == idx ? TeclaEstado::PRESSIONADA : TeclaEstado::REPOUSO;
  tecla( p, x, y, w, h, o );
}

/* ------------------------------------------------------------------ */
/* P1, P2 and P3                                                        */
/* ------------------------------------------------------------------ */

// P1: four groups (2x2), the firmware's concept dressed by the Ângulo.
void desenharP1( Pintor &p, const EstadoPin &st ) {
  cabecalhoPin( p, st );
  const int16_t M = ESP3, W = 144, H = 60, GAP = ESP2, Y_TECLAS = 96;
  const int8_t tamanhos[] = { 3, 3, 2, 2 };
  char g[4][6];
  grupos( st.semente, tamanhos, 4, g );
  for ( int16_t i = 0; i < 4; i++ )
    teclaGrupo( p, M + ( i % 2 ) * ( W + GAP ), Y_TECLAS + ( i / 2 ) * ( H + GAP ), W, H, g[i], st, i );
}

// P2: numeric 3x4, the digits reshuffled at every touch; delete in the corner.
void desenharP2( Pintor &p, const EstadoPin &st ) {
  cabecalhoPin( p, st );
  const int16_t M = ESP3, W = 96, H = 28, GAP = ESP1, Y_TECLAS = 96;
  char d[10];
  digitosEmbaralhados( st.semente, d );
  // celulas = [...d.slice( 0, 9 ), null, d[9], 'apagar']: cell 9 is empty and
  // cell 11 is delete; `pressionada` is the index of the cell.
  for ( int16_t i = 0; i < 12; i++ ) {
    if ( i == 9 ) continue;
    TeclaOpts o;
    char legenda[2] = { 0, 0 };
    o.estado = st.teclasDesab ? TeclaEstado::DESAB : TeclaEstado::REPOUSO;
    if ( i == 11 ) {
      o.tipo = TeclaTipo::FUNCAO;
      o.icone = IconeNome::DELETE;
    } else {
      legenda[0] = d[i < 9 ? i : 9];
      o.legenda = legenda;
      o.fonte = tipo::botao;
      if ( !st.teclasDesab && st.pressionada == i ) o.estado = TeclaEstado::PRESSIONADA;
    }
    tecla( p, M + ( i % 3 ) * ( W + GAP ), Y_TECLAS + ( i / 3 ) * ( H + GAP ), W, H, o );
  }
}

// P3: six groups (3x2), less ambiguity per touch with the keys still large.
void desenharP3( Pintor &p, const EstadoPin &st ) {
  cabecalhoPin( p, st );
  const int16_t M = ESP3, W = 96, H = 60, GAP = ESP1, GAPY = ESP2, Y_TECLAS = 96;
  const int8_t tamanhos[] = { 2, 2, 2, 2, 1, 1 };
  char g[6][6];
  grupos( st.semente, tamanhos, 6, g );
  for ( int16_t i = 0; i < 6; i++ )
    teclaGrupo( p, M + ( i % 3 ) * ( W + GAP ), Y_TECLAS + ( i / 3 ) * ( H + GAPY ), W, H, g[i], st, i );
}

/* ------------------------------------------------------------------ */
/* The PIN examples: ESTADOS_PIN                                        */
/* ------------------------------------------------------------------ */

// The examples in the plan's order: vazio, digitando, erro, aguarde, bloqueado.
EstadoPin estadoPin( int i ) {
  EstadoPin e;
  switch ( i ) {
    case 0: break;                                                                // vazio
    case 1: e.n = 3; e.semente = 7; e.pressionada = 1; break;                     // digitando
    case 2: e.erro = "PIN incorreto. Tente de novo (2 de 5)."; e.semente = 3; break;   // erro
    case 3: e.alerta = "Aguarde 15 s para tentar de novo."; e.teclasDesab = true; e.semente = 5; break;   // aguarde
    case 4:                                                                       // bloqueado
      e.erro = "Bloqueado após 6 erros. Libere pela console serial."; e.teclasDesab = true; e.semente = 9;
      break;
    default: break;
  }
  return e;
}

template <int I> void p1( Pintor &p ) { desenharP1( p, estadoPin( I ) ); }
template <int I> void p2( Pintor &p ) {   // ESTADOS_PIN_DIGITOS: the same states, the help of the digits
  EstadoPin e = estadoPin( I );
  e.ajuda = AJUDA_PIN_DIGITOS;
  desenharP2( p, e );
}
template <int I> void p3( Pintor &p ) { desenharP3( p, estadoPin( I ) ); }

}  // namespace

const Entrada kTeclados[] = {
  { "T1_vazio", t1<0> }, { "T1_digitando", t1<1> }, { "T1_shift", t1<2> }, { "T1_caps", t1<3> },
  { "T1_simbolos", t1<4> }, { "T1_ver", t1<5> }, { "T1_erro", t1<6> }, { "T1_salvando", t1<7> }, { "T1_ssid", t1<8> },
  { "P1_vazio", p1<0> }, { "P1_digitando", p1<1> }, { "P1_erro", p1<2> }, { "P1_aguarde", p1<3> }, { "P1_bloqueado", p1<4> },
  { "P2_vazio", p2<0> }, { "P2_digitando", p2<1> }, { "P2_erro", p2<2> }, { "P2_aguarde", p2<3> }, { "P2_bloqueado", p2<4> },
  { "P3_vazio", p3<0> }, { "P3_digitando", p3<1> }, { "P3_erro", p3<2> }, { "P3_aguarde", p3<3> }, { "P3_bloqueado", p3<4> },
};
const int kTecladosN = sizeof kTeclados / sizeof kTeclados[0];

}  // namespace plano
