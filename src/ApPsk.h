/**
 * @file ApPsk.h
 * @brief Setup access point key — digest to passphrase (finding V-05).
 *
 * @details The setup AP used to be OPEN. Anyone in radio range could join it
 * and reach the captive portal, the login page and every unauthenticated
 * endpoint behind it, on a network the device itself brings up whenever the
 * Wi-Fi credentials are wrong — which is exactly when nobody is watching.
 *
 * The key is DERIVED, not configured: SHA-256 over the RP2040's unique board
 * id plus a domain string, mapped here onto the same 32-symbol alphabet the
 * initial admin password uses. That buys three things a config field would
 * not. It is stable for a given board, so the label on the enclosure keeps
 * working across a factory reset. It exists before any configuration does,
 * which matters because AP mode is what an unconfigured device boots into.
 * And it costs no flash field, no migration and no CONFIG_VERSION bump.
 *
 * What it is not: a secret. Anyone who can read the board id can recompute it,
 * and the board id is printed by `show system info`. It raises the bar from
 * "in radio range" to "has been told the key", which is the bar a setup
 * network is supposed to have; the admin password remains the thing that
 * actually guards the device.
 *
 * Ten characters of a 32-symbol alphabet is ~50 bits, and WPA2 wants 8..63.
 *
 * The mapping lives in its own header, taking a digest rather than computing
 * one, so the host test can check the properties that matter (alphabet,
 * length, termination, determinism) without BearSSL.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** Characters of the AP key. Same set as the initial admin password: no O/0,
 * no I/1, because this string gets read aloud and typed on a phone. */
#define AP_PSK_ALPHABET "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

/** Key length in characters (WPA2 accepts 8..63). */
#define AP_PSK_LEN 10

/**
 * @brief Map a digest onto an AP_PSK_LEN-character key.
 *
 * @param digest    hash bytes (SHA-256 of board id + domain).
 * @param digestLen how many bytes are available; must be >= AP_PSK_LEN.
 * @param out       destination, at least AP_PSK_LEN + 1 bytes.
 * @param cap       size of @p out.
 *
 * Writes an empty string if it cannot produce a full key, so a caller that
 * ignores the return value never starts an AP with a truncated passphrase —
 * WPA2 would reject a key under 8 characters and the AP would come up open,
 * which is the failure this whole file exists to prevent.
 *
 * @return true when a full key was written.
 */
inline bool apPskFromDigest(const uint8_t* digest, size_t digestLen,
                            char* out, size_t cap) {
	if (!out || cap == 0) return false;
	out[0] = '\0';
	if (!digest || digestLen < AP_PSK_LEN || cap < AP_PSK_LEN + 1) return false;

	static const char alphabet[] = AP_PSK_ALPHABET;
	const size_t n = sizeof(alphabet) - 1;   /* 32 — a power of two, so the
	                                          * modulo below is unbiased. */
	for (size_t i = 0; i < AP_PSK_LEN; i++) {
		out[i] = alphabet[digest[i] % n];
	}
	out[AP_PSK_LEN] = '\0';
	return true;
}
