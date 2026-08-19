/**
 * @file PromMetrics.h
 * @brief Pure line formatters for the Prometheus text exposition format.
 * @details Emit `# TYPE` headers and `name{labels} value` sample lines into
 * caller buffers, snprintf-style. No Arduino deps so the native suite pins
 * the exact wire bytes. HELP lines are deliberately not emitted: they are
 * optional in the format and cost ~1.5 KB of flash across the family list —
 * the metric names carry their unit instead (`_celsius`, `_bytes`,
 * `_seconds`), which is the convention scrapers and dashboards read anyway.
 *
 * Which metrics exist is WebManager_Metrics.cpp's decision; this header only
 * knows how a line is spelled.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>

namespace PromMetrics {

/**
 * @brief Escape a label value: backslash, double quote and newline.
 *
 * The three characters the exposition format requires escaped — hwIds and
 * friendly names are user text and may carry any of them.
 */
inline void escapeLabel(const char* src, char* dst, size_t cap) {
	if (cap == 0) return;
	size_t o = 0;
	for (size_t i = 0; src && src[i]; i++) {
		char c = src[i];
		const char* rep = nullptr;
		if (c == '\\') rep = "\\\\";
		else if (c == '"') rep = "\\\"";
		else if (c == '\n') rep = "\\n";
		size_t need = rep ? 2 : 1;
		if (o + need >= cap) break;
		if (rep) { dst[o++] = rep[0]; dst[o++] = rep[1]; }
		else dst[o++] = c;
	}
	dst[o] = '\0';
}

/** @brief `# TYPE <name> <type>` header line. */
inline int typeLine(char* out, size_t cap, const char* name, const char* type) {
	return snprintf(out, cap, "# TYPE %s %s\n", name, type);
}

/** @brief Sample line with an unsigned integer value. `labels` may be "". */
inline int lineU32(char* out, size_t cap, const char* name, const char* labels,
                   uint32_t v) {
	if (labels && labels[0])
		return snprintf(out, cap, "%s{%s} %lu\n", name, labels, (unsigned long)v);
	return snprintf(out, cap, "%s %lu\n", name, (unsigned long)v);
}

/** @brief Sample line with a signed integer value. */
inline int lineI32(char* out, size_t cap, const char* name, const char* labels,
                   int32_t v) {
	if (labels && labels[0])
		return snprintf(out, cap, "%s{%s} %ld\n", name, labels, (long)v);
	return snprintf(out, cap, "%s %ld\n", name, (long)v);
}

/** @brief Sample line with a float value at `prec` decimals. */
inline int lineF(char* out, size_t cap, const char* name, const char* labels,
                 double v, int prec) {
	if (labels && labels[0])
		return snprintf(out, cap, "%s{%s} %.*f\n", name, labels, prec, v);
	return snprintf(out, cap, "%s %.*f\n", name, prec, v);
}

} /* namespace PromMetrics */
