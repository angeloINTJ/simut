/**
 * @file    Themes.h
 * @brief   Theme system with RGB565 color palettes for the TFT display.
 * @details Defines the ThemePalette struct and convenience macros for accessing
 * the current theme colors. Supports dynamic theme switching with
 * persistent selection stored in SystemConfig.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Arduino.h>

/** Convert 8-bit RGB to 16-bit RGB565 format for TFT displays. */
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/** Color palette definition for the TFT display theme system. */
struct ThemePalette {
    const char* idName;
    const char* displayName;

    uint16_t bgMain;
    uint16_t cardBg;
    uint16_t textMain;
    uint16_t textSub;
    uint16_t textOff;
    uint16_t accent;
    uint16_t accentHigh;
    uint16_t barBg;

    uint16_t tempHot;
    uint16_t tempWarm;
    uint16_t tempOk;
    uint16_t tempCold;
    uint16_t humidity;
};

extern ThemePalette currentTheme;
extern const ThemePalette availableThemes[];

void loadTheme(int index);
int getThemeCount();
String getThemeId(int index);
int getThemeIndexByName(String name);

/* Convenience macros — access current theme colors without struct prefix. */
#define C_BG_MAIN      currentTheme.bgMain
#define C_CARD_BG      currentTheme.cardBg
#define C_TEXT_MAIN    currentTheme.textMain
#define C_TEXT_SUB     currentTheme.textSub
#define C_TEXT_OFF     currentTheme.textOff
#define C_ACCENT       currentTheme.accent
#define C_ACCENT_HIGH  currentTheme.accentHigh
#define C_BAR_BG       currentTheme.barBg

#define C_TEMP_HOT     currentTheme.tempHot
#define C_TEMP_WARM    currentTheme.tempWarm
#define C_TEMP_OK      currentTheme.tempOk
#define C_TEMP_COLD    currentTheme.tempCold
#define C_HUMIDITY     currentTheme.humidity

#define C_GRID         currentTheme.barBg
#define C_AXIS         currentTheme.textOff
