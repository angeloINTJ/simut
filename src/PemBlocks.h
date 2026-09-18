/**
 * @file PemBlocks.h
 * @brief Pulling the certificate and the key out of one PEM body.
 *
 * @details POST /api/tls takes both blocks concatenated, in either order,
 * because PEM already delimits itself and every extra layer — JSON, multipart,
 * two fields — is another parser standing between an operator and the recovery
 * they are trying to perform. What is left is this: find the markers.
 *
 * Header-only and free of everything but Arduino String, for the same reason
 * ScreenRle.h is: the native suite then compiles the very functions the
 * firmware ships, and the cases worth testing here — a missing END line, the
 * two blocks in the wrong order, an encrypted key, a chain — are exactly the
 * ones that are awkward to produce against a device and trivial to write down.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <Arduino.h>
#include <stdio.h>    /* snprintf */
#include <string.h>   /* strstr, strlen */

/**
 * @brief The PEM block for @p label, markers included.
 * @return Empty when the block is absent, or opened and never closed.
 *
 * Nothing here trims, re-wraps or validates base64: whatever sits between the
 * two markers is handed to BearSSL, which is the component that gets to decide
 * whether it is a certificate. A parser that "helpfully" fixed the payload
 * would be deciding that on its behalf, with less information.
 */
inline String pemBlock(const String& body, const char* label) {
	/* strstr over c_str( ) rather than String::indexOf(String): the host stub
	 * (test/native_stubs/Arduino.h) carries the char* overloads and not the
	 * String ones, and a header that is compiled by both sides has no business
	 * needing the difference. */
	char begin[48], end[48];
	snprintf(begin, sizeof(begin), "-----BEGIN %s-----", label);
	snprintf(end,   sizeof(end),   "-----END %s-----",   label);
	const char* s = body.c_str( );
	const char* b = strstr(s, begin);
	if (!b) return String( );
	const char* e = strstr(b, end);
	if (!e) return String( );
	return body.substring((size_t)(b - s), (size_t)(e - s) + strlen(end));
}

/**
 * @brief The certificate and anything sent with it — first BEGIN to last END.
 *
 * A chain arrives whole this way, and BearSSL's X509List parses the blocks in
 * order and treats the first as the leaf, which is the order openssl writes
 * them in. Taking only the first block instead would silently drop the
 * intermediates a real CA hands out, and the device would serve a chain the
 * browser cannot complete.
 */
inline String pemCertChain(const String& body) {
	static const char* B = "-----BEGIN CERTIFICATE-----";
	static const char* E = "-----END CERTIFICATE-----";
	const char* s = body.c_str( );
	const char* b = strstr(s, B);
	if (!b) return String( );
	/* The LAST end marker, walked by hand: String::lastIndexOf takes a char on
	 * the host stub, and "the last occurrence of a string" is four lines. */
	const char* e = nullptr;
	for (const char* p = strstr(b, E); p; p = strstr(p + 1, E)) e = p;
	if (!e) return String( );
	return body.substring((size_t)(b - s), (size_t)(e - s) + strlen(E));
}

/**
 * @brief The private key block, whichever of the three spellings it uses.
 * @param encrypted set when the body holds a passphrase-encrypted key, which
 *        is the one refusal worth naming: it parses as nothing, and the
 *        operator needs to be told to decrypt it rather than told that their
 *        key is invalid.
 *
 * PKCS#8 ("PRIVATE KEY") is tried first because it is what current openssl
 * writes by default; "EC PRIVATE KEY" and "RSA PRIVATE KEY" are the legacy
 * spellings. The order matters only for speed — the three markers cannot match
 * the same block, since a BEGIN line is matched whole.
 */
inline String pemPrivateKey(const String& body, bool* encrypted = nullptr) {
	if (encrypted) *encrypted = body.indexOf("-----BEGIN ENCRYPTED PRIVATE KEY-----") >= 0;
	String k = pemBlock(body, "PRIVATE KEY");
	if (k.length( ) == 0) k = pemBlock(body, "EC PRIVATE KEY");
	if (k.length( ) == 0) k = pemBlock(body, "RSA PRIVATE KEY");
	return k;
}
