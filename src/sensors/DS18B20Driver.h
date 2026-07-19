/**
 * @file DS18B20Driver.h
 * @brief DS18B20 1-Wire sensor driver using PIO state machines.
 * @details Wraps OneWirePIO_RP2040 and DS18B20PIO_RP2040 libraries.
 * Compiled only when SIMUT_SENSOR_DS18B20=1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DS18B20

#include <Arduino.h>
#include "sensors/SensorConfig.h"
#include "OneWirePIO.h"
#include "DS18B20PIO.h"
#if SIMUT_DISPLAY_TFT
#include "SensorDrawing.h"
#endif

/* PIN_ONEWIRE_DEFAULT → see src/simut_config.h */

struct DS18B20Driver {
    OneWirePIO  bus;
    DS18B20PIO  sensor;

    enum State {
        DS_IDLE,
        DS_WAITING
    };
    State    state = DS_IDLE;
    uint32_t timer = 0;

    DS18B20Driver( )
        : bus(pio0),
          sensor(bus)
    {}

    void begin( ) {
        bus.begin(PIN_ONEWIRE_DEFAULT);
    }

    bool readROM(uint8_t gpio, uint8_t* romOut) {
        return sensor.readROM(gpio, romOut);
    }

    bool isSensorPresent( ) {
        return bus.isSensorPresent( );
    }

    void setPin(uint8_t gpio) {
        bus.setPin(gpio);
    }

    void sendReset( ) {
        bus.sendReset( );
    }

    bool setResolution(uint8_t gpio, DS18B20PIO::Resolution res) {
        return sensor.setResolution(gpio, res);
    }

    void requestTemperatures(uint8_t gpio) {
        sensor.requestTemperatures(gpio);
    }

    bool getTemperatureValidated(uint8_t gpio, float& tempC) {
        return sensor.getTemperatureValidated(gpio, tempC, DS18B20PIO::CELSIUS);
    }

    bool checkRomMatch(const uint8_t* romRead, const uint8_t* romConfig) {
        for (int i = 0; i < 8; i++) {
            if (romRead[i] != romConfig[i]) return false;
        }
        return true;
    }

};

/* ── Panel rendering (normal mode, centered, theme-aware) ──────────── */
#if SIMUT_DISPLAY_TFT
inline void DS18B20_renderPanel(GFXcanvas16* cv, float t, bool isValid,
                 int16_t cardW, bool isRedPhase, uint16_t panelBg,
                 const GFXfont& font24, const GFXfont& font12,
                 const GFXfont& font9,
                 uint16_t txtSub, uint16_t tempOk,
                 uint16_t tempHot, uint16_t textOff) {
        /* Color aliases — exact match for original */
        uint16_t tempCol = isRedPhase ? RGB565(255,255,255) : tempOk;
        uint16_t unitCol = isRedPhase ? RGB565(220,200,200) : txtSub;
        uint16_t icTherm = isRedPhase ? RGB565(220,200,200) : txtSub;
        uint16_t mercCol = isRedPhase ? RGB565(255,255,255) : tempHot;

        if (!isValid || isnan(t)) {
            cv->setFont(&font12); cv->setTextSize(1);
            cv->setTextColor(isRedPhase ? RGB565(255,255,255) : tempHot);
            cv->setCursor((cardW - 60) / 2, 28);
            cv->print("--.-");
            return;
        }

        int intPart = (int)t;
        int decPart = abs((int)(t * 10.0f) % 10);
        char iP[10]; snprintf(iP, sizeof(iP), "%d", intPart);
        char dP[5];  snprintf(dP, sizeof(dP), ".%d", decPart);
        int16_t xx, yy; uint16_t iw, ih, decW;
        cv->setFont(&font24); cv->setTextSize(1);
        cv->getTextBounds(iP, 0, 0, &xx, &yy, &iw, &ih);
        cv->getTextBounds(dP, 0, 0, &xx, &yy, &decW, &ih);

        /* Exact original drawSlotPanel centering: iconW=20 + gap=8 + numW(iw+4+decW) + gap=3 + unitW=16 */
        int totalW = 20 + 8 + ((int)iw + 4 + (int)decW) + 3 + 16;
        int offsetX = (cardW - totalW) / 2;
        int textAnchor = offsetX + 20 + 8 + (int)iw;
        int iconX = offsetX;
        int unitX = textAnchor + (int)decW + 3;

        drawThermometerLarge(cv, iconX, 4, icTherm, panelBg, mercCol);

        cv->setFont(&font24); cv->setTextSize(1);
        cv->setTextColor(tempCol);
        int numCursorX = textAnchor - (int)iw - 4;
        cv->setCursor(numCursorX, 35);
        cv->print(iP);
        if (t < 0.0f) {
            cv->getTextBounds("-", 0, 0, &xx, &yy, &decW, &ih);
            int eraseW = (int)decW / 3; if (eraseW < 2) eraseW = 2;
            cv->fillRect(numCursorX, 0, eraseW, 40, panelBg);
        }
        cv->setFont(&font24);
        cv->setCursor(textAnchor, 35); cv->print(dP);

        cv->setFont(&font9); cv->setTextColor(unitCol);
        cv->setCursor(unitX, 17); cv->print("o");
        cv->setFont(&font12);
        cv->setCursor(unitX + 8, 35); cv->print("C");
    }

/* ── Min/Max panel rendering (temp only, 43px strip) ───────────────── */
inline void DS18B20_renderMinMax(GFXcanvas16* cv,
    float minT, float maxT, bool isValid,
    int16_t cardW, bool isRedPhase, uint16_t panelBg,
    const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot, uint16_t textOff,
    uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel) {
    uint16_t icCol   = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t mercCol = isRedPhase ? RGB565(255,255,255) : tempHot;

    int16_t x1, y1; uint16_t minLblW, maxLblW, hb;
    cv->setFont(&font9);
    cv->getTextBounds(minLabel, 0, 0, &x1, &y1, &minLblW, &hb);
    cv->getTextBounds(maxLabel, 0, 0, &x1, &y1, &maxLblW, &hb);
    int biggestLbl = (minLblW > maxLblW) ? (int)minLblW : (int)maxLblW;

    const int LABEL_X = 18;
    const int THERM_X = LABEL_X + biggestLbl + 8;
    const int DOT_X  = THERM_X + 36;
    const int CONTENT_RIGHT = 230;
    const int BTN_W = 58;
    const int BTN_X = CONTENT_RIGHT + ((cardW - 1) - CONTENT_RIGHT - BTN_W) / 2;

    drawMinMaxTempRow(cv, minLabel, LABEL_X, THERM_X, DOT_X,
        0, minT, isRedPhase,
        txtSub, icCol, mercCol, tempOk, panelBg, font9);

    drawMinMaxTempRow(cv, maxLabel, LABEL_X, THERM_X, DOT_X,
        22, maxT, isRedPhase,
        txtSub, icCol, mercCol, tempOk, panelBg, font9);

    drawMinMaxGraphBtn(cv, BTN_X, 2, BTN_W, 40, accentHigh, btnTextActive);
}
#endif /* SIMUT_SENSOR_DS18B20 */
#endif // SIMUT_DISPLAY_TFT
