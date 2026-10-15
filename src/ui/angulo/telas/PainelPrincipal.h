#pragma once
// The panel's main screen (MODE_DASHBOARD): family A's dashboard_* of the TFT
// plan, with the elements the panel has today. Two sensor cards (the upper one
// pinned to a sensor or following the selection, the lower one the selected
// sensor) and the bottom row of up to five shortcuts.
//
// The view model says what is on the glass, not what the device knows: the
// glue (DisplayManager_UiA_Painel.cpp) decides which sensor each card shows,
// which buttons the bottom row has (buildDashLayout( )) and which phase of the
// alarm flash it is; this screen draws that and lists what can be touched.
#include "Telas.h"

namespace ua {
namespace tela {

struct VmPainel {
  // A tenth with nothing behind it: the day's min/max caches hold NAN until the
  // first reading of the day.
  static constexpr int16_t SEM_VALOR = INT16_MIN;

  // What the sensor measures decides a card's second value (the dispatch table
  // of SensorPanelDispatch.h): DHT22 shows humidity; BMP280 and BME280 show
  // pressure; the BME280 shows humidity, not pressure, in min/max mode.
  enum class Tipo : uint8_t { NENHUM, DS18B20, DHT22, BMP280, BME280 };

  // One of the two cards. Values in tenths, never float: the view model is
  // hashed byte for byte to decide whether the glass would change.
  struct Sensor {
    char nome[32];         // friendlyName, UTF-8, up to 31 bytes
    int16_t temp;          // tenths of °C
    int16_t umid;          // tenths of %RH (DHT22, BME280)
    int16_t tMin, tMax;    // the day's, tenths of °C; SEM_VALOR: no record yet
    int16_t uMin, uMax;    // the day's, tenths of %RH; SEM_VALOR: no record yet
    int16_t lo, hi;        // temperature alarm limits, tenths of °C (the lower card prints them)
    uint16_t pres;         // hPa, whole (BMP280, BME280); 0: no reading
    Tipo tipo;             // NENHUM: the slot has no sensor
    bool erro;             // no valid reading: "--,-"
    bool alarme;           // the slot is in alarm (a limit or a sensor error): the card flashes
    bool minmax;           // showing the day's minimum and maximum, with the Gráfico button
  };

  // One of the five positions of the bottom row, as buildDashLayout( ) filled it.
  enum class NavTipo : uint8_t { NADA, SLOT, CFG, PAGINA };
  struct Nav {
    NavTipo tipo;          // NADA: the position stays empty
    uint8_t slot;          // SLOT: the sensor's index, the label "S<slot>"
    bool selecionado;      // SLOT: the sensor of the lower card
    bool alarme;           // SLOT: in alarm; PAGINA: an alarm on a page that is not shown
  };

  VmStatus status;
  Sensor cima, baixo;
  Nav nav[5];
  char contaWeb[24];       // webOcupada: the account that is saving through the web
  uint8_t pagina, paginas; // the PAGINA button's label, "<pagina>/<paginas>" (1-based)
  bool cimaFixada;         // the upper card is pinned to its sensor (the pin icon)
  bool faseAcesa;          // the alarm flash's lit phase (the glue alternates it every 600 ms)
  bool silenciado;         // the 120 s silence: cards in alarm stop flashing and show a danger border
  bool webOcupada;         // the web is saving: the floating notice, and no touch is a target
};

// Touch targets. CARTAO_* is the whole card; GRAFICO_* sits inside it, only in
// min/max mode, and comes after it in the list (alvoEm( ) answers the last
// target that holds the point). NAV's indice is the position 0..4, only the
// positions that have a button; the glue knows what each one is.
enum AlvoPainel : uint8_t { PNL_NADA, PNL_CARTAO_CIMA, PNL_GRAFICO_CIMA, PNL_CARTAO_BAIXO, PNL_GRAFICO_BAIXO, PNL_NAV };

void desenharPainel( Pintor &p, const VmPainel &vm );
int alvosPainel( const VmPainel &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
