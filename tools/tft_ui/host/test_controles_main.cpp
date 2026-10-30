// The control recorder of the Ângulo kit (UA_CONTROLE( ) in src/ui/angulo/Angulo.h), checked on the host. host_telas.py
// holds every sample to what the recorder says it drew, so a recorder that skipped a kind of control, or a state, would
// pass every sample without being asked: this draws each control function of the kit enabled and disabled, and the few
// controls the chart draws, and asserts the records one by one (the box, enabled or not, the kind). Run by
// tools/tft_ui/test_alvos.py, which also builds copies of the kit with one change to the recorder at a time and requires
// each of them to fail at least one case below.
#include <Adafruit_GFX.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "AjustesComum.h"
#include "Angulo.h"
#include "Graficos.h"
#include "GraficosComum.h"

namespace {

int casos = 0, falhas = 0;

class LeitorCanvas : public ua::Leitor {
public:
  explicit LeitorCanvas( GFXcanvas16 &c ) : c_( c ) {}
  uint16_t ler( int16_t x, int16_t y ) override { return c_.getPixel( x, y ); }

private:
  GFXcanvas16 &c_;
};

struct Esperado { int16_t x, y, w, h; bool habilitado; const char *tipo; };

bool igual( const ua::ControleDesenhado &c, const Esperado &e ) {
  return c.x == e.x && c.y == e.y && c.w == e.w && c.h == e.h && c.habilitado == e.habilitado && strcmp( c.tipo, e.tipo ) == 0 &&
         !c.excluido[0] && !c.sob[0];
}

void despejar( const ua::ControleDesenhado *l, int n ) {
  for ( int i = 0; i < n; i++ )
    printf( "       record %d: %d,%d %dx%d %s %s%s%s%s%s\n", i, l[i].x, l[i].y, l[i].w, l[i].h, l[i].habilitado ? "enabled" : "disabled", l[i].tipo,
            l[i].excluido[0] ? ", not a control: " : "", l[i].excluido, l[i].sob[0] ? ", dimmed on purpose: " : "", l[i].sob );
}

void resultado( const char *nome, const char *o_que, bool ok, int n, const ua::ControleDesenhado *l ) {
  casos++;
  if ( ok ) { printf( "ok   %s: %s (%d record(s))\n", nome, o_que, n ); return; }
  printf( "FAIL %s: %s\n", nome, o_que );
  despejar( l, n );
  falhas++;
}

// The records are exactly these, in this order.
void caso( const char *nome, const char *o_que, std::initializer_list<Esperado> esperado ) {
  const ua::ControleDesenhado *l;
  const int n = ua::hostControles( &l );
  bool ok = n == (int)esperado.size( );
  int i = 0;
  for ( const Esperado &e : esperado ) ok = ok && i < n && igual( l[i++], e );
  resultado( nome, o_que, ok, n, l );
}

// Each of these is among the records exactly once (the screen draws other controls around them).
void contem( const char *nome, const char *o_que, std::initializer_list<Esperado> esperado ) {
  const ua::ControleDesenhado *l;
  const int n = ua::hostControles( &l );
  bool ok = true;
  for ( const Esperado &e : esperado ) {
    int vezes = 0;
    for ( int i = 0; i < n; i++ ) vezes += igual( l[i], e );
    ok = ok && vezes == 1;
  }
  resultado( nome, o_que, ok, n, l );
}

void limpar( ) { ua::hostLimparControles( ); }

}  // namespace

int main( ) {
  GFXcanvas16 canvas( 320, 240 );
  LeitorCanvas leitor( canvas );
  ua::Pintor p( canvas, ua::temaEscuro( ), leitor );
  using ua::Botao;
  using ua::IconeNome;

  // 1. botao, the four kinds, with and without an icon, enabled and disabled.
  limpar( );
  ua::botao( p, 10, 20, 100, 30, "OK", Botao::PRIMARIO, false );
  ua::botao( p, 10, 60, 100, 30, "OK", Botao::SECUNDARIO, true );
  ua::botao( p, 10, 100, 100, 30, "OK", Botao::DISCRETO, false, IconeNome::ARROW_LEFT );
  ua::botao( p, 10, 140, 100, 30, "OK", Botao::DESTRUTIVO, true, IconeNome::DELETE );
  caso( "botao", "each button records its box and whether it was drawn enabled",
        { { 10, 20, 100, 30, true, "botao" }, { 10, 60, 100, 30, false, "botao" }, { 10, 100, 100, 30, true, "botao" },
          { 10, 140, 100, 30, false, "botao" } } );

  // 2. A control drawn over another of the same box replaces it (the disabled arrow over the live one), whichever comes last.
  limpar( );
  ua::botao( p, 10, 20, 100, 30, "", Botao::SECUNDARIO, false );
  ua::botao( p, 10, 20, 100, 30, "", Botao::SECUNDARIO, true );
  ua::botao( p, 10, 60, 100, 30, "", Botao::SECUNDARIO, true );
  ua::botao( p, 10, 60, 100, 30, "", Botao::SECUNDARIO, false );
  ua::botao( p, 20, 60, 100, 30, "", Botao::SECUNDARIO, false );
  caso( "same box", "the later draw replaces the record; a box that differs by one pixel is another control",
        { { 10, 20, 100, 30, false, "botao" }, { 10, 60, 100, 30, true, "botao" }, { 20, 60, 100, 30, true, "botao" } } );

  // 3. chip: always enabled, the box is the width the kit gives it.
  limpar( );
  const int16_t w1 = ua::chip( p, 10, 20, 28, "7 d", true );
  const int16_t w2 = ua::chip( p, 60, 20, 28, "12 h", false );
  caso( "chip", "a chip records the box it drew, lit or not", { { 10, 20, w1, 28, true, "chip" }, { 60, 20, w2, 28, true, "chip" } } );

  // 4. interruptor, the four states.
  limpar( );
  ua::interruptor( p, 10, 20, true, false );
  ua::interruptor( p, 60, 20, false, false );
  ua::interruptor( p, 110, 20, true, true );
  ua::interruptor( p, 160, 20, false, true );
  caso( "interruptor", "a switch records 40 x 24, enabled or disabled, on or off",
        { { 10, 20, 40, 24, true, "interruptor" }, { 60, 20, 40, 24, true, "interruptor" }, { 110, 20, 40, 24, false, "interruptor" },
          { 160, 20, 40, 24, false, "interruptor" } } );

  // 5. segmentado: one record a segment, (200 - 2 * 4) / 3 = 64 px wide, 4 px apart.
  limpar( );
  const char *const opcoes[3] = { "A", "B", "C" };
  ua::segmentado( p, 10, 20, 200, 30, opcoes, 3, 1, false );
  ua::segmentado( p, 10, 80, 200, 30, opcoes, 3, 1, true );
  caso( "segmentado", "each segment records its box; the disabled control records all of them disabled",
        { { 10, 20, 64, 30, true, "segmento" }, { 78, 20, 64, 30, true, "segmento" }, { 146, 20, 64, 30, true, "segmento" },
          { 10, 80, 64, 30, false, "segmento" }, { 78, 80, 64, 30, false, "segmento" }, { 146, 80, 64, 30, false, "segmento" } } );

  // 6. tecla: only the disabled state is disabled.
  limpar( );
  const ua::TeclaEstado estados[5] = { ua::TeclaEstado::REPOUSO, ua::TeclaEstado::PRESSIONADA, ua::TeclaEstado::ATIVA,
                                       ua::TeclaEstado::TRAVADA, ua::TeclaEstado::DESAB };
  for ( int k = 0; k < 5; k++ ) {
    ua::TeclaOpts o;
    o.legenda = "A";
    o.estado = estados[k];
    ua::tecla( p, (int16_t)( 10 + 40 * k ), 20, 36, 32, o );
  }
  caso( "tecla", "a key records its box; resting, pressed, active and locked are enabled, only DESAB is not",
        { { 10, 20, 36, 32, true, "tecla" }, { 50, 20, 36, 32, true, "tecla" }, { 90, 20, 36, 32, true, "tecla" }, { 130, 20, 36, 32, true, "tecla" },
          { 170, 20, 36, 32, false, "tecla" } } );

  // 7. botaoIcone (the settings screens' steppers and arrows).
  limpar( );
  ua::tela::ajustes::botaoIcone( p, ua::Caixa16{ 20, 64, 40, 40 }, IconeNome::CHEVRON_LEFT, true );
  ua::tela::ajustes::botaoIcone( p, ua::Caixa16{ 260, 144, 40, 40 }, IconeNome::CHEVRON_RIGHT, false );
  caso( "botaoIcone", "an icon button records its box, enabled when active", { { 20, 64, 40, 40, true, "botao" }, { 260, 144, 40, 40, false, "botao" } } );

  // 8. The pager: both arrows alive, then the ones the screens dim (a first page has no previous, a last one no next).
  limpar( );
  ua::paginador( p, 1, 3 );
  caso( "paginador", "the kit's pager records its two arrows enabled", { { 8, 196, 48, 40, true, "botao" }, { 64, 196, 48, 40, true, "botao" } } );
  limpar( );
  ua::Acao salvar;
  salvar.rotulo = "Salvar";
  salvar.tipo = Botao::PRIMARIO;
  salvar.w = 80;
  ua::paginador( p, 1, 3, &salvar );
  caso( "paginador, action", "and its action at the right", { { 8, 196, 48, 40, true, "botao" }, { 64, 196, 48, 40, true, "botao" }, { 232, 196, 80, 40, true, "botao" } } );
  limpar( );
  ua::tela::graficos::paginadorComEstados( p, 1, 3, "Temperatura" );
  caso( "pager, first page", "the dimmed previous arrow replaces its live record", { { 8, 196, 48, 40, false, "botao" }, { 64, 196, 48, 40, true, "botao" } } );
  limpar( );
  ua::tela::graficos::paginadorComEstados( p, 3, 3, "Pressão" );
  caso( "pager, last page", "the dimmed next arrow replaces its live record", { { 8, 196, 48, 40, true, "botao" }, { 64, 196, 48, 40, false, "botao" } } );
  limpar( );
  ua::tela::graficos::paginadorComEstados( p, 2, 3, "Umidade" );
  caso( "pager, middle page", "both arrows stay enabled", { { 8, 196, 48, 40, true, "botao" }, { 64, 196, 48, 40, true, "botao" } } );

  // 9. The chart's footer: its arrows (the seta of GraficosGrafico.cpp) and the five period chips it draws by hand.
  static ua::tela::VmGrafico vm;
  memset( &vm, 0, sizeof vm );
  ua::copiar( vm.sensor, "Freezer", sizeof vm.sensor );
  vm.periodo = 3;
  limpar( );
  ua::tela::desenharGrafico( p, vm );
  contem( "chart, present", "the previous arrow is enabled, the next one disabled, and the five periods are chips of their own",
          { { 8, 196, 36, 40, true, "botao" }, { 276, 196, 36, 40, false, "botao" }, { 52, 196, 40, 40, true, "periodo" },
            { 96, 196, 40, 40, true, "periodo" }, { 140, 196, 40, 40, true, "periodo" }, { 184, 196, 40, 40, true, "periodo" },
            { 228, 196, 40, 40, true, "periodo" } } );
  vm.passado = true;
  limpar( );
  ua::tela::desenharGrafico( p, vm );
  contem( "chart, past", "in the past the next arrow is enabled", { { 8, 196, 36, 40, true, "botao" }, { 276, 196, 36, 40, true, "botao" } } );

  // 10. The notes a call leaves for the control it draws: each is for the next control only, and a clear drops them.
  limpar( );
  UA_NAO_CONTROLE( "pill" );
  ua::botao( p, 10, 20, 60, 20, "A", Botao::SECUNDARIO, false );
  ua::botao( p, 80, 20, 60, 20, "B", Botao::SECUNDARIO, false );
  UA_CONTROLE_SOB_ALVO( "row" );
  ua::interruptor( p, 10, 60, true, true );
  ua::interruptor( p, 60, 60, true, true );
  {
    const ua::ControleDesenhado *l;
    const int n = ua::hostControles( &l );
    const bool ok = n == 4 && !strcmp( l[0].excluido, "pill" ) && !l[0].sob[0] && !l[1].excluido[0] && !l[1].sob[0] && !l[2].excluido[0] &&
                    !strcmp( l[2].sob, "row" ) && !l[3].sob[0] && !l[3].excluido[0];
    resultado( "notes", "UA_NAO_CONTROLE and UA_CONTROLE_SOB_ALVO mark the next control drawn and no other", ok, n, l );
  }
  limpar( );
  UA_NAO_CONTROLE( "pill" );
  UA_CONTROLE_SOB_ALVO( "row" );
  limpar( );
  ua::botao( p, 10, 20, 60, 20, "A", Botao::SECUNDARIO, false );
  caso( "clear", "clearing the records drops the notes that were waiting", { { 10, 20, 60, 20, true, "botao" } } );

  printf( "%s: %d case(s), %d failure(s)\n", falhas ? "FAIL" : "OK", casos, falhas );
  return falhas ? 1 : 0;
}
