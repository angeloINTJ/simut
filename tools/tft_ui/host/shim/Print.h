// Host stand-in for Arduino's Print (host_ui.py, host_telas.py).
#pragma once
#include "Arduino.h"

class Print {
public:
  virtual ~Print( ) {}
  virtual size_t write( uint8_t ) = 0;
  virtual size_t write( const uint8_t *buffer, size_t size ) {
    size_t n = 0;
    while ( size-- ) n += write( *buffer++ );
    return n;
  }
  size_t print( const char *s ) { return write( (const uint8_t *)s, strlen( s ) ); }
};
