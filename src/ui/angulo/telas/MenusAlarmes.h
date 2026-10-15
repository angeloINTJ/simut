#pragma once
// The alarm screens of the menu: the list of sensors with their limits, and
// one sensor's menu (limits, alarms on or off, maintenance). ESPEC_UI_A.md §6.2.
// The rows, the pager and the targets of the list are MenusLista.h's.
#include "MenusLista.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Limites de alarme (MODE_SETTINGS_ALARMS)                             */
/* ------------------------------------------------------------------ */

constexpr int ALARMES_MAX = 16;        // sensor slots of the build (MAX_SENSORS)

struct LinhaAlarme {
  char nome[32];                       // the sensor's name
  int16_t lo, hi;                      // the temperature band, in tenths of a degree
  bool ligado;                         // the sensor's alarms are on
  bool manutencao;                     // a maintenance window is open
};

struct VmAlarmes {
  uint8_t n;                           // active sensors listed, in slot order
  uint8_t pagina;                      // 0-based; four sensors to a page
  LinhaAlarme sensores[ALARMES_MAX];
};
// Targets: AlvoLista. LST_LINHA's indice is the position in sensores[ ].
// With n == 0 the screen shows the empty card and only LST_VOLTAR is a target.
void desenharAlarmes( Pintor &p, const VmAlarmes &vm );
int alvosAlarmes( const VmAlarmes &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Um sensor (MODE_SETTINGS_ALARM_SENSOR)                               */
/* ------------------------------------------------------------------ */

enum AlvoSensorAlarme : uint8_t { SA_NADA, SA_VOLTAR, SA_LIMITES, SA_ALARMES, SA_MANUTENCAO };

struct VmSensorAlarme {
  char nome[32];                       // the sensor's name: the title
  bool alarmesLigados;                 // the switch's state
  bool podeLimites;                    // the account holds PERM_ALARM_LIMITS
  bool podeBloquear;                   // PERM_ALARM_BLOCK
  bool podeManutencao;                 // PERM_MAINT
  uint32_t manutencaoSeg;              // seconds left of an open window; 0: none is open
};
// Targets: SA_VOLTAR and, for each row the account may use, SA_LIMITES
// (opens), SA_ALARMES (flips the switch: the device saves on the spot) and
// SA_MANUTENCAO (opens). A row without its bit is drawn disabled, with a lock,
// and is not a target.
void desenharSensorAlarme( Pintor &p, const VmSensorAlarme &vm );
int alvosSensorAlarme( const VmSensorAlarme &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
