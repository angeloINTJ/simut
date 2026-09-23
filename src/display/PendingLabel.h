/**
 * @file display/PendingLabel.h
 * @brief The pending-telemetry count as the displays print it.
 *
 * Below a thousand the number itself; from there on whole thousands with a
 * "k" — 1500 reads "1k". That is how the TFT top bar has always shown it, and
 * it is what lets the 16x2 of the alpha build show it at all: no uint16_t
 * comes out longer than three characters ("65k"), and three columns are what
 * the alphanumeric screen has free on its second line. One helper for both
 * renderers, so the two cannot drift, and header-only so a host test reaches
 * it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Longest label, terminator included: "999" or "65k". */
#define PENDING_LABEL_MAX 4

inline void pendingLabel(uint16_t n, char* out, size_t cap) {
 if (!out || cap == 0) return;
 if (n >= 1000) snprintf(out, cap, "%uk", (unsigned)(n / 1000));
 else snprintf(out, cap, "%u", (unsigned)n);
}
