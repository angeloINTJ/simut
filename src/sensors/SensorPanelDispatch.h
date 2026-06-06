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

/** Calls the appropriate driver's renderPanel for the given sensor type.
 *  Theme colors passed explicitly so drivers follow the active theme. */
inline void sensorRenderPanel(GFXcanvas16* cv, SensorType type,
                              float v1, float v2, bool isValid,
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
#if SIMUT_SENSOR_DS18B20
    case TYPE_DS18B20:
        DS18B20_renderPanel(cv, v1, isValid, cardW, isRedPhase, panelBg,
                            font24, font12, font9,
                            txtSub, tempOk, tempHot, textOff);
        return;
#endif
    default: break;
    }
    /* Fallback: basic thermometer + temperature */
    DS18B20_renderPanel(cv, v1, isValid, cardW, isRedPhase, panelBg,
                        font24, font12, font9,
                        txtSub, tempOk, tempHot, textOff);
}
