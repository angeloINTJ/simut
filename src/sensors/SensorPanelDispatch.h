/**
 * @file SensorPanelDispatch.h
 * @brief Dispatches panel rendering to the correct sensor driver.
 *
 * Include this from DisplayManager_Dashboard.cpp to call
 * sensorRenderPanel() with a SensorType. The correct driver's
 * renderPanel is called based on the type.
 *
 * The two dispatchers used to be switch(type) statements, each with a
 * `#if SIMUT_SENSOR_*` per case (eight sites) so a case for a disabled driver
 * did not reference a function that was compiled out. They are now table
 * lookups: SENSOR_PANEL_DISPATCH pairs each type with a thin adapter that
 * forwards the uniform argument list to the driver's own renderPanel/
 * renderMinMax (whose signatures differ — a DS18B20 has one value, a DHT22
 * two, a BMP280 two of a different pair). The adapters keep the driver bodies
 * untouched. This file compiles only in TFT builds (DisplayManager_Dashboard
 * leaves the filter otherwise), and every TFT profile ships all three sensor
 * families, so the table's function pointers always resolve; per-family
 * conditionality returns with the driver-to-.cpp step of the seam.
 * docs/analysis/MODELO_DE_RECURSOS.md, P2 (costura dos sensores), incremento 3.
 *
 * Theme colors are passed as parameters so the drivers follow the
 * active theme without depending on Themes.h directly.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once
#include "SensorDrawing.h"
#include "DS18B20Driver.h"
#include "DHT22Driver.h"
#include "BME280Driver.h"

/* The fallback (a bare thermometer) is a reasonable last resort for a corrupt
 * stored type, and a silent wrong answer for a type someone forgot to add: a
 * BMP280 fell into it and showed only its temperature, with no hint that a
 * pressure reading existed at all. Adding a SensorType now breaks the build at
 * SENSOR_PANEL_DISPATCH below (the compiler cannot invent a table row) instead. */
static_assert(SENSOR_TYPE_MAX == TYPE_BMP280,
    "New SensorType added: give it a row in SENSOR_PANEL_DISPATCH below (panel "
    "and min/max adapters), otherwise the dashboard renders it as a "
    "temperature-only sensor without saying so.");

/* Uniform dispatch signatures — the union of what the three drivers take. Each
 * adapter forwards the subset its driver uses and ignores the rest. */
using SensorPanelFn = void (*)(GFXcanvas16* cv, float v1, float v2, float v3, bool isValid,
    int16_t cardW, bool leftAnchor, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font24, const GFXfont& font12, const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot, uint16_t valueCol, uint16_t textOff,
    const char* humSuffix);

using SensorMinMaxFn = void (*)(GFXcanvas16* cv, float minV1, float maxV1, float minV2, float maxV2,
    bool isValid, int16_t cardW, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font9, uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t valueCol, uint16_t textOff, uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel, const char* humSuffix);

/* ── Panel adapters (v1=temp, v2=hum, v3=press; valueCol = the second value's
 *    color, which the driver reads as humidity or pressure) ───────────────── */

inline void panelDS18B20(GFXcanvas16* cv, float v1, [[maybe_unused]] float v2, [[maybe_unused]] float v3,
    bool isValid, int16_t cardW, [[maybe_unused]] bool leftAnchor, bool isRedPhase, uint16_t panelBg,
    uint16_t alarmText, uint16_t alarmTextDim, const GFXfont& font24, const GFXfont& font12,
    const GFXfont& font9, uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    [[maybe_unused]] uint16_t valueCol, uint16_t textOff, [[maybe_unused]] const char* humSuffix) {
    DS18B20_renderPanel(cv, v1, isValid, cardW, isRedPhase, panelBg, alarmText, alarmTextDim,
                        font24, font12, font9, txtSub, tempOk, tempHot, textOff);
}

inline void panelDHT22(GFXcanvas16* cv, float v1, float v2, [[maybe_unused]] float v3, bool isValid,
    int16_t cardW, bool leftAnchor, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font24, const GFXfont& font12, const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot, uint16_t valueCol, uint16_t textOff,
    const char* humSuffix) {
    DHT22_renderPanel(cv, v1, v2, isValid, cardW, leftAnchor, isRedPhase, panelBg, alarmText,
                      alarmTextDim, font24, font12, font9, txtSub, tempOk, tempHot, valueCol,
                      textOff, humSuffix);
}

inline void panelBMP280(GFXcanvas16* cv, float v1, [[maybe_unused]] float v2, float v3, bool isValid,
    int16_t cardW, bool leftAnchor, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font24, const GFXfont& font12, const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot, uint16_t valueCol, uint16_t textOff,
    [[maybe_unused]] const char* humSuffix) {
    /* BMx280: always T+P — humidity detection is unreliable on some chips, so a
     * BME280 slot renders here too. v3 is pressure; valueCol is its color. */
    BMP280_renderPanel(cv, v1, v3, isValid, cardW, leftAnchor, isRedPhase, panelBg, alarmText,
                       alarmTextDim, font24, font12, font9, txtSub, tempOk, tempHot, valueCol, textOff);
}

/* ── Min/max adapters (minV1/maxV1 = temperature, minV2/maxV2 = humidity) ─── */

inline void minMaxDS18B20(GFXcanvas16* cv, float minV1, float maxV1, [[maybe_unused]] float minV2,
    [[maybe_unused]] float maxV2, bool isValid, int16_t cardW, bool isRedPhase, uint16_t panelBg,
    uint16_t alarmText, uint16_t alarmTextDim, const GFXfont& font9, uint16_t txtSub, uint16_t tempOk,
    uint16_t tempHot, [[maybe_unused]] uint16_t valueCol, uint16_t textOff, uint16_t accentHigh,
    uint16_t btnTextActive, const char* minLabel, const char* maxLabel,
    [[maybe_unused]] const char* humSuffix) {
    DS18B20_renderMinMax(cv, minV1, maxV1, isValid, cardW, isRedPhase, panelBg, alarmText, alarmTextDim,
                         font9, txtSub, tempOk, tempHot, textOff, accentHigh, btnTextActive,
                         minLabel, maxLabel);
}

inline void minMaxDHT22(GFXcanvas16* cv, float minV1, float maxV1, float minV2, float maxV2,
    bool isValid, int16_t cardW, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font9, uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t valueCol, uint16_t textOff, uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel, const char* humSuffix) {
    DHT22_renderMinMax(cv, minV1, maxV1, minV2, maxV2, isValid, cardW, isRedPhase, panelBg, alarmText,
                       alarmTextDim, font9, txtSub, tempOk, tempHot, valueCol, textOff, accentHigh,
                       btnTextActive, minLabel, maxLabel, humSuffix);
}

inline void minMaxBME280(GFXcanvas16* cv, float minV1, float maxV1, float minV2, float maxV2,
    bool isValid, int16_t cardW, bool isRedPhase, uint16_t panelBg, uint16_t alarmText,
    uint16_t alarmTextDim, const GFXfont& font9, uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t valueCol, uint16_t textOff, uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel, const char* humSuffix) {
    BME280_renderMinMax(cv, minV1, maxV1, minV2, maxV2, isValid, cardW, isRedPhase, panelBg, alarmText,
                        alarmTextDim, font9, txtSub, tempOk, tempHot, valueCol, textOff, accentHigh,
                        btnTextActive, minLabel, maxLabel, humSuffix);
}

/* Type → (panel, min/max) renderer. BMP280 draws its min/max temperature-only
 * (there is no pressure extreme tracked), which is why it points at the
 * DS18B20 min/max — matching the switch this replaced. The first row is also
 * the fallback for an unknown/corrupt type: a bare thermometer. */
struct SensorPanelDispatch {
    SensorType     type;
    SensorPanelFn  panel;
    SensorMinMaxFn minMax;
};

inline constexpr SensorPanelDispatch SENSOR_PANEL_DISPATCH[] = {
    { TYPE_DS18B20, panelDS18B20, minMaxDS18B20 },
    { TYPE_DHT22,   panelDHT22,   minMaxDHT22   },
    { TYPE_BME280,  panelBMP280,  minMaxBME280  },
    { TYPE_BMP280,  panelBMP280,  minMaxDS18B20 },
};

inline const SensorPanelDispatch& sensorPanelFor(SensorType type) {
    for (const SensorPanelDispatch& d : SENSOR_PANEL_DISPATCH) if (d.type == type) return d;
    return SENSOR_PANEL_DISPATCH[0];  /* fallback: bare thermometer */
}

/** Calls the appropriate driver's renderPanel for the given sensor type.
 *  Theme colors passed explicitly so drivers follow the active theme. */
inline void sensorRenderPanel(GFXcanvas16* cv, SensorType type,
                              float v1, float v2, float v3, bool isValid,
                              int16_t cardW, bool leftAnchor, bool isRedPhase,
                              uint16_t panelBg, uint16_t alarmText, uint16_t alarmTextDim,
                              const GFXfont& font24,
                              const GFXfont& font12, const GFXfont& font9,
                              uint16_t txtSub, uint16_t tempOk,
                              uint16_t tempHot, uint16_t humidity,
                              uint16_t textOff,
                              const char* humSuffix) {
    sensorPanelFor(type).panel(cv, v1, v2, v3, isValid, cardW, leftAnchor, isRedPhase,
                               panelBg, alarmText, alarmTextDim, font24, font12, font9,
                               txtSub, tempOk, tempHot, humidity, textOff, humSuffix);
}

/** Dispatches min/max panel rendering to the correct sensor driver.
 *  minV1/maxV1 = temperature min/max, minV2/maxV2 = humidity min/max.
 *  Temp-only sensors (DS18B20) ignore humidity values. */
inline void sensorRenderMinMax(GFXcanvas16* cv, SensorType type,
    float minV1, float maxV1, float minV2, float maxV2, bool isValid,
    int16_t cardW, bool isRedPhase, uint16_t panelBg,
    uint16_t alarmText, uint16_t alarmTextDim,
    const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t humidity, uint16_t textOff,
    uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel,
    const char* humSuffix) {
    sensorPanelFor(type).minMax(cv, minV1, maxV1, minV2, maxV2, isValid, cardW, isRedPhase,
                                panelBg, alarmText, alarmTextDim, font9, txtSub, tempOk, tempHot,
                                humidity, textOff, accentHigh, btnTextActive,
                                minLabel, maxLabel, humSuffix);
}
