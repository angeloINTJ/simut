// The system status and the licence. See MenusStatus.h.
#include "MenusStatus.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

/* ------------------------------------------------------------------ */
/* Status do sistema                                                    */
/* ------------------------------------------------------------------ */

constexpr int STATUS_LINHAS_MAX = 6;
constexpr int16_t STATUS_H_FILA = 24;
// The old screen painted the heap red under 20000 bytes (DisplayManager_Settings.cpp).
constexpr uint32_t MEM_BAIXA = 20000;
// The old screen's colour for a pile of records waiting: over 50.
constexpr uint16_t PENDENTES_ALTO = 50;
// The old screen's thresholds for the signal, dBm: above -60 good, above -80 fair.
constexpr int16_t RSSI_BOM = -60, RSSI_REGULAR = -80;
// What a row of lista( ) has between its padding: the key, a gap, the value.
constexpr int16_t LARGURA_LINHA = W_TELA - 2 * ESP2 - 2 * PAD;

struct LinhaStatus {
  char k[32];
  char v[72];
  Fonte fonte;           // the value's font: the mono one, or a narrower one for a long value
  Tom tom;               // the value's colour
  bool suave;            // the key in the secondary ink (a note row)
};

struct LayoutStatus {
  char titulo[32];
  Paginas pag;
  Caixa16 voltar;
  int n;
  LinhaStatus linhas[STATUS_LINHAS_MAX];
  char nota[64];         // under the card; "" none
};

uint16_t corDoTom( const Tema &T, Tom tom ) {
  switch ( tom ) {
    case Tom::POSITIVO: case Tom::OK: return T.positivo;
    case Tom::ALERTA: return T.alerta;
    case Tom::PERIGO: return T.perigo;
    case Tom::NEUTRO: break;
  }
  return T.tinta;
}

// A row: the key and its value. The value is in the mono font (digits that
// line up) while it fits what the key leaves of the row; a longer one (a
// server, a network's name, a device's name: 17 characters is all the mono font
// holds there) falls to `corpo`, then to `apoio`, and is cut with "…" only when
// it does not fit even in the narrowest.
LinhaStatus &anexar( LayoutStatus &l, const char *k, const char *v, Tom tom = Tom::NEUTRO ) {
  static LinhaStatus semLugar;                 // the sixth row is the last: a seventh goes nowhere
  if ( l.n >= STATUS_LINHAS_MAX ) return semLugar;
  LinhaStatus &r = l.linhas[l.n++];
  copiar( r.k, k, sizeof r.k );
  copiar( r.v, v, sizeof r.v );
  const int16_t largura = (int16_t)( LARGURA_LINHA - larguraTinta( r.k, tipo::corpo ) - ESP4 );
  static const Fonte kFontes[] = { tipo::dado, tipo::corpo, tipo::apoio };
  r.fonte = kFontes[0];
  for ( const Fonte f : kFontes ) {
    r.fonte = f;
    if ( larguraTinta( r.v, f ) <= largura ) break;
  }
  if ( larguraTinta( r.v, r.fonte ) > largura ) cortar( v, r.fonte, largura, r.v, sizeof r.v );
  r.tom = tom;
  return r;
}

// kB with one decimal from bytes, SI (1 kB = 1000 B: the unit of the 20000-byte
// heap threshold): 112400 is "112,4".
void kb( uint32_t bytes, char *out, int cap ) {
  const uint64_t decimos = ( (uint64_t)bytes + 50 ) / 100;
  snprintf( out, (size_t)cap, "%lu,%lu", (unsigned long)( decimos / 10 ), (unsigned long)( decimos % 10 ) );
}

// "−58": the typographic minus the plan uses beside dBm and GMT.
void inteiroComSinal( int v, bool mais, char *out, int cap ) {
  snprintf( out, (size_t)cap, "%s%d", v < 0 ? "\xE2\x88\x92" : mais ? "+" : "", v < 0 ? -v : v );
}

void paginaSistema( LayoutStatus &l, const VmStatusSistema &vm ) {
  // A uint32_t of bytes is at most "4294967,3" in kB: 12 bytes hold it.
  char b[48], c[12];
  anexar( l, UA_TXT( "Device" ), vm.aparelho[0] ? vm.aparelho : UA_TXT( "Unnamed" ) );
  anexar( l, UA_TXT( "Firmware" ), vm.firmware );
  const unsigned long s = vm.ligadoSeg;
  snprintf( b, sizeof b, "%lu d %02lu:%02lu:%02lu", s / 86400, ( s % 86400 ) / 3600, ( s % 3600 ) / 60, s % 60 );
  anexar( l, UA_TXT( "Uptime" ), b );
  kb( vm.memLivre, c, sizeof c );
  snprintf( b, sizeof b, "%s kB", c );
  anexar( l, UA_TXT( "Free memory" ), b, vm.memLivre < MEM_BAIXA ? Tom::PERIGO : Tom::POSITIVO );
  kb( vm.flashUsada, c, sizeof c );
  if ( vm.flashTotal ) {
    char t[12];
    kb( vm.flashTotal, t, sizeof t );
    snprintf( b, sizeof b, "%s %s %s kB", c, UA_TXT( "of" ), t );
  } else {
    snprintf( b, sizeof b, "%s kB", c );
  }
  // The file system's bytes (the WebManager's cache), the old screen's "Flash Used".
  anexar( l, UA_TXT( "Flash" ), b );
  char t1[16], gmt[8];
  inteiroComSinal( vm.fuso, true, gmt, sizeof gmt );
  snprintf( b, sizeof b, "%s °C · GMT%s", n1( vm.placaDecimos, t1 ), gmt );
  anexar( l, UA_TXT( "Board" ), b );
}

void paginaRede( LayoutStatus &l, const VmStatusSistema &vm ) {
  if ( vm.cabo ) {
    anexar( l, UA_TXT( "Cable" ), vm.conectado ? UA_TXT( "Connected" ) : UA_TXT( "No cable" ), vm.conectado ? Tom::POSITIVO : Tom::PERIGO );
  } else if ( vm.conectado ) {
    anexar( l, UA_TXT( "Wi-Fi" ), vm.ssid[0] ? vm.ssid : UA_TXT( "Connected" ), Tom::POSITIVO );
    char dbm[16], b[24];
    inteiroComSinal( vm.rssi, false, dbm, sizeof dbm );
    snprintf( b, sizeof b, "%s dBm", dbm );
    anexar( l, UA_TXT( "Signal" ), b, vm.rssi > RSSI_BOM ? Tom::POSITIVO : vm.rssi > RSSI_REGULAR ? Tom::NEUTRO : Tom::ALERTA );
  } else {
    anexar( l, UA_TXT( "Wi-Fi" ), UA_TXT( "Disconnected" ), Tom::PERIGO );
  }
  anexar( l, UA_TXT( "IP address" ), vm.ip[0] ? vm.ip : UA_TXT( "No address" ) );
  anexar( l, UA_TXT( "MAC" ), vm.mac );
  anexar( l, UA_TXT( "NTP" ), vm.ntpSincronizado ? UA_TXT( "Synced" ) : UA_TXT( "Not synced" ),
          vm.ntpSincronizado ? Tom::POSITIVO : Tom::ALERTA );
  anexar( l, UA_TXT( "NTP server" ), vm.ntpServidor[0] ? vm.ntpServidor : UA_TXT( "Not set" ) );
}

void paginaSensores( LayoutStatus &l, const VmStatusSistema &vm ) {
  char b[24];
  snprintf( b, sizeof b, "%u %s %u", (unsigned)vm.ativos, UA_TXT( "of" ), (unsigned)vm.slots );
  anexar( l, UA_TXT( "Active" ), b );
  int mostrar = vm.nSensores < STATUS_SENSORES_MAX ? vm.nSensores : STATUS_SENSORES_MAX;
  // More sensors than rows: the last row is the count of those not shown.
  const bool sobram = vm.ativos > mostrar && mostrar == STATUS_SENSORES_MAX;
  if ( sobram ) mostrar = STATUS_SENSORES_MAX - 1;
  for ( int i = 0; i < mostrar; i++ ) {
    const SensorStatus &s = vm.sensores[i];
    const char *palavra = "";
    Tom tom = Tom::NEUTRO;
    switch ( s.estado ) {
      case ESS_NA_FAIXA: palavra = UA_TXT( "In range" ); tom = Tom::POSITIVO; break;
      case ESS_NO_LIMITE: palavra = UA_TXT( "At the limit" ); tom = Tom::ALERTA; break;
      case ESS_ACIMA: palavra = UA_TXT( "Above" ); tom = Tom::PERIGO; break;
      case ESS_ABAIXO: palavra = UA_TXT( "Below" ); tom = Tom::PERIGO; break;
      case ESS_ERRO: palavra = UA_TXT( "Error" ); tom = Tom::PERIGO; break;
      case ESS_MANUTENCAO: palavra = UA_TXT( "Maintenance" ); tom = Tom::ALERTA; break;
      case ESS_ALARME: palavra = UA_TXT( "In alarm" ); tom = Tom::PERIGO; break;
      case ESS_DESLIGADO: palavra = UA_TXT( "No alarm" ); break;   // its alarms are off; "Off" ("Desligado") read as the sensor (rig, 2026-10-14)
      default: break;
    }
    // Here the name is what gives way: the state is a word, the name may be 31 characters.
    LinhaStatus &r = anexar( l, "", palavra, tom );
    cortar( s.nome, tipo::corpo, (int16_t)( LARGURA_LINHA - larguraTinta( r.v, tipo::dado ) - ESP4 ), r.k, sizeof r.k );
  }
  if ( sobram ) {
    LinhaStatus &r = anexar( l, "", "" );
    // "and 4 more sensors": the count sits inside the sentence, so it is two texts and a literal format around the number (pt-BR "e mais" + 4 + "sensores").
    snprintf( r.k, sizeof r.k, "%s %u %s", UA_TXT( "and" ), (unsigned)( vm.ativos - mostrar ), UA_TXT( "more sensors" ) );
    r.suave = true;
  }
  // "Nenhum sensor ativo." is the row above ("Ativos  0 de 11"): with it the note was 364 px
  // and left the glass at 9 pt (2026-10-15).
  if ( vm.ativos == 0 ) copiar( l.nota, UA_TXT( "Activate a sensor on the web." ), (int)sizeof l.nota );
}

void paginaTelemetria( LayoutStatus &l, const VmStatusSistema &vm ) {
  char b[32];
  anexar( l, UA_TXT( "Transport" ), vm.mqtt ? UA_TXT( "MQTT" ) : UA_TXT( "HTTP" ) );
  anexar( l, UA_TXT( "Server" ), vm.servidor[0] ? vm.servidor : UA_TXT( "Not set" ) );
  // The singular is a text of its own (a language's word may change with the number); the plural is the word after the number, in a literal format.
  if ( vm.pendentes == 1 ) copiar( b, UA_TXT( "1 pending" ), (int)sizeof b );
  else snprintf( b, sizeof b, "%u %s", (unsigned)vm.pendentes, UA_TXT( "pending" ) );
  anexar( l, UA_TXT( "Pending" ), b, vm.pendentes > PENDENTES_ALTO ? Tom::ALERTA : Tom::POSITIVO );
  if ( vm.lote == 0 ) copiar( b, UA_TXT( "Off" ), (int)sizeof b );
  else if ( vm.lote == 1 ) copiar( b, UA_TXT( "1 record" ), (int)sizeof b );
  else snprintf( b, sizeof b, "%lu %s", (unsigned long)vm.lote, UA_TXT( "records" ) );
  anexar( l, UA_TXT( "Minimum batch" ), b );
  if ( vm.mqtt )
    anexar( l, UA_TXT( "MQTT" ), vm.mqttConectado ? UA_TXT( "Connected" ) : UA_TXT( "Disconnected" ), vm.mqttConectado ? Tom::POSITIVO : Tom::PERIGO );
}

LayoutStatus layoutStatus( const VmStatusSistema &vm ) {
  LayoutStatus l{ };
  l.pag = paginasDe( vm.pagina, STATUS_PAGINAS );
  l.voltar = alvoVoltar( );
  // The page's name, looked up where the title is made (UA_TXT( ) cannot initialise a table); a switch on the enum, so that a page without a name does not build.
  const char *nome = "";
  switch ( static_cast<PaginaStatus>( l.pag.pagina ) ) {
    case PS_SISTEMA: nome = UA_TXT( "System" ); break;
    case PS_REDE: nome = UA_TXT( "Network" ); break;
    case PS_SENSORES: nome = UA_TXT( "Sensors" ); break;
    case PS_TELEMETRIA: nome = UA_TXT( "Telemetry" ); break;
  }
  snprintf( l.titulo, sizeof l.titulo, "%s · %s", UA_TXT( "Status" ), nome );
  switch ( l.pag.pagina ) {
    case PS_SISTEMA: paginaSistema( l, vm ); break;
    case PS_REDE: paginaRede( l, vm ); break;
    case PS_SENSORES: paginaSensores( l, vm ); break;
    default: paginaTelemetria( l, vm ); break;
  }
  return l;
}

}  // namespace

void desenharStatusSistema( Pintor &p, const VmStatusSistema &vm ) {
  const Tema &T = p.t( );
  const LayoutStatus l = layoutStatus( vm );
  barraTituloVoltar( p, l.titulo );
  Item itens[STATUS_LINHAS_MAX];
  for ( int i = 0; i < l.n; i++ ) {
    const LinhaStatus &r = l.linhas[i];
    itens[i].texto = r.k;
    itens[i].valor = r.v[0] ? r.v : nullptr;
    itens[i].valorFonte = r.fonte;
    itens[i].temValorCor = true;
    itens[i].valorCor = corDoTom( T, r.tom );
    if ( r.suave ) { itens[i].temTom = true; itens[i].tom = T.tinta2; }
  }
  const int16_t fim = lista( p, Y0, itens, (int16_t)l.n, STATUS_H_FILA );
  if ( l.nota[0] ) desenharNota( p, fim + ESP2, l.nota );
  desenharPaginador( p, l.pag );
}

int alvosStatusSistema( const VmStatusSistema &vm, Alvo *out ) {
  const LayoutStatus l = layoutStatus( vm );
  const int n = addAlvoVoltar( out, 0, l.voltar, LST_VOLTAR );
  return alvosPaginador( l.pag, out, n );
}

/* ------------------------------------------------------------------ */
/* Licença                                                              */
/* ------------------------------------------------------------------ */

namespace {

// The card: padding, the page's lines, a gap, the note's line, padding.
constexpr int16_t LICENCA_H_CARTAO = ESP2 + LICENCA_LINHAS * LICENCA_PASSO + ESP2 + LICENCA_PASSO + ESP2;
// The thumb is never smaller than this, however many pages there are: 18 pages
// would leave 6 px, which nobody can see.
constexpr int16_t LICENCA_POLEGAR_MIN = ESP3;

struct LayoutLicenca {
  Paginas pag;
  Caixa16 voltar;
  int n;                       // lines to draw
  int16_t xTexto, yTopo;       // the first line's box
  int16_t yNota;
  bool barra;                  // more than one page
  Caixa16 trilho;
  Caixa16 polegar;
};

LayoutLicenca layoutLicenca( const VmLicenca &vm ) {
  LayoutLicenca l{ };
  l.pag = paginasDe( vm.pagina, vm.paginas );
  l.voltar = alvoVoltar( );
  l.n = vm.n < LICENCA_LINHAS ? vm.n : LICENCA_LINHAS;
  l.xTexto = ESP2 + PAD;
  l.yTopo = Y0 + ESP2;
  l.yNota = l.yTopo + LICENCA_LINHAS * LICENCA_PASSO + ESP2;
  l.barra = l.pag.visivel;
  const int16_t altura = LICENCA_LINHAS * LICENCA_PASSO;
  l.trilho = Caixa16{ (int16_t)( ESP2 + ( W_TELA - 2 * ESP2 ) - PAD - LICENCA_BARRA ), l.yTopo, LICENCA_BARRA, altura };
  // Proportional to the share of the text on a page, and walking the whole
  // track from the first page to the last.
  int16_t h = rdiv( altura, l.pag.total );
  if ( h < LICENCA_POLEGAR_MIN ) h = LICENCA_POLEGAR_MIN;
  const int16_t y = l.pag.total > 1 ? rdiv( (int32_t)( altura - h ) * l.pag.pagina, l.pag.total - 1 ) : 0;
  l.polegar = Caixa16{ l.trilho.x, (int16_t)( l.trilho.y + y ), LICENCA_BARRA, h };
  return l;
}

}  // namespace

void desenharLicenca( Pintor &p, const VmLicenca &vm ) {
  const Tema &T = p.t( );
  const LayoutLicenca l = layoutLicenca( vm );
  barraTituloVoltar( p, UA_TXT( "MIT license" ) );
  superficie( p, ESP2, Y0, W_TELA - 2 * ESP2, LICENCA_H_CARTAO );
  // The text is the screen's content, so it takes the primary ink; the note
  // under it is the secondary one.
  for ( int i = 0; i < l.n; i++ )
    textoCentrado( p, vm.linhas[i], l.xTexto, l.yTopo + i * LICENCA_PASSO, LICENCA_PASSO, tipo::apoio, T.tinta, 'l' );
  if ( l.barra ) {
    p.fillRoundRect( l.trilho.x, l.trilho.y, l.trilho.w, l.trilho.h, RAIO_TOTAL, T.superficie2 );
    p.fillRoundRect( l.polegar.x, l.polegar.y, l.polegar.w, l.polegar.h, RAIO_TOTAL, T.linhaForte );
  }
  textoCentrado( p, UA_TXT( "The legal text stays in English." ), l.xTexto, l.yNota, LICENCA_PASSO, tipo::apoio, T.tinta2, 'l' );
  desenharPaginador( p, l.pag );
}

int alvosLicenca( const VmLicenca &vm, Alvo *out ) {
  const LayoutLicenca l = layoutLicenca( vm );
  const int n = addAlvoVoltar( out, 0, l.voltar, LST_VOLTAR );
  return alvosPaginador( l.pag, out, n );
}

}  // namespace tela
}  // namespace ua
