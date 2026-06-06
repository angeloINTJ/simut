/**
 * @file SensorDrawing.h
 * @brief Shared procedural icons for sensor panel rendering.
 *
 * Each icon function is guarded by its sensor's compile flag — when a sensor
 * is disabled via SIMUT_SENSOR_*, its icon code is stripped from flash.
 *
 * All functions draw into a GFXcanvas16. The caller is responsible for
 * blitting the canvas to the TFT at the correct position.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#include <Adafruit_GFX.h>
#ifndef RGB565
#define RGB565(r,g,b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#endif

/* ── Thermometer (large, 18×28 px) ──────────────────────────────────────── */

#if SIMUT_SENSOR_DS18B20 || SIMUT_SENSOR_DHT22 || SIMUT_SENSOR_BME280
inline void drawThermometerLarge(GFXcanvas16* cv, int16_t x, int16_t y,
                                 uint16_t outline, uint16_t bg, uint16_t mercury) {
    cv->fillCircle(x + 5, y + 26, 7, outline);
    cv->fillRoundRect(x + 1, y, 8, 24, 4, outline);
    cv->fillRoundRect(x + 3, y + 2, 4, 20, 2, bg);
    cv->fillCircle(x + 5, y + 26, 5, bg);
    cv->fillRect(x + 4, y + 10, 2, 14, mercury);
    cv->fillCircle(x + 5, y + 26, 4, mercury);
    cv->fillCircle(x + 5, y + 2, 2, outline);
}
#endif

/* ── Thermometer (mini, ~12×20 px) ──────────────────────────────────────── */

#if SIMUT_SENSOR_DS18B20 || SIMUT_SENSOR_DHT22 || SIMUT_SENSOR_BME280
inline void drawThermometerMini(GFXcanvas16* cv, int16_t x, int16_t y,
                                uint16_t outline, uint16_t bg, uint16_t mercury) {
    cv->fillCircle(x + 4, y + 15, 5, outline);
    cv->fillRoundRect(x + 1, y, 7, 14, 3, outline);
    cv->fillRoundRect(x + 2, y + 1, 5, 12, 2, bg);
    cv->fillCircle(x + 4, y + 15, 4, bg);
    cv->fillRect(x + 3, y + 8, 3, 6, mercury);
    cv->fillCircle(x + 4, y + 15, 3, mercury);
    cv->fillCircle(x + 4, y + 2, 2, outline);
}
#endif

/* ── Drop / water droplet (large, ~20×22 px) ────────────────────────────── */

#if SIMUT_SENSOR_DHT22 || SIMUT_SENSOR_BME280
inline void drawDropLarge(GFXcanvas16* cv, int16_t x, int16_t y,
                          uint16_t color, uint16_t shine) {
    cv->fillCircle(x + 6, y + 20, 8, color);
    cv->fillTriangle(x + 6, y, x - 1, y + 18, x + 13, y + 18, color);
    cv->fillCircle(x + 4, y + 17, 3, shine);
    cv->fillCircle(x + 3, y + 14, 1, shine);
}
#endif

/* ── Drop / water droplet (mini, ~12×14 px) ─────────────────────────────── */

#if SIMUT_SENSOR_DHT22 || SIMUT_SENSOR_BME280
inline void drawDropMini(GFXcanvas16* cv, int16_t x, int16_t y,
                         uint16_t color, uint16_t shine) {
    cv->fillCircle(x + 5, y + 7, 6, color);
    cv->fillTriangle(x + 5, y - 5, x, y + 5, x + 10, y + 5, color);
    cv->fillCircle(x + 3, y + 5, 2, shine);
    cv->drawPixel(x + 3, y + 2, shine);
}
#endif

/* ── "°C" unit (normal mode, 24pt value) ────────────────────────────────── */

inline void drawUnitDegC_Normal(GFXcanvas16* cv, int16_t x,
                                uint16_t color, const GFXfont& font9,
                                const GFXfont& font12) {
    cv->setFont(&font9);
    cv->setTextColor(color);
    cv->setCursor(x, 17);
    cv->print("o");
    cv->setFont(&font12);
    cv->setCursor(x + 8, 35);
    cv->print("C");
}

/* ── "°C" unit (min/max mode, 9pt value) ────────────────────────────────── */

inline void drawUnitDegC_Mini(GFXcanvas16* cv, int16_t x, int16_t baseY,
                              uint16_t color, const GFXfont& font9) {
    cv->setFont(NULL);
    cv->setTextSize(1);
    cv->setTextColor(color);
    cv->setCursor(x, baseY + 2);
    cv->print("o");
    cv->setFont(&font9);
    cv->setCursor(x + 6, baseY + 15);
    cv->print("C");
}

