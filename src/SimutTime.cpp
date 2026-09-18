/**
 * @file SimutTime.cpp
 * @brief The libc time entry points, backed by SimutTime.h's fixed offset.
 *
 * @details These three definitions override newlib's. The linker resolves an
 * object file's symbol before it reaches the archive, so localtime_r( ),
 * localtime( ) and mktime( ) here are what every call site in src/ — and
 * anything in the framework that formats a local time — ends up calling, and
 * newlib's versions, the TZ parser they read and the scanf engine that parser
 * uses are never pulled in. That is the 13,188 B.
 *
 * Overriding libc is not something to do lightly, and it is worth being exact
 * about what makes it safe here: this is a bare-metal image with one process
 * and no dynamic linking, the three functions have no other implementation in
 * play, and their contract is fully honoured for the input this firmware
 * produces — a whole-hour offset, no DST. What is NOT honoured is the TZ
 * environment variable, which nothing reads any more.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "SimutTime.h"

/* Written on Core 0 (config apply, three call sites), read on both cores —
 * DisplayManager draws the clock and the graph axis from Core 1. A 32-bit
 * aligned store is single-copy atomic on the RP2040, so a reader sees the old
 * or the new offset and never a mix; volatile is what stops the compiler from
 * keeping it in a register across a call that could have changed it.
 *
 * This is strictly less shared state than before: setenv( ) + tzset( ) built a
 * whole TZ struct on Core 0 while Core 1 read it through localtime_r( ). */
static volatile long g_offsetSeconds = 0;

void simutTimeSetOffset(int8_t hours) {
	g_offsetSeconds = (long)hours * 3600L;
}

long simutTimeOffsetSeconds( ) {
	return g_offsetSeconds;
}

/* The overrides are for the firmware only. test/test_network builds
 * NetworkManager.cpp on the host, where defining these would shadow glibc's
 * for the whole test binary — a landmine for a suite that has nothing to do
 * with time. The arithmetic they wrap is in SimutTime.h and IS tested on the
 * host, in test/test_validators, against the host's own libc. */
#if defined(ARDUINO_ARCH_RP2040)

extern "C" {

struct tm* localtime_r(const time_t* t, struct tm* out) {
	if (!t || !out) return nullptr;
	return simutLocalTimeTz(*t, out, g_offsetSeconds);
}

/* The non-reentrant form, kept because CommandManager's `show clock` uses it.
 * One static buffer, same as newlib's — and, same as newlib's, whoever calls
 * it from two cores at once gets what they asked for. */
struct tm* localtime(const time_t* t) {
	static struct tm shared;
	if (!t) return nullptr;
	return simutLocalTimeTz(*t, &shared, g_offsetSeconds);
}

time_t mktime(struct tm* tmv) {
	if (!tmv) return (time_t)-1;
	return simutMkTimeTz(tmv, g_offsetSeconds);
}

} /* extern "C" */

#endif /* ARDUINO_ARCH_RP2040 */
