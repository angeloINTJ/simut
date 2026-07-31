/**
 * @file SensorPanelDispatch.h
 * @brief Dispatches panel rendering to the correct sensor driver.
 *
 * Include this from DisplayManager_Dashboard.cpp to call
 * sensorRenderPanel() with a SensorType. The correct driver's
 * renderPanel is called based on the type.
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

/* Both switches below end in a fallback that draws a bare thermometer. That is
 * a reasonable last resort for a corrupt stored type, and a silent wrong answer
 * for a type someone forgot to add: a BMP280 fell into it and showed only its
 * temperature, with no hint that a pressure reading existed at all. Adding a
 * SensorType now breaks the build here instead. */
static_assert(SENSOR_TYPE_MAX == TYPE_BMP280,
    "New SensorType added: give it a case in sensorRenderPanel AND "
    "sensorRenderMinMax below, otherwise the dashboard renders it as a "
    "temperature-only sensor without saying so.");

/** Calls the appropriate driver's renderPanel for the given sensor type.
 *  Theme colors passed explicitly so drivers follow the active theme. */
inline void sensorRenderPanel(GFXcanvas16* cv, SensorType type,
                              float v1, float v2, float v3, bool isValid,
                              int16_t cardW, bool leftAnchor, bool isRedPhase,
                              uint16_t panelBg, const GFXfont& font24,
                              const GFXfont& font12, const GFXfont& font9,
                              uint16_t txtSub, uint16_t tempOk,
                              uint16_t tempHot, uint16_t humidity,
                              uint16_t textOff,
                              const char* humSuffix) {
    switch (type) {
#if SIMUT_SENSOR_DHT22
    case TYPE_DHT22:
        DHT22_renderPanel(cv, v1, v2, isValid, cardW, leftAnchor,
                          isRedPhase, panelBg, font24, font12, font9,
                          txtSub, tempOk, tempHot, humidity, textOff,
                          humSuffix);
        return;
#endif
#if SIMUT_SENSOR_BME280
    case TYPE_BME280:
    case TYPE_BMP280:
        /* BMx280: always T+P — humidity detection unreliable on some chips.
         * TYPE_BMP280 was missing here from the day the two parts were split
         * into separate types, so the one chip that exists to measure pressure
         * was the one rendered without it. */
        BMP280_renderPanel(cv, v1, v3, isValid, cardW, leftAnchor,
                           isRedPhase, panelBg, font24, font12, font9,
                           txtSub, tempOk, tempHot, humidity, textOff);
        return;
#endif
#if SIMUT_SENSOR_DS18B20
    case TYPE_DS18B20:
        DS18B20_renderPanel(cv, v1, isValid, cardW, isRedPhase, panelBg,
                            font24, font12, font9,
                            txtSub, tempOk, tempHot, textOff);
        return;
#endif
    default: break;
    }
#if SIMUT_SENSOR_DS18B20
    /* Fallback: basic thermometer + temperature */
    DS18B20_renderPanel(cv, v1, isValid, cardW, isRedPhase, panelBg,
                        font24, font12, font9,
                        txtSub, tempOk, tempHot, textOff);
#endif
}

/** Dispatches min/max panel rendering to the correct sensor driver.
 *  minV1/maxV1 = temperature min/max, minV2/maxV2 = humidity min/max.
 *  Temp-only sensors (DS18B20) ignore humidity values. */
inline void sensorRenderMinMax(GFXcanvas16* cv, SensorType type,
    float minV1, float maxV1, float minV2, float maxV2, bool isValid,
    int16_t cardW, bool isRedPhase, uint16_t panelBg,
    const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t humidity, uint16_t textOff,
    uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel,
    const char* humSuffix) {
    switch (type) {
#if SIMUT_SENSOR_DHT22
    case TYPE_DHT22:
        DHT22_renderMinMax(cv, minV1, maxV1, minV2, maxV2, isValid,
            cardW, isRedPhase, panelBg, font9,
            txtSub, tempOk, tempHot, humidity, textOff,
            accentHigh, btnTextActive,
            minLabel, maxLabel, humSuffix);
        return;
#endif
#if SIMUT_SENSOR_BME280
    case TYPE_BME280:
        BME280_renderMinMax(cv, minV1, maxV1, minV2, maxV2, isValid,
            cardW, isRedPhase, panelBg, font9,
            txtSub, tempOk, tempHot, humidity, textOff,
            accentHigh, btnTextActive,
            minLabel, maxLabel, humSuffix);
        return;
#endif
#if SIMUT_SENSOR_DS18B20
    case TYPE_DS18B20:
    /* BMP280 has no humidity, and min/max is only tracked for temperature and
     * humidity (DashPanel::minTemp..maxHum) — there is no pressure extreme to
     * draw yet. Temperature-only is the honest answer here, and it is stated
     * rather than reached by falling through. */
    case TYPE_BMP280:
        DS18B20_renderMinMax(cv, minV1, maxV1, isValid,
            cardW, isRedPhase, panelBg, font9,
            txtSub, tempOk, tempHot, textOff,
            accentHigh, btnTextActive,
            minLabel, maxLabel);
        return;
#endif
    default: break;
    }
#if SIMUT_SENSOR_DS18B20
    /* Fallback: temp-only min/max */
    DS18B20_renderMinMax(cv, minV1, maxV1, isValid,
        cardW, isRedPhase, panelBg, font9,
        txtSub, tempOk, tempHot, textOff,
        accentHigh, btnTextActive,
        minLabel, maxLabel);
#endif
}
