/**
 * @file BME280Driver.h
 * @brief BME280 I2C temperature/humidity/pressure sensor driver.
 * @details PIO-based I2C bit-bang via BMx280PIO_RP2040 library.
 * Works on any GPIO 0-15 pair — no hardware I2C peripheral needed.
 * Compensation formulas follow Bosch BME280 datasheet rev 1.6 §4.2.3
 * (implemented by the library).
 * Compiled only when SIMUT_SENSOR_BME280=1.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_BME280

#include <Arduino.h>
#include "BMx280PIO_RP2040.h"
#if SIMUT_DISPLAY_TFT
#include "SensorDrawing.h"
#endif

/* ── Timing ─────────────────────────────────────────────────────────────── */
#define BME280_MEAS_TIME_MS  15  /* ~9.3ms actual (osrs ×1) + margin */

struct BME280Driver {
    enum State {
        BME_IDLE,
        BME_WAITING
    };
    State    state = BME_IDLE;
    int      currentSensorIdx = -1;
    uint32_t timer = 0;

    /* ── PIO I2C sensor instance — lazy-allocated in begin() ── */
    BMx280PIO_RP2040* _sensor = nullptr;
    bool     _compLoaded = false;

    BME280Driver( ) { }

    /** @return true if the detected chip is a BME280 (has humidity).
     *         false for BMP280 (temperature + pressure only). */
    bool isBME( ) const { return _sensor && _sensor->isBME280(); }

    /** @return raw chip ID (0x58=BMP280, 0x60=BME280). */
    uint8_t getChipId( ) const { return _sensor ? _sensor->getChipID() : 0; }

    /* ── Initialization ───────────────────────────────────────────────── */
    /** Initialize the driver on the given SDA/SCL pins.
     *  Any GPIO 0-15 pair works — PIO bit-bang has no pin restrictions.
     *
     *  Two-phase strategy:
     *   1. Try PIO+DMA first (fast, but needs free PIO instruction slots).
     *      If pio0 is congested (OneWirePIO, etc.), begin() fails.
     *   2. Retry with forceGPIO(true) — GPIO bit-bang only, slower but
     *      uses zero PIO resources. Keeps pio0 free for DS18B20.
     *
     *  Tries primary address (0x76, SDO→GND) first, then secondary (0x77). */
    void begin(uint8_t sda, uint8_t scl) {
        /* ── Pass 1: PIO+DMA (fast) ──────────────────────────────────── */
        _sensor = new BMx280PIO_RP2040(sda, scl, BME280_ADDR_PRIMARY);
        if (_sensor->begin()) {
            _compLoaded = true;
            return;
        }
        delete _sensor;

        /* ── Pass 2: GPIO-only fallback (slower, zero PIO) ───────────── */
        _sensor = new BMx280PIO_RP2040(sda, scl, BME280_ADDR_PRIMARY);
        _sensor->forceGPIO(true);
        if (_sensor->begin()) {
            _compLoaded = true;
            return;
        }
        delete _sensor;

        /* ── Pass 3: secondary address (0x77), PIO+DMA ───────────────── */
        _sensor = new BMx280PIO_RP2040(sda, scl, 0x77);
        if (_sensor->begin()) {
            _compLoaded = true;
            return;
        }
        delete _sensor;

        /* ── Pass 4: secondary address (0x77), GPIO-only ─────────────── */
        _sensor = new BMx280PIO_RP2040(sda, scl, 0x77);
        _sensor->forceGPIO(true);
        if (_sensor->begin()) {
            _compLoaded = true;
            return;
        }
        /* No sensor at either address */
        delete _sensor;
        _sensor = nullptr;
        _compLoaded = false;
    }

    /** Initialize with explicit I2C address — for multi-sensor buses.
     *  Same two-phase strategy: PIO+DMA first, GPIO fallback second. */
    bool begin(uint8_t sda, uint8_t scl, uint8_t addr) {
        /* Pass 1: PIO+DMA */
        _sensor = new BMx280PIO_RP2040(sda, scl, addr);
        _compLoaded = _sensor->begin();
        Serial.printf("[BMx] PIO+DMA addr=0x%02X cid=0x%02X ok=%d\n", addr, _sensor->getChipID(), _compLoaded);
        if (_compLoaded) return true;

        /* Pass 2: GPIO-only fallback */
        delete _sensor;
        _sensor = new BMx280PIO_RP2040(sda, scl, addr);
        _sensor->forceGPIO(true);
        _compLoaded = _sensor->begin();
        Serial.printf("[BMx] GPIO addr=0x%02X cid=0x%02X ok=%d\n", addr, _sensor->getChipID(), _compLoaded);
        if (!_compLoaded) {
            delete _sensor;
            _sensor = nullptr;
        }
        return _compLoaded;
    }

    /* ── Trigger measurement (forced mode) ────────────────────────────── */
    void requestReading( ) {
        if (!_compLoaded) return;
        /* setMode(FORCED) → _applyConfig() writes CTRL_HUM + CTRL_MEAS + CONFIG.
         * Sensor performs one measurement cycle then returns to sleep. */
        _sensor->setMode(BME280_MODE_FORCED);
    }

    /* ── Read raw + compensate ────────────────────────────────────────── */
    bool getResults(float &t, float &h, float &p) {
        if (!_compLoaded) return false;

        _sensor->readAll(&t, &p, &h);

        /* Sanity checks */
        if (t < -40.0f || t > 85.0f)  t = NAN;
        { volatile float hv = h; if (hv < 0.0f || hv > 100.0f) h = NAN; }
        if (p < 300.0f  || p > 1100.0f) p = NAN;

        return !isnan(t);
    }

    void reset( ) {
        state = BME_IDLE;
        currentSensorIdx = -1;
    }
};


/* ── Panel rendering (TFT dashboard, theme-aware) ───────────────────── */
#if SIMUT_DISPLAY_TFT

/* ── BMP280 panel: Temperature + Pressure (no humidity) ────────────────────
 *  Layout mirrors DHT22 but replaces humidity with pressure:
 *  [thermometer] [TEMP °C] ... [PRESS hPa] [barometer]                    */

inline void BMP280_renderPanel(GFXcanvas16* cv, float t, float p, bool isValid,
                                int16_t cardW, bool leftAnchor, bool isRedPhase,
                                uint16_t panelBg, const GFXfont& font24,
                                const GFXfont& font12, const GFXfont& font9,
                                uint16_t txtSub, uint16_t tempOk,
                                uint16_t tempHot, uint16_t pressure,
                                uint16_t textOff) {
    uint16_t tempCol = isRedPhase ? RGB565(255,255,255) : tempOk;
    uint16_t unitCol = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t icTherm = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t mercCol = isRedPhase ? RGB565(255,255,255) : tempHot;
    uint16_t presCol = isRedPhase ? RGB565(255,255,255) : pressure;
    uint16_t baroCol = isRedPhase ? RGB565(220,200,200) : pressure;
    uint16_t unitPCol = isRedPhase ? RGB565(220,200,200) : txtSub;

    if (!isValid || isnan(t)) {
        cv->setFont(&font12); cv->setTextSize(1);
        cv->setTextColor(isRedPhase ? RGB565(255,255,255) : tempHot);
        cv->setCursor(25, 28); cv->print("--.-");
        return;
    }

    /* ── Temperature (left side) ── */
    int intPart = (int)t;
    int decPart = abs((int)(t * 10.0f) % 10);
    char iP[10]; snprintf(iP, sizeof(iP), "%d", intPart);
    char dP[5];  snprintf(dP, sizeof(dP), ".%d", decPart);
    int16_t xx, yy; uint16_t iw, ih, decW;
    cv->setFont(&font24); cv->setTextSize(1);
    cv->getTextBounds(iP, 0, 0, &xx, &yy, &iw, &ih);
    cv->getTextBounds(dP, 0, 0, &xx, &yy, &decW, &ih);

    int textAnchor, iconX;
    if (leftAnchor) {
        textAnchor = 92; iconX = 14;
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
    cv->setCursor(numCursorX, 35); cv->print(iP);
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

    /* ── Pressure (right side, mirrors humidity layout) ── */
    if (!isnan(p)) {
        const char* presUnit = "hPa";
        cv->setFont(&font12); cv->setTextSize(1);
        int16_t px, py; uint16_t puW, puH, pw, ph;
        cv->getTextBounds(presUnit, 0, 0, &px, &py, &puW, &puH);
        const int rightMargin = 15;
        int unitPX = cardW - rightMargin - (int)puW;
        int presAnchor = unitPX - 3;

        cv->setFont(&font24); cv->setTextSize(1);
        cv->setTextColor(presCol);
        char pb[7];
        snprintf(pb, sizeof(pb), "%d", (int)p);
        cv->getTextBounds(pb, 0, 0, &px, &py, &pw, &ph);
        cv->setCursor(presAnchor - (int)pw, 35);
        cv->print(pb);

        cv->setFont(&font12);
        cv->setTextColor(unitPCol);
        cv->setCursor(unitPX, 34);
        cv->print(presUnit);

        int baroRight = presAnchor - (int)pw - 6;
        int bx = baroRight - 15;
        drawBarometerLarge(cv, bx, 4, baroCol, panelBg, presCol);
    }
}

/* ── BME280 panel: Temperature + Humidity + Pressure ───────────────────────
 *  T large (center-left) + H right side (same as DHT22).
 *  Pressure shown as small badge when available (compact, non-intrusive).   */

inline void BME280_renderPanel(GFXcanvas16* cv, float t, float h, float p,
                                bool isValid, int16_t cardW,
                                bool leftAnchor, bool isRedPhase,
                                uint16_t panelBg, const GFXfont& font24,
                                const GFXfont& font12, const GFXfont& font9,
                                uint16_t txtSub, uint16_t tempOk,
                                uint16_t tempHot, uint16_t humidity,
                                uint16_t textOff) {

    uint16_t tempCol  = isRedPhase ? RGB565(255,255,255) : tempOk;
    uint16_t unitCol  = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t icTherm  = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t mercCol  = isRedPhase ? RGB565(255,255,255) : tempHot;
    uint16_t humCol   = isRedPhase ? RGB565(255,255,255) : humidity;
    uint16_t dropCol  = isRedPhase ? RGB565(220,200,200) : humidity;
    uint16_t dropShine= isRedPhase ? RGB565(255,255,255) : RGB565(200,230,255);
    uint16_t pctCol   = isRedPhase ? RGB565(220,200,200) : txtSub;

    if (!isValid || isnan(t)) {
        cv->setFont(&font12); cv->setTextSize(1);
        cv->setTextColor(isRedPhase ? RGB565(255,255,255) : tempHot);
        cv->setCursor(25, 28); cv->print("--.-");
        return;
    }

    /* ── Temperature ── */
    int intPart = (int)t;
    int decPart = abs((int)(t * 10.0f) % 10);
    char iP[10]; snprintf(iP, sizeof(iP), "%d", intPart);
    char dP[5];  snprintf(dP, sizeof(dP), ".%d", decPart);
    int16_t xx, yy; uint16_t iw, ih, decW;
    cv->setFont(&font24); cv->setTextSize(1);
    cv->getTextBounds(iP, 0, 0, &xx, &yy, &iw, &ih);
    cv->getTextBounds(dP, 0, 0, &xx, &yy, &decW, &ih);

    int textAnchor, iconX;
    if (leftAnchor) {
        textAnchor = 92; iconX = 14;
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
    cv->setCursor(numCursorX, 35); cv->print(iP);
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

    /* ── Humidity (right side) ── */
    if (!isnan(h)) {
        const char* humSuffix = "%";
        cv->setFont(&font12); cv->setTextSize(1);
        int16_t px, py; uint16_t pctW, pctH, hw, hh;
        cv->getTextBounds(humSuffix, 0, 0, &px, &py, &pctW, &pctH);
        const int rightMargin = 15;
        int pctX = cardW - rightMargin - (int)pctW;
        int humAnchor = pctX - 3;

        cv->setFont(&font24); cv->setTextSize(1);
        cv->setTextColor(humCol);
        char hb[6];
        snprintf(hb, sizeof(hb), "%d", (int)h);
        cv->getTextBounds(hb, 0, 0, &px, &py, &hw, &hh);
        cv->setCursor(humAnchor - (int)hw, 35);
        cv->print(hb);

        cv->setFont(&font12);
        cv->setTextColor(pctCol);
        cv->setCursor(pctX, 34);
        cv->print(humSuffix);

        int dropRight = humAnchor - (int)hw - 6;
        int dx = dropRight - 14;
        drawDropLarge(cv, dx, 4, dropCol, dropShine);

        /* ── Pressure badge (small, below humidity) ── */
        if (!isnan(p)) {
            cv->setFont(&font9); cv->setTextSize(1);
            cv->setTextColor(txtSub);
            char pBadge[16];
            snprintf(pBadge, sizeof(pBadge), "P:%d", (int)p);
            int16_t bpx, bpy; uint16_t bpw, bph;
            cv->getTextBounds(pBadge, 0, 0, &bpx, &bpy, &bpw, &bph);
            cv->setCursor(cardW - (int)bpw - 15, 28);
            cv->print(pBadge);
        }
    }
}


/* ── Min/Max panel rendering (T + H, 43px strip) ──────────────────────── */
inline void BME280_renderMinMax(GFXcanvas16* cv,
    float minT, float maxT, float minH, float maxH,
    bool isValid, int16_t cardW, bool isRedPhase, uint16_t panelBg,
    const GFXfont& font9,
    uint16_t txtSub, uint16_t tempOk, uint16_t tempHot,
    uint16_t humidity, uint16_t textOff,
    uint16_t accentHigh, uint16_t btnTextActive,
    const char* minLabel, const char* maxLabel,
    const char* humSuffix) {
    /* BME280 min/max mirrors DHT22 pattern: T rows + H values.
     * Pressure min/max omitted — atmospheric drift is negligible
     * at dashboard timescales. */

    uint16_t icCol   = isRedPhase ? RGB565(220,200,200) : txtSub;
    uint16_t mercCol = isRedPhase ? RGB565(255,255,255) : tempHot;
    uint16_t dropCol = isRedPhase ? RGB565(220,200,200) : humidity;
    uint16_t humCol  = isRedPhase ? RGB565(255,255,255) : humidity;
    uint16_t shine   = isRedPhase ? RGB565(255,255,255) : RGB565(200,230,255);

    int16_t x1, y1; uint16_t minLblW, maxLblW, hb, sufW;
    cv->setFont(&font9);
    cv->getTextBounds(minLabel, 0, 0, &x1, &y1, &minLblW, &hb);
    cv->getTextBounds(maxLabel, 0, 0, &x1, &y1, &maxLblW, &hb);
    cv->getTextBounds(humSuffix, 0, 0, &x1, &y1, &sufW, &hb);
    int biggestLbl = (minLblW > maxLblW) ? (int)minLblW : (int)maxLblW;

    const int LABEL_X = 18;
    const int THERM_X = LABEL_X + biggestLbl + 8;
    const int DOT_X  = THERM_X + 36;
    const int HUM_END = 230;
    const int BTN_W = 58;
    const int BTN_X = HUM_END + ((cardW - 1) - HUM_END - BTN_W) / 2;

    uint16_t numMaxW;
    cv->getTextBounds("100", 0, 0, &x1, &y1, &numMaxW, &hb);
    int DROP_FIX = HUM_END - (int)sufW - 3 - (int)numMaxW - 6;
    int sufX = HUM_END - (int)sufW;

    /* ── Min row ── */
    drawMinMaxTempRow(cv, minLabel, LABEL_X, THERM_X, DOT_X,
        0, minT, isRedPhase,
        txtSub, icCol, mercCol, tempOk, panelBg, font9);

    {
        char hnum[8];
        if (isnan(minH)) snprintf(hnum, sizeof(hnum), "--");
        else snprintf(hnum, sizeof(hnum), "%d", (int)minH);
        uint16_t hnW;
        cv->setFont(&font9);
        cv->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &hb);
        int numX = sufX - 3 - (int)hnW;
        cv->setTextColor(humCol);
        cv->setCursor(numX, 15);
        cv->print(hnum);
        cv->setTextColor(isRedPhase ? RGB565(255,255,255) : txtSub);
        cv->setCursor(sufX, 15);
        cv->print(humSuffix);
    }
    drawDropMini(cv, DROP_FIX, 6, dropCol, shine);

    /* ── Max row ── */
    drawMinMaxTempRow(cv, maxLabel, LABEL_X, THERM_X, DOT_X,
        22, maxT, isRedPhase,
        txtSub, icCol, mercCol, tempOk, panelBg, font9);

    {
        char hnum[8];
        if (isnan(maxH)) snprintf(hnum, sizeof(hnum), "--");
        else snprintf(hnum, sizeof(hnum), "%d", (int)maxH);
        uint16_t hnW;
        cv->setFont(&font9);
        cv->getTextBounds(hnum, 0, 0, &x1, &y1, &hnW, &hb);
        int numX = sufX - 3 - (int)hnW;
        cv->setTextColor(humCol);
        cv->setCursor(numX, 37);
        cv->print(hnum);
        cv->setTextColor(isRedPhase ? RGB565(255,255,255) : txtSub);
        cv->setCursor(sufX, 37);
        cv->print(humSuffix);
    }
    drawDropMini(cv, DROP_FIX, 28, dropCol, shine);

    drawMinMaxGraphBtn(cv, BTN_X, 2, BTN_W, 40, accentHigh, btnTextActive);
}
#endif /* SIMUT_DISPLAY_TFT */

#endif /* SIMUT_SENSOR_BME280 */
