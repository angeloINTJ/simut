#pragma once
// The part of a strip that lies on the glass when the display alignment (Ajustes, Alinhar; TftWithOffset.h, +-4 px) moves the image, and the strip's
// rows packed so that one DMA burst can send it (DisplayManager::uaEmpurrar( ), src/ui/angulo/DisplayManager_UiA_Empurrar.cpp). In this header, free of the
// libraries, so that the host harness runs the very code the firmware does: tools/tft_ui/host/host_perf_main.cpp --recorte.
//
// Before, a strip with a single pixel off the glass took the library's drawRGBBitmap( ), about 2 us a pixel: 154 ms a frame with a column offset (every
// strip is off the glass then), or 12.8 ms for the one strip a row offset pushes off, against 21 ms of DMA for the frame.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ua {

// The part of a w x h block, its corner at (x, y) of the W x H glass, that lies on the glass: its corner there (x, y), its size (w, h), and the column and
// row of the block where it starts (fx, fy). vazio: nothing of the block is on the glass.
struct Recorte {
  int16_t x, y, w, h, fx, fy;
  bool vazio;
};

inline Recorte recortar( int32_t x, int32_t y, int32_t w, int32_t h, int32_t W, int32_t H ) {
  const int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  const int32_t x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
  if ( x0 >= x1 || y0 >= y1 ) return Recorte{ 0, 0, 0, 0, 0, 0, true };
  return Recorte{ (int16_t)x0, (int16_t)y0, (int16_t)( x1 - x0 ), (int16_t)( y1 - y0 ), (int16_t)( x0 - x ), (int16_t)( y0 - y ), false };
}

// The part as a contiguous block of r.w x r.h pixels, which is what a DMA burst sends (it has no stride): the rows are moved in place to the start of
// the buffer, which holds `larg` pixels a row. A destination row starts at or before its source row, and the rows still to be read lie after it, so a
// forward memmove never overwrites one it has yet to read. The strip is consumed: it has been hashed already and the next one is composed afresh.
// A part of whole rows is contiguous where it is.
inline const uint16_t *compactar( uint16_t *buf, int larg, const Recorte &r ) {
  const uint16_t *src = buf + (int32_t)r.fy * larg + r.fx;
  if ( r.w == larg ) return src;
  uint16_t *dst = buf;
  for ( int j = 0; j < r.h; j++, dst += r.w, src += larg ) memmove( dst, src, (size_t)r.w * sizeof( uint16_t ) );
  return buf;
}

}  // namespace ua
