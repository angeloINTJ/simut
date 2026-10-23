#pragma once
// The text keyboard of the identity group (MODE_SETTINGS_PASSWORD, ESPEC_UI_A.md
// §6.4): the plan's T1 made functional. It types the name of a new account (in
// the clear, 15 characters at most) and, from the CLI's `screen pwd`, the legacy
// PIN (dots and the "see" eye, 7 at most). Touch is Controller (C): the plan's
// keys are not the old group grid, so the glue acts on each target itself,
// with teclaCaractere( ) for the character a key types.
//
// The character set is the old keyboard's (PasswordKeyboard.h: a-z, A-Z, 0-9,
// space and its 28 symbols) and nothing else, all of it ASCII, so a character
// is a byte and "n de 15" counts what the account's 15-byte name limit counts.
// The plan's own "#+=" layer (accents, typographic marks) is not used: each of
// those takes 2 to 3 bytes of the 15, and the old keyboard never typed one.
#include "IdentidadeComum.h"

namespace ua {
namespace tela {

// The layers: abc, ABC, 123 and #+=. `shift` over abc shows ABC for one letter
// (ON, back to OFF after it) or until it is turned off (LOCK); the glue may
// also keep the state in `camada` MAI.
enum class TextoCamada : uint8_t { MIN, MAI, NUM, SIMB };
enum class TextoShift : uint8_t { OFF, ON, LOCK };

constexpr int TEXTO_MAX = 32;           // bytes of text in the model, with the NUL

struct VmTexto {
  TextoCamada camada;
  TextoShift shift;
  bool secreto;                         // dots and the eye (the legacy PIN); a name is shown in the clear
  bool ver;                             // secreto, shown in the clear for now
  uint8_t maximo;                       // characters accepted: 15 a name, 7 the legacy PIN
  int8_t pressionada;                   // key under the finger (the index of TECLA), -1 none
  char rotulo[16];                      // the primary key: "Seguir", "Salvar"; "" says "Salvar"
  char titulo[32];                      // "Nome da conta"
  char texto[TEXTO_MAX];                // what has been typed, ASCII
  char ajuda[64];                       // the help line ("" none); an error takes its place
  char erro[96];                        // the refusal ("" none), in the danger colour
};

// TECLA: indice = row * 10 + column in the layer on screen (rows of 10, the last
// ones shorter); the character is teclaCaractere( ). SHIFT is the left key of
// abc and ABC. CAMADA is a layer key: indice = the layer it leads to (a
// TextoCamada): "123" and "abc" at the second key, "#+=" and "123" at the left
// key of 123 and #+=. VER is the eye of a secret field. Keys that cannot act
// now are inert targets: the characters and the space when the text is full,
// Apagar and Salvar when it is empty.
enum AlvoTexto : uint8_t { TXT_NADA, TXT_TECLA, TXT_SHIFT, TXT_CAMADA, TXT_ESPACO, TXT_APAGAR, TXT_VER, TXT_SALVAR, TXT_CANCELAR };
void desenharTexto( Pintor &p, const VmTexto &vm );
int alvosTexto( const VmTexto &vm, Alvo *out );

// The refusals of a new account's name, said on the keyboard's help line (the
// glue, DisplayManager_UiA_Identidade.cpp, writes them into VmTexto::erro). Here
// so that the samples show the very texts; each says what happened or what to
// do, and fits the line's 296 px in the support type (measured 2026-10-15, at the
// 9 pt floor: 247, 252, 289 and 233 px in pt-BR; the copy of the 7 pt type measured 239,
// 270, 247 and 275, and at 9 pt it was 310, 352, 319 and 356 and was cut at its
// end, which is where the way out is said). In English and in Spanish (measured
// 2026-10-23, S10c B5b, with the kit's larguraTinta( )): 228, 291, 287 and 294 px,
// and 280, 285, 272 and 235; the first wordings of the reserved name's refusal
// ("The name admin is reserved. Use another.", 318 px) were cut and were shortened.
// The line has one row: the keys start 28 px under the field.
// Functions and not constants: UA_TXT( ) answers from the pack the boot loads, so a constant built at start-up would hold the English for good (Textos.h).
// Defined in IdentidadeTexto.cpp, which includes Textos.h after its own headers.
const char *erroSoEspacos( );
const char *erroCaracteres( );
const char *erroReservado( );
const char *erroRepetido( );

// The character the key at `indice` types in the layer on screen (the layer
// `camada` shows, taking `shift` into account), or 0 where there is no key.
char teclaCaractere( TextoCamada camada, TextoShift shift, int indice );

}  // namespace tela
}  // namespace ua
