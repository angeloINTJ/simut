/**
 * @file display/CountInText.h
 * @brief A translated line with a number in it.
 *
 * The panel's lines come from a language pack, which an administrator
 * uploads. A line that needs a number carries "{n}" where the number goes,
 * and this puts it there. Not snprintf on the line: a pack line is never a
 * format string, so a "%s" in it prints as "%s" instead of reading whatever
 * the stack holds. A line without the marker comes back as it is, which is
 * what a pack older than the marker shows. Header-only, so a host test
 * reaches it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** `text` with its first "{n}" replaced by `n`, into `out`, cut to `cap`. */
inline void countInText(const char* text, unsigned n, char* out, size_t cap) {
 if (!out || cap == 0) return;
 out[0] = '\0';
 if (!text) return;
 const char* at = strstr(text, "{n}");
 if (!at) {
  snprintf(out, cap, "%s", text);
  return;
 }
 snprintf(out, cap, "%.*s%u%s", (int)(at - text), text, n, at + 3);
}
