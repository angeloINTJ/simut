/**
 * @file TftWithOffset.h
 * @brief Thin subclass of Adafruit_ILI9341 that applies a runtime display offset.
 * @details Intercepts `setAddrWindow( )` to shift every draw operation by a user-
 * configurable (offsetX, offsetY) amount. Used to compensate for small
 * physical misalignment of the TFT viewing window vs. the pixel matrix
 * (±4 px horizontal / vertical). The offset is applied uniformly to
 * every primitive because all drawing — including canvas blit and
 * direct pixel writes — funnels through setAddrWindow in Adafruit_GFX.
 *
 * Depends on Adafruit_SPITFT declaring setAddrWindow( ) as `virtual`,
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
 * @brief Define o deslocamento físico aplicado a toda operação de desenho.
 *
 * O par (ox, oy) é somado às coordenadas X/Y passadas em setAddrWindow.
 * Valores positivos movem a imagem para direita/baixo. Valores são
 * saturados em [-4, +4] para limitar perda de pixels nas bordas.
 */
 void setDisplayOffset(int8_t ox, int8_t oy) {
 _offsetX = constrain(ox, -4, 4);
 _offsetY = constrain(oy, -4, 4);
 fillMarginsBlack( );
 }

 int8_t getOffsetX( ) const { return _offsetX; }
 int8_t getOffsetY( ) const { return _offsetY; }

 /**
 * @brief Temporariamente desliga o offset automático em setAddrWindow.
 *
 * Usado por rotas que já aplicam o offset explicitamente nas coordenadas
 * (ex.: DisplayManager::blitCanvas após resolver drawRGBBitmap que
 * possivelmente devirtualiza a chamada interna de setAddrWindow). Evita
 * que um eventual virtual-dispatch efetivo cause offset duplo.
 */
 void setOffsetBypass(bool bypass) { _bypass = bypass; }
 bool isOffsetBypass( ) const { return _bypass; }

 /**
 * @brief fillScreen que cobre também as margens físicas com preto.
 *
 * O fillScreen da classe base chama fillRect(0,0,width,height) que passa
 * por setAddrWindow e portanto herda o offset. Com offsetX>0 as primeiras
 * colunas físicas ficam sem desenho; com offsetX<0 as últimas colunas
 * ficam sem desenho (idem para offsetY). Este override preenche a área
 * lógica com a cor pedida e depois cobre as tiras de margem com preto.
 */
 void fillScreen(uint16_t color) {
 Adafruit_ILI9341::fillScreen(color);
 fillMarginsBlack( );
 }

 /// Preenche as tiras de margem física com preto (bypassa o offset).
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
 * @brief Override de setAddrWindow.
 *
 * Soma o offset e satura em 0 para evitar underflow no cast para uint16_t,
 * o que produziria um endereço enorme (quase 64k) e corromperia o frame.
 * O limite superior não é verificado — coordenadas além do display são
 * descartadas pela própria lógica interna do controlador ILI9341.
 *
 * Quando _bypass == true, chama a base sem modificar — o chamador já
 * aplicou o offset externamente.
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
