/* Native stub — the raw microsecond counter FlashIrqProbe reads. */
#pragma once
#include <stdint.h>
struct timer_hw_t { uint32_t timerawl; uint32_t timerawh; };
extern timer_hw_t* timer_hw;
