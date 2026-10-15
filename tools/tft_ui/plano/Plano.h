#pragma once
// The TFT plan's family A screens with the plan's scenario, drawn with the
// kit (src/ui/angulo/Angulo.h): ports of renderizador/src/telas.js, used on
// the host to hold the kit to the plan's references, pixel for pixel
// (host_ui.py). Not firmware: the functional screens are src/ui/angulo/telas/.
#include "Angulo.h"

namespace plano {

// Draws example `nome` ("dashboard_normal", "spec_componentes_estados", ...) of
// family A in the theme of `p`. False when the name is not ported.
bool desenhar( ua::Pintor &p, const char *nome );

// Every example of family A, nullptr-terminated, and whether it is ported.
extern const char *const kNomes[];
bool portado( const char *nome );

// The keyboards the functional build uses (T1 for text, P1 to P3 for the PIN's
// layouts), named as render_teclado.js names them ("T1_vazio", "P2_erro").
extern const char *const kNomesTeclado[];

}  // namespace plano
