/**
 * @file DisplayManager_UiA.cpp
 * @brief The Ângulo build's renderer, theme, touch dispatch and the screens
 *        every group leans on (result, confirmations, the unfinished-screen
 *        placeholder). pico_wiretft_angulo only; see src/ui/angulo/UiA.h and
 *        tools/tft_ui/ESPEC_UI_A.md.
 *
 * The old chain of screens no longer runs in this build: uiPass( ) below runs
 * the core's timers and transitions (DisplayManager_Ticks.h), takes the
 * repaint requests the chain answered by drawing, and paints the frame of the
 * current mode. None of the old draw functions is linked into this image (`nm -C`
 * of it lists none), so none of the old drawing reaches the glass.
 */

#include "../../DisplayManager.h"
#include "../../simut_config.h"   /* SIMUT_NET_WIFI is tested below (the setup-network confirmation, S10c D7) and read by uaVmStatus( ): included here, not left to DisplayManager.h's chain */
#include "../../DisplayManager_Ticks.h"   /* the core's ticks, which uiPass( ) calls */
#include "../../FlashIrqProbe.h"          /* C1_PHASE */
#include "../../sensors/SensorHelpers.h"    /* sensorLimitCount( ) */


#include <stdio.h>
#include <string.h>

#include "Angulo.h"
#include "Faixa.h"
#include "UiA.h"
#include "telas/Base.h"
#include "telas/PainelBoot.h"   /* BOOT_PULAR */
#include "telas/Telas.h"
#include "telas/Ajustes.h"   /* SENSIB_CANCELAR and CALIB_BOTAO: the targets of the two hooks below */
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the confirmations' buttons run; the last project header */
#include "Textos.h"   /* UA_TXT( ): after every project header, because its pragmas hold to the end of the unit (SystemDefs_Records.h:962 stops it before DisplayManager.h) */

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

constexpr int16_t ALTURA_FAIXA = 20;
/* A strip is as wide as the panel (the old screens' canvas is the panel's width by
 * 45 rows, DisplayManager.cpp) and the strips stack to its height. int, as the
 * literals were, so that every expression below keeps its type and so its code. */
constexpr int LARGURA_FAIXA = DisplayDriver::W;
static_assert( DisplayDriver::H % ALTURA_FAIXA == 0, "the strips tile the panel's height" );

/* The strip a screen is composed on is ua::Faixa (Faixa.h): Adafruit_GFX over one of two 20-row strips in the first 40 rows of the old screens' canvas (the panel's width by
 * 45 rows), the screen's coordinates shifted up by the strip's top, clipped to the strip. The host draws through the same class. */
static_assert( LARGURA_FAIXA == ua::LARGURA_PAINEL && DisplayDriver::H == ua::ALTURA_PAINEL && ALTURA_FAIXA == ua::ALTURA_FAIXA,
               "ua::Faixa tiles the panel that DisplayDriver describes" );
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
 * hence the neutral words. The words are looked up when a verdict is drawn and not kept in a table built at start-up: UA_TXT( ) answers from the
 * pack that the boot loads (S10c, B3), so an initialiser would hold the English for good (Textos.h). Each text is bounded where it is copied
 * (ua::copiar( ) into the view model's own buffer), whatever length a pack gives it. */
struct TextoMsg { const char *texto, *detalhe; };
bool textoDaMensagem( LangKey k, TextoMsg &t ) {
  switch ( k ) {
    case TR_NO_PERMISSION:
      t = { UA_TXT( "No permission." ), UA_TXT( "Ask whoever administers the device to allow this function on your account." ) };
      return true;
    case TR_NAME_INVALID:
      t = { UA_TXT( "Could not save the account." ),
            UA_TXT( "The name may be in use or invalid. An account also cannot delete itself." ) };
      return true;
    case TR_USER_SAVED:
      t = { UA_TXT( "Account saved." ), "" };
      return true;
    case TR_USER_DELETED:
      t = { UA_TXT( "Account deleted." ), "" };
      return true;
    case TR_PIN_SAVED:
      t = { UA_TXT( "Change saved." ), "" };
      return true;
    case TR_USERS_FULL:
      // The second line names no number: the device holds MAX_USERS (32) accounts, the admin in slot 0 and EVT_USER_ADD looking for a free slot from 1 to 31. It said 31 until
      // S10c C6b (2026-10-30), and a number written in a text goes stale when the limit moves.
      t = { UA_TXT( "No room for another account." ), UA_TXT( "Delete an account to create another." ) };
      return true;
    case TR_PIN_IN_USE:
      t = { UA_TXT( "This PIN belongs to another account." ), UA_TXT( "Choose another PIN." ) };
      return true;
    case TR_INVALID_PIN:
      t = { UA_TXT( "Invalid PIN." ), UA_TXT( "Check the minimum length and the characters in PIN security." ) };
      return true;
    case TR_ERROR_LBL:
      t = { UA_TXT( "That did not work." ), UA_TXT( "Check the values and try again." ) };
      return true;
    default:
      return false;
  }
}

}  // namespace

/* ------------------------------------------------------------------ */
/* Theme and clock                                                     */
/* ------------------------------------------------------------------ */

/* cfg.themeIndex holds the mode and the accent together; ua::escolhaDoIndice( ) decodes it and is the only code that knows how
 * (S10c, A'2). The mode is light, dark or automatic (dark from 19:00 to 6:59 by the panel's clock; light while there is no clock),
 * and the accent is the palette entry whose tables the mode takes. The values 0, 1 and 2 are the three modes with the stock
 * accent, as before; a value outside the palette means light with the stock accent. Core 1 reads only this byte and const
 * tables (ua::tema( ) hands out the kit's, which the compiler built and flash holds): no copy of a theme is shared between the cores. The one thing that is not the saved
 * theme is the Tema screen's preview (_uaPrevia, set by its taps and Core 1's alone): the frame of MODE_SETTINGS_THEMES is painted with it and the frame of any
 * other mode, the update screen included, with the saved theme. `modo` is the mode of the frame being painted and not _uiMode: Core 0 paints the update
 * screen while _uiMode may still say Tema. */
const ua::Tema &DisplayManager::uaTema( UiMode modo ) {
  const ua::Escolha e = ua::escolhaDoIndice( ( modo == MODE_SETTINGS_THEMES && _uaPrevia >= 0 ) ? _uaPrevia : _sysConfigPtr ? _sysConfigPtr->themeIndex : 0 );
  bool escuro = e.modo == ua::MODO_ESCURO;
  if ( e.modo == ua::MODO_AUTOMATICO ) {
    char hora[6];
    int h = -1;
    bool provisoria = false;
    escuro = uaRelogio( hora, h, provisoria ) && ( h >= 19 || h < 7 );
  }
  return ua::tema( escuro, e.acento );
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
    const int32_t seg = falta > 0 ? falta / 1000 : 0;
    s.aviso = ua::Aviso::SILENCIADO;
    s.avisoSeg = (uint16_t)( seg > 65535 ? 65535 : seg );
  } else if ( _webNotifyStartMs > 0 && !timeSince( _webNotifyStartMs, WEB_NOTIFY_DURATION_MS ) ) {
    s.aviso = ua::Aviso::WEB;
    ua::copiar( s.avisoNome, _webNotifyUser, sizeof s.avisoNome );
  }
  /* The network glyph, from the level Core 0 publishes (NetworkManager::getRssi( ) through setSystemStatus( )): the radio's dBm while the link is up, -100 when it is not (the shared state starts
   * there too, and so does the setup access point, which is not a station link), and on a cable a stand-in above -100 while the link is up (NetworkManager_Wired.cpp). The old top bar reads the same
   * number (DisplayManager::drawTopBar( ), DisplayManager_Dashboard.cpp), and this says the same things with Lucide's own icons:
   *  - a cable image: above -100 the cable is up, at -100 or less it has no link (the old bar's green cable and its grey one; here ethernet-port and unplug, both in the neutral grey);
   *  - a Wi-Fi image: the old bar's four levels at the old bar's own thresholds, one bar at -75 or less, two at -65 or less, three at -55 or less, four above (its ladder under `> -100`), and no signal
   *    at -100 or less (it draws no bar). tools/tft_ui/test_rede.py reads that ladder out of the old bar and holds these to it. The icons are Lucide's signal bars, signal-low, signal-medium, signal-high and signal for one to four bars, and wifi-off for no signal (telas/Telas.cpp).
   *    The one-bar icon keeps the alert colour: RSSI_MIN_THRESHOLD (-78, SystemDefs_Network.h) falls inside that level, and from there down the device holds telemetry
   *    and the heavy sends. No signal is drawn in the bar's neutral ink and not in the danger colour, and the cable without link the same, with an icon of its own ("cinza nos dois", "icone proprio"): a link that is down is not an error (ANGULO.md, rule 1: colour is for what calls attention).
   * The old bar has no glyph for the setup access point (apMode is read only by the alphanumeric display) and none for "no network", so Rede::NENHUMA is never answered. The image's network is read
   * as a value, SIMUT_NET_WIFI in a constant expression, and not by an `#if`: a macro that is not defined does not compile here and is not a silent 0. It is still a site for check_feature_sprawl.py,
   * which counts an `if constexpr` of a feature macro as it counts an `#if` (feature_sprawl.json: this file holds two of SIMUT_NET_WIFI, this one and the setup-network confirmation's, below). */
  constexpr int SEM_SINAL = -100;
  constexpr int ATE_1_BARRA = -75;
  constexpr int ATE_2_BARRAS = -65;
  constexpr int ATE_3_BARRAS = -55;
  const int nivel = _lastRenderedState.wifiRssi;
  if constexpr ( SIMUT_NET_WIFI )
    s.rede = nivel <= SEM_SINAL ? ua::Rede::WIFI_SEM
           : nivel <= ATE_1_BARRA ? ua::Rede::WIFI_1
           : nivel <= ATE_2_BARRAS ? ua::Rede::WIFI_2
           : nivel <= ATE_3_BARRAS ? ua::Rede::WIFI_3
           : ua::Rede::WIFI_4;
  else
    s.rede = nivel > SEM_SINAL ? ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;
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
  const ua::Tema &tema = uaTema( modo );
  ua::Faixa faixa( cv->getBuffer( ) );
  ua::Pintor p( faixa, tema, faixa );
  /* The record of the frame (Pintor::gravar( )) lives in the canvas's rows under the strips: the canvas is 320 x 45 (allocated in DisplayManager.cpp), the two
   * strips take 40 rows and the other 5, 3,200 B, are idle during a paint. A static list cost 14,856 B of RAM (P1); this costs none. Nothing writes those
   * rows while a frame is painted: the strip class clips to its own 20 rows (Faixa.h), the hash and the push read the strip's 20, and the old screens'
   * draw functions that also use the canvas are not linked in this image (V-NM), nor is blitCanvas( ), the core's push of a canvas
   * (`nm -C` lists neither). If the canvas is not the size relied on, there is no list and every strip draws the screen, as before P1. */
  if ( cv->width( ) == LARGURA_FAIXA && cv->height( ) >= ua::LINHAS_DAS_FAIXAS + ua::LINHAS_DA_LISTA )
    ua::Pintor::lista( cv->getBuffer( ) + LARGURA_FAIXA * ua::LINHAS_DAS_FAIXAS, ua::BYTES_DA_LISTA );
  else
    ua::Pintor::lista( nullptr, 0 );
  const uint32_t t0 = millis( );
  uint8_t enviadas = 0;
  const bool tudo = _uaTudo;
  if ( tudo ) {
    /* A full repaint does not clear the glass first any more: fastClearScreen( ) was 56 ms of black at 24 MHz before the first strip (measured on
     * the bench on 2026-10-15). The strips cover the panel's 320 x 240, each paints the margin black itself (ua::margemPreta( ) below), and
     * all of them are pushed whatever their hash says (the test below), so no pixel of the old frame is left under them. What they do not reach is
     * the band the alignment offset uncovers: TftWithOffset shifts the image by (ox, oy), so |ox| columns and |oy| rows of the glass lie outside
     * it. That band is black, as fastClearScreen( ) left it (it ended in fillMarginsBlack( )), and fillMarginsBlack( ) paints exactly it, at the
     * offset the TFT has now: nothing at 0. When Ajustes, Alinhar moves the offset, the old band is covered by the new strips and the new one
     * is blackened here, before them. */
    _driver.tft->fillMarginsBlack( );
    for ( int k = 0; k < FAIXAS; k++ ) s_faixa[k] = 0;
  }
  p.gravar( );
  /* The strips are composed two at a time in the canvas's two halves (Faixa::para( )): strip k + 1 is composed while the DMA streams strip k from the other
   * half, so compose and push overlap instead of adding up. `voando` is the half a push in flight is reading. */
  const uint16_t *voando = nullptr;
  for ( int k = 0; k < FAIXAS; k++ ) {
    uint16_t *const buf = faixa.para( k, p );
    /* Strip k - 2 was composed in this half; if strip k - 1 was not pushed (its hash had not changed), the push of k - 2 may still be reading it. */
    if ( voando == buf ) {
      uaEmpurrarEsperar( );
      voando = nullptr;
    }
    faixa.fillScreen( tema.fundo );
    /* The first strip draws the screen and records what it draws; the other strips replay the record (Pintor::gravar( )), and draw the screen
     * again only when the frame did not fit the record. */
    if ( p.gravado( ) ) p.repetir( ); else q.desenhar( p, q.vm );
    p.parar( );
    /* After the screen, so that nothing it drew reaches the margin, and before the hash: a strip is pushed when its pixels changed,
     * the margin's included. */
    ua::margemPreta( p );
    const uint32_t h = ua::hashFaixa( buf, (size_t)LARGURA_FAIXA * ALTURA_FAIXA );
    if ( !tudo && h == s_faixa[k] ) continue;
    /* Starts the push (after the one in flight: one channel, one bus) and goes on composing. */
    voando = uaEmpurrar( buf, 0, (int16_t)( k * ALTURA_FAIXA ), LARGURA_FAIXA, ALTURA_FAIXA ) ? buf : nullptr;
    s_faixa[k] = h;
    enviadas++;
  }
  /* uaPintar( ) returns with no DMA in flight and spi0 free: the touch read, the GRAM readback and the update screen all rely on it (uaEmpurrar( ), DisplayManager_UiA_Empurrar.cpp). */
  uaEmpurrarEsperar( );
  /* The canvas's spare rows go back: nothing records outside a paint, and no pointer into the canvas outlives it. */
  ua::Pintor::lista( nullptr, 0 );
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
  /* The hooks and the ticks say when the state changed (uaSujo( )); a look every 250 ms
   * catches what none of them says (a humidity, the automatic theme, the
   * web's busy flag) at the cost of building one view model. */
  /* The preview lives with the Tema screen: any other mode, reached by Back, by the timeout or by the web, drops it, and the screen opens on the saved theme. */
  if ( _uiMode != MODE_SETTINGS_THEMES ) _uaPrevia = -1;
  if ( !_uaSujo && !_uaTudo && agora - _uaVisto < 250 ) return;
  _uaSujo = false;
  _uaVisto = agora;
  UaQuadro q;
  if ( !uaMontar( q ) ) return;
  const ua::Tema &tema = uaTema( _uiMode );
  const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );
  uint32_t h = fnv( q.vm, q.tam );
  /* The frame's identity: the screen, the theme (the mode AND the accent: a web or CLI change of the accent alone changes no
   * view model, so it has to be here) and the alignment. A change of theme or alignment repaints everything. */
  const ua::IdentidadeDoQuadro id = ua::identidadeDoQuadro( (uint8_t)_uiMode, tema, ox, oy );
  h = fnv( &id, sizeof id, h );
  static ua::IdentidadeDoQuadro s_id = {};
  if ( id.repintaTudo( s_id ) ) _uaTudo = true;
  if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;
  if ( uaPintar( q, _uiMode, h ) ) {
    /* Core 0 waits on this before the boot's AP-by-touch window opens, so it
     * must count frames that are on the glass (AppManager_Boot.cpp:543-546). */
    if ( _lastRenderedState.isBooting && _uiMode == MODE_DASHBOARD ) _bootPaintSeq++;
    s_id = id;
  }
  /* A long draw must not read as a dead core: the health check on Core 0
   * restarts a Core 1 whose stamp is 10 s old (AppManager_Loop.cpp). */
  if ( get_core_num( ) == 1 ) _lastHeartbeat = millis( );
}

/* The update screen (the hook uiPaintOta), painted at once by whichever core
 * called it (otaTick( ) and otaPaintFromCore0( ), DisplayManager_OtaCore.cpp),
 * whatever _uiMode says: Core 0 paints it while Core 1 is parked and has not
 * switched the mode yet. Then the counters Core 0 waits on (showOta( )) say it
 * is on the glass. */
void DisplayManager::uiPaintOta( ) {
  const uint32_t st = __atomic_load_n( &_otaState, __ATOMIC_ACQUIRE );
  if ( otaPhaseOf( st ) == OTA_PH_NONE ) return;
  /* A new phase is painted at once: Core 0 waits for it before the steps
   * that freeze the panel (showOta( )). A new percentage alone is painted at
   * most every 400 ms: during the upload Core 0 paints it from otaProgress( ),
   * inside the upload's own loop, and the old screen redrew only the bar
   * (about 3 ms a step) where this one draws the screen's twelve strips. */
  static uint32_t s_otaSeq = 0xFFFFFFFFu, s_otaMs = 0;
  const uint32_t seq = otaSeqOf( st );
  if ( seq == s_otaSeq && !_otaRedrawAll && !_uaTudo && s_modo == MODE_OTA_UPDATE && millis( ) - s_otaMs < 400 ) {
    __atomic_store_n( &_otaDrawnSeq, seq, __ATOMIC_RELEASE );
    return;
  }
  UaQuadro q;
  if ( !uaMontarOta( q ) ) return;
  const ua::Tema &tema = uaTema( MODE_OTA_UPDATE );
  uint32_t h = fnv( q.vm, q.tam );
  /* The update screen has never hashed the display alignment (it cannot change while the update is on): ox and oy are 0 here. */
  const ua::IdentidadeDoQuadro id = ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, tema, 0, 0 );
  h = fnv( &id, sizeof id, h );
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

/* Core 1, once, when it has initialised the panel (loopCore1( )): the first
 * frame repaints every strip. Both flags start true and nothing has run since,
 * so this says it again where the original cleared the glass and drew its fixed
 * background (its uiBegin( ), UiOriginal.h: fastClearScreen( ) and
 * drawInterfaceFixed( ); neither is linked into this image). */
void DisplayManager::uiBegin( ) {
  _uaTudo = true;
  uaSujo( );
}

/* The core says something this driver shows has changed (the alarm flash changed phase, the alarm ended, the bottom card
 * turned over, the row of buttons changed): the frame may be different, and the pass looks at it. The original
 * answers by drawing (redrawAlarmFlash( ), restoreNormalDashboard( ), drawSlotPanel( ) and drawBottomButtons( ), UiOriginal.h);
 * none of them is linked into this image. GRAPH_HEADER and GRAPH_BUSY, the graph's two immediate draws, are raised
 * only where the original's touch code stands: this driver has no header target and shows its busy line from its own model
 * (pedirDados( ), DisplayManager_UiA_Graficos.cpp), so they do not reach it; marking the frame would be the right answer if they did.
 * TOUCH_CAL_ENTER and DISPLAY_OFFSET_ENTER are raised on CORE 0, by showTouchCalibration( ) and showSettingsDisplayOffset( ), where the others come from
 * Core 1: the original answers the first by resetting the draw caches of its two calibration screens (resetTouchCalCaches( )) and the second by setting the
 * sentinels of its offset screen (UiOriginal.h), and this driver keeps neither. Marking the frame is redundant for them and harmless: the same entries
 * raise _forceSettingsRedraw and _repaintSettings, and the pass below turns those into uaSujo( ) itself. An answer to a kind raised on Core 0 must
 * not draw, because the glass belongs to Core 1; DISPLAY_OFFSET_ENTER is raised inside _stateMutex, so an answer to it must not take the mutex. */
void DisplayManager::uiChanged( UiChange, int16_t ) {
  uaSujo( );
}

/* The top card changed (a short tap, a held press, a slot followed): the frame may be different, and the pass looks at it.
 * The original draws the two panels here, with drawSlotPanel( ) and drawBottomButtons( ) (UiOriginal.h); neither is linked into
 * this image. */
void DisplayManager::uiTopPanel( const SystemState & ) {
  uaSujo( );
}

/* The web login notice ("Web: <user>") lasts WEB_NOTIFY_DURATION_MS. The old top bar cleared it when it found it
 * expired, as it drew; this frame only reads it (uaVmStatus), so the pass clears it, and a notice left behind cannot come back
 * when millis( ) wraps. */
void DisplayManager::tickWebNotify( ) {
  if ( _webNotifyStartMs > 0 && timeSince( _webNotifyStartMs, WEB_NOTIFY_DURATION_MS ) ) {
    _webNotifyStartMs = 0;
    _webNotifyUser[0] = '\0';
  }
}

/* The panels are in min/max mode and nobody has touched the screen for MINMAX_IDLE_MS: they go back to normal mode. The
 * original does the same in render( ) and draws the return; here the frame follows the flags. */
void DisplayManager::tickMinMaxIdle( ) {
  if ( !minMaxIdleDue( ) ) return;
  _topPanel.showMinMax = false;
  _bottomPanel.showMinMax = false;
  uaSujo( );
}

/* The Ângulo's pass of Core 1, after the theme change and the update screen (loopCore1( )). The old chain answered each
 * mode by drawing it, which the frame below replaces; what it did besides, the core's timers and the
 * state it keeps, is what this runs: the dashboard's timers and the snapshot, the PIN screen's lockout, the sensitivity
 * screen's move on to the calibration, the limits of the alarm being edited, the web notice, and the repaint requests
 * taken. Then the frame, once, from the state, and the packet arrow's flash. */
void DisplayManager::uiPass( SystemState &snap ) {
  if ( _uiMode == MODE_DASHBOARD ) {
    tickDashboard( );
    tickAlarmFlash( );
    tickMinMaxIdle( );
    C1_PHASE( C1P_SNAPSHOT );
    if ( pullSnapshot( snap ) ) {
      /* The frame is painted from _lastRenderedState, which is the snapshot itself: the mirror rules of render( ) are applied by the
       * panel's own model. setLanguage( ) (Core 0, right after uiLangLoaded( ) published the texts) raises _langChanged, and the original's boot
       * render answers it with a full redraw, to retranslate the status lines it drew in English before the pack loaded (DisplayManager_Render.cpp:
       * 176-190; until then tr( ) gives DICTIONARY_EN, DisplayManager_i18n.cpp:148-163). The Ângulo says the same: the hash of a frame covers its view
       * model, whose status lines are tr( ) text, and not the texts UA_TXT( ) looks up when the frame is drawn, so the flag repaints every strip (S10c, B3). */
      if ( snap.isBooting && _langChanged ) {
        _langChanged = false;
        _uaTudo = true;
      }
      _lastRenderedState = snap;
      uaSujo( );
    }
  } else if ( _uiMode == MODE_AUTH ) {
    tickAuthLockout( );
  } else if ( _uiMode == MODE_SETTINGS_TOUCH_SENS ) {
    tickTouchSens( );
  } else if ( _uiMode == MODE_SETTINGS_ALARM_EDIT && _repaintSettings ) {
    /* The old draw pulled MIN below MAX and kept the focus inside the sensor's limit list before it drew, and it drew on a
     * repaint request only: showAlarmEdit( ) publishes the mode before it copies the record and raises the request last, so
     * an ungated repair could land inside the copy. The request is taken below, after this. */
    const SensorType sType = (SensorType)_tempAlarmConfig.sensorType;
    const uint8_t nLimits = sensorLimitCount( sType );
    if ( nLimits != 0 ) {
      repairAlarmLimits( sType, nLimits );
      clampEditFocus( nLimits );
    }
  }
  tickWebNotify( );
  /* The requests the old chain answered by drawing. Each is taken before the frame is painted, so one that lands while
   * it is painted survives to the next pass. */
  bool pedido = false;
  if ( _repaintSettings ) { _repaintSettings = false; pedido = true; }
  if ( _repaintGraph ) { _repaintGraph = false; pedido = true; }
  if ( _repaintLoading ) { _repaintLoading = false; pedido = true; }
  if ( _repaintCalendar ) { _repaintCalendar = false; pedido = true; }
  if ( _alarmStatusDirty ) { _alarmStatusDirty = false; pedido = true; }
  if ( _forceSettingsRedraw ) { _forceSettingsRedraw = false; pedido = true; }
  if ( pedido ) uaSujo( );
  C1_PHASE( C1P_RENDER );
  uaQuadro( );
  /* The packet arrow's flash ends after a second: the state goes back to 1, and the next frame shows it. */
  if ( _pktArrowState == 3 ) {
    pktArrowShown( );
    if ( _pktArrowState != 3 ) uaSujo( );
  }
}

/* The theme changed (Core 0's refreshTheme( )): like the original's hook (ui/original/UiOriginal.h), the panel
 * goes to the dashboard, which repaints in full with the new palette. */
void DisplayManager::uiThemeChanged( ) {
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

void DisplayManager::uiTouch( int16_t x, int16_t y ) {
  /* The glass still shows another screen than the mode (Core 0 changed it
   * between two passes): this touch means nothing yet. */
  if ( !s_temQuadro || s_modo != _uiMode ) { uaSujo( ); return; }
  /* A control drawn disabled is an inert target (ua::ALVO_INERTE): alvoDoToque( ) hands the handlers nullptr for it, the touch
   * on no target that each of them already answers with nothing: no gate (so no touch sound, as the original's dead zones),
   * no busy line, no action. Taking it here and not in the switches keeps a screen from forgetting it, and leaves the
   * handlers that read "no target" as something to do (the PIN keypad closes its popup) doing it, as for any dead point. */
  const ua::Alvo *a = ua::alvoDoToque( s_alvos, s_nAlvos, x, y );
  UaToque r = uaToquePainel( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueBase( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueMenus( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueAjustes( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueIdentidade( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToqueGraficos( a, x, y );
  if ( r == UaToque::NAO_E_MEU ) r = uaToquePendente( a );
  /* By here every mode has an answer (uaToquePendente( ) takes the ones no group has): the touch is taken, and the original's chain is not in
   * this build. The frame is marked dirty only when the touch may have changed it (TRATADO); the update screen's does not (TRATADO_SEM_QUADRO). */
  if ( r == UaToque::TRATADO ) uaSujo( );
}

/* A touch while the device boots with the skip button showing (handleTouch( ) asks before the intercept above): did it land on
 * the button of the boot frame? The frame says where the button is, by its target, so the old rectangle (y > 190, x 80..240) is
 * not a rule the frame has to keep. Target types are numbered per screen (BOOT_PULAR is 1, and so are the calibration's
 * CALIB_BOTAO and the clock entry's RELOG_MAIS), so the frame on the glass has to be the boot's, and isBooting does not say
 * so: render( ) runs in MODE_DASHBOARD alone, so _lastRenderedState keeps isBooting while a boot opens another screen over
 * the log, and the glass shows that screen until the next pass paints a frame. s_modo is the mode the frame on the glass was
 * painted for (uaPintar( ) sets it together with the targets) and the boot frame is built for MODE_DASHBOARD only
 * (uaMontarPainel( )): with both, the targets listed are the boot's. */
bool DisplayManager::uiBootSkipHit( int16_t x, int16_t y ) {
  if ( !s_temQuadro || s_modo != MODE_DASHBOARD || !_lastRenderedState.isBooting ) return false;
  const ua::Alvo *a = ua::alvoEm( s_alvos, s_nAlvos, x, y );
  return a && a->tipo == ua::tela::BOOT_PULAR;
}

/* A touch on the sensitivity screen with enough pressure, or an injected one (handleTouch( ) asks inside its gate, p.z >= 50 || simNow): did it land on Cancelar? As the boot's button, the frame says where it is, by its
 * target; the frame on the glass has to be the sensitivity's (the types are numbered per screen: SENSIB_CANCELAR is 1, and so are BOOT_PULAR and RELOG_MAIS), which s_modo says. A tap
 * in the band of the old zone beside the button is a sample like any other touch. */
bool DisplayManager::uiSensCancelHit( int16_t x, int16_t y ) {
  if ( !s_temQuadro || s_modo != MODE_SETTINGS_TOUCH_SENS ) return false;
  const ua::Alvo *a = ua::alvoEm( s_alvos, s_nAlvos, x, y );
  return a && a->tipo == ua::tela::SENSIB_CANCELAR;
}

/* A touch on the calibration (handleTouch( ) asks once a verdict is on the screen): did it land on the verdict's button? The frame that measures has no target, so it never answers;
 * the mode test is as above (CALIB_BOTAO is 1 too). */
bool DisplayManager::uiCalVerdictHit( int16_t x, int16_t y ) {
  if ( !s_temQuadro || s_modo != MODE_SETTINGS_TOUCH_CAL ) return false;
  const ua::Alvo *a = ua::alvoEm( s_alvos, s_nAlvos, x, y );
  return a && a->tipo == ua::tela::CALIB_BOTAO;
}

/* ------------------------------------------------------------------ */
/* A mode no group has drawn yet                                       */
/* ------------------------------------------------------------------ */

bool DisplayManager::uaMontarPendente( UaQuadro &q ) {
  ua::tela::VmMensagem &vm = uaVm<ua::tela::VmMensagem>( );
  vm.ok = false;
  /* The mode's number sits inside the sentence: three texts and a literal format around the number (Textos.h: a looked-up text is never a format). */
  snprintf( vm.texto, sizeof vm.texto, "%s (%s %d) %s", UA_TXT( "This screen" ), UA_TXT( "mode" ), (int)_uiMode,
            UA_TXT( "is not in this build yet." ) );
  ua::copiar( vm.detalhe, UA_TXT( "Go back to the panel and use the web for this function." ), sizeof vm.detalhe );
  ua::copiar( vm.botao, UA_TXT( "Back to the panel" ), sizeof vm.botao );
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
      TextoMsg t;
      if ( textoDaMensagem( _msgKey, t ) ) {
        ua::copiar( vm.texto, t.texto, sizeof vm.texto );
        ua::copiar( vm.detalhe, t.detalhe, sizeof vm.detalhe );
      } else if ( _msgKey < TR_KEYS_COUNT ) {
        uaLatin1ParaUtf8( tr( _msgKey ), vm.texto, sizeof vm.texto );
      }
      ua::copiar( vm.botao, UA_TXT( "Got it" ), sizeof vm.botao );
      uaUsar<VmMensagem, desenharMensagem, alvosMensagem>( q, vm );
      return true;
    }
    case MODE_CONFIRM_MUTE_ALL: {
      VmDialogo &vm = uaVm<VmDialogo>( );
      ua::copiar( vm.titulo, UA_TXT( "Mute all sounds?" ), sizeof vm.titulo );
      ua::copiar( vm.texto,
                  UA_TXT( "No sound plays, not even the alarm's, until someone turns the sounds back on. Mute only takes effect "
                          "after you press Save on the sounds screen." ),
                  sizeof vm.texto );
      ua::copiar( vm.nao, UA_TXT( "Back" ), sizeof vm.nao );
      ua::copiar( vm.sim, UA_TXT( "Mute all" ), sizeof vm.sim );
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
      /* The name closes the question, so a pack's text for "Delete the account" is the words before the name and no "?": the literal format
       * adds it after the name (a translation "¿Eliminar la cuenta?" draws "¿Eliminar la cuenta? maria?"). A language that opens a question
       * writes its mark at the start of the text, as es-ES does: "¿Eliminar la cuenta". */
      snprintf( vm.titulo, sizeof vm.titulo, "%s %s?", UA_TXT( "Delete the account" ), nome );
      ua::copiar( vm.texto, UA_TXT( "The account loses access from the panel and the web. Its PIN and password are erased." ),
                  sizeof vm.texto );
      ua::copiar( vm.nao, UA_TXT( "Cancel" ), sizeof vm.nao );
      ua::copiar( vm.sim, UA_TXT( "Delete account" ), sizeof vm.sim );
      vm.destrutivo = true;
      uaUsar<VmDialogo, desenharDialogo, alvosDialogo>( q, vm );
      return true;
    }
    case MODE_CONFIRM_AP:
      /* The setup network's confirmation (S10c, D7), the dialog's third user: uaMontarConfirmaAp( ), below. A Wi-Fi image's frame; on the Wire the member is a stub that answers
       * false, so the mode keeps the placeholder it had there (and the Wire cannot reach it). */
      return uaMontarConfirmaAp( q );
    default:
      return false;
  }
}

/* No touch here moves a point into the old zones any more: each target runs the core's code behind the gate its old zone held (the
 * zones' numbers are the original's: 0xC0, 0 and 1), the actions of DisplayManager_Actions.h or leavePanelMessage( ). */
UaToque DisplayManager::uaToqueBase( const ua::Alvo *a, int16_t &, int16_t & ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_PANEL_MESSAGE:
      /* "Entendi" leaves for the screen the message came from (leavePanelMessage( ), core code), behind acceptTouch( 0 ), the gate the old
       * zone (y >= 185, DisplayManager_UsersUi.cpp) held. */
      if ( !a ) return UaToque::TRATADO;
      if ( a->tipo == MSG_BOTAO && acceptTouch( 0 ) ) leavePanelMessage( );
      return UaToque::TRATADO;
    case MODE_CONFIRM_MUTE_ALL:
      /* Confirm holds its own gate, a hand-written one with no touch click; Back takes the gate its old zone held, acceptTouch( 0xC0 )
       * (Touch.cpp). Any target but Confirm is Back, as it was when the point was moved. */
      if ( !a ) return UaToque::TRATADO;
      if ( a->tipo == DLG_SIM ) actMuteAllConfirm( );
      else if ( acceptTouch( 0xC0 ) ) actMuteAllBack( );
      return UaToque::TRATADO;
    case MODE_SETTINGS_USER_CONFIRM_DEL:
      /* The account is deleted only from the dialog's confirm target (DLG_SIM), behind acceptTouch( 1 ); every other target is Cancel,
       * behind acceptTouch( 0 ), as when the point was moved into the old zones. Whether the session may delete is Core 0's to say
       * (PERM_USER_MGR, AppManager_Panel.cpp): the panel only asks. */
      if ( !a ) return UaToque::TRATADO;
      if ( a->tipo == DLG_SIM ) {
        if ( acceptTouch( 1 ) ) actUserDelConfirm( );
      } else if ( acceptTouch( 0 ) ) {
        actUserDelCancel( );
      }
      return UaToque::TRATADO;
    case MODE_CONFIRM_AP:
      return uaToqueConfirmaAp( a );
    default:
      return UaToque::NAO_E_MEU;
  }
}

/* The setup network's confirmation (MODE_CONFIRM_AP), the dialog's third user after the two above (S10c, D7). Only a Wi-Fi image reaches the mode: MENU_SETUP_AP is not in the wired
 * build's menu (telas/MenusConfig.h:30) and showApConfirm( ) exists only under SIMUT_NET_WIFI (DisplayManager.h:672). So the one `#if` of this file on the network class carries
 * both members, and the wired Ângulo image gets the stubs of the #else, which claim nothing: it keeps neither the texts nor the code (the sprawl watermark 364 -> 365, recorded in
 * tools/features.toml next to D2's). The body says only what changes: the unit leaves its station for its own setup network, and the telemetry stops until it rejoins, 15 minutes later
 * (AP_MODE_TIMEOUT_MS, NetworkManager_Cyw43.cpp:331: with a network configured). It says nothing about sensors, because the loop keeps measuring and alarming in AP mode (6933dcb3).
 * Touch: Confirm sends the event (actStartAp( ), EVT_START_AP) behind the zone the original's chain held, acceptTouch( 0xC3 ); Core 0 checks PERM_NET_CONFIG before startApMode( )
 * (AppManager_Events.cpp:368-374). Every other target is Cancel, behind acceptTouch( 0xC2 ): back to the Settings list with showSettingsMain( ), core code, as in ui/original/UiTouchChain.h. */
#if SIMUT_NET_WIFI
bool DisplayManager::uaMontarConfirmaAp( UaQuadro &q ) {
  using namespace ua::tela;
  VmDialogo &vm = uaVm<VmDialogo>( );
  ua::copiar( vm.titulo, UA_TXT( "Open the setup network?" ), sizeof vm.titulo );
  ua::copiar( vm.texto,
              UA_TXT( "The device leaves its Wi-Fi network and opens its own setup network. Telemetry stops until it rejoins, "
                      "15 minutes later." ),
              sizeof vm.texto );
  ua::copiar( vm.nao, UA_TXT( "Cancel" ), sizeof vm.nao );
  ua::copiar( vm.sim, UA_TXT( "Start setup" ), sizeof vm.sim );
  vm.destrutivo = true;
  uaUsar<VmDialogo, desenharDialogo, alvosDialogo>( q, vm );
  return true;
}

UaToque DisplayManager::uaToqueConfirmaAp( const ua::Alvo *a ) {
  using namespace ua::tela;
  if ( !a ) return UaToque::TRATADO;
  if ( a->tipo == DLG_SIM ) {
    if ( acceptTouch( 0xC3 ) ) actStartAp( );
  } else if ( acceptTouch( 0xC2 ) ) {
    showSettingsMain( );
  }
  return UaToque::TRATADO;
}
#else
bool DisplayManager::uaMontarConfirmaAp( UaQuadro & ) { return false; }
UaToque DisplayManager::uaToqueConfirmaAp( const ua::Alvo * ) { return UaToque::NAO_E_MEU; }
#endif

