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
                 uint16_t textOff) {
        uint16_t txtCol  = isRedPhase ? RGB565(255,255,255) : txtSub;
        uint16_t tempCol = isRedPhase ? RGB565(255,255,255) : tempOk;
        uint16_t humCol  = isRedPhase ? RGB565(255,255,255) : humidity;
        uint16_t merc    = isRedPhase ? RGB565(255,255,255) : tempHot;
        uint16_t icTherm = isRedPhase ? RGB565(220,200,200) : txtCol;
        uint16_t icDrop  = isRedPhase ? RGB565(220,200,200) : humCol;
        uint16_t shine   = isRedPhase ? RGB565(255,255,255) : RGB565(200,230,255);
        uint16_t pctCol  = isRedPhase ? RGB565(220,200,200) : txtSub;

        if (!isValid || isnan(t)) {
            cv->setFont(&font12);
            cv->setTextColor(textOff);
            cv->setCursor(leftAnchor ? 80 : (cardW - 160) / 2 + 30, 15);
            cv->print("--.-");
            drawThermometerLarge(cv, leftAnchor ? 14 : (cardW - 160) / 2 - 10, 4,
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

        int anchorX = leftAnchor ? 92 : (cardW - (int)iw - 8) / 2 + 10;
        int iconX  = leftAnchor ? 14 : (cardW - 160) / 2 - 10;

        drawThermometerLarge(cv, iconX, 4, icTherm, panelBg, merc);

        cv->setFont(&font24);
        cv->setTextColor(tempCol);
        if (negMul < 0) { cv->setCursor(anchorX - (int)iw - 6, 35); cv->print("-"); }
        cv->setCursor(anchorX - (int)iw, 35);
        cv->print(iP);
        cv->setCursor(anchorX + 4, 35); cv->print(".");
        cv->print(dP);

        int unitX = anchorX + 4 + (int)((intPart >= 10 ? iw + 6 : iw)) + 6;
        drawUnitDegC_Normal(cv, unitX, txtCol, font9, font12);

        /* Humidity — right-aligned, same layout as ambient panel */
        if (!isnan(h)) {
            char hb[6];
            if (isnan(h)) snprintf(hb, sizeof(hb), "--");
            else snprintf(hb, sizeof(hb), "%d", (int)h);
            int16_t px, py; uint16_t pw, ph;
            cv->getTextBounds(hb, 0, 0, &px, &py, &pw, &ph);
            int humAnchor = cardW - 15 - pw;
            cv->setFont(&font24);
            cv->setTextColor(humCol);
            cv->setCursor(humAnchor, 35);
            cv->print(hb);
            int dropX = humAnchor - (int)pw - 20;
            drawDropLarge(cv, dropX, 4, icDrop, shine);
            cv->setFont(&font12);
            cv->setTextColor(pctCol);
            cv->setCursor(cardW - 15 - pw - 4, 34);
            cv->print("%");
        }
    }

#else
#define PIN_DHT_DEFAULT 255
#endif /* SIMUT_SENSOR_DHT22 */
