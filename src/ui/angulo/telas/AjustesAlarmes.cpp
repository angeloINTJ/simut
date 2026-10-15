// The limits and maintenance screens of one sensor. See AjustesAlarmes.h.
#include "AjustesAlarmes.h"

#include <stdio.h>

#include "AjustesComum.h"

namespace ua {
namespace tela {

namespace {

using namespace ajustes;

/* ------------------------------------------------------------------ */
/* Limites                                                              */
/* ------------------------------------------------------------------ */

// The pager row of a sensor that reads several quantities: the two steppers end
// at y 156 and the footer starts at 196, so a 28 px strip fits with 8 px above it
// and 4 below. The arrows keep the kit's pager x and width.
constexpr int16_t Y_PAG = Y0 + 2 * ( H_CARTAO + ESP2 ), H_PAG = 28;

// An arrow's touch target: the drawn 28 px is small for a finger, and the strip is free for 4 px above it (half of
// the 8 between it and the steppers) and 4 below it (down to the footer), so the target takes both.
Caixa16 alvoPagina( const Caixa16 &c ) { return Caixa16{ c.x, (int16_t)( c.y - ESP1 ), c.w, (int16_t)( c.h + 2 * ESP1 ) }; }

// Each page names its quantity in the card's label, so a page says what it
// edits even where the pager is not drawn (a sensor with one quantity).
const char *const kRotulos[3][2] = {
  { "Temperatura mínima", "Temperatura máxima" },
  { "Umidade mínima", "Umidade máxima" },
  { "Pressão mínima", "Pressão máxima" },
};
const char *const kUnidades[3] = { "°C", "%", "hPa" };

struct LayoutLimites {
  CaixasStepper cartao[2];
  bool menosAtivo[2], maisAtivo[2];
  bool temPag, anteriorAtivo, proximaAtivo;
  Caixa16 pagAnterior, pagProxima;
  CaixasRodape rodape;
};

LayoutLimites layoutLimites( const VmLimites &vm ) {
  LayoutLimites l{ };
  l.cartao[0] = caixasStepper( Y0 );
  l.cartao[1] = caixasStepper( Y0 + H_CARTAO + ESP2 );
  // Both values stop at the channel's range, and the minimum has to stay under
  // the maximum: raising the minimum pushes the maximum up (as the old editor
  // did) unless the maximum is already at the top; lowering the maximum, the
  // reverse. One tenth is the step.
  l.menosAtivo[0] = vm.minimo > vm.piso;
  l.maisAtivo[0] = vm.minimo + 1 < vm.teto;
  l.menosAtivo[1] = vm.maximo - 1 > vm.piso;
  l.maisAtivo[1] = vm.maximo < vm.teto;
  l.temPag = vm.total > 1;
  l.anteriorAtivo = vm.pagina > 0;
  l.proximaAtivo = vm.pagina + 1 < vm.total;
  const Caixa16 e = paginadorEsquerda( ), d = paginadorDireita( );
  l.pagAnterior = Caixa16{ e.x, Y_PAG, e.w, H_PAG };
  l.pagProxima = Caixa16{ d.x, Y_PAG, d.w, H_PAG };
  l.rodape = caixasRodape( "Salvar limites", "Cancelar" );
  return l;
}

/* ------------------------------------------------------------------ */
/* Manutenção                                                           */
/* ------------------------------------------------------------------ */

struct LayoutManutencao {
  CaixasStepper cartao[2];          // closed: hours and minutes
  Caixa16 restante;                 // open: the card with the time left
  bool menosAtivo[2], maisAtivo[2];
  bool primariaAtiva;
  CaixasRodape rodape;
  int16_t yNota;                    // the first line of the notes under the content
};

// The open state's card: the time's baseline stands 80 px below its top (the label
// above it, the big face's 42 px of digits), and 16 px of card below the baseline.
constexpr int16_t Y_BASE_RESTANTE = ESP7 + ESP6, H_RESTANTE = Y_BASE_RESTANTE + ESP4;

LayoutManutencao layoutManutencao( const VmManutencao &vm ) {
  LayoutManutencao l{ };
  if ( vm.aberta ) {
    l.restante = Caixa16{ ESP2, Y0, L_TELA - 2 * ESP2, H_RESTANTE };
    l.yNota = Y0 + H_RESTANTE + ESP2;
    // Ending a window needs the same permission as opening one.
    l.primariaAtiva = vm.permitido;
    l.rodape = caixasRodape( "Encerrar manutenção", nullptr );
  } else {
    l.cartao[0] = caixasStepper( Y0 );
    l.cartao[1] = caixasStepper( Y0 + H_CARTAO + ESP2 );
    l.yNota = Y0 + H_CARTAO + ESP2 + H_CARTAO + ESP1;
    // Without the permission nothing can be adjusted to any effect. Hours stop
    // at their ends; minutes wrap (55 -> 0), so they never do.
    l.menosAtivo[0] = vm.permitido && vm.horas > 0;
    l.maisAtivo[0] = vm.permitido && vm.horas < vm.horasMax;
    l.menosAtivo[1] = l.maisAtivo[1] = vm.permitido;
    // Starting a window of zero minutes would do nothing (the old screen only beeped).
    l.primariaAtiva = vm.permitido && ( vm.horas > 0 || vm.minutos > 0 );
    l.rodape = caixasRodape( "Iniciar manutenção", "Cancelar" );
  }
  return l;
}

// "1 h 29 min" centred on cx: the digits in the big display face, the units in
// the button face, on one baseline. Under an hour it is only "29 min".
void tempoRestante( Pintor &p, int16_t cx, int16_t base, uint32_t minutos ) {
  const Tema &T = p.t( );
  const uint32_t h = minutos / 60, m = minutos % 60;
  char sh[12], sm[12];
  snprintf( sh, sizeof sh, "%lu", (unsigned long)h );
  // "1 h 05 min", but "5 min": the zero pads only beside an hour.
  if ( h ) snprintf( sm, sizeof sm, "%02lu", (unsigned long)m );
  else snprintf( sm, sizeof sm, "%lu", (unsigned long)m );
  auto largura = []( const char *digitos, const char *unidade ) {
    return (int16_t)( larguraTinta( digitos, Fonte::DISP32 ) + ESP1 + larguraTinta( unidade, tipo::botao ) );
  };
  const int16_t total = h ? (int16_t)( largura( sh, "h" ) + ESP4 + largura( sm, "min" ) ) : largura( sm, "min" );
  int16_t x = cx - rd2( total );
  if ( h ) {
    x = numero( p, x, base, sh, "h", Fonte::DISP32, T.tinta, 'l', tipo::botao, true, T.tinta2 );
    x += ESP4;
  }
  numero( p, x, base, sm, "min", Fonte::DISP32, T.tinta, 'l', tipo::botao, true, T.tinta2 );
}

// The notes under the content, in the quiet type, one row of PASSO_APOIO each, from
// the card's left edge: the longest, "Os alarmes ficam suspensos até o fim.", is 293 px
// of ink at 9 pt, under the 304 of the cards' width (2026-10-15: with "deste sensor"
// it was 394, and the title already names the sensor).
void nota( Pintor &p, int16_t y, const char *texto, Tom tom ) {
  ajuda( p, ESP2, y, ESP4, texto, tom );
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Limites                                                              */
/* ------------------------------------------------------------------ */

void desenharLimites( Pintor &p, const VmLimites &vm ) {
  const Tema &T = p.t( );
  const LayoutLimites l = layoutLimites( vm );
  char titulo[64];
  // The title is the sensor's name alone, as in the sensor's menu it comes from: with " · limites"
  // after it, "Ultracongelador" (15 characters, an ordinary length) was cut to "Ultracongelad… ·
  // limites" at the 9 pt floor (2026-10-15). The screen still says what it is: the cards read
  // "Temperatura mínima" and "Temperatura máxima", and the footer "Salvar limites".
  montarTitulo( titulo, sizeof titulo, "", vm.sensor, "" );
  barraTituloVoltar( p, titulo );
  const int g = vm.grandeza <= GLIM_PRESSAO ? (int)vm.grandeza : (int)GLIM_TEMPERATURA;
  for ( int r = 0; r < 2; r++ ) {
    char valor[16];
    n1( r ? vm.maximo : vm.minimo, valor );
    desenharStepper( p, l.cartao[r], kRotulos[g][r], valor, kUnidades[g], vm.foco == r, l.menosAtivo[r], l.maisAtivo[r] );
  }
  if ( l.temPag ) {
    botaoIcone( p, l.pagAnterior, IconeNome::CHEVRON_LEFT, l.anteriorAtivo );
    botaoIcone( p, l.pagProxima, IconeNome::CHEVRON_RIGHT, l.proximaAtivo );
    char n[24];
    snprintf( n, sizeof n, "%d de %d", (int)vm.pagina + 1, (int)vm.total );
    // The kit's pager puts "n de m" 12 px after its second arrow.
    textoCentrado( p, n, l.pagProxima.x + l.pagProxima.w + ESP3, Y_PAG, H_PAG, tipo::apoio, T.tinta2, 'l' );
  }
  desenharRodape( p, l.rodape, "Salvar limites", Botao::PRIMARIO, false, "Cancelar" );
}

int alvosLimites( const VmLimites &vm, Alvo *out ) {
  const LayoutLimites l = layoutLimites( vm );
  int n = addAlvo( out, 0, caixaVoltar( ), LIMITES_VOLTAR );
  for ( int r = 0; r < 2; r++ ) {
    n = addAlvoSe( out, n, l.menosAtivo[r], l.cartao[r].menos, LIMITES_MENOS, (int16_t)r );
    n = addAlvoSe( out, n, l.maisAtivo[r], l.cartao[r].mais, LIMITES_MAIS, (int16_t)r );
  }
  if ( l.temPag ) {
    n = addAlvoSe( out, n, l.anteriorAtivo, alvoPagina( l.pagAnterior ), LIMITES_PAG_ANTERIOR );
    n = addAlvoSe( out, n, l.proximaAtivo, alvoPagina( l.pagProxima ), LIMITES_PAG_PROXIMA );
  }
  n = addAlvo( out, n, l.rodape.secundaria, LIMITES_CANCELAR );
  return addAlvo( out, n, l.rodape.primaria, LIMITES_SALVAR );
}

/* ------------------------------------------------------------------ */
/* Manutenção                                                           */
/* ------------------------------------------------------------------ */

void desenharManutencao( Pintor &p, const VmManutencao &vm ) {
  const Tema &T = p.t( );
  const LayoutManutencao l = layoutManutencao( vm );
  char titulo[64];
  montarTitulo( titulo, sizeof titulo, "Manutenção · ", vm.sensor, "" );
  barraTituloVoltar( p, titulo );
  if ( vm.aberta ) {
    // The border says what the card is about: the alarms are off for this time.
    superficie( p, l.restante.x, l.restante.y, l.restante.w, l.restante.h, false, 0, true, T.alerta );
    textoCentrado( p, "Tempo restante", l.restante.x + PAD, l.restante.y + ESP2, ESP4, tipo::rotulo, T.tinta2, 'l' );
    tempoRestante( p, X_MEIO, l.restante.y + Y_BASE_RESTANTE, vm.restanteMin );
  } else {
    char h[12], m[12];
    snprintf( h, sizeof h, "%u", (unsigned)vm.horas );
    snprintf( m, sizeof m, "%u", (unsigned)vm.minutos );
    desenharStepper( p, l.cartao[0], "Horas", h, "h", vm.foco == 0, l.menosAtivo[0], l.maisAtivo[0] );
    desenharStepper( p, l.cartao[1], "Minutos", m, "min", vm.foco == 1, l.menosAtivo[1], l.maisAtivo[1] );
  }
  // The alarms' note explains the state of a window that is open, and what starting
  // one does. Closed and without the permission nothing can start, so the account's
  // note has the row to itself: a second row, 18 px under the first (which starts at
  // y 160), would have its ink end at y 197, 1 px into the footer's buttons (y 196).
  if ( vm.permitido || vm.aberta ) nota( p, l.yNota, "Os alarmes ficam suspensos até o fim.", Tom::NEUTRO );
  if ( !vm.permitido )
    nota( p, vm.aberta ? l.yNota + PASSO_APOIO : l.yNota,
          vm.aberta ? "Sua conta não pode encerrar." : "Sua conta não pode abrir manutenção.", Tom::ALERTA );
  desenharRodape( p, l.rodape, vm.aberta ? "Encerrar manutenção" : "Iniciar manutenção",
                  vm.aberta ? Botao::DESTRUTIVO : Botao::PRIMARIO, !l.primariaAtiva, vm.aberta ? nullptr : "Cancelar" );
}

int alvosManutencao( const VmManutencao &vm, Alvo *out ) {
  const LayoutManutencao l = layoutManutencao( vm );
  int n = addAlvo( out, 0, caixaVoltar( ), MANUT_VOLTAR );
  if ( !vm.aberta ) {
    for ( int r = 0; r < 2; r++ ) {
      n = addAlvoSe( out, n, l.menosAtivo[r], l.cartao[r].menos, MANUT_MENOS, (int16_t)r );
      n = addAlvoSe( out, n, l.maisAtivo[r], l.cartao[r].mais, MANUT_MAIS, (int16_t)r );
    }
    n = addAlvo( out, n, l.rodape.secundaria, MANUT_CANCELAR );
  }
  return addAlvoSe( out, n, l.primariaAtiva, l.rodape.primaria, vm.aberta ? MANUT_ENCERRAR : MANUT_INICIAR );
}

}  // namespace tela
}  // namespace ua
