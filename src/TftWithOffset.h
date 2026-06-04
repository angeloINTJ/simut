/**
 * @file TftWithOffset.h
 * @brief Thin subclass of Adafruit_ILI9341 that applies a runtime display offset.
 * @details Intercepts `setAddrWindow()` to shift every draw operation by a user-
 * configurable (offsetX, offsetY) amount. Used to compensate for small
 * physical misalignment of the TFT viewing window vs. the pixel matrix
 * (±4 px horizontal / vertical). The offset is applied uniformly to
 * every primitive because all drawing — including canvas blit and
 * direct pixel writes — funnels through setAddrWindow in Adafruit_GFX.
 *
 * Depends on Adafruit_SPITFT declaring setAddrWindow() as `virtual`,
 * which has been the case in the official library since 2019.
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Adafruit_ILI9341.h>

class TftWithOffset : public Adafruit_ILI9341 {
public:
 TftWithOffset(int8_t cs, int8_t dc, int8_t rst = -1)
 : Adafruit_ILI9341(cs, dc, rst) {}

 /**
 * @brief Sets the physical offset applied to every draw operation.
 *
 * The pair (ox, oy) is added to X/Y coordinates passed to setAddrWindow.
 * Positive values move the image right/down. Values are
 * clamped to [-4, +4] to limit pixel loss at edges.
 */
 void setDisplayOffset(int8_t ox, int8_t oy) {
 _offsetX = constrain(ox, -4, 4);
 _offsetY = constrain(oy, -4, 4);
 fillMarginsBlack( );
 }

 int8_t getOffsetX( ) const { return _offsetX; }
 int8_t getOffsetY( ) const { return _offsetY; }

 /**
 * @brief Temporarily disables the automatic offset in setAddrWindow.
 *
 * Used by routes that already apply the offset explicitly in coordinates
 * (e.g. DisplayManager::blitCanvas after resolving drawRGBBitmap which
 * possibly devirtualizes the internal setAddrWindow call). Avoids
 * an eventual effective virtual-dispatch causing double offset.
 */
 void setOffsetBypass(bool bypass) { _bypass = bypass; }
 bool isOffsetBypass( ) const { return _bypass; }

 /**
 * @brief fillScreen that also covers physical margins with black.
 *
 * The base class fillScreen calls fillRect(0,0,width,height) which goes
 * through setAddrWindow and thus inherits the offset. With offsetX>0 the first
 * physical columns are left undrawn; with offsetX<0 the last columns
 * are left undrawn (same for offsetY). This override fills the logical
 * area with the requested color and then covers the margin strips with black.
 */
 void fillScreen(uint16_t color) {
 Adafruit_ILI9341::fillScreen(color);
 fillMarginsBlack( );
 }

 /// Fills the physical margin strips with black (bypasses the offset).
 void fillMarginsBlack( ) {
 bool prev = _bypass;
 _bypass = true;
 if (_offsetX > 0) {
 Adafruit_ILI9341::fillRect(0, 0, _offsetX, 240, 0x0000);
 } else if (_offsetX < 0) {
 Adafruit_ILI9341::fillRect(320 + _offsetX, 0, -_offsetX, 240, 0x0000);
 }
 if (_offsetY > 0) {
 Adafruit_ILI9341::fillRect(0, 0, 320, _offsetY, 0x0000);
 } else if (_offsetY < 0) {
 Adafruit_ILI9341::fillRect(0, 240 + _offsetY, 320, -_offsetY, 0x0000);
 }
 _bypass = prev;
 }

 /**
 * @brief Override of setAddrWindow.
 *
 * Adds the offset and clamps to 0 to avoid underflow on cast to uint16_t,
 * which would produce a huge address (nearly 64k) and corrupt the frame.
 * Upper bound is not checked — coordinates beyond the display are
 * discarded by the ILI9341 controller's own internal logic.
 *
 * When _bypass == true, calls base without modifying — the caller already
 * applied the offset externally.
 */
 void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override {
 if (_bypass) {
 Adafruit_ILI9341::setAddrWindow(x, y, w, h);
 return;
 }
 int32_t px = (int32_t)x + _offsetX;
 int32_t py = (int32_t)y + _offsetY;
 if (px < 0) px = 0;
 if (py < 0) py = 0;
 Adafruit_ILI9341::setAddrWindow((uint16_t)px, (uint16_t)py, w, h);
 }

private:
 int8_t _offsetX = 0;
 int8_t _offsetY = 0;
 bool _bypass = false;
};
