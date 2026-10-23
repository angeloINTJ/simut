// Samples of the panel group's screens (src/ui/angulo/telas/PainelPrincipal.h,
// PainelAlarme.h, PainelBoot.h): the sensors, the alarm's phases and the
// bottom row as the device would fill them. See Amostras.h.
#include <string.h>

#include "Amostras.h"
#include "PainelAlarme.h"
#include "PainelBoot.h"
#include "PainelPrincipal.h"

using namespace ua;
using namespace ua::tela;

namespace {

using Sensor = VmPainel::Sensor;
using Tipo = VmPainel::Tipo;
using NavTipo = VmPainel::NavTipo;
constexpr int16_t SEM = VmPainel::SEM_VALOR;

/* 31 characters, the longest a sensor's name can be, with descenders (g) that
 * the big number under the name must clear. */
constexpr char kNomeLongo[] = "Refrigerador de vacinas - Bloco";
static_assert( sizeof kNomeLongo - 1 == 31, "the longest name is 31 characters" );

/* ------------------------------------------------------------------ */
/* Builders                                                             */
/* ------------------------------------------------------------------ */

Sensor sensor( const char *nome, Tipo tipo, int temp, int umid, int pres, int lo, int hi, int tMin, int tMax, int uMin,
               int uMax ) {
  Sensor s;
  memset( &s, 0, sizeof s );
  copiar( s.nome, nome, sizeof s.nome );
  s.tipo = tipo;
  s.temp = (int16_t)temp;
  s.umid = (int16_t)umid;
  s.pres = (uint16_t)pres;
  s.lo = (int16_t)lo;
  s.hi = (int16_t)hi;
  s.tMin = (int16_t)tMin;
  s.tMax = (int16_t)tMax;
  s.uMin = (int16_t)uMin;
  s.uMax = (int16_t)uMax;
  return s;
}

// The four sensors of the bench: slots 0..3.
Sensor sala( ) { return sensor( "Sala de coleta", Tipo::BME280, 224, 460, 1013, 150, 300, 198, 253, 410, 520 ); }
Sensor vacinas( ) { return sensor( "Vacinas", Tipo::DHT22, 46, 580, 0, 20, 80, 31, 58, 510, 640 ); }
Sensor geladeira2( ) { return sensor( "Geladeira 2", Tipo::DS18B20, 52, 0, 0, 20, 80, 41, 61, SEM, SEM ); }
Sensor freezer( ) { return sensor( "Freezer", Tipo::DS18B20, -184, 0, 0, -250, -150, -196, -169, SEM, SEM ); }
Sensor semSensor( ) {
  Sensor s;
  memset( &s, 0, sizeof s );
  return s;
}

Sensor emAlarme( Sensor s, int temp ) {
  s.temp = (int16_t)temp;
  s.alarme = true;
  return s;
}
Sensor comMinMax( Sensor s ) {
  s.minmax = true;
  return s;
}
Sensor comErro( Sensor s ) {
  s.erro = true;
  s.alarme = true;
  return s;
}

/* buildDashLayout( ) of DisplayManager_Panel.cpp: a button per active slot
 * but the pinned one, then the gear; up to five in a row, or four per page and
 * the page button at position 4. */
void montarNav( VmPainel &vm, uint16_t ativos, int fixado, int pagina, int selecionado, uint16_t alarmes ) {
  VmPainel::Nav todos[18];
  int total = 0;
  for ( int i = 0; i < 16; i++ )
    if ( ( ativos >> i & 1 ) && i != fixado )
      todos[total++] = VmPainel::Nav{ NavTipo::SLOT, (uint8_t)i, i == selecionado, ( alarmes >> i & 1 ) != 0 };
  todos[total++] = VmPainel::Nav{ NavTipo::CFG, 0, false, false };
  const bool paginar = total > 5;
  const int porPagina = paginar ? 4 : 5;
  const int paginas = ( total + porPagina - 1 ) / porPagina;
  if ( pagina >= paginas ) pagina = 0;
  const int primeiro = pagina * porPagina;
  int ultimo = primeiro + porPagina;
  if ( ultimo > total ) ultimo = total;
  int pos = 0;
  bool naPagina[16] = { };
  for ( int i = primeiro; i < ultimo; i++ ) {
    vm.nav[pos++] = todos[i];
    if ( todos[i].tipo == NavTipo::SLOT ) naPagina[todos[i].slot] = true;
  }
  if ( paginar ) {
    // An alarm in an active slot with no button on this page, as the old code counts it.
    bool outra = false;
    for ( int i = 0; i < 16; i++ )
      if ( ( ativos >> i & 1 ) && ( alarmes >> i & 1 ) && !naPagina[i] ) outra = true;
    vm.nav[4] = VmPainel::Nav{ NavTipo::PAGINA, 0, false, outra };
  }
  vm.pagina = (uint8_t)( pagina + 1 );
  vm.paginas = (uint8_t)paginas;
}

VmStatus statusBase( ) {
  VmStatus s;
  memset( &s, 0, sizeof s );
  copiar( s.hora, "11:32", sizeof s.hora );
  s.rede = Rede::CABO;
  return s;
}

/* The bench's four sensors on slots 0..3 and the lower card on `selecionado`;
 * the upper card follows the selection unless `fixado` names a slot. */
struct Cenario {
  Sensor slots[16];
  int selecionado;
  int fixado;          // -1: the upper card follows the selection
  int pagina;
  uint16_t ativos, alarmes;
};

Cenario bancada( int selecionado, int fixado = -1 ) {
  Cenario c;
  memset( &c, 0, sizeof c );
  c.slots[0] = sala( );
  c.slots[1] = vacinas( );
  c.slots[2] = geladeira2( );
  c.slots[3] = freezer( );
  c.selecionado = selecionado;
  c.fixado = fixado;
  c.ativos = 0x000F;
  return c;
}

VmPainel painel( const Cenario &c ) {
  VmPainel vm;
  memset( &vm, 0, sizeof vm );
  vm.status = statusBase( );
  vm.baixo = c.slots[c.selecionado];
  vm.cima = c.fixado >= 0 ? c.slots[c.fixado] : vm.baixo;
  vm.cimaFixada = c.fixado >= 0;
  montarNav( vm, c.ativos, c.fixado, c.pagina, c.selecionado, c.alarmes );
  return vm;
}

/* The same cards as `painel( c )` with the alarm's phase and silence set. */
VmPainel fase( VmPainel vm, bool acesa, bool silenciado = false ) {
  vm.faseAcesa = acesa;
  vm.silenciado = silenciado;
  return vm;
}

Cenario emAlarmeVacinas( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = emAlarme( c.slots[1], 87 );
  c.alarmes = 1u << 1;
  return c;
}

/* ------------------------------------------------------------------ */
/* The main screen                                                      */
/* ------------------------------------------------------------------ */

const VmPainel kDht22 = painel( bancada( 1 ) );
const VmPainel kDs18b20 = painel( bancada( 2 ) );
const VmPainel kBme280 = painel( bancada( 0 ) );
const VmPainel kCimaFixado = painel( bancada( 1, 0 ) );

VmPainel minMaxCima( ) {
  Cenario c = bancada( 1, 0 );
  c.slots[0] = comMinMax( c.slots[0] );
  return painel( c );
}
VmPainel minMaxBaixo( ) {
  Cenario c = bancada( 1, 0 );
  c.slots[1] = comMinMax( c.slots[1] );
  return painel( c );
}
const VmPainel kMinMaxCima = minMaxCima( );
const VmPainel kMinMaxBaixo = minMaxBaixo( );

// DS18B20 and BMP280 keep only the temperature in min/max mode.
VmPainel minMaxSoTemperatura( ) {
  Cenario c = bancada( 2, 0 );
  c.slots[0] = comMinMax( sensor( "Câmara fria", Tipo::BMP280, 38, 0, 1009, 20, 80, 31, 44, SEM, SEM ) );
  c.slots[2] = comMinMax( c.slots[2] );
  return painel( c );
}
const VmPainel kMinMaxSoTemp = minMaxSoTemperatura( );

// A day with no record yet: every extreme is "--,-".
VmPainel minMaxSemDados( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = comMinMax( sensor( "Vacinas", Tipo::DHT22, 46, 580, 0, 20, 80, SEM, SEM, SEM, SEM ) );
  return painel( c );
}
const VmPainel kMinMaxSemDados = minMaxSemDados( );

const VmPainel kAlarmeApagada = fase( painel( emAlarmeVacinas( ) ), false );
const VmPainel kAlarmeAcesa = fase( painel( emAlarmeVacinas( ) ), true );

// The silenced alarm with its countdown in the status bar, and `pendentes` records waiting (the cable icon is
// always there): the badge has to find its room between the brand and the icons.
VmPainel silenciadoCom( int seg, uint16_t pendentes ) {
  VmPainel vm = fase( painel( emAlarmeVacinas( ) ), false, true );
  vm.status.aviso = Aviso::SILENCIADO;
  vm.status.avisoSeg = (uint16_t)seg;
  vm.status.pendentes = pendentes;
  return vm;
}
const VmPainel kSilenciado = silenciadoCom( 87, 0 );
// 3k waiting leaves 166 px of room: "Silenciado · 87 s" (164) fits it, off the panel's centre.
const VmPainel kSilenciadoPendentes = silenciadoCom( 87, 3000 );
// ... and "Silenciado · 120 s" (174), the countdown's first seconds, does not: the number is not cut, it goes.
const VmPainel kSilenciado120Pendentes = silenciadoCom( 120, 3000 );

// A silenced alarm's cards, in the phase a flash would have lit them: still no flashing.
const VmPainel kSilenciadoFaseAcesa = fase( painel( emAlarmeVacinas( ) ), true, true );

VmPainel abaixo( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = emAlarme( c.slots[1], 14 );
  c.alarmes = 1u << 1;
  return fase( painel( c ), false );
}
const VmPainel kAbaixo = abaixo( );

// Within a tenth of the range of the upper limit: the badge is "No limite", no alarm yet.
VmPainel noLimite( ) {
  Cenario c = bancada( 1 );
  c.slots[1].temp = 76;
  return painel( c );
}
const VmPainel kNoLimite = noLimite( );

// The temperature is inside its range and the slot is in alarm: another channel is out.
VmPainel outroCanal( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = emAlarme( c.slots[1], 46 );
  c.alarmes = 1u << 1;
  return fase( painel( c ), false );
}
const VmPainel kOutroCanal = outroCanal( );

VmPainel erroSensor( bool acesa ) {
  Cenario c = bancada( 1 );
  c.slots[1] = comErro( c.slots[1] );
  c.alarmes = 1u << 1;
  return fase( painel( c ), acesa );
}
const VmPainel kErroApagada = erroSensor( false );
const VmPainel kErroAcesa = erroSensor( true );

// The upper card pinned on a sensor in alarm while the lower one is fine.
VmPainel cimaEmAlarme( bool acesa ) {
  Cenario c = bancada( 1, 3 );
  c.slots[3] = emAlarme( c.slots[3], -132 );
  c.alarmes = 1u << 3;
  return fase( painel( c ), acesa );
}
const VmPainel kCimaAlarmeAcesa = cimaEmAlarme( true );
const VmPainel kCimaAlarmeApagada = cimaEmAlarme( false );

// min/max mode while the slot is in alarm, in the lit phase.
VmPainel minMaxEmAlarme( ) {
  Cenario c = emAlarmeVacinas( );
  c.slots[1].minmax = true;
  return fase( painel( c ), true );
}
const VmPainel kMinMaxAlarme = minMaxEmAlarme( );

/* 15 characters, the most the device keeps of an account's name (16 bytes with the terminator), and 31, what
 * a view model's name can hold: the notice cuts them to the room the bar has. */
constexpr char kConta15[] = "maria.aparecida";
constexpr char kConta31[] = "maria.aparecida.da.silva.santos";
static_assert( sizeof kConta15 - 1 == 15 && sizeof kConta31 - 1 == 31, "the account names are 15 and 31 characters" );

// "Web: <nome>" in the status bar, with `pendentes` records waiting and the given network.
VmPainel avisoWeb( const char *nome, uint16_t pendentes, Rede rede = Rede::CABO ) {
  VmPainel vm = painel( bancada( 1 ) );
  vm.status.aviso = Aviso::WEB;
  copiar( vm.status.avisoNome, nome, sizeof vm.status.avisoNome );
  vm.status.pendentes = pendentes;
  vm.status.rede = rede;
  return vm;
}
// The spec's example name.
const VmPainel kAvisoWeb = avisoWeb( "maria", 0 );
const VmPainel kAvisoWebCurto = avisoWeb( "ana", 0 );
// 15 characters fit whole beside the cable icon alone ("Web: maria.aparecida" is 183 px of the 184 there are) ...
const VmPainel kAvisoWeb15 = avisoWeb( kConta15, 0 );
// ... and are cut with 3k waiting, which leaves 140 px.
const VmPainel kAvisoWeb15Pendentes = avisoWeb( kConta15, 3000 );
const VmPainel kAvisoWeb31 = avisoWeb( kConta31, 0 );
// The narrowest room: a three-character count leaves 130 px.
const VmPainel kAvisoWeb31Pendentes150 = avisoWeb( kConta31, 150 );
// The bench's case of 2026-10-15: "admin" with 3k records waiting. "Web: admin" is 99 px of the 140 that 3k leaves.
const VmPainel kAvisoWebPendentes = avisoWeb( "admin", 3000 );
// Nothing on the right: the room runs to the panel's margin.
const VmPainel kAvisoWebSemRede = avisoWeb( "admin", 0, Rede::NENHUMA );

const VmPainel kWebOcupada = [] {
  VmPainel vm = painel( bancada( 1 ) );
  vm.webOcupada = true;
  copiar( vm.contaWeb, "admin", sizeof vm.contaWeb );
  return vm;
}( );

// An account name as long as the web keeps (23 bytes): the sentence gives way.
const VmPainel kWebOcupadaNomeLongo = [] {
  VmPainel vm = painel( bancada( 1 ) );
  vm.webOcupada = true;
  copiar( vm.contaWeb, "joao.silva.coordenacao", sizeof vm.contaWeb );
  return vm;
}( );

// Nine active sensors: ten buttons with the gear, three pages.
Cenario novesSensores( int pagina ) {
  Cenario c = bancada( 1 );
  static const char *const nomes[] = { "Sala de coleta", "Vacinas", "Geladeira 2", "Freezer", "Câmara fria",
                                       "Estufa", "Laboratório", "Almoxarifado", "Recepção" };
  for ( int i = 4; i < 9; i++ ) {
    c.slots[i] = sensor( nomes[i], Tipo::DS18B20, 210 + 3 * i, 0, 0, 150, 300, 190, 250, SEM, SEM );
  }
  c.ativos = 0x01FF;
  c.pagina = pagina;
  return c;
}
const VmPainel kNove1 = painel( novesSensores( 0 ) );
const VmPainel kNove3 = [] {
  Cenario c = novesSensores( 2 );
  c.selecionado = 8;
  return painel( c );
}();

// An alarm on a page that is not shown: the page button says so, in both phases.
VmPainel noveComAlarme( bool acesa ) {
  Cenario c = novesSensores( 0 );
  c.slots[6] = emAlarme( c.slots[6], 331 );
  c.alarmes = 1u << 6;
  return fase( painel( c ), acesa );
}
const VmPainel kNoveAlarmeApagada = noveComAlarme( false );
const VmPainel kNoveAlarmeAcesa = noveComAlarme( true );

// No sensor is active: the cards say so and the row has only the gear.
VmPainel nenhumSensor( ) {
  Cenario c = bancada( 0 );
  c.slots[0] = semSensor( );
  c.slots[1] = semSensor( );
  c.slots[2] = semSensor( );
  c.slots[3] = semSensor( );
  c.ativos = 0;
  return painel( c );
}
const VmPainel kNenhum = nenhumSensor( );

VmPainel nome31( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = sensor( kNomeLongo, Tipo::DHT22, 46, 580, 0, 20, 80, 31, 58, 510, 640 );
  return painel( c );
}
const VmPainel kNome31 = nome31( );

// The same name with the badge at its widest: the name gives way, not the badge.
VmPainel nome31Erro( ) {
  Cenario c = bancada( 1 );
  c.slots[1] = comErro( sensor( kNomeLongo, Tipo::DHT22, 46, 580, 0, 20, 80, 31, 58, 510, 640 ) );
  c.alarmes = 1u << 1;
  return fase( painel( c ), false );
}
const VmPainel kNome31Erro = nome31Erro( );

// The upper card pinned, its name the longest, with the pin icon and in min/max mode.
VmPainel nome31Cima( bool minmax ) {
  Cenario c = bancada( 1, 0 );
  c.slots[0] = sensor( kNomeLongo, Tipo::BME280, 224, 460, 1013, 150, 300, 198, 253, 410, 520 );
  c.slots[0].minmax = minmax;
  return painel( c );
}
const VmPainel kNome31Cima = nome31Cima( false );
const VmPainel kNome31CimaMinMax = nome31Cima( true );

// -18.5 °C beside a humidity: the widest temperature next to a second value.
VmPainel negativo( ) {
  Cenario c = bancada( 3 );
  c.slots[3] = sensor( "Freezer", Tipo::DHT22, -185, 680, 0, -250, -150, -196, -169, 640, 710 );
  return painel( c );
}
const VmPainel kNegativo = negativo( );

// ... and beside a pressure, which is wider still.
VmPainel negativoPressao( ) {
  Cenario c = bancada( 3 );
  c.slots[3] = sensor( "Freezer", Tipo::BME280, -185, 680, 1013, -250, -150, -196, -169, 640, 710 );
  return painel( c );
}
const VmPainel kNegativoPressao = negativoPressao( );

// min/max mode with a negative temperature and a 100 % humidity: the widest rows.
VmPainel minMaxLargo( ) {
  Cenario c = bancada( 3, 0 );
  c.slots[0] = comMinMax( sensor( "Freezer", Tipo::DHT22, -185, 1000, 0, -250, -150, -196, -169, 640, 1000 ) );
  c.slots[3] = comMinMax( sensor( "Freezer", Tipo::DHT22, -185, 1000, 0, -250, -150, -196, -169, 640, 1000 ) );
  return painel( c );
}
const VmPainel kMinMaxLargo = minMaxLargo( );

// A hot DS18B20 with the widest digits.
VmPainel quente( ) {
  Cenario c = bancada( 2 );
  c.slots[2] = sensor( "Estufa", Tipo::DS18B20, 1250, 0, 0, 350, 380, 360, 1250, SEM, SEM );
  return painel( c );
}
const VmPainel kQuente = quente( );

VmPainel horaProvisoria( ) {
  VmPainel vm = painel( bancada( 1 ) );
  vm.status.horaProvisoria = true;
  return vm;
}
const VmPainel kHoraProvisoria = horaProvisoria( );

VmPainel pendentes( uint16_t n, uint8_t envio ) {
  VmPainel vm = painel( bancada( 1 ) );
  vm.status.pendentes = n;
  vm.status.envio = envio;
  return vm;
}
const VmPainel kPendentes12 = pendentes( 12, 0 );
const VmPainel kPendentes1k2 = pendentes( 1200, 2 );

const VmPainel kCaboSemLink = [] {
  VmPainel vm = painel( bancada( 1 ) );
  vm.status.rede = Rede::CABO_SEM_LINK;
  return vm;
}( );

/* ------------------------------------------------------------------ */
/* The alarm's action screen                                            */
/* ------------------------------------------------------------------ */

VmAcaoAlarme acao( const char *nome, VmAcaoAlarme::Causa causa, int temp, int limite ) {
  VmAcaoAlarme vm;
  memset( &vm, 0, sizeof vm );
  vm.status = statusBase( );
  copiar( vm.nome, nome, sizeof vm.nome );
  vm.causa = causa;
  vm.temp = (int16_t)temp;
  vm.limite = (int16_t)limite;
  vm.silencioSeg = 120;
  return vm;
}

const VmAcaoAlarme kAcaoAcima = acao( "Vacinas", VmAcaoAlarme::Causa::ACIMA, 87, 80 );
const VmAcaoAlarme kAcaoAbaixo = acao( "Freezer", VmAcaoAlarme::Causa::ABAIXO, -271, -250 );
const VmAcaoAlarme kAcaoErro = acao( "Vacinas", VmAcaoAlarme::Causa::ERRO, 0, 0 );
const VmAcaoAlarme kAcaoNomeLongo = acao( kNomeLongo, VmAcaoAlarme::Causa::ACIMA, 87, 80 );
const VmAcaoAlarme kAcaoFora = acao( "Sala de coleta", VmAcaoAlarme::Causa::FORA, 224, 0 );

// The widest cause with the longest name: the name gives way to " com erro no sensor".
const VmAcaoAlarme kAcaoErroNomeLongo = acao( kNomeLongo, VmAcaoAlarme::Causa::ERRO, 0, 0 );

/* ------------------------------------------------------------------ */
/* The boot screen                                                      */
/* ------------------------------------------------------------------ */

VmBoot boot( const char *const *passos, int n, int ap, bool pular ) {
  VmBoot vm;
  memset( &vm, 0, sizeof vm );
  for ( int i = 0; i < n; i++ ) copiar( vm.passos[i], passos[i], sizeof vm.passos[i] );
  vm.nPassos = (uint8_t)n;
  copiar( vm.versao, "2.11.1", sizeof vm.versao );
  copiar( vm.mac, "28:cd:c1:15:4e:99", sizeof vm.mac );
  vm.apProgressPct = (int8_t)ap;
  vm.showSkipButton = pular;
  return vm;
}

const char *const kPassosInicio[] = { "Montando Sistema de Arquivos..." };
const char *const kPassosRede[] = { "Iniciando Interface de Comando...", "Carregando Tema & Idioma...",
                                    "Carregando Periféricos & Sensores...", "Iniciando Interface Wi-Fi...",
                                    "Aguardando roteador..." };
const char *const kPassosAp[] = { "Mantenha a tela pressionada: Modo AP..." };
const char *const kPassosLongos[] = {
  "Conecte à rede SIMUT_SETUP  PSK 7f3a91c2d4e85b60a1c97f3a91c2d4e8",
  "Acesse no celular: 192.168.4.1 (abra o navegador e digite o endereço)",
  "Iniciando Servidor de Telemetria e Servidor Web com todas as rotas...",
  "Carregando cache diário Min/Max dos 11 sensores ativos do aparelho...",
  "Sincronizando Relógio Global com o servidor de horário da rede local...",
};

const VmBoot kBootInicio = boot( kPassosInicio, 1, -1, false );
const VmBoot kBootRede = boot( kPassosRede, 5, -1, false );
const VmBoot kBootAp = boot( kPassosAp, 1, 40, false );
const VmBoot kBootPular = boot( kPassosRede, 5, -1, true );
const VmBoot kBootLongos = boot( kPassosLongos, 5, -1, false );
const VmBoot kBootPularLongos = boot( kPassosLongos, 5, -1, true );
const VmBoot kBootApCheio = boot( kPassosAp, 1, 100, false );

// One sample of the main screen, of the action screen, of the boot screen.
#define PAINEL( nome, vm ) \
  { nome, []( Pintor &p ) { desenharPainel( p, vm ); }, []( Alvo *o ) { return alvosPainel( vm, o ); } }
#define ACAO( nome, vm ) \
  { nome, []( Pintor &p ) { desenharAcaoAlarme( p, vm ); }, []( Alvo *o ) { return alvosAcaoAlarme( vm, o ); } }
#define BOOT( nome, vm ) \
  { nome, []( Pintor &p ) { desenharBoot( p, vm ); }, []( Alvo *o ) { return alvosBoot( vm, o ); } }

}  // namespace

namespace amostra {

const Amostra kAmostras[] = {
  PAINEL( "painel_dht22", kDht22 ),
  PAINEL( "painel_ds18b20", kDs18b20 ),
  PAINEL( "painel_bme280", kBme280 ),
  PAINEL( "painel_cima_fixado", kCimaFixado ),
  PAINEL( "painel_minmax_cima", kMinMaxCima ),
  PAINEL( "painel_minmax_baixo", kMinMaxBaixo ),
  PAINEL( "painel_minmax_so_temperatura", kMinMaxSoTemp ),
  PAINEL( "painel_minmax_sem_dados", kMinMaxSemDados ),
  PAINEL( "painel_minmax_largo", kMinMaxLargo ),
  PAINEL( "painel_minmax_alarme_acesa", kMinMaxAlarme ),
  PAINEL( "painel_alarme_apagada", kAlarmeApagada ),
  PAINEL( "painel_alarme_acesa", kAlarmeAcesa ),
  PAINEL( "painel_silenciado", kSilenciado ),
  PAINEL( "painel_silenciado_pendentes_3k", kSilenciadoPendentes ),
  PAINEL( "painel_silenciado_120_pendentes_3k", kSilenciado120Pendentes ),
  PAINEL( "painel_silenciado_fase_acesa", kSilenciadoFaseAcesa ),
  PAINEL( "painel_abaixo", kAbaixo ),
  PAINEL( "painel_no_limite", kNoLimite ),
  PAINEL( "painel_outro_canal", kOutroCanal ),
  PAINEL( "painel_erro_sensor_apagada", kErroApagada ),
  PAINEL( "painel_erro_sensor_acesa", kErroAcesa ),
  PAINEL( "painel_cima_alarme_apagada", kCimaAlarmeApagada ),
  PAINEL( "painel_cima_alarme_acesa", kCimaAlarmeAcesa ),
  PAINEL( "painel_aviso_web", kAvisoWeb ),
  PAINEL( "painel_aviso_web_nome_curto", kAvisoWebCurto ),
  PAINEL( "painel_aviso_web_nome_15", kAvisoWeb15 ),
  PAINEL( "painel_aviso_web_nome_15_pendentes_3k", kAvisoWeb15Pendentes ),
  PAINEL( "painel_aviso_web_nome_31", kAvisoWeb31 ),
  PAINEL( "painel_aviso_web_nome_31_pendentes_150", kAvisoWeb31Pendentes150 ),
  PAINEL( "painel_aviso_web_pendentes_3k", kAvisoWebPendentes ),
  PAINEL( "painel_aviso_web_sem_rede", kAvisoWebSemRede ),
  PAINEL( "painel_web_ocupada", kWebOcupada ),
  PAINEL( "painel_web_ocupada_nome_longo", kWebOcupadaNomeLongo ),
  PAINEL( "painel_9_sensores_p1", kNove1 ),
  PAINEL( "painel_9_sensores_p3", kNove3 ),
  PAINEL( "painel_9_sensores_alarme_apagada", kNoveAlarmeApagada ),
  PAINEL( "painel_9_sensores_alarme_acesa", kNoveAlarmeAcesa ),
  PAINEL( "painel_nenhum_sensor", kNenhum ),
  PAINEL( "painel_nome_31", kNome31 ),
  PAINEL( "painel_nome_31_erro", kNome31Erro ),
  PAINEL( "painel_nome_31_cima", kNome31Cima ),
  PAINEL( "painel_nome_31_cima_minmax", kNome31CimaMinMax ),
  PAINEL( "painel_negativo", kNegativo ),
  PAINEL( "painel_negativo_pressao", kNegativoPressao ),
  PAINEL( "painel_quente", kQuente ),
  PAINEL( "painel_hora_provisoria", kHoraProvisoria ),
  PAINEL( "painel_pendentes_12", kPendentes12 ),
  PAINEL( "painel_pendentes_1k2_falha", kPendentes1k2 ),
  PAINEL( "painel_cabo_sem_link", kCaboSemLink ),
  ACAO( "acao_acima", kAcaoAcima ),
  ACAO( "acao_abaixo", kAcaoAbaixo ),
  ACAO( "acao_erro", kAcaoErro ),
  ACAO( "acao_fora", kAcaoFora ),
  ACAO( "acao_nome_longo", kAcaoNomeLongo ),
  ACAO( "acao_erro_nome_longo", kAcaoErroNomeLongo ),
  BOOT( "boot_inicio", kBootInicio ),
  BOOT( "boot_rede", kBootRede ),
  BOOT( "boot_ap_40", kBootAp ),
  BOOT( "boot_ap_100", kBootApCheio ),
  BOOT( "boot_pular", kBootPular ),
  BOOT( "boot_textos_longos", kBootLongos ),
  BOOT( "boot_pular_textos_longos", kBootPularLongos ),
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
