/**
 * @file PasswordCheck.h
 * @brief Which account a password is checked against, and the one derivation
 *        every check pays for — whether or not the name has an account.
 *
 * @details Checking a password costs one derivation: 5000 rounds of HMAC-SHA256
 * in software on Core 0, about 645 ms on the RP2040 (SECURITY.md §3). Until
 * 2026-10-02 the check derived only once it had found the account, so a name
 * with no account came back in about 100 ms and a wrong password in about
 * 730 ms (rig, v2.9.0, three of each). The time of the answer told anyone who
 * could reach the login page which names exist, and `/metrics` with Basic
 * credentials told the same.
 *
 * So a check now derives exactly once, every time: against the account's salt
 * when the name has one, against PASSWORD_CHECK_NO_ACCOUNT_SALT when it has
 * none, and that result is thrown away. A refused name now costs the ~645 ms a
 * wrong password always cost, and that is all it costs: anyone could already
 * buy that time by trying `admin`, and the per-client lockout (2 s, doubling up
 * to 300 s) bounds how often anyone asks, as before. One kind of account still
 * answers in its own time: a legacy one (hashVersion 0, 2500 rounds), until
 * its next good login migrates it.
 *
 * Header-only, with the hashing passed in, so the host can count derivations:
 * see test/test_validators/test_main.cpp. WebManager::verifyPasswordFor is the
 * one caller on the device; the web login, `/metrics` with Basic and the forced
 * password change all go through it.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>

/** What a check derives against when no account has the name. Any fixed 8
 *  bytes do: the result is thrown away, and what matters is the work. */
inline constexpr uint8_t PASSWORD_CHECK_NO_ACCOUNT_SALT[8] = {
	's', 'i', 'm', 'u', 't', '-', 'n', 'o'
};

/**
 * Check `pass` for the account named `user` among `users[0..count)`.
 *
 * `derive` is the hashing, passed in so the host can count it:
 *   - `derive.v1(user, pass, salt)`    the current scheme, PASSWORD_HMAC_ROUNDS
 *   - `derive.legacy(user, pass)`      hashVersion 0: 2500 rounds, username salt
 *   - `derive.same(stored, computed)`  the constant-time comparison
 *
 * Names are unique — `user add` and the web refuse a taken one — so the first
 * active account with the name is the only one.
 *
 * @param legacyMatch  set when the match was against a legacy hash; the caller
 *                     re-hashes with a fresh salt and saves.
 * @return the slot whose password matched, or -1.
 */
template <typename UserT, typename Derive>
inline int passwordCheck(const UserT* users, int count, const String& user,
                         const String& pass, Derive& derive, bool& legacyMatch) {
	legacyMatch = false;
	for (int i = 0; i < count; i++) {
		if (!users[i].active || String(users[i].username) != user) continue;
		const String stored(users[i].password);
		/* Legacy: hashVersion==0, 30 chars (120 bits), username-salt, 2500 rounds. */
		if (users[i].hashVersion == 0 && stored.length( ) == 30) {
			legacyMatch = derive.same(stored, derive.legacy(user, pass));
			return legacyMatch ? i : -1;
		}
		/* V1: hashVersion>=1, 32 chars (128 bits), random salt, PASSWORD_HMAC_ROUNDS. */
		return derive.same(stored, derive.v1(user, pass, users[i].salt)) ? i : -1;
	}
	/* No account by that name: the same derivation, thrown away. */
	(void)derive.v1(user, pass, PASSWORD_CHECK_NO_ACCOUNT_SALT);
	return -1;
}
