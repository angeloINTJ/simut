/**
 * @file ConfigMigrate.h
 * @brief Reads a config blob written by an older schema into the current struct.
 *
 * @details Until v23 every migration was a tail-append: the old blob was read
 * straight into the head of the current struct and the new tail filled with
 * defaults, and the historical sizes were DERIVED from the current struct —
 * `offsetof(SystemConfig, maint) + 4` was "the v22 file". That worked only
 * while nothing before the tail ever moved, and it failed silently the day
 * something did: the derived sizes moved with it, no old file matched any of
 * them, and the device came up on factory defaults with its Wi-Fi, accounts,
 * alarms and calibration gone. Nothing in the compile caught it, because the
 * static_asserts locked the record and the tail, not the total.
 *
 * v24 moved something: users[] grew from 5 to 32 accounts and each account
 * from 62 to 70 bytes, in the MIDDLE of the struct. So this file does two
 * things the old readers never did:
 *
 *   1. The historical layouts are LITERALS. 168 bytes of head, five 62-byte
 *      accounts, then a tail whose length says which schema wrote the file.
 *      They cannot drift because they no longer depend on anything.
 *
 *   2. The copy is by SEGMENT: head to head, each old account into its new
 *      70-byte slot, the tail to where the tail now begins. Every other byte of
 *      the destination is zero, which the per-version defaults then fill.
 *
 * Pure and header-only, like AlarmPayload.h and ConfigApply.h: the native
 * test builds synthetic v20/v22/v23 blobs with a known value in every segment
 * and checks that each one lands where the current struct says it should.
 * The CRC is the caller's — it covers the file as written, so it is checked
 * over the raw blob before this runs.
 *
 * Adding a field to SystemConfig from now on means: bump CONFIG_VERSION, add
 * the new size as a literal here, teach configMigrateLegacy( ) where that
 * version's segments go, and extend the test. The static_asserts below fail
 * the build until the current struct agrees with what this file claims.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "SystemDefs_Records.h"

/* ── The legacy layouts, as literals ─────────────────────────────────────── */

/** Bytes before users[] in every schema from v15 to v24: magic, version,
 *  deviceName, the Wi-Fi and static-IP strings, useDhcp, useHttps. */
constexpr size_t CFG_LEGACY_HEAD_LEN    = 168;
/** UserAccount v15..v23: active, username[16], password[33], permissions,
 *  mustChangePassword, salt[8], hashVersion. The v24 account keeps exactly
 *  these 62 bytes first and appends pinHash. */
constexpr size_t CFG_LEGACY_USER_STRIDE = 62;
constexpr uint8_t CFG_LEGACY_MAX_USERS  = 5;
constexpr size_t CFG_LEGACY_USERS_LEN   = CFG_LEGACY_USER_STRIDE * CFG_LEGACY_MAX_USERS; /* 310 */
/** Where the post-users tail began in every legacy schema. */
constexpr size_t CFG_LEGACY_TAIL_OFF    = CFG_LEGACY_HEAD_LEN + CFG_LEGACY_USERS_LEN;     /* 478 */

/** Struct sizes (the file is this plus a 4-byte CRC). Measured on the rig on
 *  2026-09-19 — 3,921 / 4,732 / 4,796 B files — and frozen here. */
constexpr size_t CFG_V20_BLOB = 3917;  /**< v20: ends with reserved[64]        */
constexpr size_t CFG_V22_BLOB = 4728;  /**< v21 and v22: + AlarmTelConfig (811) */
constexpr size_t CFG_V23_BLOB = 4792;  /**< v23: + MaintConfig (64)             */
constexpr size_t CFG_V24_BLOB = 6730;  /**< v24: 32 x 70 B accounts + pinSalt  */

/** Tail lengths, i.e. how much of a legacy blob follows its five accounts. */
constexpr size_t CFG_V20_TAIL_LEN = CFG_V20_BLOB - CFG_LEGACY_TAIL_OFF;  /* 3439 */
constexpr size_t CFG_V22_TAIL_LEN = CFG_V22_BLOB - CFG_LEGACY_TAIL_OFF;  /* 4250 */
constexpr size_t CFG_V23_TAIL_LEN = CFG_V23_BLOB - CFG_LEGACY_TAIL_OFF;  /* 4314 */

/* ── What the CURRENT struct must look like for the copies above to be right ── */

static_assert(offsetof(SystemConfig, users) == CFG_LEGACY_HEAD_LEN,
	"the head before users[] moved — the legacy segment copy would land wrong");
static_assert(sizeof(UserAccount) >= CFG_LEGACY_USER_STRIDE &&
	offsetof(UserAccount, hashVersion) + 1 == CFG_LEGACY_USER_STRIDE,
	"UserAccount no longer starts with the 62 legacy bytes in legacy order");
static_assert(offsetof(SystemConfig, telServer) ==
	offsetof(SystemConfig, users) + (size_t)MAX_USERS * sizeof(UserAccount),
	"telServer must follow users[] directly — the legacy tail is copied there");
/* The tail between telServer and the end of reserved[] is byte-identical to
 * what v20 wrote after its accounts; alarmTel and maint follow it unchanged. */
static_assert(offsetof(SystemConfig, alarmTel) - offsetof(SystemConfig, telServer) == CFG_V20_TAIL_LEN,
	"a field was inserted between users[] and alarmTel — the v20 tail copy no longer lines up");
static_assert(offsetof(SystemConfig, maint) - offsetof(SystemConfig, telServer) == CFG_V22_TAIL_LEN,
	"alarmTel changed size or moved — the v21/v22 tail copy no longer lines up");
static_assert(offsetof(SystemConfig, pinAuth) - offsetof(SystemConfig, telServer) == CFG_V23_TAIL_LEN,
	"maint changed size or moved — the v23 tail copy no longer lines up");
static_assert(sizeof(SystemConfig) == CFG_V24_BLOB,
	"SystemConfig is not the v24 layout — bump CONFIG_VERSION, add a literal here and a case to configMigrateLegacy( )");

/** Which legacy schema a file of this size holds. */
enum CfgLegacyKind : uint8_t {
	CFG_LEGACY_NONE = 0,
	CFG_LEGACY_V20,   /**< version 20 */
	CFG_LEGACY_V22,   /**< version 21 or 22 — same layout, v22 changed a meaning */
	CFG_LEGACY_V23    /**< version 23 */
};

/** By FILE size (blob + CRC), because that is what attemptLoad( ) has before
 *  it reads a byte. */
inline CfgLegacyKind configLegacyKind(size_t fileSize) {
	const size_t crc = sizeof(uint32_t);
	if (fileSize == CFG_V20_BLOB + crc) return CFG_LEGACY_V20;
	if (fileSize == CFG_V22_BLOB + crc) return CFG_LEGACY_V22;
	if (fileSize == CFG_V23_BLOB + crc) return CFG_LEGACY_V23;
	return CFG_LEGACY_NONE;
}

/** The blob length each kind must have. */
inline size_t configLegacyBlobLen(CfgLegacyKind kind) {
	switch (kind) {
		case CFG_LEGACY_V20: return CFG_V20_BLOB;
		case CFG_LEGACY_V22: return CFG_V22_BLOB;
		case CFG_LEGACY_V23: return CFG_V23_BLOB;
		default:             return 0;
	}
}

/** The schema version stamped in a blob's header (bytes 4..5, little-endian —
 *  the RP2040's byte order, which is how the file was written). */
inline uint16_t configBlobVersion(const uint8_t* blob) {
	return (uint16_t)(blob[4] | ((uint16_t)blob[5] << 8));
}

/**
 * @brief Copy a legacy blob into the current struct, segment by segment.
 *
 * @param blob    The file minus its trailing CRC. Its magic and version are
 *                checked here; the CRC is the caller's, over these same bytes.
 * @param blobLen Must equal configLegacyBlobLen(kind).
 * @param kind    From configLegacyKind( ).
 * @param out     Zeroed first. Head, accounts and tail are copied; every byte
 *                the old schema did not have stays zero for the caller's
 *                per-version defaults (alarmTel for v20, maint for v20/v22,
 *                the PIN salt and the admin's PIN digest for all three).
 * @return false when the magic, the version or the length does not match the
 *         kind — the caller treats that exactly like a corrupt file.
 *
 * Sensitive fields stay obfuscated on the way through: they are XORed by
 * field name afterwards, so their new offsets do not matter.
 */
inline bool configMigrateLegacy(const uint8_t* blob, size_t blobLen,
                                CfgLegacyKind kind, SystemConfig& out) {
	if (!blob || kind == CFG_LEGACY_NONE || blobLen != configLegacyBlobLen(kind)) return false;

	uint32_t magic = 0;
	memcpy(&magic, blob, sizeof(magic));
	if (magic != CONFIG_MAGIC) return false;
	const uint16_t ver = configBlobVersion(blob);
	switch (kind) {
		case CFG_LEGACY_V20: if (ver != 20) return false; break;
		case CFG_LEGACY_V22: if (ver != 21 && ver != 22) return false; break;
		case CFG_LEGACY_V23: if (ver != 23) return false; break;
		default: return false;
	}

	memset(&out, 0, sizeof(out));
	uint8_t* dst = (uint8_t*)&out;

	/* 1. head: identical in both layouts */
	memcpy(dst, blob, CFG_LEGACY_HEAD_LEN);

	/* 2. accounts: five 62-byte records into the first five 70-byte slots.
	 * pinHash stays zero — no legacy account had a PIN. */
	for (uint8_t i = 0; i < CFG_LEGACY_MAX_USERS; i++) {
		memcpy(&out.users[i], blob + CFG_LEGACY_HEAD_LEN + (size_t)i * CFG_LEGACY_USER_STRIDE,
		       CFG_LEGACY_USER_STRIDE);
	}

	/* 3. tail: everything after the accounts, to where the tail now starts */
	memcpy(dst + offsetof(SystemConfig, telServer), blob + CFG_LEGACY_TAIL_OFF,
	       blobLen - CFG_LEGACY_TAIL_OFF);

	return true;
}
