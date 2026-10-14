/**
 * @file    src/ota/trial.h
 * @brief   The first boot of an RP2350 update, on trial: buy, or go back.
 * @details docs/analysis/OTA_AB_RP2350.md, step 5. The image an update installs
 *          is flagged try-before-you-buy (tools/rp2350/picobin.py). The boot
 *          ROM boots it right after the FLASH_UPDATE reboot of the apply, and
 *          keeps it only if it calls the ROM's explicit_buy; any other reset
 *          before that brings back the image it replaced. Here: when the image
 *          buys itself or gives up, and how the image that comes back reads
 *          which version did not stay. staging.cpp runs it on the board.
 *          Pure: no SDK, so native_otasig runs it (test_trial.cpp). Header-
 *          only, as slot_stage.h is, so the RP2040's images, which never
 *          include it, link nothing new.
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <string.h>

namespace ota {

/** The image buys itself after this long healthy without a break (decided on
 *  2026-10-03): LittleFS and the configuration read, the network up, the web
 *  server listening, with loop( ) feeding the watchdog all along. */
constexpr uint32_t TRIAL_HEALTHY_MS  = 60000u;
/** Not bought this long after its boot, it goes back. The healthy minute has to
 *  begin four minutes in at the latest, which leaves the Wi-Fi room to join. */
constexpr uint32_t TRIAL_DEADLINE_MS = 300000u;

enum class TrialAction : uint8_t { NONE, BUY, REVERT };

struct TrialClock {
	bool     pending;        /**< this boot is on trial: the ROM waits for the buy */
	bool     healthy;        /**< the last look saw health */
	uint32_t healthy_since;  /**< millis( ) of the first look of that healthy run */
};

/** One look at the trial, from loop( ), with @p now_ms the millis( ) since boot.
 *  BUY once the image has been healthy for TRIAL_HEALTHY_MS without a break;
 *  REVERT at TRIAL_DEADLINE_MS without that. A run that would end past the
 *  deadline does not count. */
inline TrialAction trial_step(TrialClock& c, bool healthy, uint32_t now_ms) {
	if (!c.pending) return TrialAction::NONE;
	if (!healthy) {
		c.healthy = false;
	} else if (!c.healthy) {
		c.healthy = true;
		c.healthy_since = now_ms;
	}
	if (c.healthy && now_ms - c.healthy_since >= TRIAL_HEALTHY_MS) return TrialAction::BUY;
	if (now_ms >= TRIAL_DEADLINE_MS) return TrialAction::REVERT;
	return TrialAction::NONE;
}

/** Whether a failure counter kept still since the last look, remembering @p now
 *  for the next one. loop( ) feeds it Core 1's restarts by the health check: a
 *  restart is a failure, and it breaks the healthy minute. */
inline bool trial_count_still(uint32_t now, uint32_t& last) {
	const bool still = (now == last);
	last = now;
	return still;
}

inline bool trial_version_char(uint8_t c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       c == '.' || c == '+' || c == '-';
}

/** The version of the first whole "SIMUT-ENV:<env>;v=<version>;" tag in @p buf
 *  (BuildIdentity.cpp): the env in [a-z], the version in [0-9A-Za-z.+-]. A slot
 *  is written from its start and the stage erases only what the new image
 *  covers, so an older image's tag can lie past the end of the new one: the
 *  first tag is the image's. false, with @p out empty, when there is no tag
 *  or its version does not fit @p outLen. */
inline bool trial_tag_version(const uint8_t* buf, uint32_t len, char* out, uint32_t outLen) {
	static const char pfx[] = "SIMUT-ENV:";
	const uint32_t plen = sizeof(pfx) - 1;
	if (!out || outLen == 0) return false;
	out[0] = '\0';
	if (!buf) return false;
	for (uint32_t i = 0; i + plen < len; i++) {
		if (memcmp(buf + i, pfx, plen) != 0) continue;
		uint32_t j = i + plen;
		const uint32_t env = j;
		while (j < len && buf[j] >= 'a' && buf[j] <= 'z') j++;
		if (j == env || j + 3 > len || memcmp(buf + j, ";v=", 3) != 0) continue;
		j += 3;
		const uint32_t ver = j;
		while (j < len && trial_version_char(buf[j])) j++;
		if (j == ver || j >= len || buf[j] != ';' || j - ver + 1 > outLen) continue;
		memcpy(out, buf + ver, j - ver);
		out[j - ver] = '\0';
		return true;
	}
	return false;
}

/** A version "M.m.p" as the log's context, which the binary log keeps as a
 *  signed 16-bit number and not as text: M * 10000 + m * 100 + p, so 2.10.9
 *  reads 21009. 0 when it is not three numbers, m or p is past 99, or the
 *  whole is past 32767. */
inline int trial_version_code(const char* v) {
	if (!v) return 0;
	uint32_t part[3] = { 0, 0, 0 };
	const char* p = v;
	for (int n = 0; n < 3; n++) {
		if (*p < '0' || *p > '9') return 0;
		uint32_t x = 0;
		for (int digits = 0; *p >= '0' && *p <= '9'; p++) {
			if (++digits > 5) return 0;
			x = x * 10u + (uint32_t)(*p - '0');
		}
		part[n] = x;
		if (n < 2 && *p++ != '.') return 0;
	}
	if (*p != '\0' || part[1] > 99u || part[2] > 99u) return 0;
	const uint32_t code = part[0] * 10000u + part[1] * 100u + part[2];
	return code <= 32767u ? (int)code : 0;
}

} /* namespace ota */
