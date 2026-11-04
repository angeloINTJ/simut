/**
 * @file display/ILI9341_320x240.h
 * @brief ILI9341 TFT display driver — 320x240 pixels, SPI interface.
 * @details Wraps TftWithOffset (Adafruit_ILI9341 with alignment offset).
 * Provides GFX drawing surface, off-screen canvas, and screen dimensions.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "../TftWithOffset.h"

struct Ili9341_320x240 {
	TftWithOffset* tft   = nullptr;
	GFXcanvas16*   canvas = nullptr;
	bool           firstInit = true;

	/* The panel's size in pixels, the one place the core and the Angulo driver read it
	 * from: DisplayDriver::W and ::H (the core's primitives, the touch mapping, the
	 * calibration targets, the Angulo's strips). Not yet the only place it is written:
	 * the margin fill (TftWithOffset.h), the screenshot and the CLI/API range checks
	 * still say 320/240, and docs/analysis/UI_DRIVERS.md 2.6 lists them for a panel of
	 * another size. The original UI's layouts stay 320x240: they are its layout, not
	 * the panel's. */
	static constexpr int16_t W = 320;
	static constexpr int16_t H = 240;

	/* Data members, so they cost RAM and sit before every later member of DisplayManager
	 * (_driver is one): removing them would move those and change the code. Nothing reads
	 * them (grep of 2026-10-14). */
	int16_t width  = W;
	int16_t height = H;

	/* Delegate GFX drawing calls to TFT */
	Adafruit_GFX* gfx( ) { return tft; }
};
