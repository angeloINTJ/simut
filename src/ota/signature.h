/**
 * @file    src/ota/signature.h
 * @brief   The signature check of a staged OTA image (docs/analysis/OTA_ASSINADA.md).
 *
 * @details A signed image is the unchanged .bin followed by a 241-byte trailer
 *          (layout in tools/ota_sign.py, which writes it). sigCheck( ) reads the
 *          trailer from the end of the staged bytes and decides, in this order:
 *            1. a footer `SIMUTSIG` at the very end, or the image is unsigned (8);
 *            2. the trailer's own shape: length, format, image_len, env (9);
 *            3. a root this image trusts for the certificate's scope (12);
 *            4. the root's signature over the certificate (9);
 *            5. the certificate's serial against the lowest still accepted (10);
 *            6. the signer's signature over the image and the trailer (9);
 *            7. the security version against the installed minimum (11);
 *            8. the signed env against the running variant (7).
 *          The numbers are the v= of the stage reply: 7 is the refusal
 *          validation.cpp already gives a wrong variant, 8..12 are new.
 *
 *          No Arduino and no BearSSL here: SHA-256 and the ECDSA P-256 check
 *          come in through SigCrypto, so test/test_ota_sig drives this exact
 *          code on the host against vectors from tools/ota_sign.py, and the
 *          device binds BearSSL (already linked for TLS).
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ota {

constexpr uint32_t SIG_TRAILER_LEN   = 241;
constexpr uint16_t SIG_FORMAT_V1     = 1;
constexpr uint32_t SIG_CERT_BODY_LEN = 73;   /* serial, scope, reserved, signer public key */
constexpr uint32_t SIG_SIGNED_PREFIX = 161;  /* every trailer byte before the image signature */
constexpr uint8_t  SIG_SCOPE_RELEASE = 1;
constexpr uint8_t  SIG_SCOPE_BENCH   = 2;

/** The verdict, numbered as the v= of the stage reply. */
enum class SigVerdict : uint8_t {
	OK       = 0,
	ENV      = 7,   /**< signed for another variant: ValidationStatus::ENV_MISMATCH */
	MISSING  = 8,   /**< no trailer: an unsigned image */
	INVALID  = 9,   /**< malformed trailer, or a signature that does not verify */
	REVOKED  = 10,  /**< a signer whose serial is below the lowest still accepted */
	ROLLBACK = 11,  /**< a security version below the installed minimum */
	SCOPE    = 12,  /**< no trusted root for the certificate's scope (bench key, release image) */
};

/** A root this image trusts, and the scope of the signers it may certify. */
struct SigAnchor {
	const uint8_t* pub;   /**< 65 bytes, uncompressed P-256 point */
	uint8_t scope;
};

struct SigPolicy {
	const SigAnchor* anchors;
	uint8_t anchorCount;
	uint32_t minSerial[3];       /**< by scope: [SIG_SCOPE_RELEASE], [SIG_SCOPE_BENCH]; [0] unused */
	uint32_t minSecurityVersion;
	const char* runningEnv;      /**< this image's env name, without the SIMUT-ENV: prefix */
};

/** SHA-256 and ECDSA P-256, injected. */
struct SigCrypto {
	void* hashCtx;
	void (*hashInit)(void* ctx);
	void (*hashUpdate)(void* ctx, const void* data, size_t len);
	void (*hashOut)(void* ctx, uint8_t out[32]);
	bool (*verify)(const uint8_t pub[65], const uint8_t digest[32], const uint8_t sig[64]);
};

/** Reads `len` staged bytes at `off`; false on a read failure. */
typedef bool (*SigRead)(void* src, uint32_t off, uint8_t* buf, uint32_t len);

struct SigReport {
	SigVerdict verdict;
	uint32_t securityVersion;
	uint32_t serial;
	uint8_t scope;
	uint32_t imageLen;
	char env[17];
};

/** Decide on the `stagedLen` bytes that `read` reaches. Fields of the report
 *  are filled as far as the trailer was read. */
SigReport sigCheck(uint32_t stagedLen, SigRead read, void* src,
                   const SigPolicy& pol, const SigCrypto& crypto);

}  // namespace ota
