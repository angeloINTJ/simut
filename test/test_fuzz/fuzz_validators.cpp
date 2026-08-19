/**
 * @file    test/test_fuzz/fuzz_validators.cpp
 * @brief   libFuzzer harness for the web-API input validators (issue #44).
 * @details Build & run via tools/run_fuzz.sh (clang++ -fsanitize=fuzzer,
 *          address,undefined against the native String stub).
 *
 * Why the oracle is NOT "it didn't crash": the validators in
 * SystemDefs_Validate.h are pure functions — no allocation, no recursion, no
 * unchecked index, null-guarded. Random bytes cannot crash them, so a
 * crash-only harness stays green for 60 s while measuring nothing (the
 * "shortcut that never fires looks like one that works" trap). What CAN rot
 * is the CONTRACT each caller relies on, so that is what every accepted
 * input is checked against:
 *
 *   1. parseIntStrict(s, out) == true   ⇒ strtoll of the same text parses
 *      fully, without ERANGE, fits int32, and equals out. (The pre-fix
 *      parser answered true for "2147483648" with out=2147483647 — atol
 *      saturation. This oracle is the one that would have caught it.)
 *   2. parseFloatStrict(s, out) == true ⇒ isfinite(out), and out equals
 *      (float)strtod of the same, fully-consumed text. (~40 digits saturate
 *      atof to ±inf; an inf that answers true poisons every later
 *      comparison.)
 *   3. isSafeUploadFilename(n) == true  ⇒ "/upload/" + n, resolved the way a
 *      filesystem resolves "." and "..", stays under /upload. This tests the
 *      SECURITY property (no traversal) as a black box, so a future
 *      "optimization" of the ".." check to something bypassable fails here.
 *      isSafeDirPath additionally may not let any HTML/JS-hostile byte
 *      through (finding M-7: a folder name is echoed into the /files
 *      listing).
 *
 * Plus two cheap differentials: isValidIpv4 re-derived from the dotted-quad
 * spec, and parseBoolStrict against its four documented spellings.
 *
 * PRE-REQUISITE, load-bearing: test/native_stubs/Arduino.h must mirror the
 * TARGET's numeric conversions. On the ferro (ArduinoCore-API),
 * String::toInt() is atol() — newlib strtol, 32-bit long, SATURATES on
 * overflow — and String::toFloat() is float(atof()), which overflows to
 * ±inf. The stub used to wrap std::stol/std::stof in try/catch and answer
 * 0/0.0f, i.e. it diverged from the hardware on exactly the inputs a fuzzer
 * finds first. Fuzzing over the old stub would have described the stub, not
 * the firmware. test_validators pins the stub's semantics with golden
 * vectors; this harness relies on them.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <math.h>
#include <string>
#include <vector>

#include <Arduino.h>            /* native stub — target-faithful toInt/toFloat */
#include "SystemDefs_Validate.h"
#include "ParseFloat.h"

namespace simut_native { uint32_t fake_millis_value = 0; }

[[noreturn]] static void oracleFail(const char* which, const char* input) {
	fprintf(stderr, "\nORACLE FAILED: %s\ninput: \"", which);
	for (const unsigned char* p = (const unsigned char*)input; *p; p++)
		fprintf(stderr, (*p >= 32 && *p < 127) ? "%c" : "\\x%02x", (int)*p);
	fprintf(stderr, "\"\n");
	abort();
}

/* Resolve base + "/" + name the way a filesystem would (empty and "."
 * segments skipped, ".." pops) and answer whether the result never climbs
 * above base. Deliberately independent of the validators' own ".." logic —
 * this is the property, not the implementation. */
static bool resolveStaysUnder(const char* base, const char* name) {
	std::vector<std::string> stack;
	auto walk = [&stack](const char* p, size_t floorLen) -> bool {
		std::string seg;
		for (const char* q = p; ; q++) {
			if (*q == '/' || *q == '\0') {
				if (seg == "..") {
					if (stack.size() <= floorLen) return false; /* escaped */
					stack.pop_back();
				} else if (!seg.empty() && seg != ".") {
					stack.push_back(seg);
				}
				seg.clear();
				if (*q == '\0') break;
			} else {
				seg += *q;
			}
		}
		return true;
	};
	if (!walk(base, 0)) return false;
	return walk(name, stack.size());
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	if (size > 4096) return 0; /* longest validator domain is 96 + headroom */

	/* The device's HTTP/CLI layers hand the validators C strings, so the
	 * harness does too: an embedded NUL truncates here exactly as it would
	 * on the ferro. */
	const std::string owned((const char*)data, size);
	const char* txt = owned.c_str();
	const String s(txt);

	/* ---- oracle 1: parseIntStrict ------------------------------------ */
	int iv = 0;
	if (parseIntStrict(s, iv)) {
		errno = 0;
		char* end = nullptr;
		const long long ref = strtoll(txt, &end, 10);
		if (errno == ERANGE)
			oracleFail("parseIntStrict accepted a value strtoll flags ERANGE on", txt);
		if (*end != '\0')
			oracleFail("parseIntStrict accepted text strtoll does not fully consume", txt);
		if (ref < -2147483648LL || ref > 2147483647LL)
			oracleFail("parseIntStrict accepted a value outside int32", txt);
		if ((long long)iv != ref)
			oracleFail("parseIntStrict out != the number written", txt);
	}

	/* ---- oracle 2: parseFloatStrict ---------------------------------- */
	float fv = 0.0f;
	if (parseFloatStrict(s, fv)) {
		if (!isfinite(fv))
			oracleFail("parseFloatStrict answered true with a non-finite out", txt);
		char* end = nullptr;
		const float ref = (float)strtod(txt, &end);
		if (*end != '\0')
			oracleFail("parseFloatStrict accepted text strtod does not fully consume", txt);
		/* ref non-finite with fv finite = toFloat() disagreed with atof —
		 * that is the STUB lying about the target again, not the firmware. */
		if (!isfinite(ref))
			oracleFail("parseFloatStrict accepted a value atof saturates to inf", txt);
		if (ref != fv)
			oracleFail("parseFloatStrict out != the number written", txt);
	}

	/* ---- oracle 3: path validators are traversal-proof --------------- */
	if (isSafeUploadFilename(txt)) {
		const char* n = (txt[0] == '/') ? txt + 1 : txt; /* validator strips it too */
		const size_t nlen = strlen(n);
		if (nlen == 0 || nlen > UPLOAD_FILENAME_MAX)
			oracleFail("isSafeUploadFilename length contract broken", txt);
		if (!resolveStaysUnder("/upload", n))
			oracleFail("isSafeUploadFilename allowed a path escape", txt);
	}
	if (isSafeDirPath(txt)) {
		if (!resolveStaysUnder("/", txt))
			oracleFail("isSafeDirPath allowed a path escape", txt);
		/* finding M-7: the name is echoed into the /files listing, so the
		 * allowlist must never admit an HTML/JS-hostile byte. */
		for (const unsigned char* p = (const unsigned char*)txt; *p; p++) {
			if (*p < 32 || *p == 127 || strchr("<>\"'&`%\\:|?*", (char)*p))
				oracleFail("isSafeDirPath admitted an HTML/JS-hostile byte", txt);
		}
	}

	/* ---- differential: isValidIpv4 vs the dotted-quad spec ----------- */
	if (isValidIpv4(txt)) {
		int dots = 0;
		for (const char* p = txt; *p; p++) {
			if (*p == '.') dots++;
			else if (*p < '0' || *p > '9')
				oracleFail("isValidIpv4 accepted a non-digit, non-dot byte", txt);
		}
		if (dots != 3)
			oracleFail("isValidIpv4 accepted something not made of 4 parts", txt);
		const char* p = txt;
		for (int part = 0; part < 4; part++) {
			char* end = nullptr;
			const long v = strtol(p, &end, 10);
			if (end == p)
				oracleFail("isValidIpv4 accepted an empty octet", txt);
			if (v > 255)
				oracleFail("isValidIpv4 accepted an octet above 255", txt);
			p = (*end == '.') ? end + 1 : end;
		}
	}

	/* ---- differential: parseBoolStrict vs its 4 documented spellings - */
	bool bv = false;
	if (parseBoolStrict(s, bv)) {
		const bool isTrue  = (strcmp(txt, "1") == 0) || (strcasecmp(txt, "true") == 0);
		const bool isFalse = (strcmp(txt, "0") == 0) || (strcasecmp(txt, "false") == 0);
		if (!isTrue && !isFalse)
			oracleFail("parseBoolStrict accepted an unknown spelling", txt);
		if (bv != isTrue)
			oracleFail("parseBoolStrict mapped a spelling to the wrong value", txt);
	}

	/* ---- crash/UBSan coverage for the rest of the family ------------- */
	(void)isValidName(txt);
	(void)isValidName(txt, 8);
	(void)isValidCfgString(txt, 31);
	(void)isValidCfgString(txt, 0);
	(void)passwordPolicyOk(txt);
	(void)asciiEqualsCi(txt, "true");
	(void)isInRange(iv, -100, 100);
	/* ParseFloat.h's parseFloat: no finiteness contract (out-of-domain input
	 * may yield inf/NaN); every caller range-checks, and NaN fails any
	 * comparison. Here it is exercised for crashes/UB only. */
	(void)parseFloat(txt);

	return 0;
}
