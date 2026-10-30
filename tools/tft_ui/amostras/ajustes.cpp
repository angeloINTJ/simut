// Samples of the ajustes group's screens (src/ui/angulo/telas/Ajustes.h), with
// the states and the extreme cases ESPEC_UI_A.md §6.3 lists. See Amostras.h.
#include <string.h>

#include "Amostras.h"
#include "Ajustes.h"

using namespace ua;
using namespace ua::tela;

namespace {

// A name of exactly 31 characters, the longest a sensor has.
const char *const kNome31 = "Sala de coleta de amostras fria";
// The widest letters, 31 of them: what the title cut has to survive.
const char *const kNomeLargo = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";

/* ------------------------------------------------------------------ */
/* Limites                                                              */
/* ------------------------------------------------------------------ */

// The channels' ranges (SensorChannelTable.h), in tenths.
constexpr int32_t T_PISO = -3277, T_TETO = 3277, U_PISO = 0, U_TETO = 1023, P_PISO = 0, P_TETO = 16383;

VmLimites limites( const char *sensor, GrandezaLimites g, int pagina, int total, int32_t min, int32_t max, int foco ) {
  VmLimites vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.sensor, sensor, sizeof vm.sensor );
  vm.grandeza = g;
  vm.pagina = (uint8_t)pagina;
  vm.total = (uint8_t)total;
  vm.foco = (uint8_t)foco;
  vm.minimo = min;
  vm.maximo = max;
  vm.piso = g == GLIM_TEMPERATURA ? T_PISO : g == GLIM_UMIDADE ? U_PISO : P_PISO;
  vm.teto = g == GLIM_TEMPERATURA ? T_TETO : g == GLIM_UMIDADE ? U_TETO : P_TETO;
  return vm;
}

const VmLimites kLimTemp = limites( "Vacinas", GLIM_TEMPERATURA, 0, 2, 20, 80, 1 );
const VmLimites kLimUmid = limites( "Vacinas", GLIM_UMIDADE, 1, 2, 300, 600, 0 );
const VmLimites kLimPres = limites( "Sala de coleta", GLIM_PRESSAO, 2, 3, 9800, 10400, 1 );
const VmLimites kLimNeg = limites( "Freezer", GLIM_TEMPERATURA, 0, 1, -250, -150, 0 );
const VmLimites kLimTres = limites( "Sala de coleta", GLIM_UMIDADE, 1, 3, 400, 650, 1 );
const VmLimites kLimLongo = limites( kNome31, GLIM_TEMPERATURA, 0, 3, 20, 80, 1 );
const VmLimites kLimLargo = limites( kNomeLargo, GLIM_TEMPERATURA, 0, 3, 20, 80, 1 );
const VmLimites kLimExtremos = limites( "Ultracongelador", GLIM_TEMPERATURA, 0, 2, T_PISO, T_TETO, 0 );

/* ------------------------------------------------------------------ */
/* Manutenção                                                           */
/* ------------------------------------------------------------------ */

VmManutencao manutencao( const char *sensor, bool aberta, bool permitido, int horas, int minutos, int foco, uint32_t restante ) {
  VmManutencao vm;
  memset( &vm, 0, sizeof vm );
  copiar( vm.sensor, sensor, sizeof vm.sensor );
  vm.aberta = aberta;
  vm.permitido = permitido;
  vm.horas = (uint16_t)horas;
  vm.minutos = (uint8_t)minutos;
  vm.foco = (uint8_t)foco;
  vm.horasMax = 720;
  vm.restanteMin = restante;
  return vm;
}

const VmManutencao kManFechada = manutencao( "Vacinas", false, true, 1, 0, 0, 0 );
const VmManutencao kManAberta = manutencao( "Vacinas", true, true, 0, 0, 0, 89 );
const VmManutencao kManSemPermissao = manutencao( "Vacinas", false, false, 1, 0, 0, 0 );
const VmManutencao kManAbertaSemPermissao = manutencao( "Vacinas", true, false, 0, 0, 0, 89 );
const VmManutencao kManZero = manutencao( "Vacinas", false, true, 0, 0, 1, 0 );
const VmManutencao kManLongo = manutencao( kNome31, false, true, 12, 55, 1, 0 );
const VmManutencao kManLargo = manutencao( kNomeLargo, true, true, 0, 0, 0, 719 );
const VmManutencao kManTeto = manutencao( "Vacinas", false, true, 720, 0, 0, 0 );
const VmManutencao kManMaximaAberta = manutencao( "Vacinas", true, true, 0, 0, 0, 43200 );
const VmManutencao kManUltimosMinutos = manutencao( "Vacinas", true, true, 0, 0, 0, 4 );

/* ------------------------------------------------------------------ */
/* Sons e melodia                                                       */
/* ------------------------------------------------------------------ */

constexpr uint32_t B( int linha ) { return 1u << linha; }

// `ligados` is a mask of LinhaSom; m0..m4 the melody of each sound that has one.
VmSons sons( int pagina, uint32_t ligados, int m0, int m1, int m2, int m3, int m4, int sistema, int alarme, int foco ) {
  VmSons vm;
  memset( &vm, 0, sizeof vm );
  vm.pagina = (uint8_t)pagina;
  vm.foco = (uint8_t)foco;
  for ( int i = 0; i < SOM_N; i++ ) vm.ligado[i] = ( ligados >> i ) & 1;
  const int m[SOM_COM_MELODIA] = { m0, m1, m2, m3, m4 };
  for ( int i = 0; i < SOM_COM_MELODIA; i++ ) vm.melodia[i] = (uint8_t)m[i];
  vm.volumeSistema = (uint8_t)sistema;
  vm.volumeAlarme = (uint8_t)alarme;
  return vm;
}

const VmSons kSonsP1 = sons( 0, B( SOM_TOQUE ) | B( SOM_CONFIRMACAO ) | B( SOM_ERRO ) | B( SOM_ALARME ), 0, 1, 1, 1, 3, 40, 80, 1 );
const VmSons kSonsP1Desl = sons( 0, B( SOM_CONFIRMACAO ) | B( SOM_ALARME ), 0, 2, 1, 0, 3, 40, 80, 1 );
const VmSons kSonsP2 = sons( 1, B( SOM_ATENCAO ), 0, 1, 1, 1, 3, 40, 80, 1 );
const VmSons kSonsP2Mudo = sons( 1, B( SOM_WEB ) | B( SOM_MUDO ), 0, 1, 1, 1, 3, 40, 80, 1 );
const VmSons kSonsP3 = sons( 2, B( SOM_TOQUE ), 0, 1, 1, 1, 3, 40, 80, 1 );
const VmSons kSonsP3Extremos = sons( 2, 0, 0, 1, 1, 1, 3, 0, 100, 0 );

VmMelodia melodia( int som, int escolhida ) {
  VmMelodia vm;
  memset( &vm, 0, sizeof vm );
  vm.som = (uint8_t)som;
  vm.escolhida = (uint8_t)escolhida;
  return vm;
}

const VmMelodia kMelAlarme = melodia( SOM_ALARME, 1 );
const VmMelodia kMelToque = melodia( SOM_TOQUE, 0 );
const VmMelodia kMelAtencao = melodia( SOM_ATENCAO, 3 );
const VmMelodia kMelConfirmacao = melodia( SOM_CONFIRMACAO, 5 );
const VmMelodia kMelErro = melodia( SOM_ERRO, 4 );

/* ------------------------------------------------------------------ */
/* Segurança do PIN e data e hora                                       */
/* ------------------------------------------------------------------ */

VmPoliticaPin politica( int minimo, int teto, int teclado, int alfabeto ) {
  VmPoliticaPin vm;
  memset( &vm, 0, sizeof vm );
  vm.minimo = (uint8_t)minimo;
  vm.piso = 4;
  vm.teto = (uint8_t)teto;
  vm.teclado = (uint8_t)teclado;
  vm.alfabeto = (uint8_t)alfabeto;
  return vm;
}

// The factory policy: 4 digits at least, four cards, 0-9.
const VmPoliticaPin kPinFabrica = politica( 4, 8, 3, 0 );
const VmPoliticaPin kPinOrdenadoAz = politica( 6, 16, 1, 1 );
const VmPoliticaPin kPinSeisCartoes = politica( 6, 12, 2, 0 );
const VmPoliticaPin kPinDozeCartoesAz = politica( 4, 8, 3, 1 );
const VmPoliticaPin kPinDezoitoCartoesAz = politica( 8, 12, 2, 1 );
const VmPoliticaPin kPinNoTeto = politica( 16, 16, 1, 0 );

VmRelogio relogio( bool boot, int dia, int mes, int ano, int hora, int minuto ) {
  VmRelogio vm;
  memset( &vm, 0, sizeof vm );
  vm.boot = boot;
  vm.dia = (uint8_t)dia;
  vm.mes = (uint8_t)mes;
  vm.ano = (uint16_t)ano;
  vm.hora = (uint8_t)hora;
  vm.minuto = (uint8_t)minuto;
  return vm;
}

const VmRelogio kRelogioMenu = relogio( false, 14, 10, 2026, 14, 32 );
const VmRelogio kRelogioBoot = relogio( true, 14, 10, 2026, 8, 5 );
const VmRelogio kRelogioExtremo = relogio( false, 31, 12, 2099, 23, 59 );

/* ------------------------------------------------------------------ */
/* Alinhar, sensibilidade e calibração                                  */
/* ------------------------------------------------------------------ */

VmAlinhar alinhar( int x, int y ) {
  VmAlinhar vm;
  memset( &vm, 0, sizeof vm );
  vm.x = (int8_t)x;
  vm.y = (int8_t)y;
  vm.limite = 4;
  return vm;
}

const VmAlinhar kAlinhar = alinhar( 1, 0 );
const VmAlinhar kAlinharZero = alinhar( 0, 0 );
const VmAlinhar kAlinharNegativo = alinhar( -2, -3 );
const VmAlinhar kAlinharLimites = alinhar( -4, 4 );

VmSensibilidade sensibilidade( int leituras, int estabilidade, int limiar, bool concluido ) {
  VmSensibilidade vm;
  memset( &vm, 0, sizeof vm );
  vm.leituras = (uint16_t)leituras;
  vm.estabilidade = (uint8_t)estabilidade;
  vm.limiar = (uint16_t)limiar;
  vm.concluido = concluido;
  return vm;
}

const VmSensibilidade kSensComeco = sensibilidade( 2, 6, 0, false );
const VmSensibilidade kSensMetade = sensibilidade( 7, 52, 412, false );
const VmSensibilidade kSensVerde = sensibilidade( 12, 88, 380, false );
const VmSensibilidade kSensConcluido = sensibilidade( 14, 100, 380, true );
// The counters at their widest.
const VmSensibilidade kSensGrande = sensibilidade( 65535, 100, 65535, false );

VmCalibrar calibrar( int resultado, int passo, int volta, bool segurando ) {
  VmCalibrar vm;
  memset( &vm, 0, sizeof vm );
  vm.resultado = (uint8_t)resultado;
  vm.passo = (uint8_t)passo;
  vm.volta = (uint8_t)volta;
  vm.segurando = segurando;
  return vm;
}

const VmCalibrar kCalP1 = calibrar( CALRES_MEDINDO, 0, 1, false );
const VmCalibrar kCalP2 = calibrar( CALRES_MEDINDO, 1, 1, false );
const VmCalibrar kCalP3 = calibrar( CALRES_MEDINDO, 2, 1, false );
const VmCalibrar kCalP4 = calibrar( CALRES_MEDINDO, 3, 1, false );
const VmCalibrar kCalSegurando = calibrar( CALRES_MEDINDO, 1, 1, true );
const VmCalibrar kCalVolta2 = calibrar( CALRES_MEDINDO, 2, 2, false );
const VmCalibrar kCalVolta2Segurando = calibrar( CALRES_MEDINDO, 3, 2, true );
const VmCalibrar kCalConcluida = calibrar( CALRES_CONCLUIDA, 0, 1, false );
const VmCalibrar kCalImprecisa = calibrar( CALRES_IMPRECISA, 0, 1, false );

}  // namespace

namespace amostra {

#define AMOSTRA( nome, desenhar, alvos, vm ) \
  { nome, []( Pintor &p ) { desenhar( p, vm ); }, []( Alvo *o ) { return alvos( vm, o ); } }

const Amostra kAmostras[] = {
  AMOSTRA( "limites_temperatura", desenharLimites, alvosLimites, kLimTemp ),
  AMOSTRA( "limites_umidade", desenharLimites, alvosLimites, kLimUmid ),
  AMOSTRA( "limites_pressao", desenharLimites, alvosLimites, kLimPres ),
  AMOSTRA( "limites_negativo", desenharLimites, alvosLimites, kLimNeg ),
  AMOSTRA( "limites_3_paginas", desenharLimites, alvosLimites, kLimTres ),
  AMOSTRA( "limites_nome_31", desenharLimites, alvosLimites, kLimLongo ),
  AMOSTRA( "limites_nome_largo", desenharLimites, alvosLimites, kLimLargo ),
  AMOSTRA( "limites_extremos", desenharLimites, alvosLimites, kLimExtremos ),
  AMOSTRA( "manut_fechada", desenharManutencao, alvosManutencao, kManFechada ),
  AMOSTRA( "manut_aberta", desenharManutencao, alvosManutencao, kManAberta ),
  AMOSTRA( "manut_sem_permissao", desenharManutencao, alvosManutencao, kManSemPermissao ),
  AMOSTRA( "manut_aberta_sem_permissao", desenharManutencao, alvosManutencao, kManAbertaSemPermissao ),
  AMOSTRA( "manut_zero", desenharManutencao, alvosManutencao, kManZero ),
  AMOSTRA( "manut_nome_31", desenharManutencao, alvosManutencao, kManLongo ),
  AMOSTRA( "manut_nome_largo", desenharManutencao, alvosManutencao, kManLargo ),
  AMOSTRA( "manut_teto_horas", desenharManutencao, alvosManutencao, kManTeto ),
  AMOSTRA( "manut_aberta_30_dias", desenharManutencao, alvosManutencao, kManMaximaAberta ),
  AMOSTRA( "manut_aberta_ultimos_minutos", desenharManutencao, alvosManutencao, kManUltimosMinutos ),
  AMOSTRA( "sons_p1", desenharSons, alvosSons, kSonsP1 ),
  AMOSTRA( "sons_p1_desligados", desenharSons, alvosSons, kSonsP1Desl ),
  AMOSTRA( "sons_p2", desenharSons, alvosSons, kSonsP2 ),
  AMOSTRA( "sons_p2_mudo", desenharSons, alvosSons, kSonsP2Mudo ),
  AMOSTRA( "sons_p3", desenharSons, alvosSons, kSonsP3 ),
  AMOSTRA( "sons_p3_extremos", desenharSons, alvosSons, kSonsP3Extremos ),
  AMOSTRA( "melodia_alarme", desenharMelodia, alvosMelodia, kMelAlarme ),
  AMOSTRA( "melodia_toque", desenharMelodia, alvosMelodia, kMelToque ),
  AMOSTRA( "melodia_atencao", desenharMelodia, alvosMelodia, kMelAtencao ),
  AMOSTRA( "melodia_confirmacao", desenharMelodia, alvosMelodia, kMelConfirmacao ),
  AMOSTRA( "melodia_erro", desenharMelodia, alvosMelodia, kMelErro ),
  AMOSTRA( "pin_fabrica", desenharPoliticaPin, alvosPoliticaPin, kPinFabrica ),
  AMOSTRA( "pin_ordenado_az", desenharPoliticaPin, alvosPoliticaPin, kPinOrdenadoAz ),
  AMOSTRA( "pin_6_cartoes", desenharPoliticaPin, alvosPoliticaPin, kPinSeisCartoes ),
  AMOSTRA( "pin_12_cartoes_az", desenharPoliticaPin, alvosPoliticaPin, kPinDozeCartoesAz ),
  AMOSTRA( "pin_18_cartoes_az", desenharPoliticaPin, alvosPoliticaPin, kPinDezoitoCartoesAz ),
  AMOSTRA( "pin_no_teto", desenharPoliticaPin, alvosPoliticaPin, kPinNoTeto ),
  AMOSTRA( "relogio_menu", desenharRelogio, alvosRelogio, kRelogioMenu ),
  AMOSTRA( "relogio_boot", desenharRelogio, alvosRelogio, kRelogioBoot ),
  AMOSTRA( "relogio_extremo", desenharRelogio, alvosRelogio, kRelogioExtremo ),
  AMOSTRA( "alinhar_ajuste", desenharAlinhar, alvosAlinhar, kAlinhar ),
  AMOSTRA( "alinhar_zero", desenharAlinhar, alvosAlinhar, kAlinharZero ),
  AMOSTRA( "alinhar_negativo", desenharAlinhar, alvosAlinhar, kAlinharNegativo ),
  AMOSTRA( "alinhar_limites", desenharAlinhar, alvosAlinhar, kAlinharLimites ),
  AMOSTRA( "sensibilidade_comeco", desenharSensibilidade, alvosSensibilidade, kSensComeco ),
  AMOSTRA( "sensibilidade_metade", desenharSensibilidade, alvosSensibilidade, kSensMetade ),
  AMOSTRA( "sensibilidade_verde", desenharSensibilidade, alvosSensibilidade, kSensVerde ),
  AMOSTRA( "sensibilidade_concluido", desenharSensibilidade, alvosSensibilidade, kSensConcluido ),
  AMOSTRA( "sensibilidade_numeros_grandes", desenharSensibilidade, alvosSensibilidade, kSensGrande ),
  AMOSTRA( "calibrar_ponto_1", desenharCalibrar, alvosCalibrar, kCalP1 ),
  AMOSTRA( "calibrar_ponto_2", desenharCalibrar, alvosCalibrar, kCalP2 ),
  AMOSTRA( "calibrar_ponto_3", desenharCalibrar, alvosCalibrar, kCalP3 ),
  AMOSTRA( "calibrar_ponto_4", desenharCalibrar, alvosCalibrar, kCalP4 ),
  AMOSTRA( "calibrar_segurando", desenharCalibrar, alvosCalibrar, kCalSegurando ),
  AMOSTRA( "calibrar_volta_2", desenharCalibrar, alvosCalibrar, kCalVolta2 ),
  AMOSTRA( "calibrar_volta_2_segurando", desenharCalibrar, alvosCalibrar, kCalVolta2Segurando ),
  AMOSTRA( "calibrar_concluida", desenharCalibrar, alvosCalibrar, kCalConcluida ),
  AMOSTRA( "calibrar_imprecisa", desenharCalibrar, alvosCalibrar, kCalImprecisa ),
};
const int kAmostrasN = sizeof kAmostras / sizeof kAmostras[0];

}  // namespace amostra
