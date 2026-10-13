/**
 * @file NetBoot.h
 * @brief The reset of the network chip at boot, for the drivers that have one: a
 *        capability, SIMUT_NET_BOOT_RESET, and not an identity.
 *
 * AppManager::setup( ) asks `#if SIMUT_NET_BOOT_RESET` and calls net::bootReset( );
 * it does not know which chip that is. Why the cycle exists, at every boot and not
 * only after an update, is told in AppManager_Boot.cpp, where a reader of the boot
 * finds it. What is here is the chip's side of it:
 *
 *   CYW43    1. WL_REG_ON, GP23 of the Pico W and of the Pico 2 W, is held low for
 *            500 ms, then released as an input with no pulls, and the chip gets
 *            100 ms.
 *   others   0. Only the CYW43 driver has a CYW43 to reset. Any other driver
 *            leaves GP23 alone, and on a Pico W board running the Wire image GP23
 *            is that radio's power pin, which that image does not touch
 *            (2026-10-13).
 *
 * A driver that needs a reset of its own at boot (a module with a reset line)
 * defines the macro as 1 in its own arm below, with its own net::bootReset( ).
 *
 * Whoever tests the macro includes this header ITSELF: `#if X` with X undefined is
 * 0, silently, and the CYW43 would boot without its power cycle.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once

#include "NetCaps.h"   /* the identity is read below: included here, not left to another header */

#if SIMUT_NET_CYW43

#define SIMUT_NET_BOOT_RESET 1

#include <hardware/gpio.h>
#include <pico/time.h>

namespace net {

/** Power-cycles the CYW43 through WL_REG_ON. Blocks for 600 ms. */
inline void bootReset( ) {
	const uint wlRegOn = 23;
	gpio_init(wlRegOn);
	gpio_set_dir(wlRegOn, GPIO_OUT);
	gpio_put(wlRegOn, 0);
	busy_wait_ms(500);
	gpio_set_dir(wlRegOn, GPIO_IN);
	gpio_disable_pulls(wlRegOn);
	busy_wait_ms(100);
}

}  // namespace net

#else

#define SIMUT_NET_BOOT_RESET 0

#endif
