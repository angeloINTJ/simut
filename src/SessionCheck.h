/**
 * @file SessionCheck.h
 * @brief Whether a session still belongs to the account it was opened for.
 *
 * @details A web session used to carry the account as the login saw it — slot,
 * name, permission bits — and keep all of it until it expired (15 min idle) or
 * the device restarted. While every account change made through the web
 * restarted the device, the restart was the revocation. The panel and the
 * console never restarted: an account deleted there kept its web session,
 * with every bit it had, for up to fifteen minutes. And once accounts applied
 * live through the web as well (A-08), so would every deletion.
 *
 * So a session is checked against the live account on every request:
 *   - the slot is no longer active: the account was deleted — the session ends;
 *   - the slot holds another name: deleted, and the slot taken — it ends;
 *   - the salt changed: someone else set the password (an admin's reset, the
 *     console's `user pass`, a password changed from another browser) — it
 *     ends. The route that changes the password re-stamps the session it was
 *     called from, so the owner who changed it stays logged in;
 *   - otherwise it stands, with the bits the account holds NOW, so narrowing
 *     an account takes effect at its next request instead of at its next
 *     login.
 *
 * The panel identifies by PIN, not by password, so it asks less: the account
 * still there under the same name. Its own PIN or a new web password are not
 * its business, and the panel session ends at the next CFG press anyway.
 *
 * Header-only, so the host can test it: see test/test_validators/test_main.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stdint.h>
#include <string.h>

/**
 * Is the account in `slot` still the one the panel identified as `name`?
 * On true, `perms` becomes what the account holds now.
 */
template <typename UserT>
inline bool panelAccountStillThere(const UserT* users, int count, int slot,
                                   const char* name, uint16_t& perms) {
	if (slot < 0 || slot >= count) return false;
	const UserT& u = users[slot];
	if (!u.active || strncmp(u.username, name, sizeof(u.username)) != 0) return false;
	perms = u.permissions;
	return true;
}

/**
 * May a panel session holding `caller` change an account's panel bits from
 * `before` to `after`? Only by ADDING bits the caller holds itself.
 *
 * The web refuses to grant what the caller lacks since V-09
 * (commitGrantAllowed in WebCommitSections.h); the panel did not, so an
 * account with Users and no panel bit gave Limits, Block and Maintenance to
 * any account, its own included (finding 8 of
 * docs/analysis/PLANO_REVISAO_EXTERNA.md).
 * Taking a bit away is not escalation, and neither is leaving one the caller
 * lacks where it already was: deleting the account outright needs no more than
 * Users. A new account is `before` 0. Its own account is no exception: what it
 * adds to itself it already holds.
 */
inline constexpr bool panelGrantAllowed(uint16_t before, uint16_t after, uint16_t caller) {
	return ((after & (uint16_t)~before) & (uint16_t)~caller) == 0;
}

/**
 * Is the account in `slot` still the one this web session was opened for —
 * same name, and a password nobody else has set since (`salt`, stamped at
 * login)? On true, `perms` becomes what the account holds now.
 */
template <typename UserT>
inline bool sessionStillValid(const UserT* users, int count, int slot,
                              const char* name, const uint8_t (&salt)[8], uint16_t& perms) {
	uint16_t now = 0;
	if (!panelAccountStillThere(users, count, slot, name, now)) return false;
	if (memcmp(users[slot].salt, salt, sizeof(salt)) != 0) return false;
	perms = now;
	return true;
}
