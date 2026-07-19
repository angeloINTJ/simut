/**
 * @file simut_arduino_config.h
 * @brief Arduino IDE overrides for the Alpha (HD44780 16x2) variant.
 *
 * Defines that must be set BEFORE simut_config.h is processed.
 * The Alpha variant uses HD44780 in 4-bit parallel mode on GP16-GP21.
 *
 * To customize further, edit src/simut_config.h directly.
 */
#pragma once

/* Alpha display variant — HD44780 16x2 character LCD */
#define SIMUT_DISPLAY_TFT   0
#define SIMUT_DISPLAY_ALPHA 1

/* Parallel 4-bit interface (GP16-GP21, free when TFT SPI not used) */
#define HD44780_MODE_PARALLEL 1
#undef  HD44780_MODE_I2C

#define HD44780_RS 16
#define HD44780_EN 17
#define HD44780_D4 18
#define HD44780_D5 19
#define HD44780_D6 20
#define HD44780_D7 21

#include "simut_config.h"

/* Add Alpha-specific overrides below (after simut_config.h).
 * Since simut_config.h uses #ifndef guards, use #undef first:
 *   #undef  BUZZER_PIN
 *   #define BUZZER_PIN 15
 */
