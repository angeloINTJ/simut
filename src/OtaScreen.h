/**
 * @file OtaScreen.h
 * @brief What the panel says while firmware is updated over the air.
 *
 * @details An update is an upload of about 1 MB (30 s on the bench), a
 * signature check over it (1.9 s), a second request that applies it, and a
 * copy into the app slot that ends in a reboot (about 25 s). Until 2026-10-02
 * the panel showed none of it: it froze on the last dashboard frame for the
 * whole upload, because the stage parks Core 1 for its flash writes, and kept
 * that frame through the copy. Someone standing at the device had no way to
 * know that switching it off then was the one thing not to do — and when an
 * image was refused, the panel never said so.
 *
 * The phases follow the update; a refusal names its reason, from the verdict
 * the validator returns (ota/validation.h, ValidationStatus — the numbers are
 * pinned against it in WebManager_Ota.cpp). The two screens that report an
 * ending (refused, cut) go back to the dashboard by themselves; the others
 * stay until the next step replaces them, and the 30 s idle return must not
 * take the panel away in the middle of an update.
 *
 * The alpha prints the same on its 16x2 LCD, in the fixed unaccented
 * Portuguese its other screens use: the HD44780 ROM is not Latin-1.
 *
 * Core 0 publishes the state and Core 1 may be frozen when it does — the stage
 * holds it under a flash pause from the first byte to the commit — so the
 * phase, its reason and the number of the request that set them travel in one
 * 32-bit word (otaPack), read and replaced with single atomic accesses and no
 * lock.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum OtaPhase : uint8_t {
	OTA_PH_NONE = 0,       /**< no update on the panel */
	OTA_PH_RECEIVING,      /**< the image is arriving: progress bar */
	OTA_PH_CHECKING,       /**< all of it is in; its signature is being checked */
	OTA_PH_READY,          /**< verified and committed; waiting for the apply */
	OTA_PH_INSTALLING,     /**< being copied; the device restarts by itself */
	OTA_PH_REFUSED,        /**< refused, and why */
	OTA_PH_CUT,            /**< the upload stopped before its end */
};

enum OtaWhy : uint8_t {
	OTA_WHY_NONE = 0,
	OTA_WHY_MODEL,           /**< v=7: built for another variant */
	OTA_WHY_UNSIGNED,        /**< v=8 */
	OTA_WHY_SIGNATURE,       /**< v=9: does not verify */
	OTA_WHY_RETIRED_KEY,     /**< v=10: signer below the lowest accepted serial */
	OTA_WHY_BLOCKED_VERSION, /**< v=11: below this image's security version */
	OTA_WHY_BENCH_KEY,       /**< v=12: a bench signature on a production image */
	OTA_WHY_DAMAGED,         /**< everything else: size, boot2, a stage that never finished */
};

/** The reason a refusal with validation verdict v gives. constexpr, so that
 *  WebManager_Ota.cpp can pin it against ValidationStatus at compile time. */
constexpr OtaWhy otaWhyFor(unsigned v) {
	switch (v) {
	case 0:  return OTA_WHY_NONE;
	case 7:  return OTA_WHY_MODEL;
	case 8:  return OTA_WHY_UNSIGNED;
	case 9:  return OTA_WHY_SIGNATURE;
	case 10: return OTA_WHY_RETIRED_KEY;
	case 11: return OTA_WHY_BLOCKED_VERSION;
	case 12: return OTA_WHY_BENCH_KEY;
	default: return OTA_WHY_DAMAGED;
	}
}

/** Percentage for the bar while the image arrives. total is the request's
 *  Content-Length, which also counts the multipart boundaries, so the bar
 *  stops at 99 and reads full only on the CHECKING screen. */
inline uint8_t otaPercent(uint32_t got, uint32_t total) {
	if (total == 0) return 0;
	const uint64_t p = (uint64_t)got * 100u / total;
	return (uint8_t)(p > 99 ? 99 : p);
}

/** Pixels of a bar `width` wide filled at pct. */
inline int16_t otaBarFill(uint8_t pct, int16_t width) {
	if (pct > 100) pct = 100;
	return (int16_t)((int32_t)width * pct / 100);
}

/** How long a screen that reports an ending stays before the dashboard;
 *  0 for the screens that stay until the next step. */
inline uint32_t otaHoldMs(OtaPhase p) {
	return p == OTA_PH_REFUSED ? 12000u : p == OTA_PH_CUT ? 8000u : 0u;
}

/** An update is under way: nothing may send the panel back to the dashboard. */
inline bool otaHoldsPanel(OtaPhase p) {
	return p >= OTA_PH_RECEIVING && p <= OTA_PH_INSTALLING;
}

/** A tap leaves the screen early; only the screens that report an ending. */
inline bool otaTapDismisses(OtaPhase p) {
	return p == OTA_PH_REFUSED || p == OTA_PH_CUT;
}

/** The published state: request number in the high half, reason, phase. */
inline uint32_t otaPack(uint16_t seq, OtaPhase p, OtaWhy w) {
	return (uint32_t)seq << 16 | (uint32_t)w << 8 | (uint32_t)p;
}
inline OtaPhase otaPhaseOf(uint32_t s) { return (OtaPhase)(s & 0xFFu); }
inline OtaWhy otaWhyOf(uint32_t s) { return (OtaWhy)((s >> 8) & 0xFFu); }
inline uint16_t otaSeqOf(uint32_t s) { return (uint16_t)(s >> 16); }

/** The word that publishes p after `prev`: the next request number, so a
 *  screen drawn for an older request is known to be stale. */
inline uint32_t otaNext(uint32_t prev, OtaPhase p, OtaWhy w) {
	return otaPack((uint16_t)(otaSeqOf(prev) + 1u), p, w);
}

/** The word a screen that reported an ending leaves behind: same request,
 *  no phase. Swapped in only over the word it was read from, so a phase Core 0
 *  published in between is never the one dismissed. */
inline uint32_t otaDismissed(uint32_t s) {
	return otaPack(otaSeqOf(s), OTA_PH_NONE, OTA_WHY_NONE);
}

/** Where to break `text` into two lines no wider than maxW: the offset of the
 *  space that keeps the wider of the two narrowest, or 0 when the text fits on
 *  one line or has no space to break at. Narrowest-wider rather than the first
 *  line as full as it can be: the Portuguese for "it restarts by itself in
 *  about 30 s" filled that way left "30 s" alone on the second line.
 *  width(s, n) measures the first n bytes of s in the font being drawn. */
template <class Width>
size_t otaBreak(const char* text, int32_t maxW, Width&& width) {
	const size_t n = strlen(text);
	if (width(text, n) <= maxW) return 0;
	size_t best = 0;
	int32_t bestW = INT32_MAX;
	for (size_t i = 1; i + 1 < n; i++) {
		if (text[i] != ' ') continue;
		const int32_t a = width(text, i);
		const int32_t b = width(text + i + 1, n - i - 1);
		const int32_t w = a > b ? a : b;
		if (w < bestW) { bestW = w; best = i; }
	}
	return best;
}

/** The alpha's two LCD rows, padded to 16 columns, ASCII. */
inline void otaLcdLines(OtaPhase p, OtaWhy why, char row0[17], char row1[17]) {
	const char* a = "";
	const char* b = "";
	switch (p) {
	case OTA_PH_RECEIVING:  a = "Atualizando...";   b = "Nao desligue!";    break;
	case OTA_PH_CHECKING:   a = "Conferindo a";     b = "assinatura...";    break;
	case OTA_PH_READY:      a = "Imagem conferida"; b = "aguarda instalar"; break;
	case OTA_PH_INSTALLING: a = "Instalando...";    b = "Nao desligue!";    break;
	case OTA_PH_REFUSED:
		a = "Atualiz.recusada";
		switch (why) {
		case OTA_WHY_MODEL:           b = "outro modelo";     break;
		case OTA_WHY_UNSIGNED:        b = "sem assinatura";   break;
		case OTA_WHY_SIGNATURE:       b = "assin. invalida";  break;
		case OTA_WHY_RETIRED_KEY:     b = "chave aposentada"; break;
		case OTA_WHY_BLOCKED_VERSION: b = "versao bloqueada"; break;
		case OTA_WHY_BENCH_KEY:       b = "chave de bancada"; break;
		case OTA_WHY_DAMAGED:         b = "imagem invalida";  break;
		default:                      b = "";                 break;
		}
		break;
	case OTA_PH_CUT:        a = "Atualiz.cortada";  b = "versao mantida";   break;
	default: break;
	}
	const char* src[2] = { a, b };
	char* dst[2] = { row0, row1 };
	for (int r = 0; r < 2; r++) {
		size_t n = strlen(src[r]);
		if (n > 16) n = 16;
		memcpy(dst[r], src[r], n);
		memset(dst[r] + n, ' ', 16 - n);
		dst[r][16] = '\0';
	}
}
