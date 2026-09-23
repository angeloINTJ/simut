/**
 * @file TelemetryCursor.h
 * @brief What the telemetry cursor may advance to after a send.
 *
 * Header-only and dependency-free so the rule can be tested on the host
 * (test_validators): it decides which stored records are never offered again,
 * and a mistake here does not fail anything — it loses measurements in silence.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Highest epoch safe to record as delivered.
 *
 * @p recs / @p n is what buildPayload left behind, so it is exactly what the
 * transport carries. Three things this must not do:
 *
 *  · Read the last element as the newest one. That was the rule, and it holds
 *    only while records arrive in time order — which is an assumption about
 *    the writer's clock, not a property of the data. A boot with a mis-seeded
 *    provisional clock (2026-08-14) writes blocks stamped ahead of the ones
 *    that follow them, and then the tail of the vector is not the high-water
 *    mark at all.
 *
 *  · Let a record stamped in the future set the frontier. The cursor is a
 *    scalar in time and `epoch > lastCursor` skips everything at or below it,
 *    forever — so one block stamped hours ahead buried every correctly stamped
 *    record behind it, permanently, and without a log line. That is worse than
 *    the graph bug of the same night, because a chart redraws and a telemetry
 *    record that was never sent is gone.
 *
 *  · Reach past the batch. @p fromCursor is the cursor the batch was READ
 *    from, and it only wins when nothing newer was delivered. It used to be
 *    the newest epoch the collection had gathered, which is the same number
 *    until buildPayload drops records off the end for want of heap — and then
 *    the cursor jumped over every dropped record and they were never offered
 *    again. Measured on the bench 2026-09-23 (Air v2.7.1, M0 drain of 13,678
 *    records into a collector that keeps every request): 11 of 74 batch
 *    boundaries lost exactly one record, each the one the payload had just
 *    shed, while every body that arrived was complete and valid JSON.
 *
 * Clamping to @p nowEpoch stops a future stamp from moving the frontier past
 * real time. The record still goes out; it just does not get to define what
 * counts as sent. Anything ahead of the clamp is offered again on a later
 * round, which is the right way round: ingest is keyed by timestamp, so a
 * duplicate costs a write and a gap costs the measurement.
 *
 * What this does NOT fix, because a scalar cursor in time cannot: a record
 * stamped ahead of its neighbours but still behind `now` — a mis-stamped block
 * read back hours later — advances the frontier over records that are older
 * and not yet sent, and those stay unsent. Closing that needs the cursor to
 * become a scan position rather than an instant, which is a format change, or
 * needs the stamps to be right in the first place, which is what the seed
 * ceiling in h5SeedCeiling is for. The clamp covers the live case, where the
 * bad stamp is in the future at the moment of sending, and that is the shape
 * the 2026-08-14 device was in while it was writing.
 *
 * @param epochMin  plausibility floor for @p nowEpoch (HIST_EPOCH_MIN): below
 *                  it the device has no real clock and the clamp is skipped.
 */
template <typename Rec>
inline uint32_t telDeliveredCursor(const Rec* recs, size_t n, uint32_t fromCursor,
                                   uint32_t nowEpoch, uint32_t epochMin) {
 uint32_t hi = 0;
 for (size_t i = 0; i < n; i++) {
  if (recs[i].epoch > hi) hi = recs[i].epoch;
 }
 if (hi == 0) return fromCursor;
 if (nowEpoch >= epochMin && hi > nowEpoch) hi = nowEpoch;
 return (hi < fromCursor) ? fromCursor : hi;
}
