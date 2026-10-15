#pragma once
// The boot screen (isBooting): the plan's boot_wifi. The brand, what the
// device is doing (the last lines of its boot log), the gesture that opens the
// configuration mode, and the button that gives up waiting for the network.
// The status bar is not here: there is no clock yet.
#include "Telas.h"

namespace ua {
namespace tela {

struct VmBoot {
  // The log's last lines, oldest first; the last one is the step in progress.
  // Already text (the key's translation and its suffix), in UTF-8.
  char passos[5][72];
  char versao[24];          // "2.11.1": the foot's first line, as the glue wants it said
  char mac[20];             // "28:cd:c1:15:4e:99"
  uint8_t nPassos;          // lines filled in `passos`, 0..5
  int8_t apProgressPct;     // the hold-for-AP gesture, 0..100; -1: not on (SystemState's value as is)
  bool showSkipButton;      // the button that gives up waiting for the network
};

// PULAR: the old handler answers inside x 81..239, y 191..239 before any hook
// (DisplayManager_Touch.cpp: y > 190 and 80 < x < 240), so the button lives there.
enum AlvoBoot : uint8_t { BOOT_NADA, BOOT_PULAR };

void desenharBoot( Pintor &p, const VmBoot &vm );
int alvosBoot( const VmBoot &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
