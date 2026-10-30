#include "PlanoComum.h"

namespace plano {

const Sensor SENSORES[4] = {
  { "S1", "Vacinas", 4.6, 2, 8, 3.1, 5.8, "03:12", "14:40", 0.3 },
  { "S2", "Freezer", -18.4, -25, -15, -19.6, -16.9, "05:48", "13:05", -0.1 },
  { "S3", "Câmara", 2.8, 0, 6, 1.9, 3.6, "04:20", "15:02", 0.1 },
  { "S4", "Estufa", 36.8, 35, 38, 36.2, 37.4, "02:40", "12:30", 0.2 },
};
const Ambiente AMB = { 24.6, 58, 21.9, 27.3 };
const char *const HORA = "11:32";

uint16_t corSituacao( const ua::Tema &T, const Sensor &o ) {
  if ( !isfinite( o.t ) ) return T.tinta2;
  if ( o.t < o.lo || o.t > o.hi ) return T.perigo;
  const double m = ( o.hi - o.lo ) * 0.1;
  return o.t < o.lo + m || o.t > o.hi - m ? T.alerta : T.positivo;
}

Situacao situacao( const Sensor &o ) {
  if ( !isfinite( o.t ) ) return { ua::Tom::NEUTRO, "Sem leitura" };
  if ( o.t > o.hi ) return { ua::Tom::PERIGO, "Acima" };
  if ( o.t < o.lo ) return { ua::Tom::PERIGO, "Abaixo" };
  const double m = ( o.hi - o.lo ) * 0.1;
  if ( o.t < o.lo + m || o.t > o.hi - m ) return { ua::Tom::ALERTA, "No limite" };
  return { ua::Tom::POSITIVO, "Na faixa" };
}

}  // namespace plano
