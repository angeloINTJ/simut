/**
 * @file display/AlarmLimitsSnap.h
 * @brief The limits editor's Salvar as a copy: Core 1 posts the limits on screen into a box of one entry, Core 0 takes them out once, for the
 *        slot of the event.
 *
 * @details Salvar only queues EVT_SAVE_ALARMS (actLimitsSave( )); the editor stays live until Core 0 gets to the event. Until 2026-10-31 Core 0
 * (AppManager::panelSaveAlarmLimits( )) then read the limits from the editor's working copy, DisplayManager::_tempAlarmConfig, whose writers
 * (the arrow steps, repairAlarmLimits( ), showAlarmEdit( )) run on Core 1. That saved what nobody asked for in two ways: an arrow step landing
 * between the reads of a channel's MIN and MAX saved a crossed pair (MIN 10.0 and MAX 10.0, the state DisplayManager_Ticks.h says arms an alarm
 * that never clears), and Salvar on sensor A, Voltar and the editor on sensor B before Core 0 ran wrote B's limits into A. Found by reading the
 * code on 2026-10-31, not seen on a board: F3 of the v2.13.0 Known list.
 *
 * Now Core 1 copies at Salvar, where every writer of the working copy runs, so the copy is consistent by construction; Core 0 takes it out under
 * DisplayManager::_stateMutex. It is the pattern of DisplayManager::pullSnapshot( ), in the other direction. The functions here take no lock, so
 * the host test needs no display: the two DisplayManager members that call them hold _stateMutex around the call and nothing else.
 *
 * The post is a template on the record type, which is SensorRecord in the firmware. Naming SensorRecord here would pull in SystemDefs_Records.h
 * and with it Arduino.h, and the validators suite, where the cases live, cannot include that header (its externs clash with the copies the suite
 * keeps). The box needs only the two limit arrays of the record, and the static_assert says so.
 *
 * The limit that stays: the box holds one Salvar. A second one, on another sensor, that comes before Core 0 handled the first replaces it, and
 * the first save is dropped without a sound. Before this fix the same sequence wrote the second sensor's limits into the first.
 *
 * 68 B (1 + 3 padding + 2 x 8 x 4; sizeof, 2026-10-31): a member of DisplayManager, and a local on Core 0's stack in panelSaveAlarmLimits( ).
 * Never a local on Core 1, whose stack is tight: the post copies member to member.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include "sensors/SensorChannels.h"   /* MAX_SENSOR_CHANNELS */

/** One Salvar's limits: the sensor they belong to and a MIN and a MAX per channel. slot < 0 is an empty box, which is what a new one is. */
struct AlarmLimitsSnap {
	int8_t slot = -1;
	float  chMin[MAX_SENSOR_CHANNELS];
	float  chMax[MAX_SENSOR_CHANNELS];
};

/** Core 1, at Salvar: copies the editor's limits into the box and names the sensor. The record is packed and its floats are unaligned (chMin
 *  starts at byte 66 in SensorRecord): memcpy, never the address of a member.
 *
 *  Two guards keep Core 0 from taking half a copy. The first is the lock: DisplayManager::postSavedAlarmLimits( ) and takeSavedAlarmLimits( ) hold
 *  _stateMutex around this and around the take, so the take cannot run inside the copy. The second is the slot, -1 first and the sensor last, for a
 *  Core 1 killed in the middle of the copy: DisplayManager.cpp re-initialises _stateMutex wherever Core 1 is reset or may have died holding it
 *  (lines 260, 441, 490, 946 and 976, by grep on 2026-10-31), so that a corpse-held lock cannot block Core 0, and that leaves the lock free over a
 *  box that is half written, under the slot of an earlier Salvar that was never taken (a denied one). Stored first, the -1 makes that box read
 *  as empty.
 *
 *  The second guard needs the compiler to keep the order, and left alone it does not (2026-10-31): the -1 is dead to the optimiser, because the
 *  slot is stored again below with nothing reading it in between, and GCC removed it. The disassembly of DisplayManager::postSavedAlarmLimits in
 *  the release ELF was the lock, two memcpy, one strb of the slot, the unlock. So the -1 is an atomic store, which GCC does not remove, and a
 *  compiler barrier stands between it and the copies and between the copies and the slot (no instruction on ARM: the lock is what orders the two
 *  cores). No host test holds this, because a reordered post passes on x86; the proof is the disassembly of that function in pico_w_release and
 *  pico2_w_release: the lock, a strb of 0xFF, the copies, a strb of the slot, the unlock. */
template <typename Rec>
inline void limitsSnapPost(AlarmLimitsSnap& box, const Rec& edited, int slot) {
	static_assert(sizeof edited.chMin == sizeof box.chMin && sizeof edited.chMax == sizeof box.chMax, "the record's limit arrays are the box's");
	__atomic_store_n(&box.slot, (int8_t)-1, __ATOMIC_RELAXED);   /* not box.slot = -1: GCC drops the plain store as dead */
	__atomic_signal_fence(__ATOMIC_SEQ_CST);                     /* the copies stay after the -1 ... */
	memcpy(box.chMin, edited.chMin, sizeof box.chMin);
	memcpy(box.chMax, edited.chMax, sizeof box.chMax);
	__atomic_signal_fence(__ATOMIC_SEQ_CST);                     /* ... and before the slot */
	__atomic_store_n(&box.slot, (int8_t)slot, __ATOMIC_RELAXED);
}

/** Core 0, on EVT_SAVE_ALARMS: false when the box is empty or holds another sensor's limits (nothing is touched, and a Salvar for that other
 *  sensor can still take its own); on success copies the box to `out` and empties it, so one Salvar is taken once. */
inline bool limitsSnapTake(AlarmLimitsSnap& box, int slot, AlarmLimitsSnap& out) {
	if (box.slot < 0 || box.slot != slot) return false;
	out = box;
	box.slot = -1;
	return true;
}
