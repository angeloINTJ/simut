/**
 * @file FsSecretPath.h
 * @brief The /config download guard — isSecretFsPath (finding A-4).
 *
 * @details /config holds every secret on the device: wifiPass, mqttPass and
 * telApiKey (XOR-obfuscated with the RP2040 chip id, which the backup header
 * itself carries in the clear) plus the per-user password hashes and salts.
 * /download refuses paths under it regardless of PERM_FILE_READ — a bit the
 * users page presents as "read files" must not also mean "exfiltrate the
 * credential store". The sanctioned way to move config off the device is
 * /api/backup, raised to PERM_FULL_ADMIN for the same reason. Closes the
 * sibling of the leak in docs/diretrizes_seguranca_vibecoding.md §1/§5 — the
 * very file an accidentally committed .bkp exposed.
 *
 * Header-only and dependency-free (Arduino String + one macro) so the host
 * test can reach it; StorageManager.h pulls LittleFS and pico/mutex and does
 * not compile on the host. See test/test_validators/test_main.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>

/* Normally provided by StorageManager.h, which includes this file after its
 * own definition. Defined here too so the header stands alone for the native
 * test; the #ifndef keeps the firmware build's single definition authoritative. */
#ifndef DIR_CONFIG
#define DIR_CONFIG "/config"
#endif

/**
 * @brief True for a path under /config — the credential store.
 *
 * Prefix match, case- and slash-normalised, so "config/system.bin" and
 * "/CONFIG/system.bin" cannot slip past a check written for one spelling.
 * Path traversal ("..") is rejected separately by the caller, before this
 * runs, so a "/history/../config/system.bin" never reaches the prefix test
 * still wearing its traversal.
 */
inline bool isSecretFsPath(const String& path) {
	String l = path;
	l.toLowerCase( );
	if (!l.startsWith("/")) {          /* normalise "config/..." to "/config/..." */
		String tmp = "/";
		tmp += l.c_str( );
		l = tmp;
	}
	/* DIR_CONFIG "/" is one compile-time string literal ("/config/"), so the
	 * prefix test needs no runtime String building. */
	return l.startsWith(DIR_CONFIG "/");
}

/* Same standalone-header trick as DIR_CONFIG above: normally these come from
 * SystemDefs_Limits.h, which pulls the sensor headers with it. Defined here
 * too so the host test can call downloadPermFor without them; the #ifndef
 * keeps the firmware build's single definition authoritative. */
#ifndef PERM_HISTORY
#define PERM_HISTORY 0x0002
#endif
#ifndef PERM_LOGS
#define PERM_LOGS 0x0004
#endif

/**
 * @brief Extra permission bit /download must require for `path`, or 0.
 *
 * Finding O-1 (decision D-5). PERM_FILE_READ was the whole gate, so an account
 * given "read files" and nothing else could pull /history/*.h5 and the forensic
 * log — the two datasets the users page presents as separate, revocable
 * permissions. The bit that says "may read files" must not silently mean "may
 * read every file", or the other two bits are decoration.
 *
 * /config is not here on purpose: it is refused outright by isSecretFsPath,
 * whatever bits the caller holds.
 *
 * Pure, so the host test can enumerate it — the interesting cases are the ones
 * that look adjacent: "/historyx/f.h5" is not history, and "system.blog.bak"
 * is not a log.
 */
inline uint16_t downloadPermFor(const String& path) {
	String l = path;
	l.toLowerCase( );
	if (!l.startsWith("/")) {
		String tmp = "/";
		tmp += l.c_str( );
		l = tmp;
	}
	if (l.startsWith("/history/")) return PERM_HISTORY;
	if (l.endsWith(".blog")) return PERM_LOGS;
	return 0;
}

/**
 * @brief True for the /config directory itself or any path under it.
 *
 * isSecretFsPath matches only "/config/..." (files inside the store) and
 * deliberately misses the bare "/config" — the download path never sees a
 * directory. A directory listing (/api/ls) must refuse the bare directory too,
 * or it enumerates the store's filenames (finding ACH-04). This sibling folds
 * the two cases into one check, without changing isSecretFsPath's tested
 * file-oriented contract.
 */
inline bool isSecretFsDir(const String& path) {
	String l = path;
	l.toLowerCase( );
	if (!l.startsWith("/")) {          /* normalise "config" to "/config" */
		String tmp = "/";
		tmp += l.c_str( );
		l = tmp;
	}
	return l == DIR_CONFIG || l.startsWith(DIR_CONFIG "/");
}
