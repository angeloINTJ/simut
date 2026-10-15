#pragma once
// The two screens that one sensor's menu opens (ESPEC_UI_A.md §6.3): its alarm
// limits (MODE_SETTINGS_ALARM_EDIT) and its maintenance window
// (MODE_SETTINGS_MAINT). Both edit a copy; nothing is saved until the footer's
// primary action, which the glue turns into the event the old screen sent.
#include "Telas.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Limites de um sensor (MODE_SETTINGS_ALARM_EDIT)                      */
/* ------------------------------------------------------------------ */

// What a page of the limits edits: one page per quantity the sensor type reads
// (DS18B20: temperature; DHT22: temperature, humidity; BMP280: temperature,
// pressure; BME280: all three).
enum GrandezaLimites : uint8_t { GLIM_TEMPERATURA, GLIM_UMIDADE, GLIM_PRESSAO };

struct VmLimites {
  char sensor[32];             // the sensor's name, up to 31 characters: the title cuts it
  uint8_t grandeza;            // GrandezaLimites: the quantity of this page
  uint8_t pagina;              // 0-based: page 1 of 3 is 0, and the pager says "1 de 3"
  uint8_t total;               // pages of this sensor, 1..3; with 1 the pager is not drawn
  uint8_t foco;                // the row touched last: 0 minimum, 1 maximum
  int32_t minimo, maximo;      // tenths of the unit; the old editor keeps minimo < maximo
  int32_t piso, teto;          // the channel's valid range, in tenths: the buttons stop there
};

// `indice` of LIMITES_MENOS and LIMITES_MAIS is the row (0 minimum, 1 maximum).
// A button at the end of its range is drawn disabled and is not a target; so is
// a pager arrow with no page to go to. LIMITES_VOLTAR and LIMITES_CANCELAR both
// leave without saving (the old SAIR).
enum AlvoLimites : uint8_t {
  LIMITES_NADA,
  LIMITES_VOLTAR,
  LIMITES_MENOS,
  LIMITES_MAIS,
  LIMITES_PAG_ANTERIOR,
  LIMITES_PAG_PROXIMA,
  LIMITES_SALVAR,
  LIMITES_CANCELAR
};
void desenharLimites( Pintor &p, const VmLimites &vm );
int alvosLimites( const VmLimites &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Manutenção (MODE_SETTINGS_MAINT)                                     */
/* ------------------------------------------------------------------ */

struct VmManutencao {
  char sensor[32];             // the sensor's name, up to 31 characters: the title cuts it
  bool aberta;                 // a window is open: the remaining time and "Encerrar manutenção"
  bool permitido;              // the account holds the maintenance bit; without it nothing can start or end
  uint8_t foco;                // closed: the row touched last, 0 hours, 1 minutes
  uint8_t minutos;             // closed: 0..55, in steps of 5 (wraps 55 <-> 0)
  uint16_t horas;              // closed: 0..horasMax, does not wrap
  uint16_t horasMax;           // 720: MAINT_MAX_SEC in hours
  uint32_t restanteMin;        // open: minutes left, rounded up (at least 1 while open)
};

// `indice` of MANUT_MENOS and MANUT_MAIS is the row (0 hours, 1 minutes), only in
// the closed state. MANUT_INICIAR is a target only with the permission and at
// least one minute chosen; MANUT_ENCERRAR only with the permission. MANUT_VOLTAR
// and MANUT_CANCELAR both leave the screen (the old SAIR); the open state has no
// MANUT_CANCELAR button, the bar's "Voltar" is its way out.
enum AlvoManutencao : uint8_t {
  MANUT_NADA,
  MANUT_VOLTAR,
  MANUT_MENOS,
  MANUT_MAIS,
  MANUT_INICIAR,
  MANUT_ENCERRAR,
  MANUT_CANCELAR
};
void desenharManutencao( Pintor &p, const VmManutencao &vm );
int alvosManutencao( const VmManutencao &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
