/**
 * @file BME280Driver.h
 * @brief BME280 I2C temperature/humidity/pressure sensor driver.
 * @details Communicates via I2C0 (Wire) using forced-mode measurements.
 * Compensation formulas follow Bosch BME280 datasheet rev 1.6 §4.2.3.
 * Compiled only when SIMUT_SENSOR_BME280=1.
 *
 * Oversampling ×1 on all channels keeps measurement time ~9ms and
 * minimizes flash usage. The driver is self-contained — no external
 * library dependency (avoids Adafruit_BME280 at ~15KB flash).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#if SIMUT_SENSOR_BME280

#include <Arduino.h>
#include <Wire.h>
#if SIMUT_DISPLAY_TFT
#include "SensorDrawing.h"
#endif

/* ── I2C address ─────────────────────────────────────────────────────── */
#define BME280_I2C_ADDR_PRIMARY   0x76  /* SDO → GND */
#define BME280_I2C_ADDR_SECONDARY 0x77  /* SDO → VDD */
#define BME280_CHIP_ID            0x60  /* Register 0xD0 */

/* ── Register map ─────────────────────────────────────────────────────── */
#define BME280_REG_CHIP_ID        0xD0
#define BME280_REG_RESET          0xE0
#define BME280_REG_CTRL_HUM       0xF2
#define BME280_REG_STATUS         0xF3
#define BME280_REG_CTRL_MEAS      0xF4
#define BME280_REG_CONFIG         0xF5
#define BME280_REG_PRESS_MSB      0xF7
#define BME280_REG_CALIB_T1_LSB   0x88
#define BME280_REG_CALIB_H2_LSB   0xE1
#define BME280_RESET_CMD          0xB6

/* ── Oversampling (osrs_t:H:P = 1:1:1, mode = forced) ───────────────── */
#define BME280_OSRS_T_X1          0x20  /* 001b << 5 */
#define BME280_OSRS_H_X1          0x01  /* 001b << 0 */
#define BME280_OSRS_P_X1          0x04  /* 001b << 2 */
#define BME280_MODE_FORCED        0x01  /* 01b  << 0 */

#define BME280_FORCED_T_H_P  (BME280_OSRS_T_X1 | BME280_OSRS_P_X1 | BME280_MODE_FORCED)
#define BME280_MEAS_TIME_MS  15  /* ~9.3ms actual + margin */

struct BME280Driver {
    enum State {
        BME_IDLE,
        BME_WAITING
    };
    State    state = BME_IDLE;
    int      currentSensorIdx = -1;
    uint32_t timer = 0;

    /* ── I2C bus — set via setBus() before begin() ── */
    TwoWire* _wire = &Wire;

    /* ── Compensation parameters (read once at begin) ── */
    uint16_t dig_T1;  int16_t dig_T2, dig_T3;
    uint8_t  dig_H1;  int16_t dig_H2;  uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;  int8_t dig_H6;
    uint16_t dig_P1;  int16_t dig_P2, dig_P3;
    int16_t  dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    bool     _compLoaded = false;

    BME280Driver( ) { }

    /** Set the I2C bus to use (Wire=I2C0, Wire1=I2C1).
     *  Must be called BEFORE begin(). */
    void setBus(TwoWire& bus) { _wire = &bus; }

    /* ── I2C helpers ──────────────────────────────────────────────────── */
    bool writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
        _wire->beginTransmission(addr);
        _wire->write(reg);
        _wire->write(val);
        return _wire->endTransmission() == 0;
    }

    bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
        _wire->beginTransmission(addr);
        _wire->write(reg);
        if (_wire->endTransmission() != 0) return false;
        _wire->requestFrom(addr, len);
        for (uint8_t i = 0; i < len; i++) {
            if (!_wire->available()) return false;
            buf[i] = _wire->read();
        }
        return true;
    }

    /* ── Initialization ───────────────────────────────────────────────── */
    void begin( ) {
        /* Soft-reset the sensor */
        writeReg(BME280_I2C_ADDR_PRIMARY, BME280_REG_RESET, BME280_RESET_CMD);
        delay(10);
        /* Try primary address; fall back to secondary */
        uint8_t chipId = 0;
        if (!readRegs(BME280_I2C_ADDR_PRIMARY, BME280_REG_CHIP_ID, &chipId, 1)
            || chipId != BME280_CHIP_ID) {
            if (!readRegs(BME280_I2C_ADDR_SECONDARY, BME280_REG_CHIP_ID, &chipId, 1)
                || chipId != BME280_CHIP_ID) {
                _compLoaded = false;
                return;
            }
        }
        readCompensationParams( );
    }

    bool readCompensationParams( ) {
        uint8_t calib[26]; /* T1..T3(6) + P1..P9(18) + reserved(2) = 26 bytes from 0x88 */
        if (!readRegs(BME280_I2C_ADDR_PRIMARY, BME280_REG_CALIB_T1_LSB, calib, 24)) {
            /* Try secondary */
            if (!readRegs(BME280_I2C_ADDR_SECONDARY, BME280_REG_CALIB_T1_LSB, calib, 24)) {
                _compLoaded = false;
                return false;
            }
        }

        dig_T1 = (uint16_t)(calib[1]  << 8 | calib[0]);
        dig_T2 = (int16_t) (calib[3]  << 8 | calib[2]);
        dig_T3 = (int16_t) (calib[5]  << 8 | calib[4]);

        dig_P1 = (uint16_t)(calib[7]  << 8 | calib[6]);
        dig_P2 = (int16_t) (calib[9]  << 8 | calib[8]);
        dig_P3 = (int16_t) (calib[11] << 8 | calib[10]);
        dig_P4 = (int16_t) (calib[13] << 8 | calib[12]);
        dig_P5 = (int16_t) (calib[15] << 8 | calib[14]);
        dig_P6 = (int16_t) (calib[17] << 8 | calib[16]);
        dig_P7 = (int16_t) (calib[19] << 8 | calib[18]);
        dig_P8 = (int16_t) (calib[21] << 8 | calib[20]);
        dig_P9 = (int16_t) (calib[23] << 8 | calib[22]);

        /* Humidity calibration — 7 bytes from 0xE1 + 0xE7 */
        uint8_t hcal[8];
        bool hok = readRegs(BME280_I2C_ADDR_PRIMARY, BME280_REG_CALIB_H2_LSB, hcal, 8);
        if (!hok) {
            hok = readRegs(BME280_I2C_ADDR_SECONDARY, BME280_REG_CALIB_H2_LSB, hcal, 8);
        }

        dig_H1 = calib[25]; /* 0xA1 — part of the T/P calibration block */
        if (hok) {
            dig_H2 = (int16_t) (hcal[1]  << 8 | hcal[0]);
            dig_H3 = hcal[2];
            dig_H4 = (int16_t)((hcal[3]  << 4) | (hcal[4] & 0x0F));
            dig_H5 = (int16_t)((hcal[5]  << 4) | (hcal[4] >> 4));
            dig_H6 = (int8_t)  hcal[6];
        } else {
            /* Fallback: zero humidity compensation = dry air readings.
             * Sensor still works for temperature + pressure. */
            dig_H2 = 0; dig_H3 = 0; dig_H4 = 0; dig_H5 = 0; dig_H6 = 0;
        }

        /* Humidity oversampling ×1 */
        writeReg(BME280_I2C_ADDR_PRIMARY, BME280_REG_CTRL_HUM, BME280_OSRS_H_X1);

        _compLoaded = true;
        return true;
    }

    /* ── Trigger measurement (forced mode) ────────────────────────────── */
    void requestReading( ) {
        if (!_compLoaded) return;
        /* Set osrs_t=1, osrs_p=1, mode=forced */
        writeReg(BME280_I2C_ADDR_PRIMARY, BME280_REG_CTRL_MEAS, BME280_FORCED_T_H_P);
    }

    /* ── Read raw + compensate ────────────────────────────────────────── */
    bool getResults(float &t, float &h, float &p) {
        if (!_compLoaded) return false;

        uint8_t data[8];
        bool ok = readRegs(BME280_I2C_ADDR_PRIMARY, BME280_REG_PRESS_MSB, data, 8);
        if (!ok) {
            ok = readRegs(BME280_I2C_ADDR_SECONDARY, BME280_REG_PRESS_MSB, data, 8);
        }
        if (!ok) { t = h = p = NAN; return false; }

        int32_t adc_P = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | (data[2] >> 4);
        int32_t adc_T = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | (data[5] >> 4);
        int32_t adc_H = ((int32_t)data[6] << 8)  |  (int32_t)data[7];

        /* Temperature compensation → also yields t_fine */
        int32_t t_fine = compensate_T(adc_T);
        t = (float)t_fine / 5120.0f;  /* °C */

        /* Humidity compensation */
        uint32_t hRaw = compensate_H(adc_H, t_fine);
        h = (float)hRaw / 1024.0f;    /* %RH */

        /* Pressure compensation */
        int32_t pRaw = compensate_P(adc_P, t_fine);
        p = (float)pRaw / 25600.0f;   /* Pa → hPa (÷100, then /256 = ÷25600) */

        /* Sanity checks */
        if (t < -40.0f || t > 85.0f)  t = NAN;
        if (h < 0.0f   || h > 100.0f) h = NAN;
        if (p < 300.0f  || p > 1100.0f) p = NAN;

        return true;
    }

    /* ===================================================================
     * Compensation formulas — integer math per BME280 datasheet §4.2.3
     * =================================================================== */

    /** Returns t_fine (0.01°C resolution). */
    int32_t compensate_T(int32_t adc_T) {
        int32_t var1, var2;
        var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1)))
                * ((int32_t)dig_T2)) >> 11;
        var2 = (((((adc_T >> 4) - ((int32_t)dig_T1))
                  * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12)
                * ((int32_t)dig_T3)) >> 14;
        return var1 + var2;
    }

    /** Returns humidity in 1024 * %RH. */
    uint32_t compensate_H(int32_t adc_H, int32_t t_fine) {
        int32_t v_x1_u32r = t_fine - 76800;
        v_x1_u32r = (((((adc_H << 14) - (((int32_t)dig_H4) << 20)
                        - (((int32_t)dig_H5) * v_x1_u32r)) + 16384) >> 15)
                     * (((((((v_x1_u32r * ((int32_t)dig_H6)) >> 10)
                            * (((v_x1_u32r * ((int32_t)dig_H3)) >> 11) + 32768)) >> 10)
                          + 2097152) * ((int32_t)dig_H2) + 8192) >> 14));
        v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7)
                                  * ((int32_t)dig_H1)) >> 4);
        v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
        v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
        return (uint32_t)(v_x1_u32r >> 12);
    }

    /** Returns pressure in 256 * Pa. */
    int32_t compensate_P(int32_t adc_P, int32_t t_fine) {
        int64_t var1, var2, p;
        var1 = ((int64_t)t_fine) - 128000;
        var2 = var1 * var1 * (int64_t)dig_P6;
        var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
        var2 = var2 + (((int64_t)dig_P4) << 35);
        var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8)
             + ((var1 * (int64_t)dig_P2) << 12);
        var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
        if (var1 == 0) return 0;
        p = 1048576 - adc_P;
        p = (((p << 31) - var2) * 3125) / var1;
        var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
        var2 = (((int64_t)dig_P8) * p) >> 19;
        p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
        return (int32_t)p;
    }

    void reset( ) {
        state = BME_IDLE;
        currentSensorIdx = -1;
    }

    /** Read the measurement status bit (0 = conversion running). */
    bool isMeasuring( ) {
        uint8_t status;
        if (!readRegs(BME280_I2C_ADDR_PRIMARY, BME280_REG_STATUS, &status, 1)) {
            if (!readRegs(BME280_I2C_ADDR_SECONDARY, BME280_REG_STATUS, &status, 1))
                return false;
        }
        return (status & 0x08) != 0; /* bit 3 = measuring */
    }
};


/* ── Panel rendering (TFT dashboard, theme-aware) ───────────────────── */
#if SIMUT_DISPLAY_TFT
inline void BME280_renderPanel(GFXcanvas16* cv, float t, float h, float p,
                                bool isValid, int16_t cardW,
                                bool leftAnchor, bool isRedPhase,
                                uint16_t panelBg, const GFXfont& font24,
                                const GFXfont& font12, const GFXfont& font9,
                                uint16_t txtSub, uint16_t tempOk,
                                uint16_t tempHot, uint16_t humidity,
                                uint16_t textOff) {
    (void)p; /* Reserved — pressure badge can be added in a future redesign */
    /* BME280 panel: T large (center-left) + H right side.
     * Layout mirrors DHT22 panel — proven legible at 320×240.
     * Pressure available via stats screen and API. */

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
        cv->setCursor(25, 28);
        cv->print("--.-");
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
