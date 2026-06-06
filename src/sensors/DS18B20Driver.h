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
#include "OneWirePIO.h"
#include "DS18B20PIO.h"
#include "SensorDrawing.h"

#define PIN_ONEWIRE_DEFAULT 0

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

    bool setResolution(DS18B20PIO::Resolution res) {
        return sensor.setResolution(PIN_ONEWIRE_DEFAULT, res);
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
#else
#define PIN_ONEWIRE_DEFAULT 255
#endif /* SIMUT_SENSOR_DS18B20 */
