/**
 * @file    TouchWake.h
 * @brief   When Core 1 reads the touch controller without waiting for its interrupt.
 * @details XPT2046_Touchscreen 1.4 reads the controller only while its isrWake
 *          flag is set. The PENIRQ falling-edge interrupt sets it, and a read
 *          that finds no touch clears it. A touch therefore rested on two
 *          things that nothing checked after boot:
 *            - the interrupt, armed on Core 1 by attachInterrupt( ) at every
 *              launch of that core;
 *            - the controller's own pen interrupt. The library measures with
 *              PD0 = 1, which turns PENIRQ off, and its last command (0xD0,
 *              PD0 = 0) turns it back on. A read cut between the two leaves it
 *              off, and only another read turns it on again, which the library
 *              never makes without the interrupt.
 *          When either fails, the panel goes on drawing and the touch is dead
 *          until a reboot, which builds a new driver object with isrWake set.
 *          Field log of 2026-10-01: a device whose touch was found dead 16 h
 *          after a watchdog reboot, and a reboot cured it. Nothing recorded
 *          which of the two had failed.
 *
 *          Two wakes now cover both, and each counts what it rescued:
 *            - poll: PENIRQ low with the library asleep, a touch whose edge
 *              never arrived. Read now.
 *            - timer: TOUCH_REARM_MS without a read. Read anyway: the read's
 *              last command turns the pen interrupt back on, and with nobody
 *              at the glass it finds nothing.
 *          A forced read that finds a touch is a rescue: a touch the interrupt
 *          alone would have lost. Core 0 logs EVT_UI_TOUCH once per boot for
 *          each finding (touchReportDue( )), since Core 1 cannot log.
 *
 *          Free of Arduino and of the library, so test_log_policy drives it on
 *          the host.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

/** Longest the controller goes unread: a pen interrupt left off is back on
 *  within this long, turned on by the timer's read. A read with nobody touching
 *  is 9 bytes at the library's 2 MHz. Measured idle on the rig (2026-10-01,
 *  63 s): 2.7 timer reads a second, and 16.6 more that the library made by
 *  itself with its interrupt armed. A reading between its two pressure
 *  thresholds (75 and 400) keeps it awake, and the interrupt wakes it more
 *  often than that alone explains. */
constexpr uint32_t TOUCH_REARM_MS = 250;

enum : uint8_t { TOUCH_WAKE_NONE = 0, TOUCH_WAKE_POLL = 1, TOUCH_WAKE_TIMER = 2 };

/** Core 1 writes it, once per loop iteration; Core 0 only reads the counters. */
struct TouchWake {
	volatile uint32_t awake = 0;        /**< iterations the library was awake by itself (its interrupt, or a finger still down) */
	volatile uint32_t pollWakes = 0;    /**< reads forced because PENIRQ was low */
	volatile uint32_t timerWakes = 0;   /**< reads forced by TOUCH_REARM_MS */
	volatile uint32_t pollRescues = 0;  /**< poll reads that found a touch */
	volatile uint32_t timerRescues = 0; /**< timer reads that found a touch */
	volatile bool irqUnarmed = false;   /**< Core 1 found its touch interrupt off */
	uint32_t lastReadMs = 0;
	uint8_t forced = TOUCH_WAKE_NONE;   /**< what woke this iteration's read */

	/** Before the library's read.
	 *  @return true when the read must be forced (the caller sets isrWake). */
	bool before(bool libAwake, bool penLow, uint32_t nowMs) {
		forced = TOUCH_WAKE_NONE;
		if (libAwake) {
			awake++;
			lastReadMs = nowMs;
			return false;
		}
		if (penLow) {
			forced = TOUCH_WAKE_POLL;
			pollWakes++;
		} else if (nowMs - lastReadMs >= TOUCH_REARM_MS) {
			forced = TOUCH_WAKE_TIMER;
			timerWakes++;
		} else {
			return false;
		}
		lastReadMs = nowMs;
		return true;
	}

	/** After it: whether the read found a touch. */
	void after(bool touched) {
		if (!touched) return;
		if (forced == TOUCH_WAKE_POLL) pollRescues++;
		else if (forced == TOUCH_WAKE_TIMER) timerRescues++;
	}
};

/** Core 0's side: the next finding to log, each at most once per boot.
 *  @param reported  the caller's record of what was logged, bit (1 << ctx).
 *  @return the EVT_UI_TOUCH ctx to log, 0 when there is nothing new:
 *          1 Core 1's touch interrupt is off; 2 a touch came only through the
 *          poll; 3 a touch came only through the timer. */
inline uint8_t touchReportDue(uint8_t& reported, const TouchWake& w) {
	const bool found[4] = { false, w.irqUnarmed, w.pollRescues != 0u, w.timerRescues != 0u };
	for (uint8_t ctx = 1; ctx <= 3; ctx++) {
		const uint8_t bit = (uint8_t)(1u << ctx);
		if (found[ctx] && !(reported & bit)) {
			reported |= bit;
			return ctx;
		}
	}
	return 0;
}
