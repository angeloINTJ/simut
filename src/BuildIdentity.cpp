/**
 * @file BuildIdentity.cpp
 * @brief The variant tag in .rodata and the scanner that reads one back.
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "BuildIdentity.h"
#include <string.h>

/* `used` keeps the compiler from dropping it; the reference from
 * ota_validate_staging( ) keeps the linker from garbage-collecting the
 * section. Both are needed: a tag that exists only in the source is exactly
 * the kind of promise a .bin cannot keep. */
const char SIMUT_ENV_TAG[] __attribute__((used)) =
    SIMUT_ENV_TAG_PREFIX SIMUT_ENV_NAME ";v=" SIMUT_VERSION ";";

bool simut_env_tag_scan(const unsigned char* buf, unsigned len, char* out, unsigned outLen) {
    static const unsigned char pfx[] = SIMUT_ENV_TAG_PREFIX;
    const unsigned plen = sizeof(pfx) - 1;
    if (!buf || !out || outLen == 0 || len < plen + 1) return false;
    for (unsigned i = 0; i + plen < len; i++) {
        if (memcmp(buf + i, pfx, plen) != 0) continue;
        unsigned j = i + plen, k = 0;
        while (j < len && buf[j] != ';' && k + 1 < outLen) {
            unsigned char c = buf[j];
            /* The env name is [a-z]; anything else is a coincidence in the
             * binary, not a tag. */
            if (c < 'a' || c > 'z') { k = 0; break; }
            out[k++] = (char)c;
            j++;
        }
        if (k && j < len && buf[j] == ';') { out[k] = '\0'; return true; }
    }
    out[0] = '\0';
    return false;
}
