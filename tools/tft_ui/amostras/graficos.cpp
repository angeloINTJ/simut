// Samples of the graph group's screens (src/ui_angulo/telas/Graficos.h).
//
// The series are made here and aggregated the way the Core 0 does it
// (AppManager_Graph.cpp): 200 time buckets, each with the minimum, the maximum
// and the mean of the records that fell in it, an empty one a gap; the
// statistics over every record, not over the buckets. A refrigerator between 2
// and 8 °C with its compressor cycle and its door openings, a room's humidity
// between 40 and 70 %, a freezer with its defrost at 03:00, a barometer. The
// clock stands at 13/10/2026 11:32, the plan's own. Nothing here is random:
// the "noise" is a hash of the minute, so every run draws the same frames.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Amostras.h"
#include "Graficos.h"

using namespace ua;
using namespace ua::tela;

namespace {

/* ------------------------------------------------------------------ */
/* Time: minutes since 1970, in the device's local time                 */
/* ------------------------------------------------------------------ */

constexpr double kPi = 3.14159265358979323846;

// Days since 1970-01-01 of a civil date, and back (H. Hinnant's algorithms):
// the host needs no time zone to say what day 13/10/2026 is.
constexpr int32_t diasDeCivil( int y, int m, int d ) {
  y -= m <= 2;
  const int32_t era = ( y >= 0 ? y : y - 399 ) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

void civilDeDias( int32_t z, int *y, int *m, int *d ) {
  z += 719468;
  const int32_t era = ( z >= 0 ? z : z - 146096 ) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
  const int32_t doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
  const int32_t mp = ( 5 * doy + 2 ) / 153;
  *d = doy - ( 153 * mp + 2 ) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + ( *m <= 2 );
}

constexpr int32_t kAgora = diasDeCivil( 2026, 10, 13 ) * 1440 + 11 * 60 + 32;

Instante instante( int32_t minuto ) {
  int32_t dias = minuto / 1440, resto = minuto % 1440;
  if ( resto < 0 ) { resto += 1440; dias--; }
  int y, m, d;
  civilDeDias( dias, &y, &m, &d );
  return Instante{ (uint8_t)d, (uint8_t)m, (uint8_t)( resto / 60 ), (uint8_t)( resto % 60 ) };
}

int16_t dec( double v ) { return (int16_t)lround( v * 10.0 ); }

/* ------------------------------------------------------------------ */
/* The signals                                                          */
/* ------------------------------------------------------------------ */

// A hash of the minute in [-1, 1].
double ruido( int32_t m ) {
  uint32_t h = (uint32_t)m * 2654435761u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return (double)( h & 0xFFFF ) / 32767.5 - 1.0;
}

double minutoDoDia( int32_t m ) { return (double)( ( m % 1440 + 1440 ) % 1440 ); }

// A refrigerator for vaccines, its bottles of water slowing the compressor to a
// cycle of 104 minutes, a slower swing through the day, and a bump when the
// door opens at 07:45, 12:10 and 17:30. Between 3,0 and 7,3 °C.
double tempGeladeira( int32_t m ) {
  const double dia = minutoDoDia( m );
  double t = 4.4 + 0.85 * sin( 2 * kPi * m / 104.0 ) + 0.5 * sin( 2 * kPi * ( dia - 300 ) / 1440.0 );
  static const int kPortas[3] = { 7 * 60 + 45, 12 * 60 + 10, 17 * 60 + 30 };
  for ( int porta : kPortas ) {
    const double dt = dia - porta;
    if ( dt >= 0 && dt < 15 ) t += 1.5 * ( 1 - dt / 15 );
  }
  return t + 0.06 * ruido( m );
}

// The same refrigerator with a failure this morning: the door left open from
// 06:20, 45 minutes of a climb past 8 °C.
double tempExcursao( int32_t m ) {
  double t = tempGeladeira( m );
  const double dia = minutoDoDia( m );
  if ( m >= kAgora - 1440 && dia >= 380 && dia < 425 ) t += 3.6 * sin( kPi * ( dia - 380 ) / 45.0 );
  return t;
}

// A freezer at about -18,6 °C, a cycle of 74 minutes, and its defrost at 03:00:
// 20 minutes up to -14,5.
double tempFreezer( int32_t m ) {
  const double dia = minutoDoDia( m );
  double t = -18.6 + 0.9 * sin( 2 * kPi * m / 74.0 ) + 0.2 * sin( 2 * kPi * ( dia - 200 ) / 1440.0 );
  if ( dia >= 180 && dia < 200 ) t += 3.9 * sin( kPi * ( dia - 180 ) / 20.0 );
  return t + 0.05 * ruido( m );
}

// A room: 22 °C swinging by a degree through the day.
double tempSala( int32_t m ) {
  const double dia = minutoDoDia( m );
  return 22.4 + 1.1 * sin( 2 * kPi * ( dia - 480 ) / 1440.0 ) + 0.3 * sin( 2 * kPi * m / 60.0 ) + 0.05 * ruido( m );
}

// The room's humidity, 45 to 67 %UR.
double umidSala( int32_t m ) {
  const double dia = minutoDoDia( m );
  return 56.0 + 9.0 * sin( 2 * kPi * ( dia - 400 ) / 1440.0 ) + 1.2 * sin( 2 * kPi * m / 97.0 ) + 0.2 * ruido( m );
}

// Pressure, 1009 to 1016,5 hPa over a few days.
double pressaoSala( int32_t m ) {
  return 1012.8 + 3.2 * sin( 2 * kPi * m / ( 1440.0 * 2.3 ) ) + 0.5 * sin( 2 * kPi * m / 180.0 ) + 0.05 * ruido( m );
}

// An hour with holes: 11 empty minutes, a single reading between two gaps, and
// the sensor silent for the last five minutes.
bool lacunasDaHora( int32_t m ) {
  const int32_t off = m - ( kAgora - 60 );
  return !( ( off >= 12 && off <= 22 ) || ( off >= 24 && off <= 27 ) || off >= 55 );
}

struct Cenario {
  double ( *t )( int32_t );       // temperature, °C
  double ( *h )( int32_t );       // humidity, %UR, or nullptr
  double ( *pr )( int32_t );      // pressure, hPa, or nullptr
  bool ( *tem )( int32_t );       // is there a record this minute (nullptr: always)
};

/* ------------------------------------------------------------------ */
/* What the Core 0 does with the records                                */
/* ------------------------------------------------------------------ */

struct Acum {
  int n = 0;
  double soma = 0, soma2 = 0, mx = -1e30, mn = 1e30, primeiro = 0, ultimo = 0;
  int32_t tMx = 0, tMn = 0, tUltimo = 0;
  void add( double v, int32_t m ) {
    if ( n == 0 ) primeiro = v;
    n++;
    soma += v;
    soma2 += v * v;
    if ( v > mx ) { mx = v; tMx = m; }
    if ( v < mn ) { mn = v; tMn = m; }
    ultimo = v;
    tUltimo = m;
  }
  double media( ) const { return n ? soma / n : 0; }
  // n - 1, as the Core 0 does.
  double desvio( ) const {
    if ( n < 2 ) return 0;
    const double v = ( soma2 - soma * soma / n ) / ( n - 1 );
    return v > 0 ? sqrt( v ) : 0;
  }
};

constexpr int kMinutos[N_PERIODOS] = { 60, 360, 720, 1440, 10080 };

struct Janela { int32_t ini, fim, dur; };

// The window of a period, `atras` windows before now (EVT_GRAPH_NAV's steps).
Janela janelaDe( int periodo, int atras ) {
  Janela j;
  j.dur = kMinutos[periodo];
  j.fim = kAgora - (int32_t)atras * j.dur;
  j.ini = j.fim - j.dur;
  return j;
}

EstatDetalhe estat( Grandeza g, const Acum &a ) {
  EstatDetalhe e;
  memset( &e, 0, sizeof e );
  e.grandeza = g;
  e.leituras = (uint16_t)a.n;
  if ( !a.n ) {
    e.maximo = e.minimo = e.media = e.desvio = e.variacao = e.agora = SEM_DADO;
    return e;
  }
  e.maximo = dec( a.mx );
  e.minimo = dec( a.mn );
  e.media = dec( a.media( ) );
  e.desvio = dec( a.desvio( ) );
  e.variacao = dec( a.ultimo - a.primeiro );
  e.agora = dec( a.ultimo );
  e.tMaximo = instante( a.tMx );
  e.tMinimo = instante( a.tMn );
  e.tAgora = instante( a.tUltimo );
  return e;
}

// Reads every minute of a window, as the Core 0 reads the day's file.
void ler( const Cenario &c, const Janela &j, Acum *t, Acum *h, Acum *pr ) {
  for ( int32_t m = j.ini; m < j.fim; m++ ) {
    if ( c.tem && !c.tem( m ) ) continue;
    t->add( c.t( m ), m );
    if ( c.h ) h->add( c.h( m ), m );
    if ( c.pr ) pr->add( c.pr( m ), m );
  }
}

VmGrafico construirGrafico( const char *sensor, const Cenario &c, int periodo, int atras, bool temLimites, double limInf, double limSup ) {
  VmGrafico vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.sensor, sensor, sizeof vm.sensor );
  vm.periodo = (uint8_t)periodo;
  vm.passado = atras > 0;
  vm.serie2 = c.h ? Serie2::UMIDADE : c.pr ? Serie2::PRESSAO : Serie2::NENHUMA;
  vm.temLimites = temLimites;
  if ( temLimites ) {
    vm.limiteInf = dec( limInf );
    vm.limiteSup = dec( limSup );
  }

  const Janela j = janelaDe( periodo, atras );
  // mín( 200, window / the logging interval of one minute ), at least 40.
  int baldes = j.dur < GRAFICO_BALDES ? j.dur : GRAFICO_BALDES;
  if ( baldes < 40 ) baldes = 40;
  vm.n = (uint8_t)baldes;

  double soma[GRAFICO_BALDES] = { }, somaV2[GRAFICO_BALDES] = { }, mn[GRAFICO_BALDES], mx[GRAFICO_BALDES];
  int cnt[GRAFICO_BALDES] = { }, cntV2[GRAFICO_BALDES] = { };
  for ( int b = 0; b < baldes; b++ ) { mn[b] = 1e30; mx[b] = -1e30; }
  Acum t, h, pr;
  for ( int32_t m = j.ini; m < j.fim; m++ ) {
    if ( c.tem && !c.tem( m ) ) continue;
    const double v = c.t( m );
    t.add( v, m );
    const int b = (int)( ( (int64_t)( m - j.ini ) * baldes ) / j.dur );
    soma[b] += v;
    cnt[b]++;
    if ( v < mn[b] ) mn[b] = v;
    if ( v > mx[b] ) mx[b] = v;
    if ( c.h ) { const double u = c.h( m ); h.add( u, m ); somaV2[b] += u; cntV2[b]++; }
    if ( c.pr ) {
      const double q = c.pr( m );
      pr.add( q, m );
      if ( !c.h ) { somaV2[b] += q; cntV2[b]++; }
    }
  }
  for ( int b = 0; b < baldes; b++ ) {
    vm.bMedia[b] = cnt[b] ? dec( soma[b] / cnt[b] ) : SEM_DADO;
    vm.bMin[b] = cnt[b] ? dec( mn[b] ) : SEM_DADO;
    vm.bMax[b] = cnt[b] ? dec( mx[b] ) : SEM_DADO;
    vm.bMedia2[b] = cntV2[b] ? dec( somaV2[b] / cntV2[b] ) : SEM_DADO;
  }
  vm.eixo[0] = instante( j.ini );
  vm.eixo[1] = instante( j.ini + j.dur / 2 );
  vm.eixo[2] = instante( j.fim );
  vm.maximo = dec( t.mx );
  vm.minimo = dec( t.mn );
  vm.media = dec( t.media( ) );
  vm.desvio = dec( t.desvio( ) );
  vm.variacao = dec( t.ultimo - t.primeiro );
  vm.agora = dec( t.ultimo );
  vm.tMaximo = instante( t.tMx );
  vm.tMinimo = instante( t.tMn );
  return vm;
}

// A window the Core 0 found nothing in: no buckets, nothing computed.
VmGrafico graficoVazio( const char *sensor, int periodo, int atras ) {
  VmGrafico vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.sensor, sensor, sizeof vm.sensor );
  vm.periodo = (uint8_t)periodo;
  vm.passado = atras > 0;
  const Janela j = janelaDe( periodo, atras );
  vm.eixo[0] = instante( j.ini );
  vm.eixo[1] = instante( j.ini + j.dur / 2 );
  vm.eixo[2] = instante( j.fim );
  vm.maximo = vm.minimo = vm.media = vm.desvio = vm.variacao = vm.agora = SEM_DADO;
  return vm;
}

VmDetalhe construirDetalhe( const Cenario &c, int periodo, int atras, int pagina ) {
  VmDetalhe vm;
  memset( &vm, 0, sizeof vm );
  vm.periodo = (uint8_t)periodo;
  vm.passado = atras > 0;
  const Janela j = janelaDe( periodo, atras );
  Acum t, h, pr;
  ler( c, j, &t, &h, &pr );
  vm.est[0] = estat( Grandeza::TEMPERATURA, t );
  vm.paginas = 1;
  if ( c.h ) vm.est[vm.paginas++] = estat( Grandeza::UMIDADE, h );
  if ( c.pr ) vm.est[vm.paginas++] = estat( Grandeza::PRESSAO, pr );
  vm.pagina = (uint8_t)pagina;
  return vm;
}

/* ------------------------------------------------------------------ */
/* The sensors                                                          */
/* ------------------------------------------------------------------ */

const Cenario kGeladeira = { tempGeladeira, nullptr, nullptr, nullptr };
const Cenario kGeladeiraUmid = { tempGeladeira, umidSala, nullptr, nullptr };
const Cenario kGeladeiraLacunas = { tempGeladeira, nullptr, nullptr, lacunasDaHora };
const Cenario kExcursao = { tempExcursao, nullptr, nullptr, nullptr };
const Cenario kFreezer = { tempFreezer, nullptr, nullptr, nullptr };
const Cenario kBmp = { tempSala, nullptr, pressaoSala, nullptr };
const Cenario kBme = { tempSala, umidSala, pressaoSala, nullptr };

// The longest name a sensor can have: 31 characters.
const char *const kNome31 = "Geladeira de vacinas do bloco B";

/* ------------------------------------------------------------------ */
/* The chart                                                            */
/* ------------------------------------------------------------------ */

const VmGrafico &g24hUmidade( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeiraUmid, 3, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &g24h( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeira, 3, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &g6h( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeiraUmid, 1, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &g7d( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeiraUmid, 4, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &g1hLacunas( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeiraLacunas, 0, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &gSemDados( ) { static const VmGrafico vm = graficoVazio( "Vacinas", 3, 0 ); return vm; }
const VmGrafico &gPassado( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kGeladeiraUmid, 3, 3, true, 2.0, 8.0 ); return vm; }
// The week's window asked for after a tap on "‹", still being read.
const VmGrafico &gOcupado( ) {
  static const VmGrafico vm = [] {
    VmGrafico v = construirGrafico( "Vacinas", kGeladeiraUmid, 3, 0, true, 2.0, 8.0 );
    v.ocupado = true;
    return v;
  }( );
  return vm;
}
const VmGrafico &gPressao( ) { static const VmGrafico vm = construirGrafico( "Sala de coleta", kBmp, 3, 0, false, 0, 0 ); return vm; }
const VmGrafico &gFreezer( ) { static const VmGrafico vm = construirGrafico( "Freezer", kFreezer, 3, 0, true, -25.0, -15.0 ); return vm; }
const VmGrafico &gFreezer7d( ) { static const VmGrafico vm = construirGrafico( "Freezer", kFreezer, 4, 0, true, -25.0, -15.0 ); return vm; }
const VmGrafico &gExcursao( ) { static const VmGrafico vm = construirGrafico( "Vacinas", kExcursao, 3, 0, true, 2.0, 8.0 ); return vm; }
const VmGrafico &gNome31( ) { static const VmGrafico vm = construirGrafico( kNome31, kGeladeiraUmid, 2, 0, true, 2.0, 8.0 ); return vm; }
// A sensor that stopped answering this morning: the last readings are gone and
// "agora" is not known.
const VmGrafico &gErroSensor( ) {
  static const VmGrafico vm = [] {
    VmGrafico v = construirGrafico( "Geladeira 2", kGeladeira, 3, 0, true, 2.0, 8.0 );
    for ( int b = v.n - 24; b < v.n; b++ ) v.bMedia[b] = v.bMin[b] = v.bMax[b] = v.bMedia2[b] = SEM_DADO;
    v.agora = SEM_DADO;
    v.variacao = SEM_DADO;
    return v;
  }( );
  return vm;
}

/* ------------------------------------------------------------------ */
/* The detail                                                           */
/* ------------------------------------------------------------------ */

const VmDetalhe &dTemperatura( ) { static const VmDetalhe vm = construirDetalhe( kBme, 3, 0, 0 ); return vm; }
const VmDetalhe &dUmidade( ) { static const VmDetalhe vm = construirDetalhe( kBme, 3, 0, 1 ); return vm; }
const VmDetalhe &dPressao( ) { static const VmDetalhe vm = construirDetalhe( kBme, 3, 0, 2 ); return vm; }
const VmDetalhe &dFreezer( ) { static const VmDetalhe vm = construirDetalhe( kFreezer, 3, 0, 0 ); return vm; }
const VmDetalhe &dSemana( ) { static const VmDetalhe vm = construirDetalhe( kBme, 4, 1, 0 ); return vm; }
const VmDetalhe &dSemDados( ) {
  static const VmDetalhe vm = [] {
    VmDetalhe v = construirDetalhe( kGeladeira, 3, 0, 0 );
    v.est[0] = estat( Grandeza::TEMPERATURA, Acum( ) );
    return v;
  }( );
  return vm;
}

/* ------------------------------------------------------------------ */
/* Loading, calendar, update                                            */
/* ------------------------------------------------------------------ */

VmCarregando carregando( const char *sensor, int periodo, int fase ) {
  VmCarregando vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.sensor, sensor, sizeof vm.sensor );
  vm.periodo = (uint8_t)periodo;
  vm.fase = (uint8_t)fase;
  return vm;
}

const VmCarregando kCarregando = carregando( "Vacinas", 3, 0 );
const VmCarregando kCarregandoNome31 = carregando( kNome31, 4, 3 );
const VmCarregando kCarregandoFase = carregando( "Freezer", 0, 9 );
// The glue could not tell which sensor was asked for.
const VmCarregando kCarregandoSemNome = carregando( "", 3, 6 );

// A month and the days of it that have history, as getHistoryDaysMask( ) gives them.
VmCalendario calendario( int ano, int mes, int hoje, bool avanca, uint32_t dias ) {
  VmCalendario vm;
  memset( &vm, 0, sizeof vm );
  vm.ano = (uint16_t)ano;
  vm.mes = (uint8_t)mes;
  vm.hoje = (uint8_t)hoje;
  vm.avanca = avanca;
  vm.dias = dias;
  return vm;
}

constexpr uint32_t bitDia( int d ) { return 1u << d; }
constexpr uint32_t intervalo( int de, int ate ) {
  uint32_t m = 0;
  for ( int d = de; d <= ate; d++ ) m |= bitDia( d );
  return m;
}

// October 2026: the logger has run since the 1st, today is the 13th.
const VmCalendario kCalOutubro = calendario( 2026, 10, 13, false, intervalo( 1, 13 ) );
// July 2026, a month before the logger was installed.
const VmCalendario kCalSemDados = calendario( 2026, 7, 0, true, 0 );
// The month before October, asked for with "‹" and still being read.
const VmCalendario kCalOcupado = [] {
  VmCalendario v = calendario( 2026, 10, 13, false, intervalo( 1, 13 ) );
  v.ocupado = true;
  return v;
}( );
// February 2026 starts on a Sunday and has 28 days: four rows, the card half empty.
const VmCalendario kCalFevereiro = calendario( 2026, 2, 0, true, intervalo( 3, 27 ) );
// August 2026 starts on a Saturday and has 31 days: six rows.
const VmCalendario kCalSeisSemanas = calendario( 2026, 8, 0, true, intervalo( 1, 2 ) | intervalo( 5, 9 ) | intervalo( 12, 16 ) | bitDia( 19 ) | intervalo( 23, 31 ) );
// February 2028 has a 29th.
const VmCalendario kCalBissexto = calendario( 2028, 2, 0, true, intervalo( 1, 29 ) );
// September: the name is long enough to give way to its three letters.
const VmCalendario kCalSetembro = calendario( 2026, 9, 0, true, intervalo( 2, 30 ) & ~bitDia( 14 ) & ~bitDia( 15 ) );

VmOta ota( FaseOta fase, MotivoOta motivo, int pct ) {
  VmOta vm;
  memset( &vm, 0, sizeof vm );
  vm.fase = fase;
  vm.motivo = motivo;
  vm.pct = (uint8_t)pct;
  return vm;
}

const VmOta kOtaRecebendo = ota( FaseOta::RECEBENDO, MotivoOta::NENHUM, 42 );
const VmOta kOtaRecebendo0 = ota( FaseOta::RECEBENDO, MotivoOta::NENHUM, 0 );
const VmOta kOtaRecebendo99 = ota( FaseOta::RECEBENDO, MotivoOta::NENHUM, 99 );
const VmOta kOtaConferindo = ota( FaseOta::CONFERINDO, MotivoOta::NENHUM, 0 );
const VmOta kOtaConferida = ota( FaseOta::CONFERIDA, MotivoOta::NENHUM, 0 );
const VmOta kOtaInstalando = ota( FaseOta::INSTALANDO, MotivoOta::NENHUM, 0 );
const VmOta kOtaInterrompida = ota( FaseOta::INTERROMPIDA, MotivoOta::NENHUM, 0 );
const VmOta kOtaModelo = ota( FaseOta::RECUSADA, MotivoOta::MODELO, 0 );
const VmOta kOtaSemAssinatura = ota( FaseOta::RECUSADA, MotivoOta::SEM_ASSINATURA, 0 );
const VmOta kOtaAssinatura = ota( FaseOta::RECUSADA, MotivoOta::ASSINATURA, 0 );
const VmOta kOtaChaveAposentada = ota( FaseOta::RECUSADA, MotivoOta::CHAVE_APOSENTADA, 0 );
const VmOta kOtaVersaoBloqueada = ota( FaseOta::RECUSADA, MotivoOta::VERSAO_BLOQUEADA, 0 );
const VmOta kOtaChaveBancada = ota( FaseOta::RECUSADA, MotivoOta::CHAVE_DE_BANCADA, 0 );
const VmOta kOtaDanificada = ota( FaseOta::RECUSADA, MotivoOta::DANIFICADA, 0 );

// One draw function and one targets function per view model, as the
// harness wants them: plain function pointers.
template <const VmGrafico &( *F )( )> void desG( Pintor &p ) { desenharGrafico( p, F( ) ); }
template <const VmGrafico &( *F )( )> int alvG( Alvo *o ) { return alvosGrafico( F( ), o ); }
template <const VmDetalhe &( *F )( )> void desD( Pintor &p ) { desenharDetalhe( p, F( ) ); }
template <const VmDetalhe &( *F )( )> int alvD( Alvo *o ) { return alvosDetalhe( F( ), o ); }
template <const VmCarregando &V> void desCa( Pintor &p ) { desenharCarregando( p, V ); }
template <const VmCarregando &V> int alvCa( Alvo *o ) { return alvosCarregando( V, o ); }
template <const VmCalendario &V> void desCl( Pintor &p ) { desenharCalendario( p, V ); }
template <const VmCalendario &V> int alvCl( Alvo *o ) { return alvosCalendario( V, o ); }
template <const VmOta &V> void desO( Pintor &p ) { desenharOta( p, V ); }
template <const VmOta &V> int alvO( Alvo *o ) { return alvosOta( V, o ); }

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  { "carregando_24h", desCa<kCarregando>, alvCa<kCarregando> },
  { "carregando_nome_31", desCa<kCarregandoNome31>, alvCa<kCarregandoNome31> },
  { "carregando_fase_9", desCa<kCarregandoFase>, alvCa<kCarregandoFase> },
  { "carregando_sem_nome", desCa<kCarregandoSemNome>, alvCa<kCarregandoSemNome> },

  { "grafico_24h_umidade", desG<g24hUmidade>, alvG<g24hUmidade> },
  { "grafico_24h", desG<g24h>, alvG<g24h> },
  { "grafico_6h_umidade", desG<g6h>, alvG<g6h> },
  { "grafico_7d_umidade", desG<g7d>, alvG<g7d> },
  { "grafico_1h_lacunas", desG<g1hLacunas>, alvG<g1hLacunas> },
  { "grafico_sem_dados", desG<gSemDados>, alvG<gSemDados> },
  { "grafico_passado", desG<gPassado>, alvG<gPassado> },
  { "grafico_ocupado", desG<gOcupado>, alvG<gOcupado> },
  { "grafico_pressao", desG<gPressao>, alvG<gPressao> },
  { "grafico_freezer_24h", desG<gFreezer>, alvG<gFreezer> },
  { "grafico_freezer_7d", desG<gFreezer7d>, alvG<gFreezer7d> },
  { "grafico_excursao", desG<gExcursao>, alvG<gExcursao> },
  { "grafico_nome_31", desG<gNome31>, alvG<gNome31> },
  { "grafico_erro_sensor", desG<gErroSensor>, alvG<gErroSensor> },

  { "detalhe_temperatura", desD<dTemperatura>, alvD<dTemperatura> },
  { "detalhe_umidade", desD<dUmidade>, alvD<dUmidade> },
  { "detalhe_pressao", desD<dPressao>, alvD<dPressao> },
  { "detalhe_freezer", desD<dFreezer>, alvD<dFreezer> },
  { "detalhe_semana_passado", desD<dSemana>, alvD<dSemana> },
  { "detalhe_sem_dados", desD<dSemDados>, alvD<dSemDados> },

  { "calendario_outubro", desCl<kCalOutubro>, alvCl<kCalOutubro> },
  { "calendario_sem_dados", desCl<kCalSemDados>, alvCl<kCalSemDados> },
  { "calendario_ocupado", desCl<kCalOcupado>, alvCl<kCalOcupado> },
  { "calendario_fevereiro", desCl<kCalFevereiro>, alvCl<kCalFevereiro> },
  { "calendario_seis_semanas", desCl<kCalSeisSemanas>, alvCl<kCalSeisSemanas> },
  { "calendario_bissexto", desCl<kCalBissexto>, alvCl<kCalBissexto> },
  { "calendario_setembro", desCl<kCalSetembro>, alvCl<kCalSetembro> },

  { "ota_recebendo_42", desO<kOtaRecebendo>, alvO<kOtaRecebendo> },
  { "ota_recebendo_0", desO<kOtaRecebendo0>, alvO<kOtaRecebendo0> },
  { "ota_recebendo_99", desO<kOtaRecebendo99>, alvO<kOtaRecebendo99> },
  { "ota_conferindo", desO<kOtaConferindo>, alvO<kOtaConferindo> },
  { "ota_conferida", desO<kOtaConferida>, alvO<kOtaConferida> },
  { "ota_instalando", desO<kOtaInstalando>, alvO<kOtaInstalando> },
  { "ota_interrompida", desO<kOtaInterrompida>, alvO<kOtaInterrompida> },
  { "ota_recusada_modelo", desO<kOtaModelo>, alvO<kOtaModelo> },
  { "ota_recusada_sem_assinatura", desO<kOtaSemAssinatura>, alvO<kOtaSemAssinatura> },
  { "ota_recusada_assinatura", desO<kOtaAssinatura>, alvO<kOtaAssinatura> },
  { "ota_recusada_chave_aposentada", desO<kOtaChaveAposentada>, alvO<kOtaChaveAposentada> },
  { "ota_recusada_versao_bloqueada", desO<kOtaVersaoBloqueada>, alvO<kOtaVersaoBloqueada> },
  { "ota_recusada_chave_bancada", desO<kOtaChaveBancada>, alvO<kOtaChaveBancada> },
  { "ota_recusada_danificada", desO<kOtaDanificada>, alvO<kOtaDanificada> },
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
