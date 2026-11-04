// test_idioma_pacote_main.cpp -- the Angulo's Idioma screen, RUN on the host (S10c, sync 6, C3: #294 on the Angulo). Built and run by tools/tft_ui/test_idioma_pacote.py.
//
// #294 made the panel's Idioma screen list a pack it has only FOUND in /lang when the unit booted in English (isLangListed( )): the original's draw and touch took it, and the Angulo's two sites kept reading
// _activeLangLoaded, so an English boot listed English alone and no tap could reach the pack. No frame shows it (the samples build the screen state directly), so test_idioma_pacote.py cuts out of
// DisplayManager_UiA_Menus.cpp, verbatim and without comments, the pieces that decide what the screen lists and what a tap does, and this file runs them:
//   nome_do_pacote.inc   nomeDoPacote( ), the pack's name as the list reads it;
//   montar_idioma.inc    the body of `case MODE_SETTINGS_LANG: {` of uaMontarMenus( ): the view model (the rows, the pack's name, the mark);
//   toque_prologo.inc    the statements uaToqueMenus( ) declares before its switch (voltar, linha, i);
//   toque_idioma.inc     `case MODE_SETTINGS_LANG:` of uaToqueMenus( ), up to its break;
//   mostrar_idioma.inc   the statement of the core's showSettingsLang( ) (DisplayManager_i18n.cpp) that sets the row the screen opens on, _previewLangIdx.
// They are compiled against a stand-in for what they call and the firmware's own LangChoice.h and the kit's own MenusConfig.cpp, whose alvosIdioma( ) says how many rows the glass lists. What each case must do is
// written from the case (the pack is listed, loaded or absent; the language saved), never from the firmware's arithmetic.

#include <Adafruit_GFX.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Angulo.h"
#include "UiA.h"
#include "telas/Telas.h"
#include "telas/MenusConfig.h"
#include "LangChoice.h"      // src/display: langRowsListed( ), langRowAtOpen( ), the firmware's own
#include "SettingsSteps.h"   // src/display: the text before the fix still calls langSlotCount( )
#include "Textos.h"          // UA_TXT( ): after the project headers

enum UiMode : uint8_t { MODE_SETTINGS_MAIN, MODE_SETTINGS_LANG };
enum LangId { LANG_EN = 0, LANG_PT = 1, LANG_COUNT = 2 };   // SystemDefs_Records.h: test_idioma_pacote.py reads the enum and refuses a difference
constexpr uint8_t ZONA_VOLTAR = 0xA0, ZONA_LINHA = 0xA1;    // the glue's names; the values only have to differ

struct ActiveLang { char name[32]; char code[8]; };

class DisplayManager {
 public:
  void tocaIdioma( const ua::Alvo *a );
  bool montaIdioma( UaQuadro &q );
  void showSettingsLang( int currentLang );

  // what the pieces call, and records
  bool isLangLoaded( ) const { return _activeLangLoaded; }
  bool isLangListed( ) const { return listado; }
  // the firmware's own text, accessor for accessor (src/DisplayManager_LangParser.cpp): the pack's name and code only while it is listed, an empty string otherwise. test_idioma_pacote.py compares the two bodies and refuses a difference
  const char *getListedLangName( ) const { return isLangListed( ) ? _activeLang.name : ""; }
  const char *getListedLangCode( ) const { return isLangListed( ) ? _activeLang.code : ""; }
  bool acceptTouch( uint8_t zona ) {
    if ( nZonas < 8 ) zonas[nZonas] = zona;
    nZonas++;
    return aceita;
  }
  void actApplyLang( int idx ) {
    if ( nAplicados < 8 ) aplicados[nAplicados] = idx;
    nAplicados++;
  }
  void showSettingsMain( ) { voltou++; }

  // the state they read and move
  UiMode _uiMode = MODE_SETTINGS_LANG;
  bool _activeLangLoaded = false;   // a pack in RAM
  bool listado = false;             // isLangListed( ): a pack found in /lang (an English boot) or loaded
  ActiveLang _activeLang = {};
  int _currentLangIdx = 0, _previewLangIdx = 0, _langPage = 0;

  // the gate's answer and the records
  bool aceita = true;
  int nZonas = 0, nAplicados = 0, voltou = 0;
  uint8_t zonas[8] = {};
  int aplicados[8] = {};
};

uint8_t g_uaVm[UA_VM_MAX];

namespace {
#include "nome_do_pacote.inc"
}  // namespace

bool DisplayManager::montaIdioma( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
#include "montar_idioma.inc"
    default:
      break;
  }
  return false;
}

void DisplayManager::tocaIdioma( const ua::Alvo *a ) {
  using namespace ua;
  using namespace ua::tela;
#include "toque_prologo.inc"
  switch ( _uiMode ) {
#include "toque_idioma.inc"
    default:
      break;
  }
}

void DisplayManager::showSettingsLang( int currentLang ) {
  _uiMode = MODE_SETTINGS_LANG;
#include "mostrar_idioma.inc"
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {

int checks = 0, falhas = 0, caso_n = 0;
int porCaso[8][2];   // [case][0] checks, [1] failed
int mostradas[8];

// The first three failures of each case are printed; the run goes on, so that the cases a mutant or the unfixed text fails and the ones it passes are all told.
#define CONFERE( cond, ... ) do { checks++; porCaso[caso_n][0]++; if ( !( cond ) ) { falhas++; porCaso[caso_n][1]++; \
                                    if ( mostradas[caso_n]++ < 3 ) { printf( "FAIL case %d: ", caso_n ); printf( __VA_ARGS__ ); printf( "\n" ); } } } while ( 0 )

struct Pacote { const char *nome, *codigo, *lido; };   // lido: how the list must read the pack
const Pacote PACOTES[] = {
  { "Portugues (Brasil)", "pt-BR", "Portugu\xc3\xaas (Brasil)" },   // the two packs the project ships read by their own names
  { "Espanol (Espana)", "es-ES", "Espa\xc3\xb1ol (Espa\xc3\xb1" "a)" },
  { "Klingon", "tlh", "Klingon (tlh)" },                       // any other: "name (code)"
};

static DisplayManager dm;

// The unit as the case describes it. `ativo` is the language in use (the pack's only when it is loaded). _previewLangIdx starts at 7, which is no row: the screen has to be opened through showSettingsLang( ).
void arruma( bool carregado, bool listado, const Pacote *p, int ativo ) {
  dm._uiMode = MODE_SETTINGS_LANG;
  dm._activeLangLoaded = carregado;
  dm.listado = listado;
  memset( &dm._activeLang, 0, sizeof dm._activeLang );
  if ( p && ( carregado || listado ) ) {
    snprintf( dm._activeLang.name, sizeof dm._activeLang.name, "%s", p->nome );
    snprintf( dm._activeLang.code, sizeof dm._activeLang.code, "%s", p->codigo );
  }
  dm._currentLangIdx = ativo;
  dm._previewLangIdx = 7;
  dm._langPage = 0;
  dm.aceita = true;
  dm.nZonas = dm.nAplicados = dm.voltou = 0;
}

struct Tela { bool ok; int linhas; bool indicesOk; char pacote[32]; int atual; };

Tela olha( ) {
  using namespace ua::tela;
  Tela t = {};
  UaQuadro q = {};
  t.ok = dm.montaIdioma( q );
  const VmIdioma *v = static_cast<const VmIdioma *>( q.vm );
  if ( !t.ok || !v || q.tam != sizeof( VmIdioma ) || !q.alvos ) { t.ok = false; return t; }
  snprintf( t.pacote, sizeof t.pacote, "%s", v->pacote );
  t.atual = v->atual;
  ua::Alvo alvos[32];
  const int n = q.alvos( q.vm, alvos );
  t.indicesOk = true;
  for ( int k = 0; k < n; k++ )
    if ( alvos[k].tipo == LST_LINHA ) {
      if ( alvos[k].indice != t.linhas ) t.indicesOk = false;
      t.linhas++;
    }
  return t;
}

// The screen as Core 0 opens it (showSettingsLang( ) with the saved language) and as the glass lists it.
void conferTela( const char *quem, bool carregado, bool listado, const Pacote *p, int ativo, int salvo ) {
  using namespace ua::tela;
  arruma( carregado, listado, p, ativo );
  dm.showSettingsLang( salvo );
  const int linhas = ( carregado || listado ) ? 2 : 1;
  const int marcada = salvo < linhas ? salvo : 0;   // the saved row when the screen lists it, English otherwise
  const char *lido = linhas == 2 && p ? p->lido : "";
  const Tela t = olha( );
  CONFERE( t.ok, "%s: the builder returned false or no view model", quem );
  CONFERE( t.linhas == linhas && t.indicesOk, "%s: the glass lists %d row(s), expected %d", quem, t.linhas, linhas );
  CONFERE( !strcmp( t.pacote, lido ), "%s: the pack's row reads \"%s\", expected \"%s\"", quem, t.pacote, lido );
  CONFERE( t.atual == ( marcada == 0 ? ID_INGLES : ID_PACOTE ), "%s: the mark is on %s, expected %s (saved language %d)", quem, t.atual == ID_INGLES ? "English" : "the pack",
           marcada == 0 ? "English" : "the pack", salvo );
}

// A tap on row r (a row the glass may not even list: the touch is bounded on its own), the gate granting and refusing.
void conferToques( const char *quem, bool carregado, bool listado, const Pacote *p, int ativo, int salvo ) {
  using namespace ua::tela;
  const int linhas = ( carregado || listado ) ? 2 : 1;
  for ( int r = -1; r <= 2; r++ )
    for ( const bool aceita : { true, false } ) {
      arruma( carregado, listado, p, ativo );
      dm.showSettingsLang( salvo );
      dm.aceita = aceita;
      const ua::Alvo alvo{ 10, 10, 40, 40, (uint8_t)LST_LINHA, (int16_t)r };
      dm.tocaIdioma( &alvo );
      const bool aplica = aceita && r >= 0 && r < linhas;
      CONFERE( dm.nAplicados == ( aplica ? 1 : 0 ) && ( !aplica || ( dm.aplicados[0] == r && dm.zonas[0] == ZONA_LINHA ) ) && dm.voltou == 0,
               "%s: a tap on row %d, the gate %s: applied %d time(s) (row %d), expected %s", quem, r, aceita ? "granting" : "refusing", dm.nAplicados, dm.aplicados[0], aplica ? "that row" : "nothing" );
    }
}

}  // namespace

int main( ) {
  using namespace ua::tela;
  static_assert( ID_INGLES == 0 && ID_PACOTE == 1, "the rows are the language ids" );
  char quem[96];

  caso_n = 1;   // an English boot, a pack listed (found in /lang, not in RAM): the unit runs in English whatever it saved
  for ( const Pacote &p : PACOTES ) {
    snprintf( quem, sizeof quem, "English boot, %s listed, English saved", p.codigo );
    conferTela( quem, false, true, &p, LANG_EN, LANG_EN );
    conferToques( quem, false, true, &p, LANG_EN, LANG_EN );
  }

  caso_n = 2;   // no pack
  conferTela( "no pack, English saved", false, false, nullptr, LANG_EN, LANG_EN );
  conferToques( "no pack, English saved", false, false, nullptr, LANG_EN, LANG_EN );
  conferTela( "no pack, a pack saved (it was removed)", false, false, nullptr, LANG_EN, LANG_PT );
  conferToques( "no pack, a pack saved (it was removed)", false, false, nullptr, LANG_EN, LANG_PT );

  caso_n = 3;   // a pack loaded and in use
  for ( const Pacote &p : PACOTES ) {
    snprintf( quem, sizeof quem, "%s loaded and in use", p.codigo );
    conferTela( quem, true, true, &p, LANG_PT, LANG_PT );
    conferToques( quem, true, true, &p, LANG_PT, LANG_PT );
    snprintf( quem, sizeof quem, "%s loaded, English chosen", p.codigo );
    conferTela( quem, true, true, &p, LANG_EN, LANG_EN );
  }

  caso_n = 4;   // D5: an English boot, the pack chosen and saved, the screen reopened: the saved row is marked, as the original marks it since #294
  for ( const Pacote &p : PACOTES ) {
    snprintf( quem, sizeof quem, "English boot, %s chosen and saved, reopened", p.codigo );
    conferTela( quem, false, true, &p, LANG_EN, LANG_PT );
    snprintf( quem, sizeof quem, "English boot, %s listed, English chosen again, reopened", p.codigo );
    conferTela( quem, false, true, &p, LANG_EN, LANG_EN );
  }

  caso_n = 5;   // Back: the menu is shown again, nothing is applied
  for ( const bool aceita : { true, false } ) {
    arruma( false, true, &PACOTES[0], LANG_EN );
    dm.showSettingsLang( LANG_EN );
    dm.aceita = aceita;
    const ua::Alvo alvo{ 10, 10, 40, 40, (uint8_t)LST_VOLTAR, (int16_t)-1 };
    dm.tocaIdioma( &alvo );
    CONFERE( dm.nAplicados == 0 && dm.voltou == ( aceita ? 1 : 0 ) && dm.nZonas == 1 && dm.zonas[0] == ZONA_VOLTAR, "Back, the gate %s: menu shown %d time(s), applied %d, asked the gate %d time(s)", aceita ? "granting" : "refusing", dm.voltou,
             dm.nAplicados, dm.nZonas );
  }

  static const char *nomes[] = { "", "an English boot with a pack listed", "no pack", "a pack loaded and in use", "the saved language marked when the screen is reopened (D5)", "Back" };
  for ( int n = 1; n <= 5; n++ ) printf( "CASE %d %s: %d checks, %d failed\n", n, nomes[n], porCaso[n][0], porCaso[n][1] );
  printf( "%d checks, %d failed\n", checks, falhas );
  return falhas ? 1 : 0;
}
