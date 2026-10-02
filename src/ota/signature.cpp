/**
 * @file    src/ota/signature.cpp
 * @brief   The signature check of a staged OTA image. See signature.h for the
 *          order of the checks and docs/analysis/OTA_ASSINADA.md for why.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "signature.h"

#include <string.h>

namespace ota {

namespace {

/* Domain separation: a certificate signature can never pass for an image
 * signature, nor either one for a signature made for anything else. */
constexpr char CERT_DOMAIN[]  = "SIMUT-OTA-CERT-v1";
constexpr char IMAGE_DOMAIN[] = "SIMUT-OTA-IMG-v1";
constexpr char MAGIC[8]       = { 'S', 'I', 'M', 'U', 'T', 'S', 'I', 'G' };
constexpr char TRUST_MAGIC[8] = { 'S', 'I', 'M', 'U', 'T', 'K', 'E', 'Y' };
constexpr uint8_t TRUST_FORMAT_V1 = 1;

/* Trailer offsets (tools/ota_sign.py has the table). */
constexpr uint32_t T_SECVER  = 0;
constexpr uint32_t T_ENV     = 4;
constexpr uint32_t T_IMGLEN  = 20;
constexpr uint32_t T_CERT    = 24;
constexpr uint32_t T_IMGSIG  = 161;
constexpr uint32_t T_FOOTER  = 225;
constexpr uint32_t C_SERIAL  = 0;
constexpr uint32_t C_SCOPE   = 4;
constexpr uint32_t C_PUB     = 8;
constexpr uint32_t C_ROOTSIG = 73;
constexpr uint32_t ENV_LEN   = 16;

uint32_t le32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t le16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

}  // namespace

SigReport sigCheck(uint32_t stagedLen, SigRead read, void* src,
                   const SigPolicy& pol, const SigCrypto& crypto,
                   uint8_t* chunk, uint32_t chunkLen) {
	SigReport r;
	memset(&r, 0, sizeof(r));
	r.verdict = SigVerdict::MISSING;
	if (chunk == nullptr || chunkLen == 0) {
		r.verdict = SigVerdict::INVALID;   /* nothing to read the image through: no verdict to give */
		return r;
	}

	/* 1. The footer, or an unsigned image. One byte of image at least. */
	if (stagedLen < SIG_TRAILER_LEN + 1) return r;
	uint8_t t[SIG_TRAILER_LEN];
	if (!read(src, stagedLen - SIG_TRAILER_LEN, t, SIG_TRAILER_LEN)) {
		r.verdict = SigVerdict::INVALID;
		return r;
	}
	if (memcmp(t + SIG_TRAILER_LEN - sizeof(MAGIC), MAGIC, sizeof(MAGIC)) != 0) return r;

	/* 2. The trailer's own shape. */
	r.verdict = SigVerdict::INVALID;
	if (le32(t + T_FOOTER) != SIG_TRAILER_LEN || le16(t + T_FOOTER + 4) != SIG_FORMAT_V1) return r;
	const uint8_t* cert = t + T_CERT;
	r.securityVersion = le32(t + T_SECVER);
	r.imageLen = le32(t + T_IMGLEN);
	r.serial = le32(cert + C_SERIAL);
	r.scope = cert[C_SCOPE];
	if (r.imageLen != stagedLen - SIG_TRAILER_LEN) return r;
	if (memchr(t + T_ENV, 0, ENV_LEN) == nullptr) return r;
	if (cert[5] != 0 || cert[6] != 0 || cert[7] != 0) return r;
	memcpy(r.env, t + T_ENV, ENV_LEN);
	r.env[ENV_LEN] = '\0';

	/* 3. A root this image trusts for that scope. */
	bool anyRoot = false;
	for (uint8_t i = 0; i < pol.anchorCount; i++) {
		if (pol.anchors[i].scope == r.scope) anyRoot = true;
	}
	if (!anyRoot) {
		r.verdict = SigVerdict::SCOPE;
		return r;
	}

	/* 4. The root's signature over the certificate. */
	uint8_t digest[32];
	crypto.hashInit(crypto.hashCtx);
	crypto.hashUpdate(crypto.hashCtx, CERT_DOMAIN, sizeof(CERT_DOMAIN) - 1);
	crypto.hashUpdate(crypto.hashCtx, cert, SIG_CERT_BODY_LEN);
	crypto.hashOut(crypto.hashCtx, digest);
	bool certOk = false;
	for (uint8_t i = 0; i < pol.anchorCount && !certOk; i++) {
		if (pol.anchors[i].scope != r.scope) continue;
		certOk = crypto.verify(pol.anchors[i].pub, digest, cert + C_ROOTSIG);
	}
	if (!certOk) return r;

	/* 5. A signer no older than the lowest serial still accepted. */
	if (r.scope < 3 && r.serial < pol.minSerial[r.scope]) {
		r.verdict = SigVerdict::REVOKED;
		return r;
	}

	/* 6. The signer's signature over the image and every trailer byte before it. */
	crypto.hashInit(crypto.hashCtx);
	crypto.hashUpdate(crypto.hashCtx, IMAGE_DOMAIN, sizeof(IMAGE_DOMAIN) - 1);
	for (uint32_t off = 0; off < r.imageLen; off += chunkLen) {
		const uint32_t n = (r.imageLen - off) < chunkLen ? (r.imageLen - off) : chunkLen;
		if (!read(src, off, chunk, n)) return r;
		crypto.hashUpdate(crypto.hashCtx, chunk, n);
	}
	crypto.hashUpdate(crypto.hashCtx, t, SIG_SIGNED_PREFIX);
	crypto.hashOut(crypto.hashCtx, digest);
	if (!crypto.verify(cert + C_PUB, digest, t + T_IMGSIG)) return r;

	/* 7. No older than what is installed. */
	if (r.securityVersion < pol.minSecurityVersion) {
		r.verdict = SigVerdict::ROLLBACK;
		return r;
	}

	/* 8. Signed for the variant that is running. */
	if (pol.runningEnv == nullptr || strcmp(r.env, pol.runningEnv) != 0) {
		r.verdict = SigVerdict::ENV;
		return r;
	}

	r.verdict = SigVerdict::OK;
	return r;
}

bool sigTrustParse(const uint8_t* blk, uint32_t len, SigAnchor* anchors, uint8_t maxAnchors,
                   SigPolicy& pol) {
	if (blk == nullptr || anchors == nullptr || len < SIG_TRUST_HEAD_LEN) return false;
	if (memcmp(blk, TRUST_MAGIC, sizeof(TRUST_MAGIC)) != 0 || blk[8] != TRUST_FORMAT_V1) return false;
	const uint8_t n = blk[9];
	if (n == 0 || n > SIG_TRUST_MAX_ANCHORS || n > maxAnchors) return false;
	if (blk[10] != 0 || blk[11] != 0) return false;
	if (len != SIG_TRUST_HEAD_LEN + SIG_TRUST_ANCHOR_LEN * n) return false;
	for (uint8_t i = 0; i < n; i++) {
		const uint8_t* a = blk + SIG_TRUST_HEAD_LEN + SIG_TRUST_ANCHOR_LEN * i;
		if ((a[0] != SIG_SCOPE_RELEASE && a[0] != SIG_SCOPE_BENCH) || a[1] != 0x04) return false;
		anchors[i].scope = a[0];
		anchors[i].pub = a + 1;
	}
	pol.anchors = anchors;
	pol.anchorCount = n;
	pol.minSecurityVersion = le32(blk + 12);
	pol.minSerial[0] = 0;
	pol.minSerial[SIG_SCOPE_RELEASE] = le32(blk + 16);
	pol.minSerial[SIG_SCOPE_BENCH] = le32(blk + 20);
	return true;
}

}  // namespace ota
