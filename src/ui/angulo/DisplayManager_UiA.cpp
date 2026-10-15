/**
 * @file DisplayManager_UiA.cpp
 * @brief The Ângulo build's renderer, theme, touch dispatch and the screens
 *        every group leans on (result, confirmations, the unfinished-screen
 *        placeholder). pico_wiretft_angulo only; see src/ui/angulo/UiA.h and
 *        tools/tft_ui/ESPEC_UI_A.md.
 *
 * The old screens' logic keeps running: every old draw entry point became
 * UA_DESVIO( ) (src/ui/UiDriver.h), so the timers, transitions and
 * acknowledgements around a draw still happen, and this file paints the frame
 * of the current mode once the pass has run them all.
 */

#include "../../DisplayManager.h"


#include <stdio.h>
#include <string.h>

#include "Angulo.h"
#include "UiA.h"
#include "telas/Base.h"
#include "telas/Telas.h"

alignas( 8 ) uint8_t g_uaVm[UA_VM_MAX];
volatile int8_t g_uaNav[5] = { UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA, UA_NAV_NADA };

void uaLatin1ParaUtf8( const char *latin1, char *out, int cap ) {
  int n = 0;
  if ( cap <= 0 ) return;
  for ( const uint8_t *s = (const uint8_t *)( latin1 ? latin1 : "" ); *s; s++ ) {
    if ( *s < 0x80 ) {
      if ( n + 1 >= cap ) break;
      out[n++] = (char)*s;
    } else {
      if ( n + 2 >= cap ) break;
      out[n++] = (char)( 0xC0 | ( *s >> 6 ) );
      out[n++] = (char)( 0x80 | ( *s & 0x3F ) );
    }
  }
  out[n] = 0;
}

namespace {

constexpr int16_t ALTURA_FAIXA = 40;
/* A strip is as wide as the panel (the old screens' canvas is the panel's width by
 * 45 rows, DisplayManager.cpp) and the strips stack to its height. int, as the
 * literals were, so that every expression below keeps its type and so its code. */
constexpr int LARGURA_FAIXA = DisplayDriver::W;
static_assert( DisplayDriver::H % ALTURA_FAIXA == 0, "the strips tile the panel's height" );

/* Adafruit_GFX over one 40-row strip of the old screens' canvas (the panel's width by
 * 45 rows): the screen's coordinates, shifted up by the strip's top, clipped to the
 * strip.
 * Each strip draws the whole screen and keeps what falls in it — the price of
 * a flicker-free frame without a 150 kB frame buffer — so the pixel path
 * writes the buffer itself (GFXcanvas16's layout: row-major, native order)
 * instead of a second virtual call per pixel. */
class Faixa : public Adafruit_GFX {
public:
  explicit Faixa( GFXcanvas16 *cv ) : Adafruit_GFX( DisplayDriver::W, DisplayDriver::H ), buf_( cv->getBuffer( ) ) {}
  void topo( int16_t y0 ) { y0_ = y0; }
  void drawPixel( int16_t x, int16_t y, uint16_t c ) override {
    y = (int16_t)( y - y0_ );
    if ( (uint16_t)x >= (unsigned)LARGURA_FAIXA || (uint16_t)y >= (uint16_t)ALTURA_FAIXA ) return;
    buf_[(int32_t)y * LARGURA_FAIXA + x] = c;
  }
  void fillRect( int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c ) override {
    int16_t x0 = x < 0 ? 0 : x, x1 = x + w > LARGURA_FAIXA ? LARGURA_FAIXA : (int16_t)( x + w );
    int16_t r0 = (int16_t)( y - y0_ ), r1 = (int16_t)( r0 + h );
    if ( r0 < 0 ) r0 = 0;
    if ( r1 > ALTURA_FAIXA ) r1 = ALTURA_FAIXA;
    if ( x0 >= x1 || r0 >= r1 ) return;
    for ( int16_t r = r0; r < r1; r++ ) {
      uint16_t *p = buf_ + (int32_t)r * LARGURA_FAIXA + x0;
      for ( int16_t i = x0; i < x1; i++ ) *p++ = c;
    }
  }
  void drawFastHLine( int16_t x, int16_t y, int16_t w, uint16_t c ) override { fillRect( x, y, w, 1, c ); }
  void drawFastVLine( int16_t x, int16_t y, int16_t h, uint16_t c ) override { fillRect( x, y, 1, h, c ); }
  void fillScreen( uint16_t c ) override { fillRect( 0, y0_, LARGURA_FAIXA, ALTURA_FAIXA, c ); }

private:
  uint16_t *buf_;
  int16_t y0_ = 0;
};
constexpr int FAIXAS = DisplayDriver::H / ALTURA_FAIXA;

/* The frame on the glass: what it was built from, its targets, and the hash of
 * each strip as pushed (a strip whose pixels did not change is not pushed). */
uint32_t s_hash = 0;
UiMode s_modo = MODE_DASHBOARD;
bool s_temQuadro = false;
ua::Alvo s_alvos[ua::MAX_ALVOS];
int s_nAlvos = 0;
uint32_t s_faixa[FAIXAS];
bool s_escuroNaTela = false;

/* For GET /api/ui, read from Core 0 while Core 1 paints: a sequence counter,
 * odd while the frame's record is being written (a seqlock: the reader copies
 * and retries until it saw the same even number before and after). */
volatile uint32_t s_seq = 0;
uint32_t s_pinturas = 0, s_ms = 0, s_msMax = 0;
uint8_t s_faixasEnviadas = 0;
static_assert( ua::MAX_ALVOS == UA_MAX_ALVOS, "UaEstado holds every target" );
static_assert( sizeof( ua::Alvo ) == sizeof( UaAlvoBruto ), "UaAlvoBruto mirrors ua::Alvo" );

uint32_t fnv( const void *dados, size_t n, uint32_t h = 2166136261u ) {
  const uint8_t *b = static_cast<const uint8_t *>( dados );
  for ( size_t i = 0; i < n; i++ ) { h ^= b[i]; h *= 16777619u; }
  return h;
}

/* Core 0's verdicts, said the Ângulo way: what happened and how to go on. The
 * keys come from showPanelMessage( )'s callers (AppManager_Panel.cpp,
 * AppManager_Events.cpp; grep of 2026-10-14). TR_PIN_SAVED also answers a
 * saved PIN policy and TR_NAME_INVALID the deletion of one's own account,
 * hence the neutral words. */
struct TextoMsg { LangKey k; const char *texto, *detalhe; };
const TextoMsg kMensagens[] = {
  { TR_NO_PERMISSION, "Sem permissão.", "Peça a quem administra o aparelho para liberar esta função na sua conta." },
  { TR_NAME_INVALID, "Não foi possível salvar a conta.",
    "O nome pode estar em uso ou ser inválido. Uma conta também não pode excluir a si mesma." },
  { TR_USER_SAVED, "Conta salva.", "" },
  { TR_USER_DELETED, "Conta excluída.", "" },
  { TR_PIN_SAVED, "Alteração salva.", "" },
  { TR_USERS_FULL, "Não há vaga para outra conta.", "O aparelho guarda até 31 contas. Exclua uma para criar outra." },
  { TR_PIN_IN_USE, "Este PIN já é de outra conta.", "Escolha outro PIN." },
  { TR_INVALID_PIN, "PIN inválido.", "Confira o tamanho mínimo e os caracteres em Segurança do PIN." },
  { TR_ERROR_LBL, "Não deu certo.", "Confira os valores e tente de novo." },
};

}  // namespace

/* ------------------------------------------------------------------ */
/* Theme and clock                                                     */
/* ------------------------------------------------------------------ */

/* cfg.themeIndex: 0 light, 1 dark, 2 automatic (dark from 19:00 to 6:59 by
 * the panel's clock; light while there is no clock). */
const ua::Tema &DisplayManager::uaTema( ) {
  const int8_t escolha = _sysConfigPtr ? _sysConfigPtr->themeIndex : 0;
  if ( escolha == 1 ) return ua::temaEscuro( );
  if ( escolha == 2 ) {
    char hora[6];
    int h = -1;
    bool provisoria = false;
    if ( uaRelogio( hora, h, provisoria ) && ( h >= 19 || h < 7 ) ) return ua::temaEscuro( );
  }
  return ua::temaClaro( );
}

/* "HH:MM" and the hour from the clock line Core 0 publishes
 * ("dd/mm/yy - HH:MM:SS", or " ? " in place of " - " while the clock is
 * provisional). False when there is no clock line yet. */
bool DisplayManager::uaRelogio( char hora[6], int &h, bool &provisoria ) {
  char linha[24];
  if ( !mutex_enter_timeout_us( &_stateMutex, 500 ) ) {
    memcpy( linha, _lastRenderedState.timeString, sizeof linha );
  } else {
    memcpy( linha, _sharedState.timeString, sizeof linha );
    mutex_exit( &_stateMutex );
  }
  linha[sizeof linha - 1] = 0;
  const char *sep = strstr( linha, " - " );
  provisoria = false;
  if ( !sep ) {
    sep = strstr( linha, " ? " );
    provisoria = sep != nullptr;
  }
  if ( !sep || strlen( sep + 3 ) < 5 ) { hora[0] = 0; h = -1; return false; }
  memcpy( hora, sep + 3, 5 );
  hora[5] = 0;
  h = ( hora[0] - '0' ) * 10 + ( hora[1] - '0' );
  return h >= 0 && h < 24;
}

void DisplayManager::uaVmStatus( ua::VmStatus &s ) {
  int h;
  uaRelogio( s.hora, h, s.horaProvisoria );
  if ( _alarmSilenced ) {
    const int32_t falta = (int32_t)( _alarmSilenceEnd - millis( ) );
    snprintf( s.aviso, sizeof s.aviso, "Silenciado · %ld s", (long)( falta > 0 ? falta / 1000 : 0 ) );
    s.avisoTom = ua::Tom::ALERTA;
  } else if ( _webNotifyStartMs > 0 && !timeSince( _webNotifyStartMs, WEB_NOTIFY_DURATION_MS ) ) {
    char nome[48];
    ua::copiar( nome, _webNotifyUser, 24 );
    snprintf( s.aviso, sizeof s.aviso, "%s entrou pela web", nome );
    s.avisoTom = ua::Tom::NEUTRO;
  }
  /* The wired build's RSSI is the link: above -100 the cable is up (the old
   * top bar's glyph, DisplayManager_Dashboard.cpp). */
  s.rede = _lastRenderedState.wifiRssi > -100 ? ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;
  s.pendentes = _lastRenderedState.pendingPkts;
  s.envio = _pktArrowState == 2 ? 2 : _pktArrowState == 1 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* The renderer                                                        */
/* ------------------------------------------------------------------ */

/* Paints q strip by strip and keeps its targets. Pushes only the strips whose
 * pixels changed, unless the whole panel must be repainted (_uaTudo). */
bool DisplayManager::uaPintar( const UaQuadro &q, UiMode modo, uint32_t hash ) {
  GFXcanvas16 *const cv = _driver.canvas;
  if ( !cv || !_driver.tft || !q.desenhar ) return false;
  const ua::Tema &tema = uaTema( );
  Faixa faixa( cv );
  ua::Pintor p( faixa, tema );
  const uint32_t t0 = millis( );
  uint8_t enviadas = 0;
  _uaPintando = true;
  if ( _uaTudo ) {
    /* The black margins the alignment offset uncovers, and every strip. */
    fastClearScreen( tema.fundo );
    for ( int k = 0; k < FAIXAS; k++ ) s_faixa[k] = 0;
  }
  for ( int k = 0; k < FAIXAS; k++ ) {
    faixa.topo( (int16_t)( k * ALTURA_FAIXA ) );
    p.janela( (int16_t)( k * ALTURA_FAIXA ), (int16_t)( ( k + 1 ) * ALTURA_FAIXA ) );
    faixa.fillScreen( tema.fundo );
    q.desenhar( p, q.vm );
    const uint32_t h = fnv( cv->getBuffer( ), (size_t)LARGURA_FAIXA * ALTURA_FAIXA * 2 );
    if ( h == s_faixa[k] ) continue;
    blitCanvas( cv, 0, (int16_t)( k * ALTURA_FAIXA ), LARGURA_FAIXA, ALTURA_FAIXA );
    s_faixa[k] = h;
    enviadas++;
  }
  _uaPintando = false;
  _uaTudo = false;
  s_seq = s_seq + 1;
  __dmb( );
  s_nAlvos = q.alvos ? q.alvos( q.vm, s_alvos ) : 0;
  s_modo = modo;
  s_hash = hash;
  s_escuroNaTela = tema.escuro;
  s_temQuadro = true;
  s_pinturas++;
  s_ms = millis( ) - t0;
  if ( s_ms > s_msMax ) s_msMax = s_ms;
  s_faixasEnviadas = enviadas;
  __dmb( );
  s_seq = s_seq + 1;
  return true;
}

int8_t DisplayManager::uaEstado( UaEstado &e ) const {
  /* The writer holds the count odd while it lists the frame's targets, tens
   * of microseconds; 2 ms of retries outlasts it unless Core 1 stalls there. */
  const uint32_t t0 = micros( );
  do {
    const uint32_t antes = s_seq;
    if ( antes & 1u ) { delayMicroseconds( 20 ); continue; }
    __dmb( );
    e.modo = (uint8_t)s_modo;
    e.modoAtual = (uint8_t)_uiMode;
    e.escuro = s_escuroNaTela;
    e.hash = s_hash;
    e.pinturas = s_pinturas;
    e.ms = s_ms;
    e.msMax = s_msMax;
    e.faixas = s_faixasEnviadas;
    /* Single bytes Core 1 writes: a torn read is impossible, a stale one
     * lasts one pass. */
    e.selecionado = (int8_t)_lastRenderedState.selectedSlotIdx;
    e.cima = (int8_t)_lastRenderedState.topSlotIdx;
    e.fixado = _topPanel.fixed && _topPanel.fixedIdx >= 0;
    e.minmaxCima = _topPanel.showMinMax;
    e.minmaxBaixo = _bottomPanel.showMinMax;
    for ( int i = 0; i < 5; i++ ) e.nav[i] = g_uaNav[i];
    e.nAlvos = (uint8_t)s_nAlvos;
    memcpy( e.alvos, s_alvos, sizeof( ua::Alvo ) * (size_t)s_nAlvos );
    __dmb( );
    if ( s_seq == antes ) {
      /* Core 0 writes the package and this runs on Core 0 (the web handler). */
      const bool grafico = e.modo == MODE_GRAPH_VIEW || e.modo == MODE_GRAPH_DETAIL;
      e.janelaInicio = grafico ? (uint32_t)_graphData.tsCutoff : 0;
      e.janelaFim = grafico ? (uint32_t)_graphData.tsEnd : 0;
      return s_temQuadro ? 1 : 0;
    }
  } while ( micros( ) - t0 < 2000u );
  return -1;
}

void DisplayManager::uaQuadro( ) {
  if ( !_driver.tft || !_driver.canvas ) return;
  const uint32_t agora = millis( );
  /* Old draw entry points say when the state changed; a look every 250 ms
   * catches what none of them repaints (a humidity, the automatic theme, the
   * web's busy flag) at the cost of building one view model. */
  if ( !_uaSujo && !_uaTudo && agora - _uaVisto < 250 ) return;
  _uaSujo = false;
  _uaVisto = agora;
  UaQuadro q;
  if ( !uaMontar( q ) ) return;
  const ua::Tema &tema = uaTema( );
  const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );
  uint32_t h = fnv( q.vm, q.tam );
  const uint8_t extra[4] = { (uint8_t)_uiMode, (uint8_t)tema.escuro, (uint8_t)ox, (uint8_t)oy };
  h = fnv( extra, sizeof extra, h );
  static int8_t s_ox = 0, s_oy = 0;
  static bool s_escuro = false;
  if ( ox != s_ox || oy != s_oy || tema.escuro != s_escuro ) _uaTudo = true;
  if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;
  if ( uaPintar( q, _uiMode, h ) ) {
    /* Core 0 waits on this before the boot's AP-by-touch window opens, so it
     * must count frames that are on the glass (AppManager_Boot.cpp:543-546). */
    if ( _lastRenderedState.isBooting && _uiMode == MODE_DASHBOARD ) _bootPaintSeq++;
    s_ox = ox;
    s_oy = oy;
    s_escuro = tema.escuro;
  }
  /* A long draw must not read as a dead core: the health check on Core 0
   * restarts a Core 1 whose stamp is 10 s old (AppManager_Loop.cpp). */
  if ( get_core_num( ) == 1 ) _lastHeartbeat = millis( );
}

/* The update screen, painted at once by whichever core called drawOtaScreen( )
 * (DisplayManager_Ota.cpp), whatever _uiMode says: Core 0 paints it while
 * Core 1 is parked and has not switched the mode yet. Then the counters
 * Core 0 waits on (showOta( )) say it is on the glass. */
void DisplayManager::uaOta( ) {
  const uint32_t st = __atomic_load_n( &_otaState, __ATOMIC_ACQUIRE );
  if ( otaPhaseOf( st ) == OTA_PH_NONE ) return;
  /* A new phase is painted at once: Core 0 waits for it before the steps
   * that freeze the panel (showOta( )). A new percentage alone is painted at
   * most every 400 ms: during the upload Core 0 paints it from otaProgress( ),
   * inside the upload's own loop, and the old screen redrew only the bar
   * (about 3 ms a step) where this one draws the screen's six strips. */
  static uint32_t s_otaSeq = 0xFFFFFFFFu, s_otaMs = 0;
  const uint32_t seq = otaSeqOf( st );
  if ( seq == s_otaSeq && !_otaRedrawAll && !_uaTudo && s_modo == MODE_OTA_UPDATE && millis( ) - s_otaMs < 400 ) {
    __atomic_store_n( &_otaDrawnSeq, seq, __ATOMIC_RELEASE );
    return;
  }
  UaQuadro q;
  if ( !uaMontarOta( q ) ) return;
  const ua::Tema &tema = uaTema( );
  uint32_t h = fnv( q.vm, q.tam );
  const uint8_t extra[2] = { (uint8_t)MODE_OTA_UPDATE, (uint8_t)tema.escuro };
  h = fnv( extra, sizeof extra, h );
  if ( _otaRedrawAll ) _uaTudo = true;
  if ( _uaTudo || !s_temQuadro || h != s_hash || s_modo != MODE_OTA_UPDATE ) {
    uaPintar( q, MODE_OTA_UPDATE, h );
  }
  _otaPctDrawn = __atomic_load_n( &_otaPct, __ATOMIC_ACQUIRE );
  _otaRedrawAll = false;
  s_otaSeq = seq;
  s_otaMs = millis( );
  __atomic_store_n( &_otaDrawnSeq, seq, __ATOMIC_RELEASE );
}

/* The theme changed (Core 0's refreshTheme( )): like the old block, the panel
 * goes to the dashboard, which repaints in full with the new palette. */
void DisplayManager::uaTemaAplicado( ) {
  mutex_enter_blocking( &_stateMutex );
  if ( !_sharedState.isBooting ) _uiMode = MODE_DASHBOARD;
  _forceFullRedraw = true;
  _isDirty = true;
  mutex_exit( &_stateMutex );
  _uaTudo = true;
  uaSujo( );
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontar( UaQuadro &q ) {
  return uaMontarPainel( q ) || uaMontarBase( q ) || uaMontarMenus( q ) || uaMontarAjustes( q ) ||
         uaMontarIdentidade( q ) || uaMontarGraficos( q ) || uaMontarPendente( q );
}

bool DisplayManager::uaToque( int16_t &x, int16_t &y ) {
  /* The glass still shows another screen than the mode (Core 0 changed it
   * between two passes): this touch means nothing yet. */
  if ( !s_temQuadro || s_modo != _uiMode ) { uaSujo( ); return true; }
  const ua::Alvo *a = ua::alvoEm( s_alvos, s_nAlvos, x, y );
  UaToque r = uaToquePainel( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueBase( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueMenus( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueAjustes( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueIdentidade( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueGraficos( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToquePendente( a );
  if ( r == UaToque::TRATADO ) uaSujo( );
  return r == UaToque::TRATADO;
}

/* ------------------------------------------------------------------ */
/* A mode no group has drawn yet                                       */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarPendente( UaQuadro &q ) {
  ua::tela::VmMensagem &vm = uaVm<ua::tela::VmMensagem>( );
  vm.ok = false;
  snprintf( vm.texto, sizeof vm.texto, "Esta tela (modo %d) ainda não existe nesta build.", (int)_uiMode );
  ua::copiar( vm.detalhe, "Volte ao painel e use a web para esta função.", sizeof vm.detalhe );
  ua::copiar( vm.botao, "Voltar ao painel", sizeof vm.botao );
  uaUsar<ua::tela::VmMensagem, ua::tela::desenharMensagem, ua::tela::alvosMensagem>( q, vm );
  return true;
}

UaToque DisplayManager::uaToquePendente( const ua::Alvo *a ) {
  if ( a && a->tipo == ua::tela::MSG_BOTAO && acceptTouch( 0xE0 ) ) forceDashboard( );
  return UaToque::TRATADO;
}

/* ------------------------------------------------------------------ */
/* The base group: result and confirmations (telas/Base.h)             */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarBase( UaQuadro &q ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_PANEL_MESSAGE: {
      VmMensagem &vm = uaVm<VmMensagem>( );
      vm.ok = _msgOk;
      const TextoMsg *t = nullptr;
      for ( const TextoMsg &m : kMensagens )
        if ( m.k == _msgKey ) t = &m;
      if ( t ) {
        ua::copiar( vm.texto, t->texto, sizeof vm.texto );
        ua::copiar( vm.detalhe, t->detalhe, sizeof vm.detalhe );
      } else if ( _msgKey < TR_KEYS_COUNT ) {
        uaLatin1ParaUtf8( tr( _msgKey ), vm.texto, sizeof vm.texto );
      }
      ua::copiar( vm.botao, "Entendi", sizeof vm.botao );
      uaUsar<VmMensagem, desenharMensagem, alvosMensagem>( q, vm );
      return true;
    }
    case MODE_CONFIRM_MUTE_ALL: {
      VmDialogo &vm = uaVm<VmDialogo>( );
      ua::copiar( vm.titulo, "Silenciar todos os sons?", sizeof vm.titulo );
      ua::copiar( vm.texto,
                  "Nenhum som toca, nem o do alarme, até alguém ligar os sons de novo. O mudo só vale "
                  "depois de Salvar na tela de sons.",
                  sizeof vm.texto );
      ua::copiar( vm.nao, "Voltar", sizeof vm.nao );
      ua::copiar( vm.sim, "Silenciar tudo", sizeof vm.sim );
      vm.destrutivo = true;
      uaUsar<VmDialogo, desenharDialogo, alvosDialogo>( q, vm );
      return true;
    }
    case MODE_SETTINGS_USER_CONFIRM_DEL: {
      VmDialogo &vm = uaVm<VmDialogo>( );
      char nome[32] = "";
      if ( _sysConfigPtr && _userEditSlot >= 0 && _userEditSlot < MAX_USERS ) {
        /* UserAccount::username is char[16] and nothing promises its NUL. */
        const char *u = _sysConfigPtr->users[_userEditSlot].username;
        const size_t n = strnlen( u, 16 );
        memcpy( nome, u, n );
        nome[n] = 0;
      }
      snprintf( vm.titulo, sizeof vm.titulo, "Excluir a conta %s?", nome );
      ua::copiar( vm.texto, "A conta perde o acesso pelo painel e pela web. O PIN e a senha dela são apagados.",
                  sizeof vm.texto );
      ua::copiar( vm.nao, "Cancelar", sizeof vm.nao );
      ua::copiar( vm.sim, "Excluir a conta", sizeof vm.sim );
      vm.destrutivo = true;
      uaUsar<VmDialogo, desenharDialogo, alvosDialogo>( q, vm );
      return true;
    }
    default:
      return false;
  }
}

/* All three are translations: the old handlers do what they always did. */
UaToque DisplayManager::uaToqueBase( const ua::Alvo *a, int16_t &x, int16_t &y ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_PANEL_MESSAGE:
      /* Old: any touch with y >= 185 leaves (DisplayManager_Users.cpp). */
      if ( !a ) return UaToque::TRATADO;
      x = 160; y = 205;
      return UaToque::TRADUZIDO;
    case MODE_CONFIRM_MUTE_ALL:
      /* Old: back x 20..150, confirm x 170..300, y 190..230 (Touch.cpp). */
      if ( !a ) return UaToque::TRATADO;
      x = a->tipo == DLG_SIM ? 235 : 85; y = 210;
      return UaToque::TRADUZIDO;
    case MODE_SETTINGS_USER_CONFIRM_DEL:
      /* Old: y >= 195, cancel x < 160, delete x >= 160 (Users.cpp). */
      if ( !a ) return UaToque::TRATADO;
      x = a->tipo == DLG_SIM ? 240 : 80; y = 215;
      return UaToque::TRADUZIDO;
    default:
      return UaToque::NAO_E_MEU;
  }
}

