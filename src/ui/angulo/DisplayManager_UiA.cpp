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
 * current mode. An old draw entry point is UA_DESVIO( ) (src/ui/UiDriver.h):
 * a call to one only asks for a repaint.
 */

#include "../../DisplayManager.h"
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
#include "../../DisplayManager_Actions.h"   /* the core's touch actions, which the confirmations' buttons run; last */

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
    const int32_t seg = falta > 0 ? falta / 1000 : 0;
    s.aviso = ua::Aviso::SILENCIADO;
    s.avisoSeg = (uint16_t)( seg > 65535 ? 65535 : seg );
  } else if ( _webNotifyStartMs > 0 && !timeSince( _webNotifyStartMs, WEB_NOTIFY_DURATION_MS ) ) {
    s.aviso = ua::Aviso::WEB;
    ua::copiar( s.avisoNome, _webNotifyUser, sizeof s.avisoNome );
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
  ua::Faixa faixa( cv->getBuffer( ) );
  ua::Pintor p( faixa, tema, faixa );
  /* The record of the frame (Pintor::gravar( )) lives in the canvas's rows under the strips: the canvas is 320 x 45 (DisplayManager.cpp:1272), the two
   * strips take 40 rows and the other 5, 3,200 B, are idle during a paint. A static list cost 14,856 B of RAM (P1); this costs none. Nothing writes those
   * rows while a frame is painted: the strip class clips to its own 20 rows (Faixa.h), the hash and the push read the strip's 20, and the old screens'
   * draw functions that also use the canvas are not linked in this image (V-NM) or push nothing outside uaPintar( ) (blitCanvas( ) returns
   * unless _uaPintando). If the canvas is not the size relied on, there is no list and every strip draws the screen, as before P1. */
  if ( cv->width( ) == LARGURA_FAIXA && cv->height( ) >= ua::LINHAS_DAS_FAIXAS + ua::LINHAS_DA_LISTA )
    ua::Pintor::lista( cv->getBuffer( ) + LARGURA_FAIXA * ua::LINHAS_DAS_FAIXAS, ua::BYTES_DA_LISTA );
  else
    ua::Pintor::lista( nullptr, 0 );
  const uint32_t t0 = millis( );
  uint8_t enviadas = 0;
  _uaPintando = true;
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

/* The update screen (the hook uiPaintOta), painted at once by whichever core
 * called it (otaTick( ) and otaPaintFromCore0( ), DisplayManager_Ota.cpp),
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

/* Core 1, once, when it has initialised the panel (loopCore1( )): the first
 * frame repaints every strip. Both flags start true and nothing has run since,
 * so this says it again where the original cleared the glass and drew its fixed
 * background (the old fastClearScreen( ) stops at the _uaPintando guard and
 * drawInterfaceFixed( ) at UA_DESVIO( ), which marks the frame dirty). */
void DisplayManager::uiBegin( ) {
  _uaTudo = true;
  uaSujo( );
}

/* The core says something this driver shows has changed (the alarm flash changed phase, the alarm ended, the bottom card
 * turned over, the row of buttons changed): the frame may be different, and the pass looks at it. The old
 * redrawAlarmFlash( ), restoreNormalDashboard( ), drawSlotPanel( ) and drawBottomButtons( ) came to the same through the
 * draw calls they made, each of which starts with UA_DESVIO( ). GRAPH_HEADER and GRAPH_BUSY, the graph's two immediate draws, are raised
 * only where the original's touch code stands: this driver has no header target and shows its busy line from its own model
 * (pedirDados( ), DisplayManager_UiA_Graficos.cpp), so they do not reach it; marking the frame would be the right answer if they did. */
void DisplayManager::uiChanged( UiChange, int16_t ) {
  uaSujo( );
}

/* The top card changed (a short tap, a held press, a slot followed): the frame may be different, and the pass looks at it.
 * The old drawSlotPanel( ) and drawBottomButtons( ) that redrawTopPanel( ) called came to the same through the UA_DESVIO( )
 * each of them starts with. */
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
 * mode by drawing it, which here only asked for a repaint (UA_DESVIO( )); what it did besides, the core's timers and the
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
      /* The boot screen counts _langChanged for Core 0. The frame is painted from _lastRenderedState, which is the
       * snapshot itself: the mirror rules of render( ) are applied by the panel's own model. */
      if ( snap.isBooting ) _langChanged = false;
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

bool DisplayManager::uaToque( int16_t &x, int16_t &y ) {
  /* The glass still shows another screen than the mode (Core 0 changed it
   * between two passes): this touch means nothing yet. */
  if ( !s_temQuadro || s_modo != _uiMode ) { uaSujo( ); return true; }
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
  if ( r == UaToque::TRATADO ) uaSujo( );
  return r == UaToque::TRATADO;
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

/* A touch on the sensitivity screen with enough pressure (handleTouch( ) asks inside its own block): did it land on Cancelar? As the boot's button, the frame says where it is, by its
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

/* No touch here moves a point into the old zones any more: each target runs the core's code behind the gate its old zone held (the
 * zones' numbers are the original's: 0xC0, 0 and 1), the actions of DisplayManager_Actions.h or leavePanelMessage( ). */
UaToque DisplayManager::uaToqueBase( const ua::Alvo *a, int16_t &, int16_t & ) {
  using namespace ua::tela;
  switch ( _uiMode ) {
    case MODE_PANEL_MESSAGE:
      /* "Entendi" leaves for the screen the message came from (leavePanelMessage( ), core code), behind acceptTouch( 0 ), the gate the old
       * zone (y >= 185, DisplayManager_Users.cpp) held. */
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
    default:
      return UaToque::NAO_E_MEU;
  }
}

