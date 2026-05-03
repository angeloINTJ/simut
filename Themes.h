/**
 * @file    Themes.h
 * @brief   Theme system with RGB565 color palettes for the TFT display.
 * @details Defines the ThemePalette struct and convenience macros for accessing
 * the current theme colors. Supports dynamic theme switching with
 * persistent selection stored in SystemConfig.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
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

    /* Cores adicionais (alpha24): separação de tipos de texto. Permite que
     * temas custom diferenciem labels de botões, títulos e nome de sensor. */
    uint16_t btnText;       /**< Texto de botões inativos (S0..S9, CFG, page) */
    uint16_t titleText;     /**< Top bar (data/hora) e títulos de menu */
    uint16_t sensorName;    /**< Nome do sensor no slot panel */
    uint16_t btnTextActive; /**< Texto do botão de slot SELECIONADO (sobre accentHigh) */
};

extern ThemePalette currentTheme;
extern const ThemePalette availableThemes[];

void loadTheme(int index);
int getThemeCount();
String getThemeId(int index);
int getThemeIndexByName(String name);

/** Retorna paleta (built-in PROGMEM ou custom RAM). NUNCA nullptr — fora do
 *  range cai no tema 0 (simut_def). Use isso em vez de availableThemes[i]. */
const ThemePalette* getThemePalette(int index);

/** Faz scan de arquivos .thm em /themes/ no LittleFS e popula o array de temas
 *  custom. Chamar uma vez no boot (depois de FS mount) e em hot-reload pós upload. */
void scanCustomThemes();

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

#define C_BTN_TEXT         currentTheme.btnText
#define C_TITLE_TEXT       currentTheme.titleText
#define C_SENSOR_NAME      currentTheme.sensorName
#define C_BTN_TEXT_ACTIVE  currentTheme.btnTextActive

#define C_GRID         currentTheme.barBg
#define C_AXIS         currentTheme.textOff
