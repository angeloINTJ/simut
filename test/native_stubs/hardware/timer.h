/* Native stub. On the RP2350 the SDK defines timer_hw in this header, not in
 * hardware/structs/timer.h, so FlashIrqProbe.h includes both. On the host the
 * stub in structs/timer.h is the whole of it. */
#pragma once
#include <hardware/structs/timer.h>
