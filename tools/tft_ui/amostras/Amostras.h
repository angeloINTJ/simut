#pragma once
// The samples of one group of the functional build's screens, for the host
// harness (tools/tft_ui/host_telas.py): each sample fills a view model the way
// the device would in one state, draws it, and lists its touch targets. One
// file per group (amostras/<grupo>.cpp) defines kAmostras; the harness builds
// one group at a time.
#include "Telas.h"

namespace amostra {

struct Amostra {
  const char *nome;                         // "mensagem_ok": file names, so [a-z0-9_]
  void ( *desenhar )( ua::Pintor &p );      // fills the sample's view model and draws it
  int ( *alvos )( ua::Alvo *out );          // the same view model's targets (nullptr: none)
};

extern const Amostra kAmostras[];
extern const int kAmostrasN;

}  // namespace amostra
