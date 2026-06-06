/**
 * @file display/DisplayDriver.h
 * @brief Display driver dispatcher — selects the active display hardware at compile time.
 * @details Include this header to get the correct DisplayDriver implementation.
 * The active driver is selected via build flags in platformio.ini:
 *   SIMUT_DISPLAY_TFT   — ILI9341 320x240 TFT + XPT2046 touch (default)
 *   SIMUT_DISPLAY_ALPHA — 16x2 alphanumeric HD44780 (future)
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

/* ── Driver selection ───────────────────────────────────────────────── */

#if defined(SIMUT_DISPLAY_ALPHA)
  // Future: #include "AlphaDisplayDriver.h"
  #error "SIMUT_DISPLAY_ALPHA not yet implemented"
#else
  /* Default: ILI9341 TFT */
  #include "TftDisplayDriver.h"
#endif
