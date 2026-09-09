/* Native stub. A host test has no watchdog to feed; feeding it is a no-op so
 * the code under test can call it exactly as it does on the device. */
#pragma once
#include <stdint.h>
static inline void watchdog_update(void) {}
static inline void watchdog_enable(uint32_t, bool) {}
static inline void watchdog_reboot(uint32_t, uint32_t, uint32_t) {}
struct watchdog_hw_t { uint32_t scratch[8]; };
extern watchdog_hw_t* watchdog_hw;
