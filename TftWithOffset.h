/**
 * @file    TftWithOffset.h
 * @brief   Thin subclass of Adafruit_ILI9341 that applies a runtime display offset.
 * @details Intercepts `setAddrWindow()` to shift every draw operation by a user-
 *          configurable (offsetX, offsetY) amount. Used to compensate for small
 *          physical misalignment of the TFT viewing window vs. the pixel matrix
 *          (±4 px horizontal / vertical). The offset is applied uniformly to
 *          every primitive because all drawing — including canvas blit and
 *          direct pixel writes — funnels through setAddrWindow in Adafruit_GFX.
 *
 *          Depends on Adafruit_SPITFT declaring setAddrWindow() as `virtual`,
 *          which has been the case in the official library since 2019.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @version 3.4.8
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once

#include <Adafruit_ILI9341.h>

class TftWithOffset : public Adafruit_ILI9341 {
public:
    TftWithOffset(int8_t cs, int8_t dc, int8_t rst = -1)
        : Adafruit_ILI9341(cs, dc, rst) {}

    /**
     * @brief Define o deslocamento físico aplicado a toda operação de desenho.
     *
     * O par (ox, oy) é somado às coordenadas X/Y passadas em setAddrWindow.
     * Valores positivos movem a imagem para direita/baixo. Valores são
     * saturados em [-4, +4] para limitar perda de pixels nas bordas.
     */
    void setDisplayOffset(int8_t ox, int8_t oy) {
        _offsetX = constrain(ox, -4, 4);
        _offsetY = constrain(oy, -4, 4);
    }

    int8_t getOffsetX() const { return _offsetX; }
    int8_t getOffsetY() const { return _offsetY; }

    /**
     * @brief Override wrap-safe de setAddrWindow.
     *
     * Soma o offset e satura em 0 para evitar underflow no cast para uint16_t,
     * o que produziria um endereço enorme (quase 64k) e corromperia o frame.
     * O limite superior não é verificado — coordenadas além do display são
     * descartadas pela própria lógica interna do controlador ILI9341.
     */
    void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
        int32_t px = (int32_t)x + _offsetX;
        int32_t py = (int32_t)y + _offsetY;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        Adafruit_ILI9341::setAddrWindow((uint16_t)px, (uint16_t)py, w, h);
    }

private:
    int8_t _offsetX = 0;
    int8_t _offsetY = 0;
};
