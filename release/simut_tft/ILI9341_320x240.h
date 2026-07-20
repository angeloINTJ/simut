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
#include "TftWithOffset.h"

struct Ili9341_320x240 {
	TftWithOffset* tft   = nullptr;
	GFXcanvas16*   canvas = nullptr;
	GFXcanvas16*   canvasSmall = nullptr;
	bool           firstInit = true;

	int16_t width  = 320;
	int16_t height = 240;

	/* Delegate GFX drawing calls to TFT */
	Adafruit_GFX* gfx( ) { return tft; }
};
