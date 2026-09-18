/**
 * @file SdkPrintf.cpp
 * @brief printf/vprintf/puts/putchar for the SDK, without newlib's FILE stack.
 *
 * @details Nothing in src/ calls the FILE-based printf family — every format
 * in this firmware goes through snprintf/vsnprintf into a caller's buffer.
 * The family was linked anyway, for four callers that are not ours:
 *
 *   pico-sdk panic.c        puts( ) and vprintf( ) for the panic banner
 *   pico-sdk pheap.c        printf( ) and putchar( ) in a debug dump
 *   cyw43 driver            printf( )/puts( ) on bus errors
 *   newlib assert.c         fiprintf( ) — gone since -DNDEBUG (lever 2)
 *
 * Satisfying those four pulled vfprintf (9,674 B) and the stdio object stack
 * behind it, while svfprintf — the vsnprintf this firmware actually uses — was
 * already linked and carries the same formatter. Routing the four through
 * vsnprintf and Serial removes the duplicate: 6,792 B of .bin, measured
 * 2026-09-18 (lever 3 of docs/analysis/DIETA_FLASH.md).
 *
 * The output still goes to the same place. newlib's stdout reached the USB CDC
 * through the core's _write( ) hook, which ends in the same SerialUSB::write( )
 * this file calls directly — including its 1 s give-up when the host is not
 * reading, which is what keeps a panic from hanging forever on a device nobody
 * has plugged in.
 *
 * WHAT CHANGES: a line longer than the buffer below is truncated instead of
 * streamed. The four callers print short banners; the longest in the SDK is
 * the panic format plus its arguments. A truncated panic line is a better
 * trade than 6.8 kB, and the panic itself still halts the core.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

/* 160 B of stack, not static: panic( ) can fire on either core, and a shared
 * buffer would let the second one scribble over the first one's message. */
#define SDK_PRINTF_BUF 160

extern "C" {

int vprintf(const char* fmt, va_list ap) {
	char buf[SDK_PRINTF_BUF];
	const int n = vsnprintf(buf, sizeof buf, fmt, ap);
	if (n <= 0) return n;
	const size_t len = ((size_t)n < sizeof buf) ? (size_t)n : (sizeof buf - 1);
	Serial.write((const uint8_t*)buf, len);
	return (int)len;
}

int printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	const int n = vprintf(fmt, ap);
	va_end(ap);
	return n;
}

/* POSIX puts appends the newline; GCC also rewrites printf("...\n") into a
 * puts( ) call, so this has to keep that contract or those lines run together.
 *
 * No null guard: stdio.h declares puts( ) __nonnull, and -Werror=nonnull-compare
 * rejects the check as dead code — correctly, since a caller passing null is
 * already outside the contract and GCC is entitled to assume it does not. */
int puts(const char* s) {
	Serial.write(s);
	Serial.write('\n');
	return 1;
}

int putchar(int c) {
	Serial.write((uint8_t)c);
	return c;
}

} /* extern "C" */
