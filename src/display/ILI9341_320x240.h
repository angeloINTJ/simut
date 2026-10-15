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

	/* The panel's size in pixels, the one place it is written. Whatever needs it reads
	 * DisplayDriver::W and ::H (the core's primitives, the touch mapping, the calibration
	 * targets, the Angulo's strips), so a panel of another size is another driver with
	 * other numbers. The original UI's layouts stay 320x240: they are its layout, not the
	 * panel's (docs/analysis/UI_DRIVERS.md 2.6). */
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
