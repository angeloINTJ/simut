/**
 * @file simut_arduino_config.h
 * @brief Arduino IDE overrides for the TFT (ILI9341) variant.
 *
 * Defines that must be set BEFORE simut_config.h is processed.
 * Place any additional custom overrides below the #include.
 *
 * To customize further, edit src/simut_config.h directly.
 */
#pragma once

/* TFT display variant */
#define SIMUT_DISPLAY_TFT   1
#define SIMUT_DISPLAY_ALPHA 0

#include "simut_config.h"

/* Add TFT-specific overrides below (after simut_config.h).
 * Since simut_config.h uses #ifndef guards, use #undef first:
 *   #undef  TFT_CS
 *   #define TFT_CS 15
 */
