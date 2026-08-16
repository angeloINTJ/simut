/**
 * @file WebCommitSections.h
 * @brief Per-section authorization table for POST /api/commit_all.
 *
 * @details /api/commit_all is multiplexed: one POST carries whichever of the
 * six sections the operator staged across the pages, because "Save & Restart"
 * commits one shared sessionStorage bag (window.Pending, WebUI.h). The route
 * used to be gated ONCE, on PERM_SYS_CONFIG, and then parse every section
 * regardless — so an account holding only PERM_SYS_CONFIG could add
 * administrators through users.actions (perms up to PERM_ALL_BITS) and move
 * the device to another access point through net.ssid/net.pass. Neither
 * permission was ever granted to it.
 *
 * It went unnoticed because the PAGES do separate the roles: /users needs
 * PERM_USER_MGR and /network needs PERM_NET_CONFIG just to render, so no
 * browser ever staged those sections without the bit, and only a hand-made
 * POST reached the gap. The interface was the access control — the failure
 * docs/diretrizes_seguranca_vibecoding.md §2 describes.
 *
 * The scan is deliberately FLAT — the same indexOf over the whole body the
 * parsers in WebManager_Commit.cpp already used, NOT a depth-aware walk of the
 * top-level keys. A gate that understands JSON nesting paired with parsers
 * that do not is worse than no gate at all:
 *
 *     {"sys":{"users":{"actions":[{"type":"add","perms":1023}]}}}
 *
 * reads as one sys section to a nesting-aware gate and as a users section to
 * the flat parser, and the escalation walks through a check that looks
 * correct. Matching the parser's own predicate leaves only one possible
 * disagreement — a false 403. A refusal, never a pass.
 *
 * Accepted cost of that choice: a telemetry template (sys.t_glob, 256 B of
 * free-form operator text) containing the literal "net" or "users" now costs
 * a restricted operator a 403 naming the section. The flat parser already
 * misread those payloads before this existed; it just did so silently, under
 * a 200.
 *
 * Gate and parsers cannot drift, because every section in
 * WebManager_Commit.cpp takes its start offset from THIS scan instead of
 * searching for its own needle again — the one-function-two-consumers rule
 * isProtectedFsPath follows.
 *
 * Pure and header-only so `pio test -e native` can exercise the decision
 * without a WebServer. See test/test_validators/test_main.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include "SystemDefs_Limits.h"   /* PERM_* bitmasks */

/** Index into kCommitSectionRules. Order is not significant except that the
 *  scan reports the FIRST denied section, so it is also the order the
 *  operator sees a section named in the 403. */
enum CommitSection {
	SEC_SYS = 0,
	SEC_SLOTS,
	SEC_CALIB,
	SEC_ALARMS,
	SEC_NET,
	SEC_USERS,
	SEC_COUNT
};

struct CommitSectionRule {
	const char* needle;   /**< Exactly what the parser searches for. */
	uint16_t    perm;     /**< Bit the caller must hold to change it. */
	const char* name;     /**< Reported to the client and to the log. */
};

/** Every top-level section /api/commit_all acts on.
 *
 * Adding a section to WebManager_Commit.cpp means adding a row HERE first —
 * the parser reads its offset out of the scan this table drives, so a section
 * with no row has no offset to read and cannot be parsed at all. That is the
 * point: a new section cannot end up ungated by omission, the way users and
 * net were.
 *
 * sys / slots / calib / alarms all answer to PERM_SYS_CONFIG because that is
 * what their pages require to render (/config for the first three, /alarms
 * for the fourth). The gate mirrors the page, it does not invent a policy. */
inline constexpr CommitSectionRule kCommitSectionRules[SEC_COUNT] = {
	{ "\"sys\"",    PERM_SYS_CONFIG, "sys"    },
	{ "\"slots\"",  PERM_SYS_CONFIG, "slots"  },
	{ "\"calib\"",  PERM_SYS_CONFIG, "calib"  },
	{ "\"alarms\"", PERM_SYS_CONFIG, "alarms" },
	{ "\"net\"",    PERM_NET_CONFIG, "net"    },
	{ "\"users\"",  PERM_USER_MGR,   "users"  },
};

/** Non-index outcomes of commitScanSections. A return >= 0 is the index of
 *  the first section the caller may not touch. */
enum CommitAuthResult {
	COMMIT_AUTH_OK    = -1,  /**< Every section present is permitted. */
	COMMIT_AUTH_EMPTY = -2,  /**< No recognised section — nothing to save. */
};

/**
 * @brief Locate every section in the payload and decide whether it may run.
 *
 * @param body     The urldecoded _payload.
 * @param perms    Permission bitmask of the authenticated caller.
 * @param outStart Receives one offset per section; -1 where absent. Filled
 *                 for every section regardless of the verdict, so a caller
 *                 that goes on to parse never searches the body again.
 * @return COMMIT_AUTH_OK, COMMIT_AUTH_EMPTY, or the index of the first
 *         section denied.
 *
 * All-or-nothing by design, and meant to run BEFORE the first write to cfg:
 * the handler ends in a reboot, so a partially applied commit would leave the
 * device in a state the operator never asked for and cannot inspect.
 *
 * COMMIT_AUTH_EMPTY is a refusal too. Without it, any authenticated caller
 * gets a free reboot out of `_payload={}` — the handler restarts the device
 * whether or not a single field changed. The page cannot produce it: the
 * commit button only appears once Pending.hasAny( ) is true.
 */
inline int commitScanSections(const String& body, uint16_t perms, int* outStart) {
	int found = 0;
	int denied = COMMIT_AUTH_OK;
	for (int i = 0; i < SEC_COUNT; i++) {
		outStart[i] = body.indexOf(kCommitSectionRules[i].needle);
		if (outStart[i] < 0) continue;
		found++;
		/* Keep scanning after the first denial: outStart[] is the parsers'
		 * only map of the payload, and handing back a half-filled one would
		 * make a later change to the caller silently parse the wrong bytes. */
		if (denied == COMMIT_AUTH_OK && !(perms & kCommitSectionRules[i].perm)) {
			denied = i;
		}
	}
	if (denied != COMMIT_AUTH_OK) return denied;
	return (found == 0) ? COMMIT_AUTH_EMPTY : COMMIT_AUTH_OK;
}

/** Bits that get a caller past the route's front door. Holding none of them
 *  means no section of any payload could ever be authorized, so the request
 *  is refused before the body is even read. */
inline constexpr uint16_t commitEntryPerms( ) {
	return PERM_SYS_CONFIG | PERM_NET_CONFIG | PERM_USER_MGR;
}
