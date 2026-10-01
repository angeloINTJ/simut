/**
 * @file    AlarmEdgeLatch.h
 * @brief   Which alarm-line edges were announced, and which wait for room (#161).
 * @details The alarm line reports EDGES: a limit crossed, a sensor failed.
 *          AppManager::handleAlarmTelemetryEdges( ) keeps a latch per
 *          condition so that a limit violated for an hour is one record, not
 *          one every pass.
 *
 *          Until 2026-10-01 the latch was set whether or not the queue took
 *          the record. With the queue full (the server away long enough to
 *          fill it, a_qmax records), AlarmQueue::push( ) refuses the new
 *          record, drop-newest, and the condition was marked as announced
 *          anyway. When the server came back and the queue drained, the
 *          condition, still active, was never sent. Only a reboot (the
 *          latches start at zero) or the condition clearing and tripping again
 *          brought it back. Issue #161; the maintainer chose option (a).
 *
 *          A latch is now two words:
 *            - announced: the queue took this edge's record;
 *            - refused:   the queue refused it, and it is not offered again
 *                         while the queue is still full. One refusal is one
 *                         `dropped` and one log 553, not one per pass.
 *          When the queue has room, every refused edge is due again. If its
 *          condition is still active it goes out then, stamped with that time
 *          and value. If the condition ended first, nothing is announced, and
 *          the loss stays counted where it always was.
 *
 *          What does NOT change: the queue keeps its oldest records
 *          (drop-newest, R3 of docs/analysis/ANALISE_TELEMETRIA_ALARMES.md),
 *          and one-off records (actions from the panel or the web,
 *          maintenance transitions) are not retried. They are not latches.
 *
 *          Header-only and free of Arduino, so test_alarm_queue drives it
 *          against the real AlarmQueue on the host.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>

/** One latch word: a bit per slot (sensor failure) or per channel (limits). */
template <typename Bits>
struct AlarmEdgeLatch {
	Bits announced = 0; /**< the queue took this edge's record */
	Bits refused = 0;   /**< refused by a full queue; waits for room */

	/** A condition that is active: is its record to be offered now? */
	bool due(Bits bit) const { return !(announced & bit) && !(refused & bit); }

	/** What the queue answered. Only a record it took latches the edge. */
	void offered(Bits bit, bool accepted) {
		if (accepted) {
			announced |= bit;
			refused &= (Bits)~bit;
		} else {
			refused |= bit;
		}
	}

	/** The condition ended, or the slot left the line: nothing to announce. */
	void clear(Bits bit) {
		announced &= (Bits)~bit;
		refused &= (Bits)~bit;
	}

	void reset( ) { announced = 0; refused = 0; }
};

/** Once per pass, before any offer: with room in the queue, every edge it
 *  refused is due again. */
template <typename Bits>
inline void alarmEdgeRoom(AlarmEdgeLatch<Bits>& latch, bool queueFull) {
	if (!queueFull) latch.refused = 0;
}

/** One condition, one pass. An ended condition is forgotten; an active one
 *  has its record offered when due, and latches only if the queue took it.
 *  `push` builds and queues the record and returns its seq, 0 when refused;
 *  it runs only when the record is offered.
 *  @return the seq the queue gave, 0 when nothing was queued. */
template <typename Bits, typename Push>
inline uint16_t alarmEdgeOffer(AlarmEdgeLatch<Bits>& latch, Bits bit, bool active, Push push) {
	if (!active) {
		latch.clear(bit);
		return 0;
	}
	if (!latch.due(bit)) return 0;
	const uint16_t seq = push( );
	latch.offered(bit, seq != 0);
	return seq;
}
