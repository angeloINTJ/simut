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
        uint16_t txtCol  = isRedPhase ? RGB565(255,255,255) : txtSub;
        uint16_t tempCol = isRedPhase ? RGB565(255,255,255) : tempOk;
        uint16_t merc    = isRedPhase ? RGB565(255,255,255) : tempHot;
        uint16_t icTherm = isRedPhase ? RGB565(220,200,200) : txtCol;

        if (!isValid || isnan(t)) {
            cv->setFont(&font12);
            cv->setTextColor(textOff);
            cv->setCursor((cardW - 60) / 2, 15);
            cv->print("--.-");
            drawThermometerLarge(cv, (cardW - 160) / 2 - 10, 4,
                                 icTherm, panelBg, merc);
            return;
        }

        int negMul = (t < 0.0f) ? -1 : 1;
        float absT = (t < 0.0f) ? -t : t;
        int intPart = (int)absT;
        int decPart = (int)((absT - (float)intPart) * 10.0f + 0.5f);
        if (decPart >= 10) { intPart++; decPart = 0; }
        char iP[8]; snprintf(iP, sizeof(iP), "%d", intPart);
        char dP[4]; snprintf(dP, sizeof(dP), "%d", decPart);
        int16_t xx, yy; uint16_t iw, ih;
        cv->getTextBounds(iP, 0, 0, &xx, &yy, &iw, &ih);

        int anchorX = (cardW - (int)iw - 8) / 2 + 10;
        int iconX   = (cardW - 160) / 2 - 10;

        drawThermometerLarge(cv, iconX, 4, icTherm, panelBg, merc);

        cv->setFont(&font24);
        cv->setTextColor(tempCol);
        if (negMul < 0) { cv->setCursor(anchorX - (int)iw - 6, 35); cv->print("-"); }
        cv->setCursor(anchorX - (int)iw, 35);
        cv->print(iP);
        cv->setCursor(anchorX + 4, 35); cv->print(".");
        cv->print(dP);

        int unitX = anchorX + 4 + (int)iw + 6;
        drawUnitDegC_Normal(cv, unitX, txtCol, font9, font12);
    }

#else
#define PIN_ONEWIRE_DEFAULT 255
#endif /* SIMUT_SENSOR_DS18B20 */
