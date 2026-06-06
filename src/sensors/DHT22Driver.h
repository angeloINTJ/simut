/**
 * @file DHT22Driver.h
 * @brief DHT22 single-wire sensor driver using PIO state machines.
 * @details Wraps DHTBus and DHT22PIO_RP2040 libraries.
 * Compiled only when SIMUT_SENSOR_DHT22=1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_DHT22

#include <Arduino.h>
#include "DHTBus.h"
#include "DHT22PIO.h"
#include "SensorDrawing.h"

#define PIN_DHT_DEFAULT 10

struct DHT22Driver {
    DHTBus    bus;
    DHT22PIO  sensor;

    enum State {
        DHT_IDLE,
        DHT_WAITING
    };
    State    state = DHT_IDLE;
    int      currentSensorIdx = -1;
    uint32_t timer = 0;

    DHT22Driver( )
        : bus(pio1),
          sensor(bus)
    {}

    void begin( ) {
        bus.begin(PIN_DHT_DEFAULT);
        gpio_set_pulls(PIN_DHT_DEFAULT, true, false);
    }

    void requestReading(uint8_t gpio) {
        sensor.requestReading(gpio);
    }

    void update( ) {
        sensor.update( );
    }

    DHT22PIO::State getState( ) {
        return sensor.getState( );
    }

    bool getResults(float& t, float& h) {
        return sensor.getResults(t, h);
    }

    void reset( ) {
        sensor.reset( );
    }

};

/* ── Panel rendering (normal mode, theme-aware) ──────────────────────── */
inline void DHT22_renderPanel(GFXcanvas16* cv, float t, float h, bool isValid,
                 int16_t cardW, bool leftAnchor, bool isRedPhase,
                 uint16_t panelBg, const GFXfont& font24,
                 const GFXfont& font12, const GFXfont& font9,
                 uint16_t txtSub, uint16_t tempOk,
                 uint16_t tempHot, uint16_t humidity,
                 uint16_t textOff,
                 const char* humSuffix) {
        /* Color aliases — exact match for original drawAmbientPanel normal mode */
        uint16_t tempCol   = isRedPhase ? RGB565(255,255,255) : tempOk;
        uint16_t unitCol   = isRedPhase ? RGB565(220,200,200) : txtSub;
        uint16_t icTherm   = isRedPhase ? RGB565(220,200,200) : txtSub;
        uint16_t mercCol   = isRedPhase ? RGB565(255,255,255) : tempHot;
        uint16_t humCol    = isRedPhase ? RGB565(255,255,255) : humidity;
        uint16_t dropCol   = isRedPhase ? RGB565(220,200,200) : humidity;
        uint16_t dropShine = isRedPhase ? RGB565(255,255,255) : RGB565(200,230,255);
        uint16_t pctCol    = isRedPhase ? RGB565(220,200,200) : txtSub;

        if (!isValid || isnan(t)) {
            cv->setFont(&font12); cv->setTextSize(1);
            cv->setTextColor(isRedPhase ? RGB565(255,255,255) : tempHot);
            cv->setCursor(25, 28);
            cv->print("--.-");
            return;
        }

        /* ── Temperature (exact original drawAmbientPanel logic) ── */
        int intPart = (int)t;
        int decPart = abs((int)(t * 10.0f) % 10);
        char iP[10]; snprintf(iP, sizeof(iP), "%d", intPart);
        char dP[5];  snprintf(dP, sizeof(dP), ".%d", decPart);
        int16_t xx, yy; uint16_t iw, ih, decW;
        cv->setFont(&font24); cv->setTextSize(1);
        cv->getTextBounds(iP, 0, 0, &xx, &yy, &iw, &ih);
        cv->getTextBounds(dP, 0, 0, &xx, &yy, &decW, &ih);

        /* Ambient: textAnchor=92, iconX=14. Slot: centered via exact offsetX. */
        int textAnchor, iconX;
        if (leftAnchor) {
            textAnchor = 92;
            iconX = 14;
        } else {
            int totalW = 20 + 8 + ((int)iw + 4 + (int)decW) + 3 + 16;
            int offsetX = (cardW - totalW) / 2;
            textAnchor = offsetX + 20 + 8 + (int)iw;
            iconX = offsetX;
        }
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

        /* ── Humidity (right side, exact original layout) ── */
        if (!isnan(h)) {
            cv->setFont(&font12); cv->setTextSize(1);
            int16_t px, py; uint16_t pctW, pctH, hw, hh;
            cv->getTextBounds(humSuffix, 0, 0, &px, &py, &pctW, &pctH);
            const int rightMargin = 15;
            int pctX = cardW - rightMargin - (int)pctW;
            int humAnchor = pctX - 3;

            cv->setFont(&font24); cv->setTextSize(1);
            cv->setTextColor(humCol);
            char hb[6];
            if (isnan(h)) snprintf(hb, sizeof(hb), "--");
            else snprintf(hb, sizeof(hb), "%d", (int)h);
            cv->getTextBounds(hb, 0, 0, &px, &py, &hw, &hh);
            cv->setCursor(humAnchor - (int)hw, 35);
            cv->print(hb);

            cv->setFont(&font12);
            cv->setTextColor(pctCol);
            cv->setCursor(pctX, 34);
            cv->print(humSuffix);

            /* Drop icon */
            int dropRight = humAnchor - (int)hw - 6;
            int dx = dropRight - 14;
            drawDropLarge(cv, dx, 4, dropCol, dropShine);
        }
    }
#else
#define PIN_DHT_DEFAULT 255
#endif /* SIMUT_SENSOR_DHT22 */
