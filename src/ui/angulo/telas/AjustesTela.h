#pragma once
// The three screens that set the panel itself up (ESPEC_UI_A.md §6.3): the
// display alignment (MODE_SETTINGS_DISPLAY_OFFSET), the touch sensitivity
// (MODE_SETTINGS_TOUCH_SENS) and the touch calibration (MODE_SETTINGS_TOUCH_CAL).
//
// The last two are RAW touch: the old code reads them before any hook, so what
// the screen draws has to sit where the old zones are, and the comments in
// AjustesTela.cpp say which numbers those are and where they come from.
#include "Telas.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Alinhar a tela (MODE_SETTINGS_DISPLAY_OFFSET)                        */
/* ------------------------------------------------------------------ */

struct VmAlinhar {
  int8_t x, y;                 // the live preview's offset, in pixels, -limite..limite
  uint8_t limite;              // 4: TftWithOffset's limit
};

// The arrows move the image the way they point: CIMA lowers y by one, BAIXO raises
// it, ESQUERDA lowers x, DIREITA raises it (the old zones 20..23: y-1, y+1, x-1,
// x+1). An arrow at the limit is drawn disabled and is not a target.
// ALINH_REDEFINIR zeroes both (the old square in the middle); ALINH_VOLTAR leaves
// and restores the saved offset (the old SAIR); ALINH_SALVAR is the old APLICAR,
// which also wipes the touch calibration and opens its wizard.
enum AlvoAlinhar : uint8_t {
  ALINH_NADA,
  ALINH_VOLTAR,
  ALINH_CIMA,
  ALINH_BAIXO,
  ALINH_ESQUERDA,
  ALINH_DIREITA,
  ALINH_REDEFINIR,
  ALINH_SALVAR
};
void desenharAlinhar( Pintor &p, const VmAlinhar &vm );
int alvosAlinhar( const VmAlinhar &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Sensibilidade do toque (MODE_SETTINGS_TOUCH_SENS)                    */
/* ------------------------------------------------------------------ */

struct VmSensibilidade {
  uint16_t leituras;           // the count the "Leituras" line shows: the old _sensCount, readings 15 ms apart (not taps)
  uint8_t estabilidade;        // 0..100 (%): the vertical bar, in the positive colour from 85
  uint16_t limiar;             // the pressure threshold found so far, as the old screen printed it; 0: none yet
  bool concluido;              // the bar is full: the device has saved the threshold on its own
};

// The only target is SENSIB_CANCELAR, inside the old zone (x 0..124, y 196..239).
// Every other touch, anywhere, is a sample and belongs to the old handler.
enum AlvoSensibilidade : uint8_t { SENSIB_NADA, SENSIB_CANCELAR };
void desenharSensibilidade( Pintor &p, const VmSensibilidade &vm );
int alvosSensibilidade( const VmSensibilidade &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Calibrar o toque (MODE_SETTINGS_TOUCH_CAL)                           */
/* ------------------------------------------------------------------ */

enum ResultadoCalibrar : uint8_t { CALRES_MEDINDO, CALRES_CONCLUIDA, CALRES_IMPRECISA };

// Where the sight must stand at each step: the core's capture assumes the finger is
// on CAL_SCR_X/Y (DisplayManager.h, CAL_INSET px inside the panel's corners) and its
// validation extrapolates the screen's edges from them. They are not a design choice.
// Kept here because the screens build on the host without DisplayManager.h; the
// glue holds the two equal at compile time (DisplayManager_UiA_Ajustes.cpp,
// MODE_SETTINGS_TOUCH_CAL: a panel of another size stops the build there, review of
// S3, 2026-10-15).
inline constexpr int16_t MIRA_X[4] = { 20, 300, 20, 300 };
inline constexpr int16_t MIRA_Y[4] = { 20, 20, 220, 220 };

struct VmCalibrar {
  uint8_t resultado;           // ResultadoCalibrar: the sight, or the verdict (the base's message screen)
  uint8_t passo;               // 0..3: top left, top right, bottom left, bottom right
  uint8_t volta;               // 1..2: the four points are taken twice
  bool segurando;              // the hold has reached CAL_HOLD_MS (400 ms): the sight turns positive, "release now"
};

// While measuring there are NO targets: any touch anywhere is a capture. In the two
// verdicts the only one is CALIB_BOTAO, the base's message button.
enum AlvoCalibrar : uint8_t { CALIB_NADA, CALIB_BOTAO };
void desenharCalibrar( Pintor &p, const VmCalibrar &vm );
int alvosCalibrar( const VmCalibrar &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
