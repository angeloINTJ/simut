/**
 * @file display/DisplayConfig.h
 * @brief Compile-time display feature flags.
 *
 * Set via platformio.ini build_flags:
 *   -DSIMUT_DISPLAY_TFT=0     (disable ILI9341 TFT + touch UI, saves ~30 KB)
 *   -DSIMUT_DISPLAY_ALPHA=1   (enable HD44780 16x2 character display)
 *
 * Default keeps TFT enabled.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once

#ifndef SIMUT_DISPLAY_TFT
#define SIMUT_DISPLAY_TFT 1
#endif

#ifndef SIMUT_DISPLAY_ALPHA
#define SIMUT_DISPLAY_ALPHA 0
#endif
