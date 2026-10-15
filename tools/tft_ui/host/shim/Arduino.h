// Host stand-in for the few Arduino names Adafruit_GFX.cpp uses (host_ui.py, host_telas.py).
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

typedef bool boolean;
#define PROGMEM
#define radians( deg ) ( ( deg ) * 0.017453292519943295 )

class __FlashStringHelper;

class String {
public:
  String( const char *c = "" ) : s_( c ) {}
  const char *c_str( ) const { return s_.c_str( ); }
  unsigned int length( ) const { return (unsigned int)s_.size( ); }

private:
  std::string s_;
};
