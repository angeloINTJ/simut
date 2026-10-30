// What the ajustes screens share. See AjustesComum.h.
#include "AjustesComum.h"

#include <stdio.h>
#include <string.h>

namespace ua {
namespace tela {
namespace ajustes {

namespace {

// The most ink the title may have: from the title's inset (PAD) to ESP2 short
// of the "Voltar" button (the kit's acaoVoltar( ), the one barraTituloVoltar( ) draws).
int16_t larguraMaxTitulo( ) { return L_TELA - PAD - larguraAcao( acaoVoltar( ) ) - ESP2 - PAD; }

}  // namespace

Caixa16 caixaVoltar( ) {
  const int16_t x = (int16_t)( L_TELA - PAD - larguraAcao( acaoVoltar( ) ) - ESP1 );
  return Caixa16{ x, 0, (int16_t)( L_TELA - x ), Y0 };
}

void montarTitulo( char *dst, int cap, const char *antes, const char *nome, const char *depois ) {
  const int total = (int)strlen( nome );
  int n = total;
  for ( ;; ) {
    // A cut leaves no space before the ellipsis ("Sala de coleta …").
    while ( n > 0 && nome[n - 1] == ' ' ) n--;
    snprintf( dst, (size_t)cap, "%s%.*s%s%s", antes, n, nome, n < total ? "\xE2\x80\xA6" : "", depois );
    if ( n == 0 || larguraTinta( dst, tipo::subtitulo ) <= larguraMaxTitulo( ) ) return;
    // Back off to the start of the previous UTF-8 sequence.
    do n--; while ( n > 0 && ( (uint8_t)nome[n] & 0xC0 ) == 0x80 );
  }
}

CaixasRodape caixasRodape( const char *primaria, const char *secundaria, int16_t y ) {
  CaixasRodape c{ };
  // rodapeAcoes( )'s arithmetic: the primary is its label and 16 px each side, the
  // discreet action its label and 8 px each side, both 8 px from the edge and each other.
  const int16_t wP = larguraTinta( primaria, tipo::botao ) + 2 * ESP4;
  c.primaria = Caixa16{ (int16_t)( L_TELA - ESP2 - wP ), y, wP, H_RODAPE };
  c.rotuloPrimaria = primaria;
  if ( secundaria ) {
    const int16_t wS = larguraTinta( secundaria, tipo::botao ) + 2 * ESP2;
    c.secundaria = Caixa16{ (int16_t)( L_TELA - ESP2 - wP - ESP2 - wS ), y, wS, H_RODAPE };
    c.rotuloSecundaria = secundaria;
  }
  return c;
}

void desenharRodape( Pintor &p, const CaixasRodape &c, Botao tipoPrimaria, bool primariaDesabilitada ) {
  // rodapeAcoes( ) cannot draw its primary disabled, which the screens that
  // need a permission do; the look is otherwise the same call it makes. The labels
  // are the ones the boxes were measured from (CaixasRodape).
  botao( p, c.primaria.x, c.primaria.y, c.primaria.w, c.primaria.h, c.rotuloPrimaria, tipoPrimaria, primariaDesabilitada );
  if ( c.rotuloSecundaria )
    botao( p, c.secundaria.x, c.secundaria.y, c.secundaria.w, c.secundaria.h, c.rotuloSecundaria, Botao::DISCRETO );
}

void botaoIcone( Pintor &p, const Caixa16 &c, IconeNome ic, bool ativo ) {
  const Tema &T = p.t( );
  botao( p, c.x, c.y, c.w, c.h, "", Botao::SECUNDARIO, !ativo );
  p.icone( ic, L_ICONE, c.x + rd2( c.w - L_ICONE ), c.y + rd2( c.h - L_ICONE ), ativo ? T.tinta : desab( T, T.tinta ) );
}

Caixa16 segmento( int16_t x, int16_t y, int16_t w, int16_t h, int n, int i ) {
  const int16_t livre = w - ( n - 1 ) * ESP1;
  const int16_t sw = livre / n, resto = livre % n;
  // The first `resto` segments are a pixel wider, and each of the others starts that
  // many pixels after where the plain split would put it.
  return Caixa16{ (int16_t)( x + i * ( sw + ESP1 ) + ( i < resto ? i : resto ) ), y, (int16_t)( sw + ( i < resto ? 1 : 0 ) ), h };
}

void segmentadoLeve( Pintor &p, int16_t x, int16_t y, int16_t w, int16_t h, const char *const *opcoes, int16_t n,
                     int16_t ativa ) {
  const Tema &T = p.t( );
  for ( int16_t i = 0; i < n; i++ ) {
    const Caixa16 s = segmento( x, y, w, h, n, i );
    const bool on = i == ativa;
    UA_CONTROLE( s.x, s.y, s.w, s.h, true, "segmento" );
    p.fillRoundRect( s.x, s.y, s.w, s.h, RAIO_CONTROLE, on ? T.superficie : T.superficie2 );
    p.roundRect( s.x, s.y, s.w, s.h, RAIO_CONTROLE, on ? T.acento : T.linha );
    textoCentrado( p, opcoes[i], s.x + rd2( s.w ), s.y, s.h, tipo::apoio, on ? T.acento : T.tinta2, 'c' );
  }
}

CaixasStepper caixasStepper( int16_t y ) {
  const int16_t x = ESP2, w = L_TELA - 2 * ESP2;
  CaixasStepper c;
  c.cartao = Caixa16{ x, y, w, H_CARTAO };
  c.menos = Caixa16{ (int16_t)( x + w - PAD - L_BOTAO - ESP2 - L_BOTAO ), (int16_t)( y + ESP2 ), L_BOTAO, L_BOTAO };
  c.mais = Caixa16{ (int16_t)( x + w - PAD - L_BOTAO ), (int16_t)( y + ESP2 ), L_BOTAO, L_BOTAO };
  return c;
}

void desenharStepper( Pintor &p, const CaixasStepper &c, const char *rotulo, const char *valor, const char *unidade,
                      bool foco, bool menosAtivo, bool maisAtivo ) {
  const Tema &T = p.t( );
  superficie( p, c.cartao.x, c.cartao.y, c.cartao.w, c.cartao.h, false, 0, foco, T.acento );
  textoCentrado( p, rotulo, c.cartao.x + PAD, c.cartao.y + ESP2, ESP4, tipo::rotulo, T.tinta2, 'l' );
  // The plan sets the value's baseline 48 px below the card's top; the unit
  // follows it on the same baseline, in the quieter colour.
  numero( p, c.cartao.x + PAD, c.cartao.y + ESP7, valor, unidade, tipo::titulo, T.tinta, 'l', tipo::botao, true, T.tinta2 );
  botaoIcone( p, c.menos, IconeNome::MINUS, menosAtivo );
  botaoIcone( p, c.mais, IconeNome::PLUS, maisAtivo );
}

}  // namespace ajustes
}  // namespace tela
}  // namespace ua
