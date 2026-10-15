#pragma once
// The alarm's action screen (MODE_ALARM_ACTION): what is wrong with which
// sensor, and the three things the panel can do about it. The plan's
// alarme_acao_acoes, without the "desde" of its strip: the device does not
// keep the time an alarm began.
#include "Telas.h"

namespace ua {
namespace tela {

struct VmAcaoAlarme {
  // What put the slot in alarm. FORA is none of the three the screen can say with
  // numbers: a channel the card does not print (humidity, pressure) out of its
  // limits while the temperature is inside, or a slot whose reading the glue
  // could not find (neither card shows it).
  enum class Causa : uint8_t { ACIMA, ABAIXO, ERRO, FORA };

  VmStatus status;
  char nome[32];            // the sensor in alarm, UTF-8, up to 31 bytes
  int16_t temp;             // tenths of °C, the reading (ACIMA, ABAIXO)
  int16_t limite;           // tenths of °C, the limit crossed: the maximum for ACIMA, the minimum for ABAIXO
  uint16_t silencioSeg;     // what the first button silences for, in seconds (EVT_ALARM_SILENCE's param)
  Causa causa;
};

// Where the old zones are and what they do: SILENCIAR is EVT_ALARM_SILENCE,
// DESATIVAR is EVT_ALARM_DEACTIVATE (and asks for the PIN), MINMAX turns the
// lower card to min/max and goes back to the panel.
enum AlvoAcaoAlarme : uint8_t { ACA_NADA, ACA_SILENCIAR, ACA_MINMAX, ACA_DESATIVAR };

void desenharAcaoAlarme( Pintor &p, const VmAcaoAlarme &vm );
int alvosAcaoAlarme( const VmAcaoAlarme &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
