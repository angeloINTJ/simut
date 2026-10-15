#pragma once
// The pure part of the glue for the graph group (DisplayManager_UiA_Graficos.cpp):
// how the old panel's state, Core 0's GraphDataPackage with its floats, NaNs and
// epochs, becomes the screens' view models (telas/Graficos.h) with their tenths
// and local dates. Nothing here touches the DisplayManager, so the host can run
// it against a package it makes up (it needs three SystemDefs headers and the
// clock's localtime_r( ), nothing else).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "simut_config.h"      /* first, as in SystemDefs.h */
#include "SystemDefs_Limits.h"  /* MAX_SENSORS, MINMAX_SLOT_BOARD_TEMP */
#include "SystemDefs_Records.h" /* GraphDataPackage, SensorRecord, SystemConfig */
#include "sensors/SensorChannels.h" /* CH_TEMP, CH_HUM, CH_PRESS */
#include "telas/Graficos.h"

namespace ua {
namespace cola {

/* A float of Core 0's as the tenths the screens take (4,6 °C is 46); SEM_DADO
 * for a NaN. The model holds up to 3276,7, so a stray sentinel is clamped
 * rather than left to wrap. */
inline int16_t decimos( float v ) {
  if ( isnan( v ) ) return tela::SEM_DADO;
  if ( v > 3200.0f ) v = 3200.0f;
  if ( v < -3200.0f ) v = -3200.0f;
  const float d = v * 10.0f;
  return (int16_t)( d >= 0.0f ? d + 0.5f : d - 0.5f );
}

/* An epoch as the local date and time the old screens print (they call
 * localtime_r( ) for every label); dia = 0 when it is not a time. */
inline void instante( time_t t, tela::Instante &out ) {
  out = tela::Instante{ };
  if ( t <= 0 ) return;
  struct tm ti;
  localtime_r( &t, &ti );
  out.dia = (uint8_t)ti.tm_mday;
  out.mes = (uint8_t)( ti.tm_mon + 1 );
  out.hora = (uint8_t)ti.tm_hour;
  out.minuto = (uint8_t)ti.tm_min;
}

/* The end of a window as the axis says it. A window that ends at local
 * midnight (a day picked in the calendar, or a step back that lands there)
 * would read "00:00" of the next day: its last minute is 23:59, as the old
 * header had it (AppManager_Graph.cpp:573-581). */
inline void fimDaJanela( time_t fim, tela::Instante &out ) {
  if ( fim > 0 ) {
    struct tm ti;
    localtime_r( &fim, &ti );
    if ( ti.tm_hour == 0 && ti.tm_min == 0 ) fim -= 60;
  }
  instante( fim, out );
}

/* The title the Core 0 gives a graph (renderGraphOptimized( ),
 * AppManager_Graph.cpp:168-191): the board's own temperature, the slot's name,
 * or "Sensor n" for a slot that is not active. Empty for no slot at all, which
 * the screens call "Sensor". The names are raw UTF-8, as typed on the web
 * page: the kit draws UTF-8 (tr( ) is Latin-1 and is not used here). */
inline void nomeDoSensor( const SystemConfig *cfg, int idx, char *out, int cap ) {
  if ( idx == (int)MINMAX_SLOT_BOARD_TEMP ) {
    copiar( out, "Board Temp", cap );
  } else if ( idx >= 0 && idx < (int)MAX_SENSORS && cfg && cfg->sensors[idx].active ) {
    copiar( out, cfg->sensors[idx].friendlyName, cap );
  } else if ( idx >= 0 && idx < (int)MAX_SENSORS ) {
    snprintf( out, (size_t)cap, "Sensor %d", idx + 1 );
  } else {
    out[0] = 0;
  }
}

/* What identifies the graph package on hand: the sensor, the period, the
 * window and how much it holds. A new read changes it, even a step back by one
 * window; it is read the way the old draw reads the package, without the lock,
 * and a torn read costs one frame. */
inline uint32_t assinaturaDoGrafico( const GraphDataPackage &g ) {
  uint32_t h = 2166136261u;
  const uint32_t v[6] = { (uint32_t)g.sensorIdx,    (uint32_t)g.timeRange, (uint32_t)g.count,
                          (uint32_t)g.sampleCount, (uint32_t)g.tsCutoff,  (uint32_t)g.tsEnd };
  for ( uint32_t x : v ) {
    h ^= x;
    h *= 16777619u;
  }
  return h;
}

/* The chart's model from the package Core 0 sent (renderGraphOptimized( ),
 * AppManager_Graph.cpp:110-588) and the state around it. `vm` comes zeroed. */
inline void preencherGrafico( tela::VmGrafico &vm, const GraphDataPackage &g, float minHum, bool passado, const SystemConfig *cfg ) {
  using namespace tela;
  copiar( vm.sensor, g.title, sizeof vm.sensor );
  vm.periodo = (uint8_t)( g.timeRange < 0 ? 0 : g.timeRange > 4 ? 4 : g.timeRange );
  vm.passado = passado;

  /* The second curve: the humidity, or the pressure on a part that has no
   * humidity (drawGraphScreen( ), DisplayManager_Graph.cpp:530: hasV2 and
   * v2IsPress). */
  const bool temV2 = ( g.hasHumidity || g.v2IsPress ) && !isnan( minHum );
  vm.serie2 = !temV2 ? Serie2::NENHUMA : g.v2IsPress ? Serie2::PRESSAO : Serie2::UMIDADE;

  /* The alarm limits of the temperature channel, only while the sensor's
   * alarms are on. */
  if ( cfg && g.sensorIdx >= 0 && g.sensorIdx < (int)MAX_SENSORS ) {
    const SensorRecord &sr = cfg->sensors[g.sensorIdx];
    if ( sr.active && sr.alarmsActive ) {
      vm.temLimites = true;
      vm.limiteInf = decimos( sr.chMin[CH_TEMP] );
      vm.limiteSup = decimos( sr.chMax[CH_TEMP] );
    }
  }

  /* The window, start, middle and end. */
  instante( g.tsCutoff, vm.eixo[0] );
  instante( g.tsCutoff + ( g.tsEnd - g.tsCutoff ) / 2, vm.eixo[1] );
  fimDaJanela( g.tsEnd, vm.eixo[2] );

  /* No data is what drawGraphScreen( ) calls hasData (:531): two buckets and a peak. */
  const int n = ( g.count >= 2 && g.count <= GRAFICO_BALDES && g.idxMaxTemp >= 0 ) ? g.count : 0;
  vm.n = (uint8_t)n;
  vm.maximo = vm.minimo = vm.media = vm.desvio = vm.variacao = vm.agora = SEM_DADO;
  if ( n == 0 ) return;

  int ultimo = -1;
  for ( int i = 0; i < n; i++ ) {
    if ( isnan( g.pointsV1[i] ) ) {
      vm.bMedia[i] = vm.bMin[i] = vm.bMax[i] = SEM_DADO;
    } else {
      vm.bMedia[i] = decimos( g.pointsV1[i] );
      vm.bMin[i] = isnan( g.minV1[i] ) ? vm.bMedia[i] : decimos( g.minV1[i] );
      vm.bMax[i] = isnan( g.maxV1[i] ) ? vm.bMedia[i] : decimos( g.maxV1[i] );
      ultimo = i;
    }
    vm.bMedia2[i] = temV2 && !isnan( g.pointsV2[i] ) ? decimos( g.pointsV2[i] ) : SEM_DADO;
  }
  vm.maximo = decimos( g.realMaxVal );
  vm.minimo = decimos( g.realMinVal );
  vm.media = decimos( g.avgTemp );
  vm.desvio = decimos( g.stdTemp );
  vm.variacao = decimos( g.deltaTemp );
  instante( g.tsRealMax, vm.tMaximo );
  instante( g.tsRealMin, vm.tMinimo );
  /* "Agora" is the last bucket that has data: the package carries no last
   * reading, and in the past the screen says "fim". */
  if ( ultimo >= 0 ) vm.agora = vm.bMedia[ultimo];
}

/* The pages the detail has, as the old screen numbers them (_detailPage: 0 the
 * temperature, 1 the humidity, 2 the pressure): the temperature always, the
 * humidity when the sensor has it and the plot has its axis, the pressure when
 * the sensor has the channel (drawGraphDetailScreen( ),
 * DisplayManager_Graph.cpp:897-900, and the tap that cycles them,
 * DisplayManager_Touch.cpp:718-736). A window with no data has the one page.
 * Returns how many; out[] gets their numbers in order. */
inline int paginasDoDetalhe( const GraphDataPackage &g, float minHum, uint8_t out[3] ) {
  int n = 0;
  out[n++] = 0;
  if ( g.count >= 2 && g.hasHumidity && !isnan( minHum ) ) out[n++] = 1;
  if ( g.count >= 2 && g.hasPressure ) out[n++] = 2;
  return n;
}

/* The detail's model: the six numbers of each page the sensor has, from the
 * package's statistics (the humidity's extremes are the axis pair Core 0 hands
 * showGraphPlot( ), as in the old rows). `paginaAntiga` is _detailPage; a page
 * the sensor lacks reads as the temperature, as the old screen drew it. */
inline void preencherDetalhe( tela::VmDetalhe &vm, const GraphDataPackage &g, float minHum, float maxHum, bool passado,
                              uint8_t paginaAntiga ) {
  using namespace tela;
  vm.periodo = (uint8_t)( g.timeRange < 0 ? 0 : g.timeRange > 4 ? 4 : g.timeRange );
  vm.passado = passado;
  uint8_t paginas[3];
  const int n = paginasDoDetalhe( g, minHum, paginas );
  vm.paginas = (uint8_t)n;
  vm.pagina = 0;
  for ( int k = 0; k < n; k++ )
    if ( paginas[k] == paginaAntiga ) vm.pagina = (uint8_t)k;

  /* The readings behind the mean: the old rows' n= (the buckets when the
   * package does not count them). 0 is "no data". */
  const int leituras = g.count < 2 ? 0 : g.sampleCount > 0 ? g.sampleCount : g.count;

  /* The last bucket that has data, in the plot's series or in the second
   * curve's: the package carries no last reading. */
  int ultimo1 = -1, ultimo2 = -1;
  if ( leituras > 0 && g.count <= GRAFICO_BALDES ) {
    for ( int i = 0; i < g.count; i++ ) {
      if ( !isnan( g.pointsV1[i] ) ) ultimo1 = i;
      if ( !isnan( g.pointsV2[i] ) ) ultimo2 = i;
    }
  }
  for ( int k = 0; k < n; k++ ) {
    EstatDetalhe &e = vm.est[k];
    e.maximo = e.minimo = e.media = e.desvio = e.variacao = e.agora = SEM_DADO;
    e.leituras = (uint16_t)( leituras > 65535 ? 65535 : leituras );
    int ultimo = -1;
    const float *serie = nullptr;
    if ( paginas[k] == 1 ) {
      e.grandeza = Grandeza::UMIDADE;
      e.maximo = decimos( maxHum );
      e.minimo = decimos( minHum );
      e.media = decimos( g.avgHum );
      e.desvio = decimos( g.stdHum );
      e.variacao = decimos( g.deltaHum );
      instante( g.tsMaxHum, e.tMaximo );
      instante( g.tsMinHum, e.tMinimo );
      if ( !g.v2IsPress ) { ultimo = ultimo2; serie = g.pointsV2; }
    } else if ( paginas[k] == 2 ) {
      e.grandeza = Grandeza::PRESSAO;
      e.maximo = decimos( g.realMaxPress );
      e.minimo = decimos( g.realMinPress );
      e.media = decimos( g.avgPress );
      e.desvio = decimos( g.stdPress );
      e.variacao = decimos( g.deltaPress );
      instante( g.tsRealMaxPress, e.tMaximo );
      instante( g.tsRealMinPress, e.tMinimo );
      /* Only the sensor whose second curve IS the pressure has it per bucket. */
      if ( g.v2IsPress ) { ultimo = ultimo2; serie = g.pointsV2; }
    } else {
      e.grandeza = Grandeza::TEMPERATURA;
      e.maximo = decimos( g.realMaxVal );
      e.minimo = decimos( g.realMinVal );
      e.media = decimos( g.avgTemp );
      e.desvio = decimos( g.stdTemp );
      e.variacao = decimos( g.deltaTemp );
      instante( g.tsRealMax, e.tMaximo );
      instante( g.tsRealMin, e.tMinimo );
      ultimo = ultimo1;
      serie = g.pointsV1;
    }
    if ( leituras == 0 ) {
      /* Nothing to show: the screen says so, and no stale zero may pass for a reading. */
      e.maximo = e.minimo = e.media = e.desvio = e.variacao = SEM_DADO;
      e.tMaximo = e.tMinimo = Instante{ };
    } else if ( ultimo >= 0 && serie ) {
      e.agora = decimos( serie[ultimo] );
      instante( (time_t)g.tsPoints[ultimo], e.tAgora );
    }
  }
}

/* The calendar's model: the month on show, the days with history (bit d = day
 * d, getHistoryDaysMask( )), today when it falls in this month
 * (drawCalendarScreen( ), DisplayManager_Calendar.cpp:67-72) and whether "›"
 * has a month to go to: only while the one shown is before the current one. A
 * clock nobody has set (1970) has no current month to stop at. */
inline void preencherCalendario( tela::VmCalendario &vm, int ano, int mes, uint32_t dias, time_t agora ) {
  vm.ano = (uint16_t)ano;
  vm.mes = (uint8_t)mes;
  vm.dias = dias;
  struct tm ti;
  localtime_r( &agora, &ti );
  const int anoAgora = ti.tm_year + 1900, mesAgora = ti.tm_mon + 1;
  vm.hoje = ( anoAgora == ano && mesAgora == mes ) ? (uint8_t)ti.tm_mday : 0;
  vm.avanca = anoAgora < 2020 || ano < anoAgora || ( ano == anoAgora && mes < mesAgora );
}

/* What identifies the month on show: a new read changes it. */
inline uint32_t assinaturaDoCalendario( int ano, int mes, uint32_t dias ) {
  uint32_t h = 2166136261u;
  const uint32_t v[3] = { (uint32_t)ano, (uint32_t)mes, dias };
  for ( uint32_t x : v ) {
    h ^= x;
    h *= 16777619u;
  }
  return h;
}

}  // namespace cola
}  // namespace ua
