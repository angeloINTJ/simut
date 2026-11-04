// The sound settings and the choice of a melody. See AjustesSons.h.
#include "AjustesSons.h"

#include <stdio.h>

#include "AjustesComum.h"
#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

using namespace ajustes;

// What the sounds list says: the label of each row, which is also the name of the sound in the melody screen's title. Looked up where it is drawn
// (UA_TXT( ) cannot initialise a table); the switch is on the enum, so that a row without a text does not build (-Wswitch is an error).
const char *rotuloDoSom( int som ) {
  switch ( static_cast<LinhaSom>( som ) ) {
    case SOM_TOQUE: return UA_TXT( "Touch click" );
    case SOM_CONFIRMACAO: return UA_TXT( "Confirmation" );
    case SOM_ERRO: return UA_TXT( "Error" );
    case SOM_ALARME: return UA_TXT( "Alarm" );
    case SOM_ATENCAO: return UA_TXT( "Attention" );
    case SOM_WEB: return UA_TXT( "Web access" );
    case SOM_MUDO: return UA_TXT( "Mute all" );
    case SOM_N: break;
  }
  return "";
}

// The six melodies of each sound that has them, in the order of the old selector (DisplayManager_Settings.cpp, drawMelodySelect): the English is that
// selector's own names, without their numbers; the pack gives the others. One lookup per name drawn, where it is drawn.
const char *nomeMelodia( int som, int melodia ) {
  if ( som < 0 || som >= SOM_COM_MELODIA || melodia < 0 || melodia >= MELODIAS_POR_SOM ) return "";
  switch ( static_cast<LinhaSom>( som ) ) {
    case SOM_TOQUE:
      switch ( melodia ) {
        case 0: return UA_TXT( "Click" );
        case 1: return UA_TXT( "Bubble" );
        case 2: return UA_TXT( "Tick" );
        case 3: return UA_TXT( "Snap" );
        case 4: return UA_TXT( "Drop" );
        case 5: return UA_TXT( "Chirp" );
      }
      break;
    case SOM_CONFIRMACAO:
      switch ( melodia ) {
        case 0: return UA_TXT( "Ascending" );
        case 1: return UA_TXT( "Fanfare" );
        case 2: return UA_TXT( "Chime" );
        case 3: return UA_TXT( "Triumph" );
        case 4: return UA_TXT( "Sparkle" );
        case 5: return UA_TXT( "Resolve" );
      }
      break;
    case SOM_ERRO:
      switch ( melodia ) {
        case 0: return UA_TXT( "Descending" );
        case 1: return UA_TXT( "Buzz" );
        case 2: return UA_TXT( "Low" );
        case 3: return UA_TXT( "Harsh" );
        case 4: return UA_TXT( "Decline" );
        case 5: return UA_TXT( "Blip" );
      }
      break;
    case SOM_ALARME:
      switch ( melodia ) {
        case 0: return UA_TXT( "Dual beep" );
        case 1: return UA_TXT( "Siren" );
        case 2: return UA_TXT( "Rapid" );
        case 3: return UA_TXT( "Pulse" );
        case 4: return UA_TXT( "Escalate" );
        case 5: return UA_TXT( "Staccato" );
      }
      break;
    case SOM_ATENCAO:
      switch ( melodia ) {
        case 0: return UA_TXT( "Notify" );
        case 1: return UA_TXT( "Bell" );
        case 2: return UA_TXT( "Pulse" );
        case 3: return UA_TXT( "Low chime" );
        case 4: return UA_TXT( "Rise" );
        case 5: return UA_TXT( "Soft ding" );
      }
      break;
    case SOM_WEB: case SOM_MUDO: case SOM_N: break;
  }
  return "";
}

/* ------------------------------------------------------------------ */
/* Sons                                                                 */
/* ------------------------------------------------------------------ */

constexpr int16_t H_LINHA = 40;
constexpr int16_t X_CARTAO = ESP2, W_CARTAO = L_TELA - 2 * ESP2;
// The switch's left edge: 12 px from the card's right edge, on or off, so
// flipping it does not move it (the kit's list would shift it for a chevron).
constexpr int16_t X_CHAVE = X_CARTAO + W_CARTAO - PAD - L_CHAVE;

// A volume's card: the label row, then the button row with 4 px above and
// below it.
constexpr int16_t H_VOLUME = ESP2 + ESP4 + ESP1 + L_BOTAO + ESP1;
constexpr int16_t RAIO_BOLA = 8;       // the plan's slider knob
constexpr int VOLUME_MAX = 100;        // percent

struct LayoutSons {
  uint8_t pagina;
  uint8_t primeira, n;                 // the rows on the page: LinhaSom from `primeira`, `n` of them
  Caixa16 cartao;                      // the rows' card
  Caixa16 linha[4];
  Caixa16 deslizante[2], menos[2], mais[2];
  bool menosAtivo[2], maisAtivo[2];
  bool anteriorAtivo, proximaAtivo;
  Acao salvar;
};

LayoutSons layoutSons( const VmSons &vm ) {
  LayoutSons l{ };
  l.pagina = vm.pagina < SONS_PAGINAS ? vm.pagina : 0;
  l.primeira = l.pagina == 0 ? SOM_TOQUE : SOM_ATENCAO;
  l.n = l.pagina == 0 ? SOM_ATENCAO - SOM_TOQUE : l.pagina == 1 ? SOM_N - SOM_ATENCAO : 0;
  l.cartao = Caixa16{ X_CARTAO, Y0, W_CARTAO, (int16_t)( l.n * H_LINHA ) };
  for ( int i = 0; i < l.n; i++ ) l.linha[i] = Caixa16{ X_CARTAO, (int16_t)( Y0 + i * H_LINHA ), W_CARTAO, H_LINHA };
  if ( l.pagina == SONS_PAGINAS - 1 ) {
    for ( int i = 0; i < 2; i++ ) {
      const int16_t y = (int16_t)( Y0 + i * ( H_VOLUME + ESP2 ) );
      const int16_t yb = (int16_t)( y + ESP2 + ESP4 + ESP1 );
      l.deslizante[i] = Caixa16{ X_CARTAO, y, W_CARTAO, H_VOLUME };
      l.menos[i] = Caixa16{ (int16_t)( X_CARTAO + PAD ), yb, L_BOTAO, L_BOTAO };
      l.mais[i] = Caixa16{ (int16_t)( X_CARTAO + W_CARTAO - PAD - L_BOTAO ), yb, L_BOTAO, L_BOTAO };
      const int v = i == 0 ? vm.volumeSistema : vm.volumeAlarme;
      l.menosAtivo[i] = v > 0;
      l.maisAtivo[i] = v < VOLUME_MAX;
    }
  }
  l.anteriorAtivo = l.pagina > 0;
  l.proximaAtivo = l.pagina < SONS_PAGINAS - 1;
  l.salvar.rotulo = UA_TXT( "Save sounds" );   // looked up once: its width below and the pager's button both come from this pointer
  l.salvar.tipo = Botao::PRIMARIO;
  // paginador( ) draws its action as wide as `w`: the primary's width, as rodapeAcoes( ) has it.
  l.salvar.w = larguraTinta( l.salvar.rotulo, tipo::botao ) + 2 * ESP4;
  return l;
}

// A row of the list: the label; for a sound that is on and has a melody, the
// melody's name and a chevron that says the name opens something; the switch.
void desenharLinha( Pintor &p, const Caixa16 &c, int som, const VmSons &vm, bool primeira ) {
  const Tema &T = p.t( );
  if ( !primeira ) p.hline( c.x + PAD, c.y, c.w - 2 * PAD, T.linha );
  textoCentrado( p, rotuloDoSom( som ), c.x + PAD, c.y, c.h, tipo::corpo, T.tinta, 'l' );
  if ( som < SOM_COM_MELODIA && vm.ligado[som] ) {
    const int16_t xc = X_CHAVE - ESP2 - L_ICONE;
    p.icone( IconeNome::CHEVRON_RIGHT, L_ICONE, xc, c.y + rd2( c.h - L_ICONE ), T.tinta2 );
    textoCentrado( p, nomeMelodia( som, vm.melodia[som] ), xc - ESP1, c.y, c.h, tipo::corpo, T.tinta2, 'r' );
  }
  interruptor( p, X_CHAVE, c.y + rd2( c.h - H_CHAVE ), vm.ligado[som] );
}

// The plan's slider (telas.js tela_sons) with the minus and plus buttons the
// panel needs: the old screen steered it by tapping the left or right half of
// its row. The track is inset by the knob's radius, so the knob keeps 8 px
// clear of both buttons at either end.
void desenharDeslizante( Pintor &p, const LayoutSons &l, int i, const char *rotulo, int pct, bool foco ) {
  const Tema &T = p.t( );
  const Caixa16 &c = l.deslizante[i];
  superficie( p, c.x, c.y, c.w, c.h, false, 0, foco, T.acento );
  textoCentrado( p, rotulo, c.x + PAD, c.y + ESP2, ESP4, tipo::rotulo, T.tinta2, 'l' );
  char valor[16];
  snprintf( valor, sizeof valor, "%d %%", pct );
  textoCentrado( p, valor, c.x + c.w - PAD, c.y + ESP2, ESP4, tipo::dado, T.tinta, 'r' );
  botaoIcone( p, l.menos[i], IconeNome::MINUS, l.menosAtivo[i] );
  botaoIcone( p, l.mais[i], IconeNome::PLUS, l.maisAtivo[i] );
  const int16_t tx = l.menos[i].x + l.menos[i].w + ESP2 + RAIO_BOLA;
  const int16_t tw = l.mais[i].x - ESP2 - RAIO_BOLA - tx;
  const int16_t cy = l.menos[i].y + rd2( L_BOTAO );
  const int16_t cheio = rdiv( (int32_t)tw * pct, VOLUME_MAX );
  p.fillRoundRect( tx, cy - 2, tw, 4, RAIO_TOTAL, T.superficie2 );
  p.fillRoundRect( tx, cy - 2, cheio, 4, RAIO_TOTAL, T.acento );
  p.fillCircle( tx + cheio, cy, RAIO_BOLA, T.acento );
}

/* ------------------------------------------------------------------ */
/* Melodia                                                              */
/* ------------------------------------------------------------------ */

constexpr int16_t H_TECLA = 44;
constexpr int16_t W_TECLA = ( L_TELA - 3 * ESP2 ) / 2;     // two columns across the card's width, ESP2 between

struct LayoutMelodia {
  Caixa16 tecla[MELODIAS_POR_SOM];
  CaixasRodape rodape;
};

LayoutMelodia layoutMelodia( ) {
  LayoutMelodia l{ };
  for ( int i = 0; i < MELODIAS_POR_SOM; i++ )
    l.tecla[i] = Caixa16{ (int16_t)( ESP2 + ( i % 2 ) * ( W_TECLA + ESP2 ) ), (int16_t)( Y0 + ( i / 2 ) * ( H_TECLA + ESP2 ) ),
                          W_TECLA, H_TECLA };
  l.rodape = caixasRodape( UA_TXT( "Use this melody" ), UA_TXT( "Cancel" ) );   // looked up once: the draw takes the labels from these boxes
  return l;
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Sons                                                                 */
/* ------------------------------------------------------------------ */

void desenharSons( Pintor &p, const VmSons &vm ) {
  const LayoutSons l = layoutSons( vm );
  barraTituloVoltar( p, UA_TXT( "Sounds" ) );
  if ( l.n ) {
    superficie( p, l.cartao.x, l.cartao.y, l.cartao.w, l.cartao.h );
    for ( int i = 0; i < l.n; i++ ) desenharLinha( p, l.linha[i], l.primeira + i, vm, i == 0 );
  } else {
    desenharDeslizante( p, l, 0, UA_TXT( "System volume" ), vm.volumeSistema, vm.foco == 0 );
    desenharDeslizante( p, l, 1, UA_TXT( "Alarm volume" ), vm.volumeAlarme, vm.foco == 1 );
  }
  // The kit's pager draws both arrows alive; the one with no page behind it is
  // painted over, disabled, by the very same boxes.
  paginador( p, l.pagina + 1, SONS_PAGINAS, &l.salvar );
  if ( !l.anteriorAtivo ) botaoIcone( p, paginadorEsquerda( ), IconeNome::CHEVRON_LEFT, false );
  if ( !l.proximaAtivo ) botaoIcone( p, paginadorDireita( ), IconeNome::CHEVRON_RIGHT, false );
}

int alvosSons( const VmSons &vm, Alvo *out ) {
  const LayoutSons l = layoutSons( vm );
  int n = addAlvoVoltar( out, 0, caixaVoltar( ), SONS_VOLTAR );
  for ( int i = 0; i < l.n; i++ ) {
    const Caixa16 &c = l.linha[i];
    const int som = l.primeira + i;
    if ( som < SOM_COM_MELODIA && vm.ligado[som] ) {
      // The melody's name and chevron open the choice; the switch's zone starts
      // 8 px before the switch.
      const int16_t xz = X_CHAVE - ESP2;
      n = addAlvo( out, n, c.x, c.y, xz - c.x, c.h, SONS_LINHA, som );
      n = addAlvo( out, n, xz, c.y, c.x + c.w - xz, c.h, SONS_INTERRUPTOR, som );
    } else {
      n = addAlvo( out, n, c, SONS_INTERRUPTOR, som );
    }
  }
  if ( l.pagina == SONS_PAGINAS - 1 ) {
    for ( int i = 0; i < 2; i++ ) {
      n = addAlvoSe( out, n, l.menosAtivo[i], l.menos[i], SONS_MENOS, (int16_t)i );
      n = addAlvoSe( out, n, l.maisAtivo[i], l.mais[i], SONS_MAIS, (int16_t)i );
    }
  }
  n = addAlvoSe( out, n, l.anteriorAtivo, paginadorEsquerda( ), SONS_PAG_ANTERIOR );
  n = addAlvoSe( out, n, l.proximaAtivo, paginadorDireita( ), SONS_PAG_PROXIMA );
  return addAlvo( out, n, paginadorAcao( l.salvar ), SONS_SALVAR );
}

/* ------------------------------------------------------------------ */
/* Melodia                                                              */
/* ------------------------------------------------------------------ */

void desenharMelodia( Pintor &p, const VmMelodia &vm ) {
  const LayoutMelodia l = layoutMelodia( );
  const int som = vm.som < SOM_COM_MELODIA ? vm.som : 0;
  char titulo[48];
  snprintf( titulo, sizeof titulo, "%s · %s", UA_TXT( "Melody" ), rotuloDoSom( som ) );
  barraTituloVoltar( p, titulo );
  for ( int i = 0; i < MELODIAS_POR_SOM; i++ ) {
    TeclaOpts o;
    o.legenda = nomeMelodia( som, i );
    // The kit's latched key (the accent fill) is the mark: seen at a glance, and not by
    // colour alone. The outlined "active" key was too faint to tell the chosen melody
    // from the other five.
    o.estado = i == vm.escolhida ? TeclaEstado::TRAVADA : TeclaEstado::REPOUSO;
    tecla( p, l.tecla[i].x, l.tecla[i].y, l.tecla[i].w, l.tecla[i].h, o );
  }
  desenharRodape( p, l.rodape, Botao::PRIMARIO, false );
}

int alvosMelodia( const VmMelodia &, Alvo *out ) {
  const LayoutMelodia l = layoutMelodia( );
  int n = addAlvoVoltar( out, 0, caixaVoltar( ), MELOD_VOLTAR );
  for ( int i = 0; i < MELODIAS_POR_SOM; i++ ) n = addAlvo( out, n, l.tecla[i], MELOD_ITEM, i );
  n = addAlvo( out, n, l.rodape.secundaria, MELOD_CANCELAR );
  return addAlvo( out, n, l.rodape.primaria, MELOD_USAR );
}

}  // namespace tela
}  // namespace ua
