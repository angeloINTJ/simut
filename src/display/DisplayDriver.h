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

#include "DisplayConfig.h"

#if SIMUT_DISPLAY_ALPHA
  #include "HD44780_16x2.h"
  using DisplayDriver = Hd44780_16x2;
  /* Note: DisplayManager rendering code is TFT-specific. Full alpha
   * display support requires refactoring DisplayManager to abstract
   * the rendering surface (GFX vs character framebuffer). */
#elif SIMUT_DISPLAY_TFT
  /* Default: ILI9341 TFT + XPT2046 touch */
  #include "ILI9341_320x240.h"
  #include "XPT2046.h"

  struct DisplayDriver : public Ili9341_320x240, public Xpt2046_Touch {
  };
#else
  /* Headless (SIMUT Air): no display driver at all. DisplayManager keeps a
   * default-constructed empty driver; every driver-touching code path is
   * compiled out under #if SIMUT_DISPLAY_* guards. */
  struct DisplayDriver {
  };
#endif
