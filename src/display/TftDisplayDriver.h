/**
 * @file DisplayDriver.h
 * @brief Display driver abstraction for compile-time display selection.
 * Currently supports ILI9341 TFT (default). 16x2 alpha display (future).
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "TftWithOffset.h"
#include <XPT2046_Touchscreen.h>

struct DisplayDriver {
	TftWithOffset*         tft   = nullptr;
	XPT2046_Touchscreen*   ts    = nullptr;
	GFXcanvas16*           canvas = nullptr;
	GFXcanvas16*           canvasSmall = nullptr;
	bool                   firstInit = true;

	int16_t width  = 320;
	int16_t height = 240;

	/* Delegate GFX calls to TFT */
	Adafruit_GFX* gfx( ) { return tft; }
};
