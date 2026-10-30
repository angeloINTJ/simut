/**
 * @file Themes.h
 * @brief Theme system with RGB565 color palettes for the TFT display.
 * @details Defines the ThemePalette struct and convenience macros for accessing
 * the current theme colors. Supports dynamic theme switching with
 * persistent selection stored in SystemConfig.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include "simut_config.h"  /* theme pack flags (SIMUT_THEMES_*) */

/** Convert 8-bit RGB to 16-bit RGB565 format for TFT displays. */
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/** Halfway blend toward white, per RGB565 channel. Used for icon glints
 * (e.g. the humidity drop shine) so highlights derive from the theme
 * color they sit on instead of a fixed light blue. */
static inline uint16_t themeTint(uint16_t c) {
 uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
 return (uint16_t)((((r + 32) >> 1) << 11) | (((g + 64) >> 1) << 5) | ((b + 32) >> 1));
}

#include "ThemeCatalog.h"   /* ThemePalette and the six functions of the catalogue: core, so the Ângulo can define them without this header's drawing */

extern ThemePalette currentTheme;
extern const ThemePalette availableThemes[];


/* Convenience macros — access current theme colors without struct prefix. */
#define C_BG_MAIN currentTheme.bgMain
#define C_CARD_BG currentTheme.cardBg
#define C_TEXT_MAIN currentTheme.textMain
#define C_TEXT_SUB currentTheme.textSub
#define C_TEXT_OFF currentTheme.textOff
#define C_ACCENT currentTheme.accent
#define C_ACCENT_HIGH currentTheme.accentHigh
#define C_BAR_BG currentTheme.barBg

#define C_TEMP_HOT currentTheme.tempHot
#define C_TEMP_WARM currentTheme.tempWarm
#define C_TEMP_OK currentTheme.tempOk
#define C_TEMP_COLD currentTheme.tempCold
#define C_HUMIDITY currentTheme.humidity

#define C_BTN_TEXT currentTheme.btnText
#define C_TITLE_TEXT currentTheme.titleText
#define C_SENSOR_NAME currentTheme.sensorName
#define C_BTN_TEXT_ACTIVE currentTheme.btnTextActive

#define C_ALARM_BG currentTheme.alarmBg
#define C_ALARM_TEXT currentTheme.alarmText
#define C_ALARM_TEXT_DIM currentTheme.alarmTextDim
#define C_ALARM_BORDER currentTheme.alarmBorder
#define C_CAUTION_BG currentTheme.cautionBg
/* Alarme de ERRO de sensor (v21.1): cor FIXA, não é escolha de tema — um
 * sensor em falha (sem comunicação / trocado) deve ser distinguível do
 * alarme de limite à primeira vista. Fundo âmbar BRILHANTE + texto branco,
 * como pedido ("aparelo brilhante e branco"). */
#define C_ALARM_ERR_BG 0xFAE0   /* âmbar brilhante (#FFBF00) */
#define C_ALARM_ERR_TEXT 0xFFFF /* branco */
#define C_SEL_BG currentTheme.selBg
#define C_STAMP_TEXT currentTheme.stampText

#define C_GRID currentTheme.barBg
#define C_AXIS currentTheme.textOff
