/**
 * @file WebManager_Tls.cpp
 * @brief POST /api/tls — installing the HTTPS pair on a device in service.
 *
 * @details Until this route existed there was no way to do it, and the manual
 * said otherwise for a month: it pointed at the Files page, and
 * handleUploadData( ) has refused every upload that resolves under /config
 * since the 2026-08-29 audit. Both halves were right on their own — a write
 * into the credential store can overwrite system.bin, or plant a forged pair
 * and sit in the middle of the admin's next session — and together they left
 * HTTPS installable only by reflashing the filesystem, which reformats it
 * (issue #133).
 *
 * This route buys back exactly one write, and pays for it three ways:
 *
 *   NARROW   two fixed paths. Nothing here takes a filename from the request,
 *            so there is no traversal surface and no way to reach system.bin.
 *   ADMIN    getAuthPerms( ) == PERM_FULL_ADMIN, the gate /api/ota/apply and
 *            /api/restore?op=apply use. A user carrying PERM_FILE_UPLOAD still
 *            cannot install a certificate — which is the attack the /config
 *            guard exists to stop, and it stays stopped.
 *   PROVEN   the pair must parse AND belong to each other before either file
 *            is written. That is the second gap this closes: a pair that
 *            parses but does not match was discovered at the NEXT BOOT, as
 *            HTTPS quietly not coming up, with the working pair already gone.
 *
 * Compiled only where HTTPS is (pico_w_release today): with the server
 * compiled out, a route that installs its certificate is 2 kB of flash that
 * can only mislead.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "WebManager.h"

#ifdef SIMUT_WEB_HTTPS

#include "WebTlsFiles.h"
#include "PemBlocks.h"   /* pemCertChain / pemPrivateKey — tested on the host */
#include "LogManager.h"
#include <LittleFS.h>
#include <bearssl/bearssl_x509.h>
#include <bearssl/bearssl_ec.h>
#include <bearssl/bearssl_rsa.h>

using ReadGuard = StorageManager::ReadGuard;

/* Does this private key belong to this certificate?
 *
 * The check is the definition rather than a heuristic: derive the public key
 * from the private one and compare it byte for byte with the public key the
 * certificate carries. File names, dates and subjects do not enter into it.
 *
 * `why` is filled with a sentence the operator can act on. "The key does not
 * belong to this certificate" is a different mistake from "this build cannot
 * serve that curve", and the second one is invisible until a handshake fails.
 */
static bool tlsPairMatches(const BearSSL::X509List& certs,
                           const BearSSL::PrivateKey& key,
                           const char** why, String& describe) {
	const br_x509_certificate* leaf = certs.getX509Certs( );
	if (!leaf || !leaf->data || leaf->data_len == 0) { *why = "the certificate is empty"; return false; }

	/* No trust anchor (nullptr): this asks what public key the certificate
	 * carries, not whether anyone vouches for it. Self-signed is the normal
	 * case here and must not be refused for being self-signed. */
	br_x509_decoder_context dc;
	/* Four nulls: no subject-DN and no issuer-DN receiver. This build's
	 * BearSSL takes both callbacks and both contexts; the names are of no use
	 * here, only the public key is. */
	br_x509_decoder_init(&dc, nullptr, nullptr, nullptr, nullptr);
	br_x509_decoder_push(&dc, leaf->data, leaf->data_len);
	const br_x509_pkey* pub = br_x509_decoder_get_pkey(&dc);
	if (!pub) { *why = "the certificate did not decode"; return false; }

	if (key.isEC( )) {
		if (pub->key_type != BR_KEYTYPE_EC) {
			*why = "the certificate holds an RSA key and the private key is EC"; return false;
		}
		const br_ec_private_key* sk = key.getEC( );
		if (!sk) { *why = "the private key did not decode"; return false; }
		if (sk->curve != pub->key.ec.curve) {
			*why = "certificate and key are on different curves"; return false;
		}
		unsigned char q[BR_EC_KBUF_PUB_MAX_SIZE];
		const size_t qlen = br_ec_compute_pub(br_ec_get_default( ), nullptr, q, sk);
		/* Zero means this build's EC implementation does not know that curve:
		 * the pair would parse, install, and then fail every handshake, which
		 * is the failure this route exists to catch before it is written.
		 * P-256 is the curve the manual recommends, but it is not the only one
		 * that works — a P-521 pair was installed and served on the rig
		 * (2026-09-18, ECDHE-ECDSA-AES256-GCM-SHA384), so the test is what the
		 * implementation says, not a list kept here. */
		if (qlen == 0) { *why = "this firmware's EC implementation does not know that curve"; return false; }
		if (qlen != pub->key.ec.qlen || memcmp(q, pub->key.ec.q, qlen) != 0) {
			*why = "the key does not belong to this certificate"; return false;
		}
		describe = String("EC (curve ") + (int)sk->curve + ")";
		return true;
	}

	if (key.isRSA( )) {
		if (pub->key_type != BR_KEYTYPE_RSA) {
			*why = "the certificate holds an EC key and the private key is RSA"; return false;
		}
		const br_rsa_private_key* sk = key.getRSA( );
		if (!sk) { *why = "the private key did not decode"; return false; }
		br_rsa_compute_modulus cm = br_rsa_compute_modulus_get_default( );
		const size_t nlen = cm(nullptr, sk);
		if (nlen == 0 || nlen > 1024) { *why = "unsupported RSA key size"; return false; }
		/* Heap, not stack: a 4096-bit modulus is 512 B and this runs on the web
		 * handler's stack, which is already carrying the request. */
		uint8_t* n = (uint8_t*)malloc(nlen);
		if (!n) { *why = "out of memory while checking the key"; return false; }
		const bool got = cm(n, sk) == nlen;
		/* BearSSL writes the modulus minimally; a certificate may carry the
		 * DER sign-padding zero. Normalise both before comparing. */
		const uint8_t* cn = (const uint8_t*)pub->key.rsa.n; size_t cl = pub->key.rsa.nlen;
		while (cl > 1 && *cn == 0) { cn++; cl--; }
		const uint8_t* mn = n; size_t ml = nlen;
		while (ml > 1 && *mn == 0) { mn++; ml--; }
		const bool sameN = got && ml == cl && memcmp(mn, cn, ml) == 0;
		const size_t bits = ml * 8;
		free(n);
		if (!sameN) { *why = "the key does not belong to this certificate"; return false; }
		/* The modulus, and deliberately not the public exponent.
		 *
		 * br_rsa_compute_pubexp( ) recovers e from dp, and it can only do that
		 * when p = 3 mod 4 (src/rsa/rsa_i31_pubexp.c, the check right after the
		 * dp decode) — openssl does not choose primes with that in mind, so it
		 * answers 0 for roughly half of all legitimate keys. On the rig it did
		 * exactly that for an openssl RSA-2048 pair: e=0 against a certificate
		 * that plainly says 65537, and the pair was refused (2026-09-18, found
		 * by the bench matrix, which is why the matrix exists).
		 *
		 * What the modulus proves: this key and this certificate share the same
		 * n, which is what tells a wrong key file from the right one — the
		 * mistake an operator actually makes. What it does not prove: that d
		 * and e are inverses, which would take an RSA round trip (~1 s of
		 * RP2040) and would only catch a pair crafted to share a modulus and
		 * differ in the exponent. Nobody reaches this route without being admin
		 * already, so that pair is not an attack, it is a puzzle. */
		describe = String("RSA (") + (int)bits + " bits)";
		return true;
	}

	*why = "the private key is neither RSA nor EC";
	return false;
}

void WebManager::handleApiTls( ) {
	/* Equality and not a bit test — the gate /api/ota/apply uses, for the same
	 * reason: what this installs decides who the browser trusts from the next
	 * boot onwards. */
	if (getAuthPerms( ) != PERM_FULL_ADMIN) {
		_server->send(403, "application/json", "{\"error\":\"Forbidden — admin only\"}");
		return;
	}
	/* Two flash writes per call; same 5 s window /api/calib uses. */
	if (isRateLimited(5000)) {
		_server->sendHeader("Retry-After", "5");
		_server->send(429, "application/json", "{\"error\":\"rate limited\"}");
		return;
	}

	const String body = _server->hasArg("plain") ? _server->arg("plain") : String( );
	if (body.length( ) == 0) {
		_server->send(400, "application/json",
		              "{\"error\":\"empty body - send the certificate and key PEM blocks\"}");
		return;
	}
	/* 8 KB is what loadServerCert( ) accepts per file at boot. A body that
	 * could not be read back at boot is refused here instead of written. */
	if (body.length( ) > 8192) {
		_server->send(413, "application/json", "{\"error\":\"payload too large (8 KB)\"}");
		return;
	}

	const String certPem = pemCertChain(body);
	if (certPem.length( ) == 0) {
		_server->send(400, "application/json", "{\"error\":\"no CERTIFICATE block in the body\"}");
		return;
	}
	/* Which spelling the key uses is PemBlocks.h's problem; the encrypted
	 * case is named explicitly here because it is the one refusal an operator
	 * can act on without knowing anything about PEM. */
	bool encrypted = false;
	const String keyPem = pemPrivateKey(body, &encrypted);
	if (keyPem.length( ) == 0) {
		_server->send(400, "application/json", encrypted
		    ? "{\"error\":\"the private key is passphrase-encrypted; decrypt it first: openssl pkey -in key.pem -out plain.pem\"}"
		    : "{\"error\":\"no PRIVATE KEY block in the body\"}");
		return;
	}

	/* Parsed into the same two objects beginServer( ) builds at boot, so what
	 * is accepted here is what will load there. Scoped so both are freed
	 * before anything is written. */
	bool ok = false;
	String describe;
	const char* why = "the certificate or the key did not parse";
	{
		BearSSL::X509List certs(certPem.c_str( ));
		BearSSL::PrivateKey key(keyPem.c_str( ));
		if (certs.getCount( ) == 0)             why = "the certificate did not parse";
		else if (!key.isRSA( ) && !key.isEC( )) why = "the private key did not parse";
		else ok = tlsPairMatches(certs, key, &why, describe);
	}
	if (!ok) {
		LOG_CODE(LOG_WARN, "WEB", WEB_CERT_INVALID, 3, "refused by /api/tls");
		String msg = "{\"error\":\"";
		msg += why;
		msg += "\",\"installed\":false}";
		_server->send(400, "application/json", msg);
		return;
	}

	/* Both temporaries first, then both renames. A power cut between the two
	 * renames leaves a mismatched pair, which loadServerCert( ) survives: it
	 * logs WEB_CERT_INVALID and falls back to HTTP. Writing in place would
	 * instead risk losing a WORKING pair to a failed write, and that is the
	 * outcome worth engineering against.
	 *
	 * RenderGuard for the same reason the calibration POST carries one: a
	 * LittleFS write is a flash op, and a Core 1 XIP fetch that collides with
	 * it freezes the QSPI arbiter with the watchdog unfed. ReadGuard is the
	 * filesystem mutex — Core 1 writes history without asking anyone. */
	RenderGuard rg(_displayRef);
	bool wrote = false;
	{
		ReadGuard fsg(_storageRef);
		LittleFS.remove(FILE_WEB_CERT ".new");
		LittleFS.remove(FILE_WEB_KEY ".new");
		File cf = LittleFS.open(FILE_WEB_CERT ".new", "w");
		const bool cOk = cf && cf.print(certPem) == certPem.length( );
		if (cf) cf.close( );
		feedWatchdog( );
		File kf = LittleFS.open(FILE_WEB_KEY ".new", "w");
		const bool kOk = kf && kf.print(keyPem) == keyPem.length( );
		if (kf) kf.close( );
		feedWatchdog( );
		if (cOk && kOk) {
			LittleFS.remove(FILE_WEB_CERT);
			LittleFS.remove(FILE_WEB_KEY);
			wrote = LittleFS.rename(FILE_WEB_CERT ".new", FILE_WEB_CERT)
			     && LittleFS.rename(FILE_WEB_KEY ".new", FILE_WEB_KEY);
		} else {
			LittleFS.remove(FILE_WEB_CERT ".new");
			LittleFS.remove(FILE_WEB_KEY ".new");
		}
	}
	if (!wrote) {
		_server->send(500, "application/json",
		              "{\"error\":\"could not write the pair to /config\",\"installed\":false}");
		return;
	}

	LOG_CODE(LOG_INFO, "WEB", WEB_CERT_INSTALLED, (int16_t)certPem.length( ), describe.c_str( ));

	/* What the next boot will do, said plainly, because the operator cannot
	 * see it from here: the server object is built once, at begin( ), and this
	 * response is travelling over whichever one it chose. The AP caveat is not
	 * a footnote — loadServerCert( ) serves HTTP unconditionally in setup-AP
	 * mode, so an operator who installs a pair while on the AP and reboots
	 * still on the AP would otherwise read it as the install having failed. */
	String resp = "{\"ok\":true,\"installed\":true,\"key\":\"";
	resp += describe;
	resp += "\",\"serving\":\"";
	resp += _serverIsHttps ? "https" : "http";
	resp += "\",\"nextBoot\":\"https\",\"rebootRequired\":true,";
	resp += "\"note\":\"setup AP mode always serves HTTP\"}";
	_server->send(200, "application/json", resp);
}

#endif /* SIMUT_WEB_HTTPS */
