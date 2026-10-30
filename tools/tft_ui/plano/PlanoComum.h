#pragma once
// The plan's scenario and the telas.js helpers the screen ports share
// (renderizador/src/telas.js: SENSORES, AMB, HORA, n1, corSituacao, situacao).
#include <math.h>
#include <stdint.h>

#include "Angulo.h"

namespace plano {

struct Sensor {
  const char *chip, *nome;
  double t, lo, hi, min, max;
  const char *minAt, *maxAt;
  double tend;
};
extern const Sensor SENSORES[4];
struct Ambiente { double t, h, tMin, tMax; };
extern const Ambiente AMB;
extern const char *const HORA;

// telas.js n1(v): Math.round(v * 10) / 10, toFixed(1), comma. buf >= 16 bytes.
inline char *n1( double v, char *buf ) {
  const double d = floor( v * 10 + 0.5 );   // Math.round
  return ua::n1( (int32_t)d, buf );
}

// telas.js corSituacao(T, o) and situacao(o).
uint16_t corSituacao( const ua::Tema &T, const Sensor &o );
struct Situacao { ua::Tom tom; const char *texto; };
Situacao situacao( const Sensor &o );

// One ported example: draws on a cleared surface.
struct Entrada { const char *nome; void ( *fn )( ua::Pintor & ); };

// The tables of the two groups of ports (declared here so the definitions,
// consts at namespace scope, keep external linkage).
extern const Entrada kTelasPainel[];
extern const int kTelasPainelN;
extern const Entrada kTelasConfig[];
extern const int kTelasConfigN;
extern const Entrada kTeclados[];
extern const int kTecladosN;

}  // namespace plano
