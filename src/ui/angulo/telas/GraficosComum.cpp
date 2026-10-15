// What the graph group's screens share. See GraficosComum.h.
#include "GraficosComum.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ua {
namespace tela {
namespace graficos {

const char *rotuloPeriodo( int periodo ) {
  static const char *const kRotulos[N_PERIODOS] = { "1 h", "6 h", "12 h", "24 h", "7 d" };
  return kRotulos[periodo < 0 ? 0 : periodo >= N_PERIODOS ? N_PERIODOS - 1 : periodo];
}

void tituloComPeriodo( char *dst, int cap, const char *nome, int periodo, const Acao *acoes, int n, bool periodoPodeSair ) {
  // The title may run up to ESP2 short of the first action. barraTitulo( ) keeps
  // the larger font only when the title ends 2 * ESP2 short of it and otherwise
  // draws the smaller, the font this measures in: whichever the bar picks, the
  // title clears the actions, and a name that fit the larger one is never cut.
  int16_t maxW = 320 - 2 * PAD - ESP2;
  if ( n > 0 ) {
    Caixa16 caixas[4];
    caixasDasAcoes( acoes, n > 4 ? 4 : n, caixas );
    maxW = (int16_t)( caixas[0].x - PAD - ESP2 );
  }
  char sufixo[16];
  snprintf( sufixo, sizeof sufixo, " · %s", rotuloPeriodo( periodo ) );
  char curto[40];
  // A request the glue could not name (a loading screen with no slot) is just
  // "Sensor": an accented word like "Histórico" puts the acute 1 px above the
  // safe area in the larger title font (the harness caught it).
  copiar( curto, nome && nome[0] ? nome : "Sensor", sizeof curto );
  snprintf( dst, (size_t)cap, "%s%s", curto, sufixo );
  if ( larguraTinta( dst, tipo::subtitulo ) <= maxW ) return;
  if ( periodoPodeSair ) sufixo[0] = 0;
  int len = (int)strlen( curto );
  bool cortou = false;
  for ( ;; ) {
    snprintf( dst, (size_t)cap, "%s%s%s", curto, cortou ? "…" : "", sufixo );
    if ( len == 0 || larguraTinta( dst, tipo::subtitulo ) <= maxW ) return;
    // Drop the last character (a UTF-8 sequence), then the spaces it leaves
    // at the end, so the ellipsis follows a letter.
    do len--; while ( len > 0 && ( (uint8_t)curto[len] & 0xC0 ) == 0x80 );
    while ( len > 0 && curto[len - 1] == ' ' ) len--;
    curto[len] = 0;
    cortou = true;
  }
}

void caixasDasAcoes( const Acao *acoes, int n, Caixa16 *out ) {
  int16_t x = 320 - PAD;
  for ( int i = n - 1; i >= 0; i-- ) {
    const int16_t w = larguraAcao( acoes[i] );
    x -= w;
    out[i] = Caixa16{ x, Y_ACAO, w, H_ACAO };
    x -= ESP2;
  }
}

#ifdef UA_HOST
void conferirCaixas( const char *tela, const Caixa16 *kit, const Caixa16 *meu, int n ) {
  for ( int i = 0; i < n; i++ ) {
    if ( kit[i].x == meu[i].x && kit[i].y == meu[i].y && kit[i].w == meu[i].w && kit[i].h == meu[i].h ) continue;
    fprintf( stderr, "%s: action %d is at %d,%d %dx%d in the kit's title bar, and its target was made for %d,%d %dx%d\n", tela, i, kit[i].x,
             kit[i].y, kit[i].w, kit[i].h, meu[i].x, meu[i].y, meu[i].w, meu[i].h );
    abort( );
  }
}
#endif

Caixa16 alvoDaAcao( const Caixa16 &c ) {
  const int16_t x = (int16_t)( c.x - ESP1 );
  // The action at the right margin goes on to the edge of the glass, where a resistive panel reads worst.
  const int16_t x1 = c.x + c.w >= 320 - PAD ? (int16_t)320 : (int16_t)( c.x + c.w + ESP1 );
  return Caixa16{ x, 0, (int16_t)( x1 - x ), (int16_t)( H_BARRA + ESP1 ) };
}

char *horaMin( const Instante &t, char *buf, int cap ) {
  snprintf( buf, (size_t)cap, "%02u:%02u", (unsigned)t.hora, (unsigned)t.minuto );
  return buf;
}

char *diaMes( const Instante &t, char *buf, int cap ) {
  snprintf( buf, (size_t)cap, "%02u/%02u", (unsigned)t.dia, (unsigned)t.mes );
  return buf;
}

char *diaMesHoraMin( const Instante &t, char *buf, int cap ) {
  snprintf( buf, (size_t)cap, "%02u/%02u %02u:%02u", (unsigned)t.dia, (unsigned)t.mes, (unsigned)t.hora,
            (unsigned)t.minuto );
  return buf;
}

namespace {

// The kit's pager arrow, drawn over the live one: the same box, the same radius,
// so the live arrow underneath is covered whole.
void setaDesabilitada( Pintor &p, const Caixa16 &c, IconeNome seta ) {
  const Tema &T = p.t( );
  botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, true );
  p.icone( seta, ICONE_M, (int16_t)( c.x + rd2( c.w - ICONE_M ) ), (int16_t)( c.y + rd2( c.h - ICONE_M ) ), desab( T, T.tinta ) );
}

}  // namespace

void paginadorComEstados( Pintor &p, int16_t pagina, int16_t total, const char *nome ) {
  const Tema &T = p.t( );
  paginador( p, pagina, total );
  const Caixa16 esq = paginadorEsquerda( ), dir = paginadorDireita( );
  if ( pagina <= 1 ) setaDesabilitada( p, esq, IconeNome::CHEVRON_LEFT );
  if ( pagina >= total ) setaDesabilitada( p, dir, IconeNome::CHEVRON_RIGHT );
  if ( nome && *nome ) {
    // Where the kit puts "n de m" (right of the second arrow, ESP3 away), and
    // the page's name ESP2 after it.
    char contagem[24];
    snprintf( contagem, sizeof contagem, "%d de %d", (int)pagina, (int)total );
    const int16_t x = (int16_t)( dir.x + dir.w + ESP3 + larguraTinta( contagem, tipo::apoio ) + ESP2 );
    char legenda[40];
    snprintf( legenda, sizeof legenda, "· %s", nome );
    textoCentrado( p, legenda, x, esq.y, esq.h, tipo::rotulo, T.tinta, 'l' );
  }
}

void linhaOcupado( Pintor &p ) {
  p.fillRect( 0, H_BARRA, 320, H_OCUPADO, p.t( ).acento );
}

void semDados( Pintor &p, const Caixa16 &area, const char *dica ) {
  const Tema &T = p.t( );
  // A line of body text, ESP1 of air, a line of support text, as one block.
  const int16_t alto = H_CORPO + ESP1 + H_APOIO;
  const int16_t y = (int16_t)( area.y + rd2( area.h - alto ) );
  const int16_t cx = (int16_t)( area.x + rd2( area.w ) );
  textoCentrado( p, "Sem dados neste período.", cx, y, H_CORPO, tipo::corpo, T.tinta, 'c' );
  textoCentrado( p, dica, cx, (int16_t)( y + H_CORPO + ESP1 ), H_APOIO, tipo::apoio, T.tinta2, 'c' );
}

}  // namespace graficos
}  // namespace tela
}  // namespace ua
