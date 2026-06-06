/**
 * @file display/DisplayDriver.h
 * @brief Display driver — composes display panel + touch controller.
 * @details The active hardware is selected at compile time via build flags.
 * Currently supported:
 *   Default: ILI9341 320x240 TFT + XPT2046 touch
 *   Future:  HD44780 16x2 alphanumeric (SIMUT_DISPLAY_ALPHA)
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#if defined(SIMUT_DISPLAY_ALPHA)
  // Future: #include "HD44780_16x2.h"
  #error "SIMUT_DISPLAY_ALPHA not yet implemented"
#else
  /* Default: ILI9341 TFT + XPT2046 touch */
  #include "ILI9341_320x240.h"
  #include "XPT2046.h"

  struct DisplayDriver : public Ili9341_320x240, public Xpt2046_Touch {
  };
#endif
