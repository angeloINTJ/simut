/**
 * @file BuildIdentity.h
 * @brief What this image is: variant, version, and the tag a fleet manager
 *        looks for inside a .bin before staging it.
 * @author  Ângelo Moisés Alves
 * @license MIT License
 *
 * @details The tag is a plain string in .rodata:
 *
 *     SIMUT-ENV:<env>;v=<version>;
 *
 * so a client can scan the file it is about to upload and refuse it before
 * a single byte reaches the device — and the device itself scans the staged
 * image (ota_validate_staging) so a client that did not check still cannot
 * stage a variant this hardware cannot run. An image without a tag is an
 * older build: accepted, and reported as env "" so the caller knows the
 * check did not happen.
 */
#pragma once
#include "simut_config.h"
#include "SystemDefs_Limits.h"

#define SIMUT_ENV_TAG_PREFIX "SIMUT-ENV:"
#define SIMUT_ENV_TAG_MAX    32   /* prefix + env + ";v=" + version + ";" */

/** The tag itself. Referenced by the staging validator, so the linker keeps it. */
extern const char SIMUT_ENV_TAG[];

/** "release" | "alpha" | "air" — the variant this image was built for. */
inline const char* simut_env_name( ) { return SIMUT_ENV_NAME; }

/**
 * Finds a SIMUT-ENV tag in a byte window and copies its env name.
 * @return true when a tag was found and `out` holds the env (NUL-terminated).
 * Pure, so the host tests can pin it.
 */
bool simut_env_tag_scan(const unsigned char* buf, unsigned len, char* out, unsigned outLen);
