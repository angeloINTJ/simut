// Samples of the "menus" screens (src/ui/angulo/telas/Menus*.h), one for each
// state ESPEC_UI_A.md §6.2 lists, with the texts and values the device would
// show. See Amostras.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Amostras.h"
#include "MenusAlarmes.h"
#include "MenusConfig.h"
#include "MenusLicenca.h"
#include "MenusStatus.h"

using namespace ua;
using namespace ua::tela;

namespace {

// A name at the 31-character limit (the device's arrays hold 31 plus the
// terminator), in the letters a person would type and in the widest ones.
constexpr char kNome31[] = "Camara fria do laboratorio B-12";
constexpr char kNome31Largo[] = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
static_assert( sizeof kNome31 - 1 == 31, "a 31-character name" );
static_assert( sizeof kNome31Largo - 1 == 31, "a 31-character name" );

/* ------------------------------------------------------------------ */
/* Configurações                                                        */
/* ------------------------------------------------------------------ */

// The menu's order (MENU_ORDER, src/display/SettingsMenu.h): the radio build with every bit.
const uint8_t kAdmin[] = { IM_LIMITES, IM_SONS, IM_STATUS, IM_DATA_HORA, IM_PIN_PROPRIO, IM_USUARIOS, IM_SEGURANCA_PIN,
                           IM_MODO_CONFIG, IM_IDIOMA, IM_TEMA, IM_CALIBRAR, IM_ALINHAR, IM_LICENCA };
// The wired build leaves the setup access point out: 12 items, three pages.
const uint8_t kCabeada[] = { IM_LIMITES, IM_SONS, IM_STATUS, IM_DATA_HORA, IM_PIN_PROPRIO, IM_USUARIOS, IM_SEGURANCA_PIN,
                             IM_IDIOMA, IM_TEMA, IM_CALIBRAR, IM_ALINHAR, IM_LICENCA };
// An account with only panel bits: alarms, status, its own PIN, the licence.
const uint8_t kPainel[] = { IM_LIMITES, IM_STATUS, IM_PIN_PROPRIO, IM_LICENCA };

VmMenu menu( const uint8_t *itens, int n, int pagina, const char *conta ) {
  VmMenu vm;
  memset( &vm, 0, sizeof vm );
  vm.n = (uint8_t)n;
  vm.pagina = (uint8_t)pagina;
  memcpy( vm.itens, itens, (size_t)n );
  copiar( vm.conta, conta, sizeof vm.conta );
  return vm;
}

const VmMenu kAdminP1 = menu( kAdmin, sizeof kAdmin, 0, "admin" );
const VmMenu kAdminP2 = menu( kAdmin, sizeof kAdmin, 1, "admin" );
const VmMenu kAdminP3 = menu( kAdmin, sizeof kAdmin, 2, "admin" );
const VmMenu kAdminP4 = menu( kAdmin, sizeof kAdmin, 3, "admin" );
const VmMenu kCabeadaP2 = menu( kCabeada, sizeof kCabeada, 1, "admin" );
const VmMenu kContaPainel = menu( kPainel, sizeof kPainel, 0, "maria" );
const VmMenu kContaLonga = menu( kAdmin, sizeof kAdmin, 0, "maria.aparecida.da.silva.santos" );

/* ------------------------------------------------------------------ */
/* Tema e idioma                                                        */
/* ------------------------------------------------------------------ */

// The Tema screen (S10c, A'3): each mode, with the stock accent and with the palette's last one, in the states a control can be in on this screen. The
// saved choice is the next mode with the same accent, so "Aplicar tema" has something to apply, except in the disabled state, where the choice on show is
// the saved one (the screen as it opens). A pressed target is the neighbour of the selected one, except for the selected segment's own state.
enum EstadoTema : uint8_t { ET_REPOUSO, ET_COR_PREMIDA, ET_APLICAR_PREMIDO, ET_APLICAR_DESAB, ET_MODO_PREMIDO, ET_MODO_ATUAL_PREMIDO, ET_N };

VmTemas temas( int modo, int acento, int estado ) {
  VmTemas vm;
  memset( &vm, 0, sizeof vm );
  vm.modo = (uint8_t)modo;
  vm.acento = (uint8_t)acento;
  vm.salvo = estado == ET_APLICAR_DESAB ? indiceDaEscolha( (uint8_t)modo, (uint8_t)acento ) : indiceDaEscolha( (uint8_t)( ( modo + 1 ) % TM_N ), (uint8_t)acento );
  vm.premidoIndice = -1;
  if ( estado == ET_COR_PREMIDA ) { vm.premidoTipo = TT_COR; vm.premidoIndice = (int8_t)( ( acento + 1 ) % TEMA_CORES_N ); }
  if ( estado == ET_APLICAR_PREMIDO ) vm.premidoTipo = TT_APLICAR;
  if ( estado == ET_MODO_PREMIDO ) { vm.premidoTipo = TT_MODO; vm.premidoIndice = (int8_t)( ( modo + 1 ) % TM_N ); }
  if ( estado == ET_MODO_ATUAL_PREMIDO ) { vm.premidoTipo = TT_MODO; vm.premidoIndice = (int8_t)modo; }
  return vm;
}

// [mode][0 stock accent, 1 the last][state]
struct TabelaTemas {
  VmTemas v[TM_N][2][ET_N];
  TabelaTemas( ) {
    for ( int m = 0; m < TM_N; m++ )
      for ( int a = 0; a < 2; a++ )
        for ( int e = 0; e < ET_N; e++ ) v[m][a][e] = temas( m, a ? TEMA_CORES_N - 1 : 0, e );
  }
};
const TabelaTemas kTemas;

#define TEMA_AMOSTRA( nome, m, a, e ) \
  { nome, []( Pintor &p ) { desenharTemas( p, kTemas.v[m][a][e] ); }, []( Alvo *o ) { return alvosTemas( kTemas.v[m][a][e], o ); } }
#define TEMA_AMOSTRAS( nome, m, a ) \
  TEMA_AMOSTRA( nome "_repouso", m, a, ET_REPOUSO ), TEMA_AMOSTRA( nome "_cor_premida", m, a, ET_COR_PREMIDA ), \
  TEMA_AMOSTRA( nome "_aplicar_premido", m, a, ET_APLICAR_PREMIDO ), TEMA_AMOSTRA( nome "_aplicar_desab", m, a, ET_APLICAR_DESAB ), \
  TEMA_AMOSTRA( nome "_modo_premido", m, a, ET_MODO_PREMIDO ), TEMA_AMOSTRA( nome "_modo_atual_premido", m, a, ET_MODO_ATUAL_PREMIDO )

VmIdioma idioma( int atual, const char *pacote ) {
  VmIdioma vm;
  memset( &vm, 0, sizeof vm );
  vm.atual = (uint8_t)atual;
  copiar( vm.pacote, pacote, sizeof vm.pacote );
  return vm;
}

const VmIdioma kIdiomaPacote = idioma( ID_PACOTE, "Português (Brasil)" );
const VmIdioma kIdiomaIngles = idioma( ID_INGLES, "Português (Brasil)" );
const VmIdioma kIdiomaSoIngles = idioma( ID_INGLES, "" );

/* ------------------------------------------------------------------ */
/* Limites de alarme                                                    */
/* ------------------------------------------------------------------ */

LinhaAlarme sensor( const char *nome, int lo, int hi, bool ligado, bool manutencao ) {
  LinhaAlarme s;
  memset( &s, 0, sizeof s );
  copiar( s.nome, nome, sizeof s.nome );
  s.lo = (int16_t)lo;
  s.hi = (int16_t)hi;
  s.ligado = ligado;
  s.manutencao = manutencao;
  return s;
}

VmAlarmes alarmes( const LinhaAlarme *s, int n, int pagina ) {
  VmAlarmes vm;
  memset( &vm, 0, sizeof vm );
  vm.n = (uint8_t)n;
  vm.pagina = (uint8_t)pagina;
  for ( int i = 0; i < n; i++ ) vm.sensores[i] = s[i];
  return vm;
}

// Three sensors: on, on with a negative band, and one in maintenance.
const LinhaAlarme kTres[] = { sensor( "Vacinas", 20, 80, true, false ), sensor( "Freezer", -250, -150, true, false ),
                              sensor( "Sala de coleta", 150, 300, true, true ) };
// Six, so the list has two pages (4 + 2); one is off.
const LinhaAlarme kSeis[] = { sensor( "Vacinas", 20, 80, true, false ),       sensor( "Geladeira 2", 20, 80, true, false ),
                              sensor( "Freezer", -250, -150, true, false ),    sensor( "Sala de coleta", 150, 300, false, false ),
                              sensor( "Câmara fria", 0, 60, true, false ),     sensor( "Estufa", 350, 380, true, true ) };
// The extremes: 31 characters, the widest letters, the widest band, in maintenance and off.
const LinhaAlarme kLongos[] = { sensor( kNome31, 20, 80, true, false ), sensor( kNome31Largo, 20, 80, false, false ),
                                sensor( kNome31, -3276, 3276, true, true ), sensor( "Vacinas", 20, 80, true, false ) };

const VmAlarmes kAlarmes3 = alarmes( kTres, 3, 0 );
const VmAlarmes kAlarmes6P1 = alarmes( kSeis, 6, 0 );
const VmAlarmes kAlarmes6P2 = alarmes( kSeis, 6, 1 );
const VmAlarmes kAlarmesVazio = alarmes( nullptr, 0, 0 );
const VmAlarmes kAlarmesLongos = alarmes( kLongos, 4, 0 );

/* ------------------------------------------------------------------ */
/* Um sensor                                                            */
/* ------------------------------------------------------------------ */

VmSensorAlarme sensorAlarme( const char *nome, bool ligados, bool limites, bool bloquear, bool manutencao, uint32_t seg ) {
  VmSensorAlarme vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.nome, nome, sizeof vm.nome );
  vm.alarmesLigados = ligados;
  vm.podeLimites = limites;
  vm.podeBloquear = bloquear;
  vm.podeManutencao = manutencao;
  vm.manutencaoSeg = seg;
  return vm;
}

const VmSensorAlarme kSensorLiberado = sensorAlarme( "Vacinas", true, true, true, true, 0 );
const VmSensorAlarme kSensorDesligado = sensorAlarme( "Vacinas", false, true, true, true, 0 );
const VmSensorAlarme kSensorManutencao = sensorAlarme( "Vacinas", true, true, true, true, 90 * 60 );
// Exactly one hour left: the singular (the rig read "1 h restantes", 2026-10-14).
const VmSensorAlarme kSensorManutencao1h = sensorAlarme( "Vacinas", true, true, true, true, 60 * 60 );
const VmSensorAlarme kSensorSemBloqueio = sensorAlarme( "Vacinas", true, true, false, true, 0 );
// Only the maintenance bit, and somebody else's window open: two locked rows.
const VmSensorAlarme kSensorSoManutencao = sensorAlarme( "Geladeira 2", true, false, false, true, 45 * 60 );
const VmSensorAlarme kSensorLongo = sensorAlarme( kNome31, true, true, true, true, 0 );
const VmSensorAlarme kSensorLargo = sensorAlarme( kNome31Largo, false, true, true, true, 59 );

/* ------------------------------------------------------------------ */
/* Status do sistema                                                    */
/* ------------------------------------------------------------------ */

SensorStatus sensorStatus( const char *nome, int estado ) {
  SensorStatus s;
  memset( &s, 0, sizeof s );
  copiar( s.nome, nome, sizeof s.nome );
  s.estado = (uint8_t)estado;
  return s;
}

// The wired build running normally: the cable up, NTP synced, HTTP telemetry.
VmStatusSistema statusBase( int pagina ) {
  VmStatusSistema vm;
  memset( &vm, 0, sizeof vm );
  vm.pagina = (uint8_t)pagina;
  copiar( vm.aparelho, "simuttft", sizeof vm.aparelho );
  copiar( vm.firmware, "2.11.1", sizeof vm.firmware );
  vm.ligadoSeg = 3 * 86400 + 4 * 3600 + 12 * 60 + 55;
  vm.memLivre = 112400;
  vm.flashUsada = 61400;
  vm.flashTotal = 868000;
  vm.placaDecimos = 412;
  vm.fuso = -3;
  vm.cabo = true;
  vm.conectado = true;
  vm.ntpSincronizado = true;
  copiar( vm.ip, "192.168.3.40", sizeof vm.ip );
  copiar( vm.mac, "28:cd:c1:15:4e:99", sizeof vm.mac );
  copiar( vm.ntpServidor, "pool.ntp.org", sizeof vm.ntpServidor );
  vm.ativos = 4;
  vm.slots = 11;
  vm.nSensores = 4;
  vm.sensores[0] = sensorStatus( "Vacinas", ESS_NA_FAIXA );
  vm.sensores[1] = sensorStatus( "Geladeira 2", ESS_NO_LIMITE );
  vm.sensores[2] = sensorStatus( "Sala de coleta", ESS_ACIMA );
  vm.sensores[3] = sensorStatus( "Freezer", ESS_ERRO );
  vm.mqtt = false;
  vm.pendentes = 12;
  vm.lote = 25;
  copiar( vm.servidor, "https://telemetria.hospital.exemplo.com.br/api/ingest", sizeof vm.servidor );
  return vm;
}

VmStatusSistema statusMemoriaBaixa( ) {
  VmStatusSistema vm = statusBase( PS_SISTEMA );
  vm.memLivre = 18300;
  return vm;
}

// Cable pulled: no link, no address, the clock never synced.
VmStatusSistema statusSemRede( ) {
  VmStatusSistema vm = statusBase( PS_REDE );
  vm.conectado = false;
  memset( vm.ip, 0, sizeof vm.ip );
  vm.ntpSincronizado = false;
  return vm;
}

// The radio build, fair signal, a 31-character network name.
VmStatusSistema statusWifi( ) {
  VmStatusSistema vm = statusBase( PS_REDE );
  vm.cabo = false;
  vm.rssi = -72;
  copiar( vm.ssid, "HOSP-IOT-Bloco-B-Pavimento-2-Sala-14", sizeof vm.ssid );
  copiar( vm.ntpServidor, "time.cloudflare.com", sizeof vm.ntpServidor );
  return vm;
}

// The radio build with no network: nothing associated, no address, no clock from NTP.
VmStatusSistema statusWifiOff( ) {
  VmStatusSistema vm = statusBase( PS_REDE );
  vm.cabo = false;
  vm.conectado = false;
  memset( vm.ip, 0, sizeof vm.ip );
  vm.ntpSincronizado = false;
  return vm;
}

// Nine active sensors: the page lists four and says how many are left.
VmStatusSistema statusSensores9( ) {
  VmStatusSistema vm = statusBase( PS_SENSORES );
  vm.ativos = 9;
  vm.nSensores = 5;
  vm.sensores[4] = sensorStatus( kNome31, ESS_MANUTENCAO );
  return vm;
}

// Five sensors, in the words the functional build's glue can tell: a limit
// alarm firing, alarms off, a window open (31-character names), a failing sensor.
VmStatusSistema statusSensores5( ) {
  VmStatusSistema vm = statusBase( PS_SENSORES );
  vm.ativos = 5;
  vm.nSensores = 5;
  vm.sensores[1] = sensorStatus( "Geladeira 2", ESS_ALARME );
  vm.sensores[2] = sensorStatus( "Sala de coleta", ESS_DESLIGADO );
  vm.sensores[3] = sensorStatus( kNome31, ESS_MANUTENCAO );
  vm.sensores[4] = sensorStatus( kNome31Largo, ESS_ERRO );
  return vm;
}

VmStatusSistema statusSensoresNenhum( ) {
  VmStatusSistema vm = statusBase( PS_SENSORES );
  vm.ativos = 0;
  vm.nSensores = 0;
  return vm;
}

// MQTT, down, with a pile of records waiting.
VmStatusSistema statusMqtt( ) {
  VmStatusSistema vm = statusBase( PS_TELEMETRIA );
  vm.mqtt = true;
  vm.mqttConectado = false;
  vm.pendentes = 120;
  vm.lote = 1;
  copiar( vm.servidor, "mqtt.hospital.exemplo.com.br", sizeof vm.servidor );
  return vm;
}

// Names and numbers at their limits: a 31-character device name, a version of 15,
// an uptime of three digits of days, a file system near its size.
VmStatusSistema statusLimites( ) {
  VmStatusSistema vm = statusBase( PS_SISTEMA );
  copiar( vm.aparelho, kNome31, sizeof vm.aparelho );
  copiar( vm.firmware, "2.11.1-rc10-test", sizeof vm.firmware );
  vm.ligadoSeg = 123u * 86400 + 23 * 3600 + 59 * 60 + 59;
  vm.flashUsada = 867900;
  vm.placaDecimos = -45;
  vm.fuso = 12;
  return vm;
}

const VmStatusSistema kStatusSistema = statusBase( PS_SISTEMA );
const VmStatusSistema kStatusRede = statusBase( PS_REDE );
const VmStatusSistema kStatusSensores = statusBase( PS_SENSORES );
const VmStatusSistema kStatusTelemetria = statusBase( PS_TELEMETRIA );
const VmStatusSistema kStatusMemoria = statusMemoriaBaixa( );
const VmStatusSistema kStatusSemRede = statusSemRede( );
const VmStatusSistema kStatusWifi = statusWifi( );
const VmStatusSistema kStatusWifiOff = statusWifiOff( );
const VmStatusSistema kStatusSensores9 = statusSensores9( );
const VmStatusSistema kStatusSensores5 = statusSensores5( );
const VmStatusSistema kStatusSensoresNenhum = statusSensoresNenhum( );
const VmStatusSistema kStatusMqtt = statusMqtt( );
const VmStatusSistema kStatusLimites = statusLimites( );

/* ------------------------------------------------------------------ */
/* Licença                                                              */
/* ------------------------------------------------------------------ */

// The sources the licence screen lays out (HelpLicenseEN.h, the English of the
// page): the opening's five strings with the blank lines the screen puts after
// each, the MIT licence, and the head of the third-party list.
const char kAbertura0[] = "Free software. Below, what that means in practice.";
const char kAbertura1[] = "In short";
const char kAbertura2[] =
  "You may use, copy, modify, merge, publish, distribute, sublicense and sell copies of this software, free of "
  "charge and without restriction.\n\nThe only obligation is to keep the copyright notice and the permission notice in "
  "every copy.\n\nThe software is provided \"as is\", without warranty of any kind. The author is not liable for any "
  "damage arising from its use.";
const char kAbertura3[] = "A plain-language summary, written to be understood. It does not replace the licence text.";
// The em dash is in the real text and not in the kit's fonts: it is drawn as a hyphen.
const char kAbertura4[] = "The text below stays in English because it is the original — that is the version with legal force.";
const char kMit[] =
  "MIT License\n\nCopyright (c) 2026 Ângelo Moisés Alves\n\n"
  "Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated "
  "documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation "
  "the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to "
  "permit persons to whom the Software is furnished to do so, subject to the following conditions:\n\n"
  "The above copyright notice and this permission notice shall be included in all copies or substantial portions of "
  "the Software.\n\n"
  "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO "
  "THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
  "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
  "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
  "SOFTWARE.\n\n";
const char kTerceiros[] =
  "--- Third-party software ---\n\n"
  "Arduino-Pico core\n  Earle F. Philhower, III - LGPL-2.1\n"
  "Raspberry Pi Pico SDK\n  Raspberry Pi (Trading) Ltd. - BSD-3-Clause\n"
  "TinyUSB\n  Ha Thach (tinyusb.org) - MIT\n"
  "lwIP\n  Swedish Institute of Computer Science - BSD-3-Clause\n"
  "BearSSL\n  Thomas Pornin - MIT\n"
  "LittleFS\n  Arm Limited and the littlefs authors - BSD-3-Clause\n"
  "LEAmDNS\n  LaborEtArs - MIT\n"
  "newlib\n  Red Hat, Inc. and others - BSD-style (COPYING.NEWLIB)\n"
  "GCC runtime libraries\n  Free Software Foundation, Inc. - GPL-3.0 with the GCC Runtime Library Exception\n";

// The glue's own list of sources: the English opening (a blank line after each
// string but the second: DisplayManager.cpp's kLicOpeningSep), then the two texts.
const FonteLicenca kFontes[] = { { kAbertura0, 1 }, { kAbertura1, 0 }, { kAbertura2, 1 }, { kAbertura3, 1 },
                                 { kAbertura4, 1 }, { kMit, 0 },       { kTerceiros, 1 } };

// A text that would take twenty-odd pages: the licence and the list, over and
// over, in one source (the index holds 192 lines; the rest is left out).
char gRepetido[8192];
const char *repetido( ) {
  gRepetido[0] = 0;
  for ( int i = 0; i < 6 && strlen( gRepetido ) + sizeof kMit + sizeof kTerceiros < sizeof gRepetido; i++ ) {
    strcat( gRepetido, kMit );
    strcat( gRepetido, kTerceiros );
    strcat( gRepetido, "\n" );
  }
  return gRepetido;
}

// A paragraph longer than the buffer quebrar( ) is fed from (512 bytes): wrapped
// in pieces, with no word lost at the seams.
char gLongo[1500];
const char *longo( ) {
  const char *frase = "A sensor that reports every minute fills a day of history in 1440 records, and the record of one day stays "
                      "in one file; ";
  gLongo[0] = 0;
  while ( strlen( gLongo ) + strlen( frase ) < sizeof gLongo ) strcat( gLongo, frase );
  return gLongo;
}

// Words of a text: what lies between spaces and newlines.
bool palavra( const char *&p, char *w, int cap ) {
  while ( *p == ' ' || *p == '\n' ) p++;
  if ( !*p ) return false;
  int n = 0;
  while ( *p && *p != ' ' && *p != '\n' ) { if ( n < cap - 1 ) w[n++] = *p; p++; }
  w[n] = 0;
  return true;
}

// What the index must keep, checked as the samples are built (a violation
// aborts the run, which the harness reports as "cannot run"): every line fits
// the column and the view model's line, and the lines read in order spell the
// sources' words, none lost or repeated at a window's seam.
void conferirIndice( const char *nome, const IndiceLicenca &ix, const FonteLicenca *fontes, int nFontes ) {
  static char junto[32768];
  static char original[32768];
  junto[0] = original[0] = 0;
  for ( int i = 0; i < nFontes; i++ ) { strcat( original, fontes[i].texto ); strcat( original, "\n" ); }
  for ( int i = 0; i < ix.n; i++ ) {
    const IndiceLicenca::Linha &l = ix.linhas[i];
    char linha[LICENCA_BYTES + 1];
    memcpy( linha, ix.fontes[l.fonte] + l.inicio, l.tam );
    linha[l.tam] = 0;
    if ( l.tam >= LICENCA_BYTES || larguraTinta( linha, tipo::apoio ) > LICENCA_LARGURA ) {
      fprintf( stderr, "%s: line %d does not fit (%d bytes)\n", nome, i, (int)l.tam );
      abort( );
    }
    strcat( junto, linha );
    strcat( junto, " " );
  }
  const char *a = original, *b = junto;
  char wa[LICENCA_BYTES], wb[LICENCA_BYTES];
  int k = 0;
  while ( true ) {
    const bool ta = palavra( a, wa, sizeof wa ), tb = palavra( b, wb, sizeof wb );
    // An index that is not full holds the whole text; a full one (192 lines) may stop short, and then its words are a prefix.
    if ( !tb ) {
      if ( ta && ix.n < LICENCA_INDICE_MAX ) {
        fprintf( stderr, "%s: the text goes on after the lines, at word %d (\"%s\")\n", nome, k, wa );
        abort( );
      }
      break;
    }
    if ( !ta || strcmp( wa, wb ) ) {
      fprintf( stderr, "%s: word %d is \"%s\" in the lines and \"%s\" in the text\n", nome, k, wb, ta ? wa : "(end)" );
      abort( );
    }
    k++;
  }
}

IndiceLicenca gIndiceTudo, gIndiceRepetido, gIndiceLongo, gIndiceCurto;
const FonteLicenca kRepetido[] = { { repetido( ), 0 } };
const FonteLicenca kLongo[] = { { longo( ), 0 } };
const FonteLicenca kCurto[] = { { "Uma página só.\n\nO texto cabe nela.", 0 } };

int indexar( const char *nome, IndiceLicenca &ix, const FonteLicenca *fontes, int n ) {
  const int linhas = indexarLicenca( ix, fontes, n );
  conferirIndice( nome, ix, fontes, n );
  return linhas;
}

// The page `pagina` of an index, as the glue builds it.
VmLicenca licenca( const IndiceLicenca &ix, int pagina ) {
  VmLicenca vm;
  memset( &vm, 0, sizeof vm );
  paginaDaLicenca( ix, pagina, vm );
  return vm;
}

// Static initialization runs these once, in order, before any sample draws.
const int kLinhasRepetido = indexar( "licenca repetida", gIndiceRepetido, kRepetido, 1 );
const int kLinhasLongo = indexar( "paragrafo longo", gIndiceLongo, kLongo, 1 );
const int kLinhasCurto = indexar( "licenca curta", gIndiceCurto, kCurto, 1 );

// A text that fits one page: no pager, no scroll bar.
const VmLicenca kLicencaUma = licenca( gIndiceCurto, 0 );
// Two digits in the pager ("32 de 32"), the thumb at the end of a long track, the
// index at its limit (192 lines: 32 pages of six, the last one full).
const VmLicenca kLicencaRepetida = licenca( gIndiceRepetido, ( kLinhasRepetido + LICENCA_LINHAS - 1 ) / LICENCA_LINHAS - 1 );
// The page of the opening's last string, the one with the em dash drawn as a hyphen.
int paginaDaFonte( const IndiceLicenca &ix, int fonte ) {
  for ( int i = 0; i < ix.n; i++ )
    if ( ix.linhas[i].fonte == fonte && ix.linhas[i].tam ) return i / LICENCA_LINHAS;
  return 0;
}
// The whole text is indexed at the first draw and not at static initialization (S10c C4): its last four sources are the Angulo's own
// (anexarFontesDoAngulo( ), UA_TXT( )), which read the pack that host_telas.py --idioma loads after the static initializers ran.
int linhasTudo( ) {
  static int linhas = -1;
  if ( linhas < 0 ) {
    FonteLicenca fontes[LICENCA_FONTES_MAX];
    int nf = 0;
    for ( const FonteLicenca &f : kFontes ) fontes[nf++] = f;
    nf = anexarFontesDoAngulo( fontes, nf, LICENCA_FONTES_MAX );
    linhas = indexar( "licenca", gIndiceTudo, fontes, nf );
  }
  return linhas;
}
int paginasTudo( ) { return ( linhasTudo( ) + LICENCA_LINHAS - 1 ) / LICENCA_LINHAS; }
VmLicenca licencaP1( ) { linhasTudo( ); return licenca( gIndiceTudo, 0 ); }
VmLicenca licencaMeio( ) { linhasTudo( ); return licenca( gIndiceTudo, paginasTudo( ) / 2 ); }
// The last page holds what is left of the text: now the Angulo's heading and its three paragraphs.
VmLicenca licencaUltima( ) { linhasTudo( ); return licenca( gIndiceTudo, paginasTudo( ) - 1 ); }
VmLicenca licencaTraco( ) { linhasTudo( ); return licenca( gIndiceTudo, paginaDaFonte( gIndiceTudo, 4 ) ); }
const VmLicenca kLicencaLongo = licenca( gIndiceLongo, 0 );

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  { "menu_admin_p1", []( Pintor &p ) { desenharMenu( p, kAdminP1 ); }, []( Alvo *o ) { return alvosMenu( kAdminP1, o ); } },
  { "menu_admin_p2", []( Pintor &p ) { desenharMenu( p, kAdminP2 ); }, []( Alvo *o ) { return alvosMenu( kAdminP2, o ); } },
  { "menu_admin_p3", []( Pintor &p ) { desenharMenu( p, kAdminP3 ); }, []( Alvo *o ) { return alvosMenu( kAdminP3, o ); } },
  { "menu_admin_p4", []( Pintor &p ) { desenharMenu( p, kAdminP4 ); }, []( Alvo *o ) { return alvosMenu( kAdminP4, o ); } },
  { "menu_cabeada_p2", []( Pintor &p ) { desenharMenu( p, kCabeadaP2 ); }, []( Alvo *o ) { return alvosMenu( kCabeadaP2, o ); } },
  { "menu_conta_painel", []( Pintor &p ) { desenharMenu( p, kContaPainel ); }, []( Alvo *o ) { return alvosMenu( kContaPainel, o ); } },
  { "menu_conta_longa", []( Pintor &p ) { desenharMenu( p, kContaLonga ); }, []( Alvo *o ) { return alvosMenu( kContaLonga, o ); } },

  TEMA_AMOSTRA( "temas_claro", TM_CLARO, 0, ET_APLICAR_DESAB ),
  TEMA_AMOSTRA( "temas_escuro", TM_ESCURO, 0, ET_APLICAR_DESAB ),
  TEMA_AMOSTRA( "temas_automatico", TM_AUTOMATICO, 0, ET_APLICAR_DESAB ),
  TEMA_AMOSTRAS( "temas_claro_padrao", TM_CLARO, 0 ), TEMA_AMOSTRAS( "temas_claro_ultima", TM_CLARO, 1 ),
  TEMA_AMOSTRAS( "temas_escuro_padrao", TM_ESCURO, 0 ), TEMA_AMOSTRAS( "temas_escuro_ultima", TM_ESCURO, 1 ),
  TEMA_AMOSTRAS( "temas_automatico_padrao", TM_AUTOMATICO, 0 ), TEMA_AMOSTRAS( "temas_automatico_ultima", TM_AUTOMATICO, 1 ),

  { "idioma_pacote", []( Pintor &p ) { desenharIdioma( p, kIdiomaPacote ); }, []( Alvo *o ) { return alvosIdioma( kIdiomaPacote, o ); } },
  { "idioma_english", []( Pintor &p ) { desenharIdioma( p, kIdiomaIngles ); }, []( Alvo *o ) { return alvosIdioma( kIdiomaIngles, o ); } },
  { "idioma_so_english", []( Pintor &p ) { desenharIdioma( p, kIdiomaSoIngles ); }, []( Alvo *o ) { return alvosIdioma( kIdiomaSoIngles, o ); } },

  { "alarmes_3", []( Pintor &p ) { desenharAlarmes( p, kAlarmes3 ); }, []( Alvo *o ) { return alvosAlarmes( kAlarmes3, o ); } },
  { "alarmes_6_p1", []( Pintor &p ) { desenharAlarmes( p, kAlarmes6P1 ); }, []( Alvo *o ) { return alvosAlarmes( kAlarmes6P1, o ); } },
  { "alarmes_6_p2", []( Pintor &p ) { desenharAlarmes( p, kAlarmes6P2 ); }, []( Alvo *o ) { return alvosAlarmes( kAlarmes6P2, o ); } },
  { "alarmes_vazio", []( Pintor &p ) { desenharAlarmes( p, kAlarmesVazio ); }, []( Alvo *o ) { return alvosAlarmes( kAlarmesVazio, o ); } },
  { "alarmes_nome_longo", []( Pintor &p ) { desenharAlarmes( p, kAlarmesLongos ); }, []( Alvo *o ) { return alvosAlarmes( kAlarmesLongos, o ); } },

  { "sensor_liberado", []( Pintor &p ) { desenharSensorAlarme( p, kSensorLiberado ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorLiberado, o ); } },
  { "sensor_desligado", []( Pintor &p ) { desenharSensorAlarme( p, kSensorDesligado ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorDesligado, o ); } },
  { "sensor_manutencao", []( Pintor &p ) { desenharSensorAlarme( p, kSensorManutencao ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorManutencao, o ); } },
  { "sensor_manutencao_1h", []( Pintor &p ) { desenharSensorAlarme( p, kSensorManutencao1h ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorManutencao1h, o ); } },
  { "sensor_sem_bloqueio", []( Pintor &p ) { desenharSensorAlarme( p, kSensorSemBloqueio ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorSemBloqueio, o ); } },
  { "sensor_so_manutencao", []( Pintor &p ) { desenharSensorAlarme( p, kSensorSoManutencao ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorSoManutencao, o ); } },
  { "sensor_nome_longo", []( Pintor &p ) { desenharSensorAlarme( p, kSensorLongo ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorLongo, o ); } },
  { "sensor_nome_largo", []( Pintor &p ) { desenharSensorAlarme( p, kSensorLargo ); }, []( Alvo *o ) { return alvosSensorAlarme( kSensorLargo, o ); } },

  { "status_sistema", []( Pintor &p ) { desenharStatusSistema( p, kStatusSistema ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSistema, o ); } },
  { "status_rede", []( Pintor &p ) { desenharStatusSistema( p, kStatusRede ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusRede, o ); } },
  { "status_sensores", []( Pintor &p ) { desenharStatusSistema( p, kStatusSensores ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSensores, o ); } },
  { "status_telemetria", []( Pintor &p ) { desenharStatusSistema( p, kStatusTelemetria ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusTelemetria, o ); } },
  { "status_memoria_baixa", []( Pintor &p ) { desenharStatusSistema( p, kStatusMemoria ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusMemoria, o ); } },
  { "status_sem_rede", []( Pintor &p ) { desenharStatusSistema( p, kStatusSemRede ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSemRede, o ); } },
  { "status_rede_wifi", []( Pintor &p ) { desenharStatusSistema( p, kStatusWifi ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusWifi, o ); } },
  { "status_rede_wifi_off", []( Pintor &p ) { desenharStatusSistema( p, kStatusWifiOff ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusWifiOff, o ); } },
  { "status_sensores_5", []( Pintor &p ) { desenharStatusSistema( p, kStatusSensores5 ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSensores5, o ); } },
  { "status_sensores_9", []( Pintor &p ) { desenharStatusSistema( p, kStatusSensores9 ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSensores9, o ); } },
  { "status_sensores_nenhum", []( Pintor &p ) { desenharStatusSistema( p, kStatusSensoresNenhum ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusSensoresNenhum, o ); } },
  { "status_telemetria_mqtt", []( Pintor &p ) { desenharStatusSistema( p, kStatusMqtt ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusMqtt, o ); } },
  { "status_sistema_limites", []( Pintor &p ) { desenharStatusSistema( p, kStatusLimites ); }, []( Alvo *o ) { return alvosStatusSistema( kStatusLimites, o ); } },

  { "licenca_p1", []( Pintor &p ) { desenharLicenca( p, licencaP1( ) ); }, []( Alvo *o ) { return alvosLicenca( licencaP1( ), o ); } },
  { "licenca_meio", []( Pintor &p ) { desenharLicenca( p, licencaMeio( ) ); }, []( Alvo *o ) { return alvosLicenca( licencaMeio( ), o ); } },
  { "licenca_ultima", []( Pintor &p ) { desenharLicenca( p, licencaUltima( ) ); }, []( Alvo *o ) { return alvosLicenca( licencaUltima( ), o ); } },
  { "licenca_uma_pagina", []( Pintor &p ) { desenharLicenca( p, kLicencaUma ); }, []( Alvo *o ) { return alvosLicenca( kLicencaUma, o ); } },
  { "licenca_traco", []( Pintor &p ) { desenharLicenca( p, licencaTraco( ) ); }, []( Alvo *o ) { return alvosLicenca( licencaTraco( ), o ); } },
  { "licenca_repetida", []( Pintor &p ) { desenharLicenca( p, kLicencaRepetida ); }, []( Alvo *o ) { return alvosLicenca( kLicencaRepetida, o ); } },
  { "licenca_paragrafo_longo", []( Pintor &p ) { desenharLicenca( p, kLicencaLongo ); }, []( Alvo *o ) { return alvosLicenca( kLicencaLongo, o ); } },
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
