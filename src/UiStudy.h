/**
 * @file UiStudy.h
 * @brief Ângulo panel study — the tokens the theme table cannot carry, and
 *        the runtime switch between the shipped dashboard and the study's
 *        three layouts. Bench image only (SIMUT_UI_STUDY, env:pico_w_uistudy).
 * @details ANGULO.md §3 defines seventeen colour roles per theme; the TFT
 * palette has 24 fields with other names and other roles (Themes.h). Two of
 * the study's themes fold the tokens onto those fields (Themes.cpp), so every
 * screen the shipped drawers paint already comes out in Ângulo colours. What
 * the fold loses — `linha`, `positivo`, the three `-suave` backgrounds,
 * `perigo-tinta` — is what the study's own widgets read from here.
 *
 * Nothing in this header exists unless SIMUT_UI_STUDY is 1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#include "simut_config.h"
#if SIMUT_UI_STUDY

#include <Arduino.h>

/** The Ângulo colour roles (§3.1), in RGB565, one table per theme. Names are
 *  the standard's, so a value can be traced to its row in the token table. */
struct AnguloTokens {
	uint16_t fundo, superficie, superficie2;
	uint16_t tinta, tinta2, linha, linhaForte;
	uint16_t acento, acentoForte, acentoTinta;
	uint16_t positivo, positivoSuave;
	uint16_t alerta, alertaSuave;
	uint16_t perigo, perigoSuave, perigoTinta;
};

/** The token table of the active theme: claro when `angulo_claro` is loaded,
 *  escuro otherwise (it is also what a non-Ângulo theme gets, so a study
 *  layout always has a full table to draw with). */
const AnguloTokens& anguloTokens( );

/** True while one of the two Ângulo themes is the active palette. The shared
 *  chrome (UiWidgets.cpp) and the settings rows switch on this, so the same
 *  image shows the shipped look under simut_def and the study's under the
 *  Ângulo themes — one binary for every arm of the comparison. */
bool uiStudyThemeActive( );

/** Which dashboard is painted: 0 = the shipped drawers, 1..3 = layouts A
 *  (two cards), B (list), C (focus). Set from the console (`screen uN`),
 *  read on Core 1 at every render; a byte, so no lock. */
extern volatile uint8_t g_uiStudyVariant;

#endif /* SIMUT_UI_STUDY */
