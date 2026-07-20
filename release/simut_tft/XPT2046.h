/**
 * @file display/XPT2046.h
 * @brief XPT2046 resistive touch controller driver.
 * @details Wraps the XPT2046_Touchscreen library. Provides touch point
 * reading and raw touch state detection.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include <Arduino.h>
#include <XPT2046_Touchscreen.h>

struct Xpt2046_Touch {
	XPT2046_Touchscreen* ts = nullptr;
};
