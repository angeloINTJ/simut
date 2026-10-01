/**
 * @file    test/test_ota_sig/test_main.cpp
 * @brief   Host tests for the OTA signature check (src/ota/signature.cpp).
 * @details Runs via `pio test -e native_otasig`. The vectors come from
 *          `tools/ota_sign.py vectors`, signed with a fixed TEST key set that no
 *          image trusts. The device binds BearSSL; here SHA-256 is a plain
 *          reference (checked first against FIPS 180-4 vectors), and ECDSA is
 *          answered from the list of genuine signatures the tool verified with
 *          real ECDSA: true for exactly those (key, digest, signature) triples.
 *          So a module that hashes the wrong bytes, in the wrong order or with
 *          the wrong domain, cannot match a genuine signature and fails.
 *
 *          The cases are the ones `tools/ota_sign.py selftest` runs against the
 *          Python reference, with the same expected refusals: the two
 *          implementations agree case by case.
 *
 * @project SIMUT — signed OTA (docs/analysis/OTA_ASSINADA.md)
 * @license MIT License
 */

#include <unity.h>
#include <string.h>
#include <vector>
#include "ota/signature.h"
#include "sha256_ref.h"
#include "vectors.h"

using ota::SigVerdict;

/* ----- the injected crypto ----- */
static Sha256Ref g_hash;
static unsigned g_verifyCalls = 0;

static bool vecVerify(const uint8_t pub[65], const uint8_t digest[32], const uint8_t sig[64]) {
	g_verifyCalls++;
	for (const VecSig& v : kVecValidSigs) {
		if (memcmp(v.pub, pub, 65) == 0 && memcmp(v.digest, digest, 32) == 0 && memcmp(v.sig, sig, 64) == 0) {
			return true;
		}
	}
	return false;
}

static const ota::SigCrypto kCrypto = { &g_hash, sha256RefInit, sha256RefUpdate, sha256RefOut, vecVerify };

/* ----- the staged bytes ----- */
struct Staged {
	std::vector<uint8_t> bytes;
	unsigned reads = 0;
	bool outOfRange = false;
};

static bool readStaged(void* src, uint32_t off, uint8_t* buf, uint32_t len) {
	Staged* s = static_cast<Staged*>(src);
	s->reads++;
	if ((uint64_t)off + len > s->bytes.size()) {
		s->outOfRange = true;
		return false;
	}
	memcpy(buf, s->bytes.data() + off, len);
	return true;
}

static Staged staged(const uint8_t* p, size_t n) {
	Staged s;
	s.bytes.assign(p, p + n);
	return s;
}

static Staged flipped(const uint8_t* p, size_t n, size_t at) {
	Staged s = staged(p, n);
	s.bytes[at] ^= 0x01;
	return s;
}

/* ----- the policies: a release image trusts the release root only; a bench
 * image trusts both roots ----- */
static const ota::SigAnchor kReleaseAnchors[] = { { kVecRootPub, ota::SIG_SCOPE_RELEASE } };
static const ota::SigAnchor kBenchAnchors[] = { { kVecRootPub, ota::SIG_SCOPE_RELEASE },
                                                { kVecBenchRootPub, ota::SIG_SCOPE_BENCH } };

static ota::SigPolicy releasePolicy( ) {
	ota::SigPolicy p;
	memset(&p, 0, sizeof(p));
	p.anchors = kReleaseAnchors;
	p.anchorCount = 1;
	p.runningEnv = "pico_w_test";
	return p;
}

static ota::SigPolicy benchPolicy( ) {
	ota::SigPolicy p = releasePolicy( );
	p.anchors = kBenchAnchors;
	p.anchorCount = 2;
	return p;
}

static SigVerdict run(Staged& s, const ota::SigPolicy& p) {
	const ota::SigReport r = ota::sigCheck((uint32_t)s.bytes.size( ), readStaged, &s, p, kCrypto);
	TEST_ASSERT_FALSE_MESSAGE(s.outOfRange, "the check read past the staged bytes");
	return r.verdict;
}

static const size_t kGoodLen = sizeof(kVecGood);
static const size_t kN = kVecImageLen;   /* where the trailer starts */

void setUp(void) { g_verifyCalls = 0; }
void tearDown(void) { }

/* ============================================================================
 *  The reference SHA-256 first: everything below rests on it.
 * ============================================================================ */

static void test_sha256_reference_matches_fips(void) {
	static const uint8_t abc[32] = {
		0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad };
	static const uint8_t two[32] = {   /* "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" */
		0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
		0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1 };
	uint8_t out[32];
	Sha256Ref c;
	sha256RefInit(&c);
	sha256RefUpdate(&c, "abc", 3);
	sha256RefOut(&c, out);
	TEST_ASSERT_EQUAL_MEMORY(abc, out, 32);
	const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	sha256RefInit(&c);
	sha256RefUpdate(&c, m, 20);                 /* in two pieces: the buffering path */
	sha256RefUpdate(&c, m + 20, strlen(m) - 20);
	sha256RefOut(&c, out);
	TEST_ASSERT_EQUAL_MEMORY(two, out, 32);
}

/* ============================================================================
 *  What is accepted
 * ============================================================================ */

static void test_a_signed_image_is_accepted(void) {
	Staged s = staged(kVecGood, kGoodLen);
	const ota::SigReport r = ota::sigCheck((uint32_t)s.bytes.size( ), readStaged, &s, releasePolicy( ), kCrypto);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::OK, (uint8_t)r.verdict);
	TEST_ASSERT_EQUAL_UINT32(3, r.securityVersion);
	TEST_ASSERT_EQUAL_UINT32(5, r.serial);
	TEST_ASSERT_EQUAL_UINT8(ota::SIG_SCOPE_RELEASE, r.scope);
	TEST_ASSERT_EQUAL_UINT32(kVecImageLen, r.imageLen);
	TEST_ASSERT_EQUAL_STRING("pico_w_test", r.env);
	TEST_ASSERT_EQUAL_UINT(2, g_verifyCalls);   /* the certificate, then the image */
}

static void test_a_bench_signed_image_is_accepted_by_a_bench_image(void) {
	Staged s = staged(kVecBench, sizeof(kVecBench));
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::OK, (uint8_t)run(s, benchPolicy( )));
}

static void test_the_minimums_themselves_are_accepted(void) {
	ota::SigPolicy p = releasePolicy( );
	p.minSerial[ota::SIG_SCOPE_RELEASE] = 5;
	p.minSecurityVersion = 3;
	Staged s = staged(kVecGood, kGoodLen);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::OK, (uint8_t)run(s, p));
}

/* ============================================================================
 *  Unsigned, or not a trailer at all: 8
 * ============================================================================ */

static void test_an_unsigned_image_is_missing(void) {
	Staged s = staged(kVecGood, kN);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::MISSING, (uint8_t)run(s, releasePolicy( )));
	TEST_ASSERT_EQUAL_UINT(0, g_verifyCalls);
}

static void test_a_broken_magic_is_missing(void) {
	Staged s = flipped(kVecGood, kGoodLen, kGoodLen - 1);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::MISSING, (uint8_t)run(s, releasePolicy( )));
}

static void test_a_byte_after_the_trailer_is_missing(void) {
	Staged s = staged(kVecGood, kGoodLen);
	s.bytes.push_back(0);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::MISSING, (uint8_t)run(s, releasePolicy( )));
}

static void test_fewer_bytes_than_a_trailer_is_missing(void) {
	Staged s = staged(kVecGood + kGoodLen - 100, 100);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::MISSING, (uint8_t)run(s, releasePolicy( )));
	TEST_ASSERT_EQUAL_UINT(0, s.reads);
}

/* ============================================================================
 *  One bit changed anywhere: 9
 * ============================================================================ */

static void test_the_trailer_length_and_format_are_checked(void) {
	Staged a = flipped(kVecGood, kGoodLen, kGoodLen - 16);   /* trailer_len */
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(a, releasePolicy( )));
	Staged b = flipped(kVecGood, kGoodLen, kGoodLen - 12);   /* format */
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(b, releasePolicy( )));
}

static void test_a_bit_of_the_image_is_invalid(void) {
	Staged s = flipped(kVecGood, kGoodLen, 100);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )));
}

static void test_the_last_byte_of_the_image_is_covered(void) {
	Staged s = flipped(kVecGood, kGoodLen, kN - 1);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )));
}

static void test_every_signed_trailer_field_is_covered(void) {
	/* security_version, env, image_len, certificate serial, certificate key,
	 * root signature, image signature */
	const size_t at[] = { kN + 0, kN + 5, kN + 20, kN + 24, kN + 40, kN + 100, kN + 170 };
	for (size_t off : at) {
		Staged s = flipped(kVecGood, kGoodLen, off);
		TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )),
		                                "a flipped trailer byte was accepted");
	}
}

static void test_bytes_between_the_image_and_the_trailer_are_invalid(void) {
	/* The signature covers image_len bytes; anything slipped in after them and
	 * before the trailer would go to flash unsigned. */
	Staged s = staged(kVecGood, kGoodLen);
	s.bytes.insert(s.bytes.begin( ) + kN, 7, 0x00);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )));
}

static void test_a_release_certificate_from_the_bench_root_is_invalid(void) {
	/* A bench image trusts the bench root, but only for bench signers. A
	 * certificate that says "release" and carries the bench root's genuine
	 * signature must not pass as a release signer. */
	Staged s = staged(kVecScopeForged, sizeof(kVecScopeForged));
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, benchPolicy( )));
}

static void test_an_env_with_no_terminator_is_invalid(void) {
	/* Sixteen env bytes, signed for real: the signature holds, so only the
	 * format check can refuse it. */
	Staged s = staged(kVecEnvFull, sizeof(kVecEnvFull));
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )));
}

static void test_a_root_nobody_trusts_is_invalid(void) {
	Staged s = staged(kVecRogue, sizeof(kVecRogue));
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, releasePolicy( )));
}

/* ============================================================================
 *  The policy: 12, 10, 11, 7
 * ============================================================================ */

static void test_a_bench_key_on_a_release_image_is_out_of_scope(void) {
	Staged s = staged(kVecBench, sizeof(kVecBench));
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::SCOPE, (uint8_t)run(s, releasePolicy( )));
	TEST_ASSERT_EQUAL_UINT(0, g_verifyCalls);   /* refused before any signature work */
}

static void test_a_signer_below_the_lowest_serial_is_revoked(void) {
	ota::SigPolicy p = releasePolicy( );
	p.minSerial[ota::SIG_SCOPE_RELEASE] = 6;
	Staged s = staged(kVecGood, kGoodLen);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::REVOKED, (uint8_t)run(s, p));
}

static void test_the_bench_serial_does_not_revoke_release_signers(void) {
	/* The rig takes release candidates and bench builds: each scope keeps its
	 * own lowest serial, or the first CI candidate would revoke the bench key. */
	ota::SigPolicy p = benchPolicy( );
	p.minSerial[ota::SIG_SCOPE_BENCH] = 99;
	Staged s = staged(kVecGood, kGoodLen);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::OK, (uint8_t)run(s, p));
}

static void test_an_older_security_version_is_a_rollback(void) {
	ota::SigPolicy p = releasePolicy( );
	p.minSecurityVersion = 4;
	Staged s = staged(kVecGood, kGoodLen);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::ROLLBACK, (uint8_t)run(s, p));
}

static void test_an_image_signed_for_another_variant_is_refused(void) {
	ota::SigPolicy p = releasePolicy( );
	p.runningEnv = "pico_w_release";
	Staged s = staged(kVecGood, kGoodLen);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::ENV, (uint8_t)run(s, p));
}

static void test_a_tampered_image_is_invalid_before_it_is_a_rollback(void) {
	/* The fields of an image that does not verify are not trusted: a forged
	 * low security version must read as a bad signature, not as a rollback. */
	ota::SigPolicy p = releasePolicy( );
	p.minSecurityVersion = 4;
	Staged s = flipped(kVecGood, kGoodLen, 100);
	TEST_ASSERT_EQUAL_UINT8((uint8_t)SigVerdict::INVALID, (uint8_t)run(s, p));
}

int main(int, char**) {
	UNITY_BEGIN( );
	RUN_TEST(test_sha256_reference_matches_fips);

	RUN_TEST(test_a_signed_image_is_accepted);
	RUN_TEST(test_a_bench_signed_image_is_accepted_by_a_bench_image);
	RUN_TEST(test_the_minimums_themselves_are_accepted);

	RUN_TEST(test_an_unsigned_image_is_missing);
	RUN_TEST(test_a_broken_magic_is_missing);
	RUN_TEST(test_a_byte_after_the_trailer_is_missing);
	RUN_TEST(test_fewer_bytes_than_a_trailer_is_missing);

	RUN_TEST(test_the_trailer_length_and_format_are_checked);
	RUN_TEST(test_a_bit_of_the_image_is_invalid);
	RUN_TEST(test_the_last_byte_of_the_image_is_covered);
	RUN_TEST(test_every_signed_trailer_field_is_covered);
	RUN_TEST(test_bytes_between_the_image_and_the_trailer_are_invalid);
	RUN_TEST(test_a_release_certificate_from_the_bench_root_is_invalid);
	RUN_TEST(test_an_env_with_no_terminator_is_invalid);
	RUN_TEST(test_a_root_nobody_trusts_is_invalid);

	RUN_TEST(test_a_bench_key_on_a_release_image_is_out_of_scope);
	RUN_TEST(test_a_signer_below_the_lowest_serial_is_revoked);
	RUN_TEST(test_the_bench_serial_does_not_revoke_release_signers);
	RUN_TEST(test_an_older_security_version_is_a_rollback);
	RUN_TEST(test_an_image_signed_for_another_variant_is_refused);
	RUN_TEST(test_a_tampered_image_is_invalid_before_it_is_a_rollback);
	return UNITY_END( );
}
