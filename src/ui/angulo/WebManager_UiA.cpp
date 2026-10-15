/**
 * @file WebManager_UiA.cpp
 * @brief GET /api/ui: what the Ângulo build has on the panel.
 *
 * @details Part of the Ângulo UI driver (transitional, bench image
 * pico_wiretft_angulo; platformio.ini): no product image compiles this unit.
 * The bench reads which mode's frame is on the glass, its palette, how long
 * the last paint took, and the touch targets of that frame, so a script
 * touches the centre of what is drawn (POST /api/touch) instead of
 * coordinates copied from a layout that may have moved.
 *
 * The permission is the screenshot's: the targets say as much about the
 * screen as its picture does.
 */

#include "WebManager.h"

#include "../../DisplayManager.h"

void WebManager::registerUiARoutes( ) {
  _server->on( "/api/ui", HTTP_GET, std::bind( &WebManager::handleApiUiA, this ) );
}

// GET /api/ui ->
//   {"modo":5,"modo_atual":5,"tema":"claro","hash":"1a2b3c4d","pinturas":812,
//    "ms":94,"ms_max":431,"faixas":2,"painel":{"sel":1,"cima":1,"fixado":false,
//    "minmax":[0,0],"nav":[0,1,2,3,-2]},"janela":[0,0],"agora":1791999999,
//    "alvos":[[12,36,296,40,2,0],...]}
//
// "modo" is the UiMode whose frame is on the glass and "modo_atual" the one the
// panel is in now: they differ for one pass after Core 0 changes the screen.
// "painel" is the dashboard's state the panel saves (the selected slot, the top
// card's, whether it is pinned, the two min/max modes, the slot behind each
// button: -1 none, -2 settings, -3 page), for the bench to put back.
// "janela" is the chart's window [start, end] in epoch seconds while the frame
// is the chart or its detail ([0,0] otherwise) and "agora" the device's clock:
// a chart opened from another screen must end now.
// Each target is [x, y, w, h, tipo, indice] in the screen's coordinates; tipo
// and indice mean what the screen's enum says (src/ui/angulo/telas/*.h).
void WebManager::handleApiUiA( ) {
  if ( !requirePerm( PERM_SYS_CONFIG ) ) return;

  UaEstado e;
  const int8_t r = _displayRef ? _displayRef->uaEstado( e ) : 0;
  if ( r <= 0 ) {
    _server->sendHeader( "Retry-After", "1" );
    _server->send( 503, "application/json",
                   r < 0 ? "{\"erro\":\"o painel estava sendo redesenhado; tente de novo\"}"
                         : "{\"erro\":\"nenhum quadro na tela ainda\"}" );
    return;
  }

  // One buffer and one write: a chunk per target was up to 49 writes, the
  // pattern that starved lwIP's heap in #189. The fixed part is at most 298 B
  // (174 B of text, the counters and the clock at ten digits, the panel's nine
  // numbers at four, the modes at three; counted from the format 2026-10-14)
  // and a target at most 41 B: 298 + 48 x 41 + 2 + the NUL = 2269 B. Static,
  // because Core 0's stack has 8 KB and one handler runs at a time.
  static char s_json[2304];
  size_t n = (size_t)snprintf( s_json, sizeof s_json,
            "{\"modo\":%u,\"modo_atual\":%u,\"tema\":\"%s\",\"hash\":\"%08lx\",\"pinturas\":%lu,"
            "\"ms\":%lu,\"ms_max\":%lu,\"faixas\":%u,\"painel\":{\"sel\":%d,\"cima\":%d,\"fixado\":%s,"
            "\"minmax\":[%d,%d],\"nav\":[%d,%d,%d,%d,%d]},\"janela\":[%lu,%lu],\"agora\":%lu,\"alvos\":[",
            (unsigned)e.modo, (unsigned)e.modoAtual, e.escuro ? "escuro" : "claro", (unsigned long)e.hash,
            (unsigned long)e.pinturas, (unsigned long)e.ms, (unsigned long)e.msMax, (unsigned)e.faixas,
            e.selecionado, e.cima, e.fixado ? "true" : "false", e.minmaxCima ? 1 : 0, e.minmaxBaixo ? 1 : 0,
            e.nav[0], e.nav[1], e.nav[2], e.nav[3], e.nav[4], (unsigned long)e.janelaInicio,
            (unsigned long)e.janelaFim, (unsigned long)time( nullptr ) );
  for ( int i = 0; i < e.nAlvos && n < sizeof s_json; i++ ) {
    const UaAlvoBruto &a = e.alvos[i];
    n += (size_t)snprintf( s_json + n, sizeof s_json - n, "%s[%d,%d,%d,%d,%u,%d]", i ? "," : "", a.x, a.y, a.w,
                           a.h, (unsigned)a.tipo, a.indice );
  }
  if ( n < sizeof s_json ) n += (size_t)snprintf( s_json + n, sizeof s_json - n, "]}" );
  if ( n >= sizeof s_json ) {
    _server->send( 500, "application/json", "{\"erro\":\"o quadro não coube na resposta\"}" );
    return;
  }
  _server->sendHeader( "Cache-Control", "no-cache, no-store, must-revalidate" );
  _server->send( 200, "application/json", s_json, n );
}
