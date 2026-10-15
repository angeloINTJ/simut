#pragma once
// The graph group of the functional build: the history chart and the screens
// around it (loading, detail, calendar) and the firmware-update screen.
//
//   MODE_GRAPH_LOADING  desenharCarregando( )   no targets: the touch is dead
//   MODE_GRAPH_VIEW     desenharGrafico( )      periods, time navigation, day, chart
//   MODE_GRAPH_DETAIL   desenharDetalhe( )      six statistics a quantity, a pager
//   MODE_CALENDAR       desenharCalendario( )   the days that have history
//   MODE_OTA_UPDATE     desenharOta( )          no targets: the glue takes any tap
//
// Numbers are integers in tenths (a temperature of 4,6 °C is 46, a pressure of
// 1013,2 hPa is 10132): what the Core 0 computes in float is rounded once, by
// the glue, and the screens print the tenths with the kit's n1( ). An unknown
// number or an empty bucket is SEM_DADO, the Core 0's NAN. The chart keeps the
// Core 0's 200 buckets: VmGrafico is 1674 bytes (measured 2026-10-14), the
// largest here, inside the 4096 of UA_VM_MAX (UiA.h).
#include "Telas.h"

namespace ua {
namespace tela {

// The Core 0's GRAPH_WIDTH (SystemDefs_Limits.h): the most buckets it sends.
constexpr int GRAFICO_BALDES = 200;

// An empty bucket or a statistic nobody could compute, in tenths. The glue
// writes it explicitly: a zero is a real 0,0.
constexpr int16_t SEM_DADO = INT16_MIN;

// The periods, as the Core 0 numbers them (RANGE_1H .. RANGE_1W): 0 = 1 h,
// 1 = 6 h, 2 = 12 h, 3 = 24 h, 4 = 7 d.
constexpr int N_PERIODOS = 5;

// A local date and time as the device prints it. dia = 0: unknown.
struct Instante {
  uint8_t dia, mes, hora, minuto;
};

/* ------------------------------------------------------------------ */
/* Loading (MODE_GRAPH_LOADING)                                         */
/* ------------------------------------------------------------------ */

// The Core 0 has no percentage for the read, so the bar is a segment that
// sweeps instead of a fill that grows. The glue may step `fase` about four
// times a second to move it; left at 0 the segment rests in the middle, which
// reads as "working", not as a progress of 0 % or 100 %.
constexpr int FASES_CARREGANDO = 12;
struct VmCarregando {
  char sensor[32];       // up to 31 characters, cut with "…" in the title
  uint8_t periodo;       // 0..4
  uint8_t fase;          // 0..FASES_CARREGANDO - 1
};
void desenharCarregando( Pintor &p, const VmCarregando &vm );
int alvosCarregando( const VmCarregando &vm, Alvo *out );   // always 0

/* ------------------------------------------------------------------ */
/* The chart (MODE_GRAPH_VIEW)                                          */
/* ------------------------------------------------------------------ */

// What the second curve of the chart is, drawn thin with its own scale at the
// right: humidity in %UR, or pressure in hPa on a part that has no humidity.
enum class Serie2 : uint8_t { NENHUMA, UMIDADE, PRESSAO };

struct VmGrafico {
  char sensor[32];
  uint8_t periodo;       // 0..4: the active chip
  bool passado;          // the window ends before now: "next" is alive, "agora" reads "fim"
  bool ocupado;          // another window was asked for and the Core 0 is reading it: a line under the title bar
  Serie2 serie2;
  // The sensor's alarm limits, drawn as dashed lines when they fall inside
  // the chart's scale (never stretching it).
  bool temLimites;
  int16_t limiteInf, limiteSup;
  // Buckets used, 0 to 200. 0 (or one that is all empty) is "Sem dados neste
  // período."; the Core 0 sends 0 or at least 40.
  uint8_t n;
  // The window's start, middle and end, for the time axis: HH:MM up to 24 h,
  // DD/MM at 7 d. The date also appears on the first label when the window
  // crosses midnight or is in the past, where the time alone would mislead.
  Instante eixo[3];
  // Temperature statistics of the whole window (every record, not just the
  // buckets). `agora` is the last reading of the window.
  int16_t maximo, minimo, media, desvio, variacao, agora;
  Instante tMaximo, tMinimo;
  // The buckets, evenly spaced in time: the mean (the line), the minimum and
  // the maximum (the band) of the temperature and the mean of the second curve.
  // SEM_DADO in bMedia leaves a gap; in the others, the value is not there.
  int16_t bMedia[GRAFICO_BALDES];
  int16_t bMin[GRAFICO_BALDES];
  int16_t bMax[GRAFICO_BALDES];
  int16_t bMedia2[GRAFICO_BALDES];
};
// `indice` of GRF_PERIODO is the period, 0..4. GRF_SEGUINTE exists only when
// the view is in the past; GRF_AREA is the whole chart card.
enum AlvoGrafico : uint8_t { GRF_NADA, GRF_VOLTAR, GRF_DIA, GRF_PERIODO, GRF_ANTERIOR, GRF_SEGUINTE, GRF_AREA };
void desenharGrafico( Pintor &p, const VmGrafico &vm );
int alvosGrafico( const VmGrafico &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Detail (MODE_GRAPH_DETAIL)                                           */
/* ------------------------------------------------------------------ */

enum class Grandeza : uint8_t { TEMPERATURA, UMIDADE, PRESSAO };

// The six numbers of one quantity over the window.
struct EstatDetalhe {
  Grandeza grandeza;
  uint16_t leituras;     // records behind the mean; 0: nothing to show
  int16_t maximo, minimo, media, desvio, variacao, agora;
  Instante tMaximo, tMinimo, tAgora;
};

struct VmDetalhe {
  uint8_t periodo;       // 0..4
  bool passado;          // the window ends before now: "Agora" reads "Fim"
  uint8_t pagina;        // 0-based
  uint8_t paginas;       // 1..3: the temperature, then the humidity and the pressure the sensor has
  EstatDetalhe est[3];
};
enum AlvoDetalhe : uint8_t { DET_NADA, DET_VOLTAR, DET_PAG_ANTERIOR, DET_PAG_PROXIMA };
void desenharDetalhe( Pintor &p, const VmDetalhe &vm );
int alvosDetalhe( const VmDetalhe &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Calendar (MODE_CALENDAR)                                             */
/* ------------------------------------------------------------------ */

struct VmCalendario {
  uint16_t ano;
  uint8_t mes;           // 1..12
  uint8_t hoje;          // today's day of the month if today is in this month, else 0
  bool avanca;           // the month shown is before the current one: "›" is alive
  bool ocupado;          // another month was asked for and the Core 0 is reading it: a line under the title bar
  uint32_t dias;         // bit d (1..31) set: day d has history (getHistoryDaysMask( ))
};
// `indice` of CAL_DIA is the day, 1..31; only days with history are targets.
enum AlvoCalendario : uint8_t { CAL_NADA, CAL_VOLTAR, CAL_MES_ANTERIOR, CAL_MES_SEGUINTE, CAL_DIA, CAL_HOJE };
void desenharCalendario( Pintor &p, const VmCalendario &vm );
int alvosCalendario( const VmCalendario &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Firmware update (MODE_OTA_UPDATE)                                    */
/* ------------------------------------------------------------------ */

// Numbered as OtaPhase and OtaWhy (OtaScreen.h), so the glue converts with a
// cast it can pin with a static_assert.
enum class FaseOta : uint8_t { NENHUMA, RECEBENDO, CONFERINDO, CONFERIDA, INSTALANDO, RECUSADA, INTERROMPIDA };
enum class MotivoOta : uint8_t {
  NENHUM, MODELO, SEM_ASSINATURA, ASSINATURA, CHAVE_APOSENTADA, VERSAO_BLOQUEADA, CHAVE_DE_BANCADA, DANIFICADA
};
struct VmOta {
  FaseOta fase;          // NENHUMA draws only the title: the panel is the dashboard's, not this screen's
  MotivoOta motivo;      // only with RECUSADA
  uint8_t pct;           // 0..100, only with RECEBENDO (the Core 0 stops at 99)
};
void desenharOta( Pintor &p, const VmOta &vm );
int alvosOta( const VmOta &vm, Alvo *out );   // always 0: any tap on RECUSADA or INTERROMPIDA goes back

}  // namespace tela
}  // namespace ua
