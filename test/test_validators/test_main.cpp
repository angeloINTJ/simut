/**
 * @file    test/test_validators/test_main.cpp
 * @brief   EXT-009 (F-BUILD) — host-side unit tests dos validators puros.
 * @details Roda via `pio test -e native` (sem HW). Cobre:
 *            · isValidIpv4, isSafeUploadFilename, isValidName, isValidCfgString,
 *              isInRange  (de SystemDefs_Validate.h, incluído diretamente)
 *            · parseIntStrict  (idem, usa Arduino String stubada em native_stubs/)
 *            · timeReached, timeSince  (de SystemDefs_Time.h, millis() stubado)
 *            · dallasCrc8  (copiado de SystemUtils.cpp — função pequena, evita
 *              compilar SystemUtils.cpp inteiro com suas deps)
 *            · floatToI16, i16ToFloat  (copiados de SystemDefs_Records.h —
 *              originalmente static inline dentro de BinaryHistoryRecord, copiar
 *              evita pular o resto da struct + suas deps em <time.h>)
 *
 * Functions copiadas DEVEM ser mantidas em sync com originais. Drift é
 * detectável: quando produção mudar e este teste passar com lógica antiga,
 * o teste vira false-positive — code review deve flag.
 *
 * @project SIMUT — EXT-009 (F-BUILD)
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include <unity.h>
#include <stdlib.h>
#include "SystemDefs_Validate.h"
#include "ParseFloat.h"
#include "SystemDefs_Time.h"
#include "SystemDefs_Network.h"  /* authLockoutMs — shared auth lockout backoff */
#include <cmath>      /* isnan, NAN para floatToI16 */
#include "SystemDefs_Logging.h"  /* tagStringToId — B1/B2 */
#include "sensors/SensorChannelTable.h" /* channel table integrity */
#include "sensors/CalibCurve.h"         /* calibration curve engine */
#include "WebJsonSlice.h"               /* depth-aware JSON slicing */
#include "SimutTime.h"                 /* fixed-offset localtime/mktime */
#include "PemBlocks.h"                 /* the PEM splitting POST /api/tls does */
#include "WebCommitSections.h"          /* per-section authz for /api/commit_all */
#include "FsSecretPath.h"               /* /config download guard (A-4) */
#include "CorsOrigin.h"                 /* what may go into Allow-Origin */
#include "ApPsk.h"                     /* setup-AP key derivation (V-05) */
#include "HaDiscovery.h"                /* Home Assistant MQTT Discovery formatters */
#include "B64Decode.h"                  /* Basic-auth base64 decoder (strict) */
#include "PromMetrics.h"                /* Prometheus text exposition formatters */
#include "Syslog5424.h"                 /* RFC 5424 syslog line formatter */
#include "ScreenRle.h"                  /* palette RLE for the TFT mirror */

/* ----- Define obrigatório de simut_native::fake_millis_value ----- */
namespace simut_native {
    uint32_t fake_millis_value = 0;
}


/* =========================================================================== */
/*  COPIAS LOCAIS (mantenha em sync com os originais — ver doc no topo)       */
/* =========================================================================== */

/* v3.36.4 (Fase 18.5 / M5): drift entre estas cópias e os originais é
 * detectado pelos golden-vector tests abaixo (test_dallasCrc8_known_vectors
 * + test_floatToI16_basic/clamp/nan + test_i16ToFloat_basic/nan). Se você
 * alterou um destes algoritmos no firmware, os tests aqui devem refletir
 * — caso contrário um dos lados está errado e o build host vai quebrar.
 * Dedup completa via build_src_filter foi avaliada e descartada: SystemUtils
 * .cpp inclui SystemDefs.h (heavy) que arrastra deps de hardware no env
 * native. Manter cópias + golden vectors é mais simples e mais robusto. */

/* dallasCrc8 — copiado byte-a-byte de SystemUtils.cpp:25 */
static uint8_t dallasCrc8(const uint8_t *addr, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t inbyte = addr[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

/* HIST_NAN_SENTINEL + floatToI16/i16ToFloat — copiados de SystemDefs_Records.h:487/517/530 */
static constexpr int16_t HIST_NAN_SENTINEL = INT16_MIN;  /* -32768 */

static inline int16_t floatToI16(float v) {
    if (std::isnan(v)) return HIST_NAN_SENTINEL;
    float scaled = v * 100.0f;
    if (scaled >  32767.0f) return  32767;
    if (scaled < -32767.0f) return -32767;
    return (int16_t)std::round(scaled);
}

static inline float i16ToFloat(int16_t v) {
    if (v == HIST_NAN_SENTINEL) return NAN;
    return (float)v / 100.0f;
}


/* =========================================================================== */
/*                              UNITY HOOKS                                    */
/* =========================================================================== */

void setUp(void) { simut_native::fake_millis_value = 0; }
void tearDown(void) {}


/* =========================================================================== */
/*  isValidIpv4                                                                */
/* =========================================================================== */
void test_isValidIpv4_valid(void) {
    TEST_ASSERT_TRUE(isValidIpv4("192.168.1.1"));
    TEST_ASSERT_TRUE(isValidIpv4("0.0.0.0"));
    TEST_ASSERT_TRUE(isValidIpv4("255.255.255.255"));
    TEST_ASSERT_TRUE(isValidIpv4("10.0.0.1"));
    TEST_ASSERT_TRUE(isValidIpv4("8.8.8.8"));
}

void test_isValidIpv4_invalid_format(void) {
    TEST_ASSERT_FALSE(isValidIpv4(""));
    TEST_ASSERT_FALSE(isValidIpv4("1.2.3"));            /* 3 octetos */
    TEST_ASSERT_FALSE(isValidIpv4("1.2.3.4.5"));        /* 5 octetos */
    TEST_ASSERT_FALSE(isValidIpv4("a.b.c.d"));          /* não-numérico */
    TEST_ASSERT_FALSE(isValidIpv4("1.2.3."));           /* trailing dot */
    TEST_ASSERT_FALSE(isValidIpv4("1.2..3"));           /* dot duplo */
    TEST_ASSERT_FALSE(isValidIpv4("1.2.3.4 "));         /* trailing space */
}

void test_isValidIpv4_invalid_octets(void) {
    TEST_ASSERT_FALSE(isValidIpv4("256.0.0.0"));        /* > 255 */
    TEST_ASSERT_FALSE(isValidIpv4("0.0.0.300"));        /* > 255 */
    TEST_ASSERT_FALSE(isValidIpv4("999.999.999.999"));
}

void test_isValidIpv4_size_bounds(void) {
    TEST_ASSERT_FALSE(isValidIpv4(nullptr));
    TEST_ASSERT_FALSE(isValidIpv4("123"));              /* < 7 chars */
    TEST_ASSERT_FALSE(isValidIpv4("1234567890123456")); /* > 15 chars */
}


/* =========================================================================== */
/*  isSafeUploadFilename                                                       */
/* =========================================================================== */
void test_isSafeUploadFilename_valid(void) {
    TEST_ASSERT_TRUE(isSafeUploadFilename("file.txt"));
    TEST_ASSERT_TRUE(isSafeUploadFilename("/file.txt"));      /* leading / é stripped */
    TEST_ASSERT_TRUE(isSafeUploadFilename("a"));
    TEST_ASSERT_TRUE(isSafeUploadFilename("path/to/file.txt"));
    TEST_ASSERT_TRUE(isSafeUploadFilename("foo.bar"));
    TEST_ASSERT_TRUE(isSafeUploadFilename("system.bin"));
    TEST_ASSERT_TRUE(isSafeUploadFilename("history/2026-04-01.bin"));
}

void test_isSafeUploadFilename_traversal(void) {
    TEST_ASSERT_FALSE(isSafeUploadFilename(".."));
    TEST_ASSERT_FALSE(isSafeUploadFilename("../etc"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("foo/../bar"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("...."));      /* contém .. */
    TEST_ASSERT_FALSE(isSafeUploadFilename("/.."));       /* após strip de / sobra .. */
}

void test_isSafeUploadFilename_dangerous_chars(void) {
    TEST_ASSERT_FALSE(isSafeUploadFilename("a%2eb"));     /* % bloqueia percent-encoding */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a%2e%2e/b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a\\b"));      /* backslash */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a\"b"));      /* aspas */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a:b"));       /* dois pontos */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a<b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a>b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a|b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a?b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a*b"));
    TEST_ASSERT_FALSE(isSafeUploadFilename("a\x01" "b"));    /* control char 0x01 (split string p/ terminar hex escape) */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a\x1f" "b"));    /* control char 0x1F */
    TEST_ASSERT_FALSE(isSafeUploadFilename("a\x7f" "b"));    /* DEL 0x7F */
}

void test_isSafeUploadFilename_size(void) {
    TEST_ASSERT_FALSE(isSafeUploadFilename(""));
    TEST_ASSERT_FALSE(isSafeUploadFilename(nullptr));
    TEST_ASSERT_FALSE(isSafeUploadFilename("/"));         /* só / vira vazio */

    char too_long[66];
    memset(too_long, 'a', 65);
    too_long[65] = '\0';
    TEST_ASSERT_FALSE(isSafeUploadFilename(too_long));    /* 65 chars > UPLOAD_FILENAME_MAX (64) */

    char ok[65];
    memset(ok, 'a', 64);
    ok[64] = '\0';
    TEST_ASSERT_TRUE(isSafeUploadFilename(ok));           /* exatamente 64 chars */
}


/* =========================================================================== */
/*  isValidName                                                                */
/* =========================================================================== */
void test_isValidName_valid(void) {
    TEST_ASSERT_TRUE(isValidName("admin"));
    TEST_ASSERT_TRUE(isValidName("user1"));
    TEST_ASSERT_TRUE(isValidName("a"));               /* 1 char OK */
    TEST_ASSERT_TRUE(isValidName("Sensor 1"));        /* espaços OK */
    TEST_ASSERT_TRUE(isValidName("name-with-dash"));
}

void test_isValidName_invalid(void) {
    TEST_ASSERT_FALSE(isValidName(""));
    TEST_ASSERT_FALSE(isValidName(nullptr));
    TEST_ASSERT_FALSE(isValidName("user\"x"));        /* aspas */
    TEST_ASSERT_FALSE(isValidName("user\\x"));        /* backslash */
    TEST_ASSERT_FALSE(isValidName("user\x01" ""));    /* control char */

    char too_long[33];
    memset(too_long, 'x', 32);
    too_long[32] = '\0';
    TEST_ASSERT_FALSE(isValidName(too_long));         /* 32 > maxLen default 31 */
}


/* =========================================================================== */
/*  isValidCfgString                                                           */
/* =========================================================================== */
void test_isValidCfgString(void) {
    TEST_ASSERT_TRUE(isValidCfgString("", 31));        /* vazio é válido em cfg */
    TEST_ASSERT_TRUE(isValidCfgString("hello", 31));
    TEST_ASSERT_TRUE(isValidCfgString("a\"b\\c", 31)); /* aspas/backslash são válidos em cfg
                                                          (senhas WPA2 podem ter, paths podem ter) */
    TEST_ASSERT_FALSE(isValidCfgString("a\x01" "b", 31)); /* control char rejeitado (split p/ terminar hex escape) */
    TEST_ASSERT_FALSE(isValidCfgString("toolong", 5)); /* len > maxLen */
    TEST_ASSERT_FALSE(isValidCfgString(nullptr, 31));  /* null rejeitado */
}


/* =========================================================================== */
/*  isInRange                                                                  */
/* =========================================================================== */
/* Config v21 -> v22: the telemetry interval in milliseconds becomes a minimum
 * batch in records. The field keeps its offset and its CRC, so nothing but this
 * arithmetic stands between an upgraded device and silent telemetry — the old
 * default of 300000 read as a count means "300,000 records pending", which
 * never arrives. */
void test_tel_min_batch_from_legacy_ms(void) {
    /* the field default and the two the bench used, at one reading a minute */
    TEST_ASSERT_EQUAL_UINT32(5, telMinBatchFromLegacyMs(300000, 1, 20000));
    TEST_ASSERT_EQUAL_UINT32(1, telMinBatchFromLegacyMs(60000, 1, 20000));
    TEST_ASSERT_EQUAL_UINT32(10, telMinBatchFromLegacyMs(600000, 1, 20000));
    /* a slower reading interval means fewer records in the same wall time */
    TEST_ASSERT_EQUAL_UINT32(1, telMinBatchFromLegacyMs(300000, 5, 20000));
    TEST_ASSERT_EQUAL_UINT32(2, telMinBatchFromLegacyMs(600000, 5, 20000));
    /* off stays off — the one value whose meaning did not change */
    TEST_ASSERT_EQUAL_UINT32(0, telMinBatchFromLegacyMs(0, 1, 20000));
    /* an interval shorter than one reading still has to send something */
    TEST_ASSERT_EQUAL_UINT32(1, telMinBatchFromLegacyMs(1, 1, 20000));
    TEST_ASSERT_EQUAL_UINT32(1, telMinBatchFromLegacyMs(59999, 1, 20000));
    /* the ceiling, and the largest value v21 could hold (24 h) */
    TEST_ASSERT_EQUAL_UINT32(1440, telMinBatchFromLegacyMs(86400000, 1, 20000));
    TEST_ASSERT_EQUAL_UINT32(100, telMinBatchFromLegacyMs(86400000, 1, 100));
    /* a history interval outside the valid range must not divide by zero */
    TEST_ASSERT_EQUAL_UINT32(5, telMinBatchFromLegacyMs(300000, 0, 20000));
}

void test_isInRange(void) {
    TEST_ASSERT_TRUE(isInRange(5, 0, 10));
    TEST_ASSERT_TRUE(isInRange(0, 0, 10));             /* boundary baixa */
    TEST_ASSERT_TRUE(isInRange(10, 0, 10));            /* boundary alta */
    TEST_ASSERT_FALSE(isInRange(-1, 0, 10));
    TEST_ASSERT_FALSE(isInRange(11, 0, 10));
    TEST_ASSERT_TRUE(isInRange(-5, -10, 0));           /* range negativo */
}


/* =========================================================================== */
/*  parseIntStrict (usa String stubada em native_stubs/Arduino.h)             */
/* =========================================================================== */
void test_parseIntStrict_valid(void) {
    int out;
    TEST_ASSERT_TRUE(parseIntStrict(String("0"), out));      TEST_ASSERT_EQUAL_INT(0, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("123"), out));    TEST_ASSERT_EQUAL_INT(123, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("-456"), out));   TEST_ASSERT_EQUAL_INT(-456, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("+789"), out));   TEST_ASSERT_EQUAL_INT(789, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("00042"), out));  TEST_ASSERT_EQUAL_INT(42, out);
}

void test_parseIntStrict_invalid(void) {
    int out;
    TEST_ASSERT_FALSE(parseIntStrict(String(""), out));      /* vazio */
    TEST_ASSERT_FALSE(parseIntStrict(String("abc"), out));   /* não-numérico */
    TEST_ASSERT_FALSE(parseIntStrict(String("12a"), out));   /* parcial */
    TEST_ASSERT_FALSE(parseIntStrict(String(" 123"), out));  /* leading space */
    TEST_ASSERT_FALSE(parseIntStrict(String("123 "), out));  /* trailing space */
    TEST_ASSERT_FALSE(parseIntStrict(String("-"), out));     /* só sinal */
    TEST_ASSERT_FALSE(parseIntStrict(String("+"), out));     /* só sinal */
    TEST_ASSERT_FALSE(parseIntStrict(String("1.5"), out));   /* decimal */
    TEST_ASSERT_FALSE(parseIntStrict(String("0x10"), out));  /* hex */
}

/* v3.36.3 (Fase 18.4 / M7): cobertura de parseFloatStrict adicionada na 18.2.
 * Distingue "0.0" legítimo de input não-numérico (que toFloat() silencia → 0). */
void test_parseFloatStrict_valid(void) {
    float out;
    TEST_ASSERT_TRUE(parseFloatStrict(String("0"), out));       TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("0.0"), out));     TEST_ASSERT_EQUAL_FLOAT(0.0f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("3.14"), out));    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("-2.5"), out));    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.5f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("+100"), out));    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("999"), out));     TEST_ASSERT_FLOAT_WITHIN(0.001f, 999.0f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String(".5"), out));      TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("-0.0"), out));    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, out);
}

/* 2026-09-18: the fuzz gate caught parseFloatStrict( ) one ulp off (float)strtod
 * on a 27-digit input — past 2^53 the double accumulator in parseFloat( )
 * rounds at every step. The validator now refuses past PARSE_FLOAT_EXACT_DIGITS.
 * These vectors pin the ceiling from both sides and compare with ==, not
 * "within 0.001": a tolerance is exactly what would have let this through. */
/* ---- PemBlocks.h: what POST /api/tls pulls out of one body ----------------
 * The route takes both PEM blocks concatenated, in either order (issue #133).
 * These are the cases that are awkward to produce against a device and trivial
 * to write down: reversed order, a chain, a block that is never closed, and the
 * encrypted key that has to be named rather than called invalid. */
static const char* CERT_A =
    "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
static const char* CERT_B =
    "-----BEGIN CERTIFICATE-----\nBBBB\n-----END CERTIFICATE-----\n";
static const char* KEY_PKCS8 =
    "-----BEGIN PRIVATE KEY-----\nKKKK\n-----END PRIVATE KEY-----\n";
static const char* KEY_EC =
    "-----BEGIN EC PRIVATE KEY-----\nEEEE\n-----END EC PRIVATE KEY-----\n";
static const char* KEY_RSA =
    "-----BEGIN RSA PRIVATE KEY-----\nRRRR\n-----END RSA PRIVATE KEY-----\n";
static const char* KEY_ENC =
    "-----BEGIN ENCRYPTED PRIVATE KEY-----\nZZZZ\n-----END ENCRYPTED PRIVATE KEY-----\n";

void test_pem_cert_then_key(void) {
    String body = String(CERT_A) + KEY_PKCS8;
    TEST_ASSERT_TRUE(pemCertChain(body).indexOf("AAAA") > 0);
    TEST_ASSERT_TRUE(pemCertChain(body).indexOf("KKKK") < 0);   /* the key does not ride along */
    TEST_ASSERT_TRUE(pemPrivateKey(body).indexOf("KKKK") > 0);
}

void test_pem_key_then_cert(void) {
    /* `cat web_key.pem web_cert.pem` is as valid as the other order. */
    String body = String(KEY_PKCS8) + CERT_A;
    TEST_ASSERT_TRUE(pemCertChain(body).indexOf("AAAA") > 0);
    TEST_ASSERT_TRUE(pemPrivateKey(body).indexOf("KKKK") > 0);
}

void test_pem_chain_is_kept_whole(void) {
    /* First BEGIN to LAST END: an intermediate must not be dropped, or the
     * device serves a chain no browser can complete. */
    String body = String(CERT_A) + CERT_B + KEY_EC;
    String chain = pemCertChain(body);
    TEST_ASSERT_TRUE(chain.indexOf("AAAA") > 0);
    TEST_ASSERT_TRUE(chain.indexOf("BBBB") > 0);
    TEST_ASSERT_TRUE(chain.indexOf("KKKK") < 0);
    TEST_ASSERT_TRUE(chain.endsWith("-----END CERTIFICATE-----"));
}

void test_pem_three_key_spellings(void) {
    TEST_ASSERT_TRUE(pemPrivateKey(String(CERT_A) + KEY_PKCS8).indexOf("KKKK") > 0);
    TEST_ASSERT_TRUE(pemPrivateKey(String(CERT_A) + KEY_EC).indexOf("EEEE") > 0);
    TEST_ASSERT_TRUE(pemPrivateKey(String(CERT_A) + KEY_RSA).indexOf("RRRR") > 0);
}

void test_pem_encrypted_key_is_named_not_parsed(void) {
    bool enc = false;
    String k = pemPrivateKey(String(CERT_A) + KEY_ENC, &enc);
    TEST_ASSERT_EQUAL_INT(0, k.length( ));   /* BearSSL cannot open it */
    TEST_ASSERT_TRUE(enc);                   /* and the operator is told why */
}

void test_pem_unterminated_block_is_not_a_block(void) {
    /* A truncated upload must read as absent, never as a block running to the
     * end of the body — that is what would reach BearSSL as garbage. */
    String cut = "-----BEGIN CERTIFICATE-----\nAAAA\n";
    TEST_ASSERT_EQUAL_INT(0, pemCertChain(cut).length( ));
    String cutKey = String(CERT_A) + "-----BEGIN PRIVATE KEY-----\nKKKK\n";
    TEST_ASSERT_EQUAL_INT(0, pemPrivateKey(cutKey).length( ));
}

void test_pem_absent_blocks(void) {
    TEST_ASSERT_EQUAL_INT(0, pemCertChain(String("")).length( ));
    TEST_ASSERT_EQUAL_INT(0, pemPrivateKey(String("nothing here")).length( ));
    TEST_ASSERT_EQUAL_INT(0, pemCertChain(String(KEY_PKCS8)).length( ));
}

void test_parseFloatStrict_digit_ceiling(void) {
    float out = 0.0f;
    TEST_ASSERT_FALSE(parseFloatStrict(String("33333333.0000000000030033000"), out)); /* the fuzz input, 27 digits */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1234567890123456"), out));             /* 16 digits */
    TEST_ASSERT_FALSE(parseFloatStrict(String("0.000000000000001"), out));            /* 16 digits: leading zeros count */
    TEST_ASSERT_TRUE(parseFloatStrict(String("123456789012345"), out));               /* 15 digits */
    TEST_ASSERT_TRUE(out == (float)strtod("123456789012345", nullptr));
    TEST_ASSERT_TRUE(parseFloatStrict(String("33333333.0000003"), out));              /* 15 digits, the fuzz shape */
    TEST_ASSERT_TRUE(out == (float)strtod("33333333.0000003", nullptr));
    TEST_ASSERT_TRUE(parseFloatStrict(String("-0.00000000000001"), out));             /* 15 digits, 14 fractional */
    TEST_ASSERT_TRUE(out == (float)strtod("-0.00000000000001", nullptr));
    TEST_ASSERT_TRUE(parseFloatStrict(String("1013.25"), out));                       /* what a calibration point looks like */
    TEST_ASSERT_TRUE(out == (float)strtod("1013.25", nullptr));
}

void test_parseFloatStrict_invalid(void) {
    float out;
    TEST_ASSERT_FALSE(parseFloatStrict(String(""), out));       /* vazio */
    TEST_ASSERT_FALSE(parseFloatStrict(String("abc"), out));    /* não-numérico */
    TEST_ASSERT_FALSE(parseFloatStrict(String("NaN"), out));    /* NaN literal rejeitado */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1.2.3"), out));  /* 2 pontos */
    TEST_ASSERT_FALSE(parseFloatStrict(String("3,14"), out));   /* vírgula decimal (locale) */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1e5"), out));    /* notação científica */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1.5f"), out));   /* sufixo */
    TEST_ASSERT_FALSE(parseFloatStrict(String(" 1.5"), out));   /* leading space */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1.5 "), out));   /* trailing space */
    TEST_ASSERT_FALSE(parseFloatStrict(String("-"), out));      /* só sinal */
    TEST_ASSERT_FALSE(parseFloatStrict(String("+"), out));      /* só sinal */
    TEST_ASSERT_FALSE(parseFloatStrict(String("."), out));      /* só ponto, sem dígito */
    TEST_ASSERT_FALSE(parseFloatStrict(String("-."), out));     /* sinal + ponto, sem dígito */
}

/* Issue #44 (fuzz dos validadores): overflow deixava parseIntStrict responder
 * true com out saturado — no ferro toInt() é atol(), e o strtol de 32 bits da
 * newlib SATURA em ±2^31 em vez de falhar ("2147483648" respondia true com
 * out=2147483647, um valor que o cliente nunca escreveu). O parser agora
 * acumula os dígitos com guarda e overflow responde false. No float, ~40
 * dígitos saturam atof em ±inf, e um inf que responde true envenena qualquer
 * limiar comparado depois. */
void test_parseIntStrict_int32_boundaries(void) {
    int out;
    TEST_ASSERT_TRUE(parseIntStrict(String("2147483647"), out));   TEST_ASSERT_EQUAL_INT(2147483647, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("+2147483647"), out));  TEST_ASSERT_EQUAL_INT(2147483647, out);
    TEST_ASSERT_TRUE(parseIntStrict(String("-2147483648"), out));  TEST_ASSERT_EQUAL_INT(-2147483647 - 1, out);
    /* zeros à esquerda não contam para o limite — o valor sim */
    TEST_ASSERT_TRUE(parseIntStrict(String("0000000002147483647"), out)); TEST_ASSERT_EQUAL_INT(2147483647, out);
}

void test_parseIntStrict_overflow_rejected(void) {
    int out = 77;
    TEST_ASSERT_FALSE(parseIntStrict(String("2147483648"), out));           /* INT32_MAX+1 */
    TEST_ASSERT_FALSE(parseIntStrict(String("-2147483649"), out));          /* INT32_MIN-1 */
    TEST_ASSERT_FALSE(parseIntStrict(String("99999999999999999999"), out)); /* 20 dígitos */
    TEST_ASSERT_FALSE(parseIntStrict(String("+99999999999999999999"), out));
    TEST_ASSERT_EQUAL_INT(77, out); /* contrato: out intocado no false */
}

void test_parseFloatStrict_overflow_rejected(void) {
    float out = 1.5f;
    /* 39 noves ≈ 1e39 > FLT_MAX (3.40e38) → toFloat satura em ±inf. Desde o teto
     * de 15 dígitos (2026-09-18) são recusados antes de o valor existir; os
     * vetores ficam porque o contrato que fixam — false, out intocado — é o mesmo. */
    TEST_ASSERT_FALSE(parseFloatStrict(String("999999999999999999999999999999999999999"), out));
    TEST_ASSERT_FALSE(parseFloatStrict(String("-999999999999999999999999999999999999999"), out));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, out); /* contrato: out intocado no false */
}

void test_parseFloatStrict_large_finite_ok(void) {
    float out;
    /* O maior valor que o teto de 15 dígitos admite, ≈1e15 ≪ FLT_MAX → finito,
     * aceita. (Até 2026-09-18 o vetor era 38 noves ≈ 1e38: finito também, mas 38
     * dígitos passa de onde parseFloat( ) é exato, e o validador agora recusa
     * por isso — ver test_parseFloatStrict_digit_ceiling.) */
    TEST_ASSERT_TRUE(parseFloatStrict(String("999999999999999"), out));
    TEST_ASSERT_TRUE(isfinite(out));
    TEST_ASSERT_TRUE(out == (float)strtod("999999999999999", nullptr));
}

/* Contrato do stub (native_stubs/Arduino.h): toInt/toFloat têm de se comportar
 * como o FERRO — atol satura (long de 32 bits na newlib), atof estoura para
 * ±inf. O stub antigo usava std::stol/std::stof, que LANÇAM em overflow, e o
 * catch respondia 0/0.0f: o host divergia do alvo exatamente no caso que um
 * fuzzer encontraria. Golden vectors da semântica do ArduinoCore-API
 * (String::toInt = atol; String::toFloat = float(atof)). */
void test_stub_toInt_saturates_like_target(void) {
    TEST_ASSERT_EQUAL_INT32(2147483647, (int32_t)String("99999999999999999999").toInt());
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, (int32_t)String("-99999999999999999999").toInt());
    TEST_ASSERT_EQUAL_INT32(0, (int32_t)String("abc").toInt());
}

void test_stub_toFloat_overflows_to_inf_like_target(void) {
    const float v = String("999999999999999999999999999999999999999").toFloat();
    TEST_ASSERT_TRUE(isinf(v));
    TEST_ASSERT_TRUE(v > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, String("xyz").toFloat());
}


/* =========================================================================== */
/*  timeReached / timeSince (millis() stubado)                                */
/* =========================================================================== */
void test_timeReached_basic(void) {
    set_native_millis(1000);
    TEST_ASSERT_TRUE(timeReached(500));     /* deadline passou */
    TEST_ASSERT_TRUE(timeReached(1000));    /* exatamente agora */
    TEST_ASSERT_FALSE(timeReached(2000));   /* deadline ainda no futuro */
}

void test_timeReached_wrap_safe(void) {
    /* Uptime quase no wrap (millis() ~49.7 dias = 0xFFFFFFFF). */
    set_native_millis(0xFFFFFFFFu);
    /* Deadline 1ms no futuro = 0 (após wrap) */
    TEST_ASSERT_FALSE(timeReached(0));
    /* Deadline 1s no passado */
    TEST_ASSERT_TRUE(timeReached(0xFFFFFFFFu - 1000));

    /* Pós-wrap: agora=100, deadline=0xFFFFFFFFu (1ms antes do wrap) */
    set_native_millis(100);
    TEST_ASSERT_TRUE(timeReached(0xFFFFFFFFu));  /* deadline foi 100ms atrás (wrap-safe) */
}

void test_timeSince_basic(void) {
    set_native_millis(5000);
    TEST_ASSERT_TRUE(timeSince(0, 1000));        /* 5000ms desde 0, > 1000ms */
    TEST_ASSERT_TRUE(timeSince(4000, 1000));     /* exatamente 1000ms */
    TEST_ASSERT_FALSE(timeSince(4500, 1000));    /* só 500ms decorreu */
}


/* =========================================================================== */
/*  dallasCrc8                                                                 */
/* =========================================================================== */
void test_dallasCrc8_known_vectors(void) {
    /* Vetor 1: byte único 0x00 → CRC 0x00 (identidade). */
    uint8_t z = 0;
    TEST_ASSERT_EQUAL_UINT8(0x00, dallasCrc8(&z, 1));

    /* Vetor 2: byte único 0x01 → calculado manualmente per Maxim algorithm:
     *   inbyte=0x01, crc=0
     *   it1: mix=(0^1)&1=1; crc>>=1 → 0; crc^=0x8C → 0x8C; inbyte>>=1 → 0
     *   it2-8: mix=0 (inbyte=0); crc apenas shifta para direita.
     *   final: 0x8C >> 7 = 0x01, mas com xor encadeado vira 0x5E.
     * 0x5E é o valor canônico da literatura Dallas/Maxim para input 0x01.
     */
    uint8_t one = 0x01;
    TEST_ASSERT_EQUAL_UINT8(0x5E, dallasCrc8(&one, 1));

    /* Vetor 3: comprimento 0 → CRC 0 (loop não executa). */
    uint8_t any = 0xAA;
    TEST_ASSERT_EQUAL_UINT8(0x00, dallasCrc8(&any, 0));

    /* Vetor 4: ROM DS18B20 com CRC válido conhecido (Maxim app note 27).
     * Family=0x28, serial=11:22:33:44:55:66, CRC=byte 7. Computar com 7 bytes
     * de payload deve resultar no CRC byte. Neste caso vamos validar que
     * dallasCrc8(rom, 8) = 0 (CRC do ROM completo com seu próprio CRC = 0). */
    uint8_t rom[7] = { 0x28, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    uint8_t computed = dallasCrc8(rom, 7);
    /* Construímos o ROM completo com o CRC computado e validamos circular = 0 */
    uint8_t rom_full[8] = { 0x28, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, computed };
    TEST_ASSERT_EQUAL_UINT8(0x00, dallasCrc8(rom_full, 8));
}

void test_dallasCrc8_determinism(void) {
    uint8_t buf[16] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78,
                        0x9A, 0xBC, 0xDE, 0xF0, 0x01, 0x02, 0x03, 0x04 };
    uint8_t a = dallasCrc8(buf, 16);
    uint8_t b = dallasCrc8(buf, 16);
    TEST_ASSERT_EQUAL_UINT8(a, b);
}


/* =========================================================================== */
/*  floatToI16 / i16ToFloat                                                   */
/* =========================================================================== */
void test_floatToI16_basic(void) {
    TEST_ASSERT_EQUAL_INT16(2500,  floatToI16(25.0f));    /* 25.00 °C */
    TEST_ASSERT_EQUAL_INT16(0,     floatToI16(0.0f));
    TEST_ASSERT_EQUAL_INT16(-2500, floatToI16(-25.0f));
    TEST_ASSERT_EQUAL_INT16(2550,  floatToI16(25.5f));    /* meio-passo */
    TEST_ASSERT_EQUAL_INT16(2501,  floatToI16(25.013f));  /* arredondamento */
}

void test_floatToI16_clamp(void) {
    TEST_ASSERT_EQUAL_INT16(32767,  floatToI16(1000.0f));   /* satura positivo */
    TEST_ASSERT_EQUAL_INT16(-32767, floatToI16(-1000.0f));  /* satura negativo (-32768 reservado) */
    TEST_ASSERT_EQUAL_INT16(32767,  floatToI16(327.67f));
    TEST_ASSERT_EQUAL_INT16(32767,  floatToI16(327.68f));   /* fora do range -> clamp */
}

void test_floatToI16_nan(void) {
    TEST_ASSERT_EQUAL_INT16(HIST_NAN_SENTINEL, floatToI16(NAN));
}

void test_i16ToFloat_basic(void) {
    TEST_ASSERT_EQUAL_FLOAT(25.0f,  i16ToFloat(2500));
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   i16ToFloat(0));
    TEST_ASSERT_EQUAL_FLOAT(-25.0f, i16ToFloat(-2500));
    TEST_ASSERT_EQUAL_FLOAT(25.5f,  i16ToFloat(2550));
}

void test_i16ToFloat_nan(void) {
    float r = i16ToFloat(HIST_NAN_SENTINEL);
    TEST_ASSERT_TRUE(std::isnan(r));   /* NaN é o único float que != ele mesmo */
}

void test_floatToI16_roundtrip(void) {
    /* Valores que cabem exatos: roundtrip preserva 2 casas decimais. */
    TEST_ASSERT_EQUAL_FLOAT( 23.45f, i16ToFloat(floatToI16( 23.45f)));
    TEST_ASSERT_EQUAL_FLOAT(-99.99f, i16ToFloat(floatToI16(-99.99f)));
    TEST_ASSERT_EQUAL_FLOAT(  0.01f, i16ToFloat(floatToI16(  0.01f)));
}

/* ── parseFloat (inline, replaces atof/toFloat) ────────────────── */
void test_parseFloat_basic(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parseFloat("0"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parseFloat("0.0"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, parseFloat("3.14"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.5f, parseFloat("-2.5"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, parseFloat("100"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, parseFloat(".5"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, parseFloat("-.5"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parseFloat("-0.0"));
}
void test_parseFloat_edge(void) {
    TEST_ASSERT_TRUE(isnan(parseFloat(nullptr)));
    TEST_ASSERT_TRUE(isnan(parseFloat("")));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parseFloat("abc"));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, parseFloat("5.0"));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.75f, parseFloat("12.75"));
}


/* =========================================================================== */
/*                                  MAIN                                       */
/* =========================================================================== */

/* ---- log tags: tagStringToId ----------------------------------------------
 * v1.5.6 answered TAG_SENSOR for "SEC" (both share tag[1] == 'E'), so all 36
 * security/audit call sites were persisted under the sensor tag and TAG_SEC
 * was unreachable; "OTA" had no case at all and landed on TAG_UNKNOWN. Both
 * were invisible because nothing exercised this mapping. Every tag literal
 * actually used by LOG_CODE in the firmware is asserted here. */

void test_tag_sec_is_not_sensor(void) {
    TEST_ASSERT_EQUAL(TAG_SEC, tagStringToId("SEC"));
    TEST_ASSERT_EQUAL(TAG_SENSOR, tagStringToId("SENSOR"));
}

void test_tag_ota_has_its_own_id(void) {
    TEST_ASSERT_EQUAL(TAG_OTA, tagStringToId("OTA"));
}

void test_tag_all_literals_used_in_firmware(void) {
    TEST_ASSERT_EQUAL(TAG_APP,    tagStringToId("APP"));
    TEST_ASSERT_EQUAL(TAG_NET,    tagStringToId("NET"));
    TEST_ASSERT_EQUAL(TAG_TEL,    tagStringToId("TEL"));
    TEST_ASSERT_EQUAL(TAG_STO,    tagStringToId("STO"));
    TEST_ASSERT_EQUAL(TAG_WEB,    tagStringToId("WEB"));
    TEST_ASSERT_EQUAL(TAG_CFG,    tagStringToId("CFG"));
    TEST_ASSERT_EQUAL(TAG_CLI,    tagStringToId("CLI"));
    TEST_ASSERT_EQUAL(TAG_HIST,   tagStringToId("HIST"));
    TEST_ASSERT_EQUAL(TAG_SYS,    tagStringToId("SYS"));
    TEST_ASSERT_EQUAL(TAG_DSP,    tagStringToId("DSP"));
}

void test_tag_unknown_inputs(void) {
    TEST_ASSERT_EQUAL(TAG_UNKNOWN, tagStringToId(nullptr));
    TEST_ASSERT_EQUAL(TAG_UNKNOWN, tagStringToId("ZZZ"));
    /* An unrecognised 'S*' tag must not silently become SEC — that was the
     * old fallback, and it would quietly mislabel any tag added later. */
    TEST_ASSERT_EQUAL(TAG_UNKNOWN, tagStringToId("SPI"));
}

void test_tag_id_to_string_roundtrip(void) {
    /* The browser and the CLI both index a parallel name table; a mapped id
     * with no name renders as "?" and hides the record's origin. */
    TEST_ASSERT_EQUAL_STRING("SEC", tagIdToString(TAG_SEC));
    TEST_ASSERT_EQUAL_STRING("OTA", tagIdToString(TAG_OTA));
    TEST_ASSERT_EQUAL_STRING("SENSOR", tagIdToString(TAG_SENSOR));
}

/* ===========================================================================
 * CHANNEL TABLE INTEGRITY
 *
 * The table is the single source of truth for what a measurement axis is, so a
 * bad row is not a local mistake — it corrupts calibration keys and history
 * packing at once. A duplicated letter would make two quantities share a
 * calib.csv row and a V4 measurement key; a bitWidth too small for the declared
 * range would silently clamp readings at the top of the scale. Both are cheap
 * to assert here and expensive to discover on a device.
 * =========================================================================== */

void test_channel_letters_unique(void) {
    for (uint8_t a = 0; a < CH_COUNT; a++) {
        TEST_ASSERT_TRUE_MESSAGE(channelTable()[a].letter != 0, "channel row has no letter");
        for (uint8_t b = (uint8_t)(a + 1); b < CH_COUNT; b++) {
            TEST_ASSERT_TRUE_MESSAGE(channelTable()[a].letter != channelTable()[b].letter,
                                     "two channels claim the same letter");
        }
        /* Round-trip through the lookup the calib reader uses. */
        TEST_ASSERT_EQUAL_INT((int)a, channelByLetter(channelTable()[a].letter));
    }
}

void test_channel_keys_unique_and_resolvable(void) {
    for (uint8_t a = 0; a < CH_COUNT; a++) {
        TEST_ASSERT_NOT_NULL(channelTable()[a].key);
        TEST_ASSERT_TRUE(channelTable()[a].key[0] != '\0');
        TEST_ASSERT_EQUAL_INT((int)a, channelByKey(channelTable()[a].key));
        for (uint8_t b = (uint8_t)(a + 1); b < CH_COUNT; b++) {
            TEST_ASSERT_TRUE_MESSAGE(strcmp(channelTable()[a].key, channelTable()[b].key) != 0,
                                     "two channels claim the same API key");
        }
    }
    TEST_ASSERT_EQUAL_INT(-1, channelByKey("nope"));
    TEST_ASSERT_EQUAL_INT(-1, channelByKey(nullptr));
}

void test_channel_range_fits_bit_width(void) {
    for (uint8_t c = 0; c < CH_COUNT; c++) {
        const ChannelInfo& ci = channelTable()[c];
        TEST_ASSERT_TRUE_MESSAGE(ci.bitWidth > 0 && ci.bitWidth <= 32, "implausible bitWidth");
        TEST_ASSERT_TRUE_MESSAGE(ci.scale > 0, "scale must be positive");
        TEST_ASSERT_TRUE_MESSAGE(ci.saneMax > ci.saneMin, "empty plausible range");

        /* Top raw value the field holds, minus the all-ones NaN sentinel.
         * Compared with a relative slack because saneMax is a float literal
         * sitting exactly on the boundary — 167772.15f is really 167772.15625,
         * which overshoots by a fraction of one raw unit and says nothing about
         * the row being wrong. What this must catch is a row off by orders of
         * magnitude, e.g. 8 bits declared for a 0..10000 range. */
        double maxRaw = (double)((1ULL << ci.bitWidth) - 1ULL) - 1.0;
        if (ci.isSigned) maxRaw /= 2.0; /* top bit carries the sign */
        double needed = (double)ci.saneMax * (double)ci.scale;
        TEST_ASSERT_TRUE_MESSAGE(needed <= maxRaw * 1.0001 + 2.0,
                                 "saneMax * scale overflows bitWidth — readings would clamp");
        if (!ci.isSigned) {
            TEST_ASSERT_TRUE_MESSAGE(ci.saneMin >= 0.0f,
                                     "unsigned channel with a negative plausible minimum");
        }
    }
}

void test_channel_defaults_inside_sane_range(void) {
    /* defMin/defMax seed a factory config. A default outside the channel's own
     * plausible range would arm an alarm the user never set and cannot satisfy. */
    for (uint8_t c = 0; c < CH_COUNT; c++) {
        const ChannelInfo& ci = channelTable()[c];
        TEST_ASSERT_TRUE_MESSAGE(ci.defMin >= ci.saneMin, "defMin below the plausible range");
        TEST_ASSERT_TRUE_MESSAGE(ci.defMax <= ci.saneMax, "defMax above the plausible range");
        TEST_ASSERT_TRUE_MESSAGE(ci.defMin < ci.defMax, "factory band is empty or inverted");
    }
}

void test_channel_slots_cover_table(void) {
    /* chMin[]/chMax[]/channelBitWidth[] are sized by MAX_SENSOR_CHANNELS and
     * indexed by channel id, so a table longer than the arrays writes past
     * them — into the next field of a packed record that goes to flash. */
    TEST_ASSERT_TRUE_MESSAGE(CH_COUNT <= MAX_SENSOR_CHANNELS,
                             "more channels in the table than slots in SensorRecord");
}

void test_channel_unknown_falls_back(void) {
    /* histV4ChannelPrefix( ) answered 'x' for an unknown channel long before the
     * table existed; keys already written in the wild depend on it. */
    TEST_ASSERT_EQUAL_CHAR('x', channelInfo(99).letter);
    TEST_ASSERT_EQUAL_CHAR('x', channelInfo(CH_COUNT).letter);
    TEST_ASSERT_FALSE(channelValid(CH_COUNT));
    TEST_ASSERT_TRUE(channelValid(CH_TEMP));
    TEST_ASSERT_EQUAL_INT(-1, channelByLetter('z'));
}

/* ===========================================================================
 * CALIB CURVE
 *
 * The curve engine is the whole correctness story of multi-point calibration:
 * every consumer (display, history, alarms, telemetry) sees whatever
 * calibCurveApply says, and the CSV pts column round-trips through
 * encode/decode on every save. A wrong segment lookup or a lossy round-trip
 * would corrupt readings silently, which is exactly the class of bug that is
 * cheap to pin here and expensive to notice on a device.
 * =========================================================================== */

void test_calibcurve_build_sorts_input(void) {
    CalibCurve c;
    const float raws[3] = { 35.40f, 20.10f, 27.00f };
    const float refs[3] = { 35.00f, 20.00f, 27.10f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, raws, refs, 3));
    TEST_ASSERT_EQUAL_UINT8(3, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.10f, c.raw[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 27.00f, c.raw[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.40f, c.raw[2]);
    /* Offsets followed their raws through the sort. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.10f, c.off[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f,  0.10f, c.off[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.40f, c.off[2]);
}

void test_calibcurve_build_rejects_bad_input(void) {
    CalibCurve c;
    /* Duplicate raws at 2-decimal granularity — 20.10 and 20.104 collide. */
    {
        const float raws[2] = { 20.10f, 20.104f };
        const float refs[2] = { 20.00f, 21.00f };
        TEST_ASSERT_FALSE(calibCurveBuild(c, raws, refs, 2));
        TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
    }
    /* More points than the model holds. */
    {
        const float raws[6] = { 1, 2, 3, 4, 5, 6 };
        const float refs[6] = { 1, 2, 3, 4, 5, 6 };
        TEST_ASSERT_FALSE(calibCurveBuild(c, raws, refs, 6));
    }
    /* Non-finite anywhere. */
    {
        const float raws[2] = { 1.0f, NAN };
        const float refs[2] = { 1.0f, 2.0f };
        TEST_ASSERT_FALSE(calibCurveBuild(c, raws, refs, 2));
    }
    {
        const float raws[1] = { 1.0f };
        const float refs[1] = { INFINITY };
        TEST_ASSERT_FALSE(calibCurveBuild(c, raws, refs, 1));
    }
    /* Zero points is a valid "no correction", not an error. */
    TEST_ASSERT_TRUE(calibCurveBuild(c, nullptr, nullptr, 0));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
}

void test_calibcurve_apply_identity(void) {
    CalibCurve c;
    TEST_ASSERT_EQUAL_FLOAT(-50.0f, calibCurveApply(c, -50.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f,   calibCurveApply(c, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(150.0f, calibCurveApply(c, 150.0f));
    TEST_ASSERT_TRUE(std::isnan(calibCurveApply(c, NAN)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibCurveOffsetAt(c, 25.0f));
}

void test_calibcurve_apply_single_point(void) {
    CalibCurve c;
    const float raws[1] = { 25.00f };
    const float refs[1] = { 24.70f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, raws, refs, 1));
    /* One point is a constant offset over the whole axis. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1000.30f, calibCurveApply(c, -1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f,    24.70f, calibCurveApply(c, 25.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f,   999.70f, calibCurveApply(c, 1000.0f));
    TEST_ASSERT_TRUE(std::isnan(calibCurveApply(c, NAN)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.30f, calibCurveOffsetAt(c, NAN));
}

void test_calibcurve_apply_two_points(void) {
    CalibCurve c;
    const float raws[2] = { 20.10f, 35.40f };
    const float refs[2] = { 20.00f, 35.00f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, raws, refs, 2));
    /* Exact at the knots. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.00f, calibCurveApply(c, 20.10f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 35.00f, calibCurveApply(c, 35.40f));
    /* Midpoint of the raw span: offset halfway between -0.10 and -0.40. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 27.50f, calibCurveApply(c, 27.75f));
    /* Beyond the ends the end offset is held, not extrapolated. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.90f, calibCurveApply(c, 19.00f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 39.60f, calibCurveApply(c, 40.00f));
}

void test_calibcurve_apply_five_points(void) {
    CalibCurve c;
    const float raws[5] = { 0.0f, 10.0f, 20.0f, 30.0f, 40.0f };
    const float refs[5] = { 0.5f, 10.0f, 19.5f, 30.2f, 40.0f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, raws, refs, 5));
    /* Every knot lands exactly on its reference. */
    for (uint8_t i = 0; i < 5; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001f, refs[i], calibCurveApply(c, raws[i]));
    }
    /* Interior segments interpolate the offset linearly. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.75f, calibCurveApply(c, 15.0f)); /* off 0.0 -> -0.5 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.85f, calibCurveApply(c, 25.0f)); /* off -0.5 -> 0.2 */
}

void test_calibcurve_from_offset(void) {
    CalibCurve c;
    calibCurveFromOffset(c, 0.30f);
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.30f, calibCurveApply(c, 25.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -9.70f, calibCurveApply(c, -10.0f));
    /* Anchor-free: encodes to "" so the CSV row stays 4 columns. */
    char buf[CALIB_PTS_BUF];
    TEST_ASSERT_EQUAL_UINT(0, calibCurveEncodePts(c, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
    /* Offset zero is not a correction at all. */
    calibCurveFromOffset(c, 0.0f);
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
}

void test_calibcurve_encode_decode_roundtrip(void) {
    const float raws[5] = { -10.25f, 0.0f, 21.37f, 100.0f, 1013.25f };
    const float refs[5] = { -10.00f, 0.3f, 21.00f, 100.5f, 1010.00f };
    for (uint8_t count = 1; count <= 5; count++) {
        CalibCurve a, b;
        TEST_ASSERT_TRUE(calibCurveBuild(a, raws, refs, count));
        char buf[CALIB_PTS_BUF];
        TEST_ASSERT_TRUE(calibCurveEncodePts(a, buf, sizeof(buf)) > 0);
        TEST_ASSERT_TRUE(calibCurveDecodePts(buf, b));
        TEST_ASSERT_EQUAL_UINT8(a.n, b.n);
        for (uint8_t i = 0; i < a.n; i++) {
            /* %.2f granularity: half a hundredth of slack. */
            TEST_ASSERT_FLOAT_WITHIN(0.006f, a.raw[i], b.raw[i]);
            TEST_ASSERT_FLOAT_WITHIN(0.011f, a.off[i], b.off[i]);
        }
    }
}

void test_calibcurve_encode_is_flat_csv_cells(void) {
    /* The user-facing contract: every number is its own CSV cell, so a
     * spreadsheet opens one value per column. Exact-string pin. */
    CalibCurve c;
    const float raws[2] = { 20.90f, 24.90f };
    const float refs[2] = { 21.90f, 25.30f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, raws, refs, 2));
    char buf[CALIB_PTS_BUF];
    TEST_ASSERT_TRUE(calibCurveEncodePts(c, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("20.90,21.90,24.90,25.30", buf);
}

void test_calibcurve_decode_sorts_and_tolerates(void) {
    CalibCurve a, b;
    /* Out-of-order input decodes to the same curve as sorted input. */
    TEST_ASSERT_TRUE(calibCurveDecodePts("35.40,35.00,20.10,20.00", a));
    TEST_ASSERT_TRUE(calibCurveDecodePts("20.10,20.00,35.40,35.00", b));
    TEST_ASSERT_EQUAL_UINT8(a.n, b.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, b.raw[0], a.raw[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, b.off[1], a.off[1]);
    /* Separator-agnostic: the packed bench-era form reads as the same curve. */
    TEST_ASSERT_TRUE(calibCurveDecodePts("20.10:20.00;35.40:35.00", b));
    TEST_ASSERT_EQUAL_UINT8(a.n, b.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, b.raw[0], a.raw[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, b.off[1], a.off[1]);
    /* Spaces, a trailing separator, a CR off a hand-edited file. */
    TEST_ASSERT_TRUE(calibCurveDecodePts(" 20.10 , 20.00 , 35.40,35.00, \r", a));
    TEST_ASSERT_EQUAL_UINT8(2, a.n);
    /* Empty and NULL are the legacy 4-column tail, not errors. */
    TEST_ASSERT_TRUE(calibCurveDecodePts("", a));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(a));
    TEST_ASSERT_TRUE(calibCurveDecodePts(nullptr, a));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(a));
}

void test_calibcurve_decode_rejects_malformed(void) {
    CalibCurve c;
    TEST_ASSERT_FALSE(calibCurveDecodePts("a,b", c));
    TEST_ASSERT_FALSE(calibCurveDecodePts("1,2,3", c));   /* odd count: a raw without its ref */
    TEST_ASSERT_FALSE(calibCurveDecodePts("1:2:3", c));   /* odd through any separator */
    TEST_ASSERT_FALSE(calibCurveDecodePts("5", c));       /* a single lonely value */
    TEST_ASSERT_FALSE(calibCurveDecodePts("1,,2", c));    /* empty cell mid-list */
    TEST_ASSERT_FALSE(calibCurveDecodePts("1,1,2,2,3,3,4,4,5,5,6,6", c)); /* sixth pair */
    TEST_ASSERT_FALSE(calibCurveDecodePts("20.10,20.00,20.10,21.00", c)); /* duplicate raw */
    /* A failed decode leaves identity behind, never half a curve. */
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
}


void test_calibcurve_smooth_monotone_cubic(void) {
    /* Fritsch-Carlson on the offsets: through every anchor, never past any,
     * flat into the held zones. Δ knots +0.8 / −0.6 / +0.5 change sign at
     * the middle anchor, which forces its slope to 0 — the overshoot killer. */
    CalibCurve lin, cub;
    const float raws[3] = { 0.0f, 20.0f, 40.0f };
    const float refs[3] = { 0.8f, 19.4f, 40.5f };
    TEST_ASSERT_TRUE(calibCurveBuild(lin, raws, refs, 3, CALIB_MODE_LINEAR));
    TEST_ASSERT_TRUE(calibCurveBuild(cub, raws, refs, 3, CALIB_MODE_SMOOTH));
    for (uint8_t i = 0; i < 3; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0005f, refs[i], calibCurveApply(lin, raws[i]));
        TEST_ASSERT_FLOAT_WITHIN(0.0005f, refs[i], calibCurveApply(cub, raws[i]));
    }
    /* Genuinely a different function between anchors... */
    TEST_ASSERT_TRUE(fabsf(calibCurveApply(cub, 5.0f) - calibCurveApply(lin, 5.0f)) > 0.05f);
    /* ...that never leaves the segment's offset envelope (no overshoot). */
    for (float x = 0.0f; x <= 40.0f; x += 0.5f) {
        float d = calibCurveApply(cub, x) - x;
        TEST_ASSERT_TRUE(d <= 0.8f + 0.001f);
        TEST_ASSERT_TRUE(d >= -0.6f - 0.001f);
    }
    /* End slope 0: the curve meets the held zone without a kink. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, calibCurveApply(cub, 0.0f) - 0.0f,
                                    calibCurveApply(cub, 0.4f) - 0.4f);
}

void test_calibcurve_smooth_small_n_is_linear(void) {
    /* With two anchors the monotone cubic IS the straight line, so SMOOTH
     * below 3 points must evaluate identically to LINEAR. */
    CalibCurve s, l;
    const float raws[2] = { 10.0f, 30.0f };
    const float refs[2] = { 10.5f, 29.8f };
    TEST_ASSERT_TRUE(calibCurveBuild(s, raws, refs, 2, CALIB_MODE_SMOOTH));
    TEST_ASSERT_TRUE(calibCurveBuild(l, raws, refs, 2, CALIB_MODE_LINEAR));
    for (float x = 5.0f; x <= 35.0f; x += 2.5f) {
        TEST_ASSERT_FLOAT_WITHIN(0.0005f, calibCurveApply(l, x), calibCurveApply(s, x));
    }
}

void test_calibrow_mode_token(void) {
    CalibCurve c; char name[40];
    /* name,cub,pairs — even count whose second field is the mode vocabulary */
    TEST_ASSERT_TRUE(calibRowParseTail("AMBIENTE,cub,0.00,0.80,20.00,19.40,40.00,40.50", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(3, c.n);
    TEST_ASSERT_EQUAL_UINT8(CALIB_MODE_SMOOTH, c.mode);
    TEST_ASSERT_EQUAL_STRING("AMBIENTE", name);
    /* hand-edited lin token reads too, though the writer never emits it */
    TEST_ASSERT_TRUE(calibRowParseTail("AMBIENTE,lin,1.00,1.50", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(CALIB_MODE_LINEAR, c.mode);
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    /* the transitional even shape still dispatches by its numeric first field */
    TEST_ASSERT_TRUE(calibRowParseTail("9.99,GELADEIRA,1.00,1.50", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(CALIB_MODE_LINEAR, c.mode);
    TEST_ASSERT_EQUAL_STRING("GELADEIRA", name);
}

void test_calibrow_format_shapes(void) {
    /* The one write-side authority: web rewrite and boot ROM binder both go
     * through calibRowFormat, so the shapes are pinned here once. */
    char line[352];
    CalibCurve c;
    /* identity -> 3 cells */
    TEST_ASSERT_TRUE(calibRowFormat(line, sizeof(line), "KEY", "ID", "NAME", c) > 0);
    TEST_ASSERT_EQUAL_STRING("KEY,ID,NAME", line);
    /* anchor-free constant -> legacy 4-column */
    calibCurveFromOffset(c, 0.5f);
    TEST_ASSERT_TRUE(calibRowFormat(line, sizeof(line), "KEY", "ID", "NAME", c) > 0);
    TEST_ASSERT_EQUAL_STRING("KEY,ID,0.50,NAME", line);
    /* anchored linear -> name then flat cells */
    const float r1[1] = { 1.0f }, v1[1] = { 1.5f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, r1, v1, 1));
    TEST_ASSERT_TRUE(calibRowFormat(line, sizeof(line), "KEY", "ID", "NAME", c) > 0);
    TEST_ASSERT_EQUAL_STRING("KEY,ID,NAME,1.00,1.50", line);
    /* smooth -> cub cell after the name */
    const float r3[3] = { 0.0f, 10.0f, 20.0f }, v3[3] = { 0.5f, 10.0f, 20.2f };
    TEST_ASSERT_TRUE(calibCurveBuild(c, r3, v3, 3, CALIB_MODE_SMOOTH));
    TEST_ASSERT_TRUE(calibRowFormat(line, sizeof(line), "KEY", "ID", "NAME", c) > 0);
    TEST_ASSERT_EQUAL_STRING("KEY,ID,NAME,cub,0.00,0.50,10.00,10.00,20.00,20.20", line);
    /* and the parser reads its own writer back */
    CalibCurve back; char name[40];
    TEST_ASSERT_TRUE(calibRowParseTail(strchr(strchr(line, ',') + 1, ',') + 1, back, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(3, back.n);
    TEST_ASSERT_EQUAL_UINT8(CALIB_MODE_SMOOTH, back.mode);
    TEST_ASSERT_EQUAL_STRING("NAME", name);
}

void test_calibrow_parse_tail_shapes(void) {
    /* The row shape is identified by field count — this is the contract the
     * whole file format now stands on. */
    CalibCurve c; char name[40];
    /* 1 field: identity DB row (DS18B20 ROM->id/name), no correction. */
    TEST_ASSERT_TRUE(calibRowParseTail("GELADEIRA", c, name, sizeof(name)));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
    TEST_ASSERT_EQUAL_STRING("GELADEIRA", name);
    /* 2 fields: legacy offset,name -> anchor-free constant offset. */
    TEST_ASSERT_TRUE(calibRowParseTail("0.50,GELADEIRA", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.50f, calibCurveApply(c, 23.0f));
    TEST_ASSERT_EQUAL_STRING("GELADEIRA", name);
    /* Legacy row with an empty name keeps its offset — trailing-empty
     * stripping must not eat the name field and shift the shape. */
    TEST_ASSERT_TRUE(calibRowParseTail("0.50,", c, name, sizeof(name)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, calibCurveOffsetAt(c, NAN));
    TEST_ASSERT_EQUAL_STRING("", name);
    /* Odd >= 3: canonical name,raw,ref[,...]. */
    TEST_ASSERT_TRUE(calibRowParseTail("GELADEIRA,1.00,1.50", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.00f, c.raw[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, c.off[0]);
    TEST_ASSERT_EQUAL_STRING("GELADEIRA", name);
    TEST_ASSERT_TRUE(calibRowParseTail("AMBIENTE,20.80,21.80,24.80,25.20,", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(2, c.n); /* trailing comma tolerated */
    /* Even >= 4: transitional offset,name,pairs — the pairs win. */
    TEST_ASSERT_TRUE(calibRowParseTail("9.99,GELADEIRA,1.00,1.50", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.00f, c.raw[0]);
    TEST_ASSERT_EQUAL_STRING("GELADEIRA", name);
}

void test_calibrow_parse_tail_fallbacks(void) {
    CalibCurve c; char name[40];
    /* Even shape with broken cells falls back to its offset column. */
    TEST_ASSERT_FALSE(calibRowParseTail("0.50,NAME,1.00,xx", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, calibCurveOffsetAt(c, NAN));
    /* Canonical shape with broken cells has no offset to fall back on. */
    TEST_ASSERT_FALSE(calibRowParseTail("NAME,1.00,xx", c, name, sizeof(name)));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
    /* Bench-era packed row: the ';'-joined pair cell carries no commas, so
     * the count reads one off — the numeric first field is rescued as the
     * offset instead of zeroing a real correction. */
    TEST_ASSERT_FALSE(calibRowParseTail("0.50,AMBIENTE,20.90:21.90;24.90:25.30", c, name, sizeof(name)));
    TEST_ASSERT_EQUAL_UINT8(1, c.n);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, calibCurveOffsetAt(c, NAN));
    TEST_ASSERT_EQUAL_STRING("AMBIENTE", name);
    /* Empty tail is the shape of a row that ends at the id — identity. */
    TEST_ASSERT_TRUE(calibRowParseTail("", c, name, sizeof(name)));
    TEST_ASSERT_TRUE(calibCurveIsIdentity(c));
}


/* ===========================================================================
 * JSON SLICE (jsonMatchEnd)
 *
 * The walkers this replaces sliced elements at the first '}' — fine until an
 * element contains a nested object, at which point trailing keys silently
 * fall off (the commit_all "al" field did exactly that). These tests pin the
 * three ways the naive scan went wrong: nesting, brackets inside string
 * literals, and escaped quotes.
 * =========================================================================== */

void test_jsonMatchEnd_flat(void) {
    String s("{\"a\":1}");
    TEST_ASSERT_EQUAL_INT(6, jsonMatchEnd(s, 0));
    String arr("[1,2,3]");
    TEST_ASSERT_EQUAL_INT(6, jsonMatchEnd(arr, 0));
}

void test_jsonMatchEnd_nested(void) {
    /* The commit_all slot shape: "al" sits after the nested lim{}. */
    String s("{\"i\":4,\"lim\":{\"temp\":[0,40]},\"al\":true}");
    const int end = jsonMatchEnd(s, 0);
    TEST_ASSERT_EQUAL_INT((int)s.length() - 1, end);
    /* The calibration shape: pair arrays inside an object inside an object. */
    String cal("{\"slot\":0,\"cal\":{\"temp\":[[20.1,20.0],[35.4,35.0]]}}");
    TEST_ASSERT_EQUAL_INT((int)cal.length() - 1, jsonMatchEnd(cal, 0));
    /* Matching an inner array from its own opening bracket. */
    String inner("[[1,2],[3,4]]");
    TEST_ASSERT_EQUAL_INT(5, jsonMatchEnd(inner, 1));
}

void test_jsonMatchEnd_brackets_inside_strings(void) {
    String s("[\"a]b\",2]");
    TEST_ASSERT_EQUAL_INT((int)s.length() - 1, jsonMatchEnd(s, 0));
    String t("{\"name\":\"chao {mido}\"}");
    TEST_ASSERT_EQUAL_INT((int)t.length() - 1, jsonMatchEnd(t, 0));
}

void test_jsonMatchEnd_escaped_quotes(void) {
    /* {"k":"a\"}b"} — the escaped quote must not end the string, and the
     * brace inside the literal must not close the object. */
    String s("{\"k\":\"a\\\"}b\"}");
    TEST_ASSERT_EQUAL_INT((int)s.length() - 1, jsonMatchEnd(s, 0));
}

void test_jsonMatchEnd_invalid(void) {
    String open("{\"a\":1");
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(open, 0));
    String arr("[1,2");
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(arr, 0));
    String mixed("{\"a\":1]");
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(mixed, 0));
    String notBracket("x{}");
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(notBracket, 0));
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(notBracket, -1));
    TEST_ASSERT_EQUAL_INT(-1, jsonMatchEnd(notBracket, 99));
}

/* ===========================================================================
 * /api/commit_all — per-section authorization
 * ===========================================================================
 * The route multiplexes six sections under three permission bits. One gate on
 * the route cannot express who may change what, and for a while there was only
 * one: PERM_SYS_CONFIG got you in, and every section then parsed regardless —
 * so a config operator could add administrators and re-point the Wi-Fi.
 *
 * These are the positive control. Each escalation payload below is asserted to
 * be REFUSED, and the legitimate payload of the same operator asserted to pass,
 * because a gate that refuses everything would satisfy the first half alone.
 */

/* The permission sets that actually ship: what /config, /network and /users
 * each require to render, plus the built-in admin. */
static const uint16_t P_CONFIG_ONLY = PERM_SYS_CONFIG;
static const uint16_t P_USERMGR_ONLY = PERM_USER_MGR;
static const uint16_t P_NETONLY = PERM_NET_CONFIG;
static const uint16_t P_ADMIN = PERM_FULL_ADMIN;
static const uint16_t P_VIEWER = PERM_DASHBOARD | PERM_HISTORY;

void test_commit_sys_operator_cannot_add_users(void) {
    int st[SEC_COUNT];
    /* The escalation from the audit: perms 1023 = PERM_ALL_BITS. */
    String esc("{\"users\":{\"actions\":[{\"type\":\"add\",\"name\":\"svc\",\"perms\":1023}]}}");
    TEST_ASSERT_EQUAL_INT(SEC_USERS, commitScanSections(esc, P_CONFIG_ONLY, st));
    /* Buried in a legitimate-looking sys commit — same verdict. */
    String mixed("{\"sys\":{\"name\":\"SIMUT\"},\"users\":{\"actions\":[{\"type\":\"reset\",\"id\":1}]}}");
    TEST_ASSERT_EQUAL_INT(SEC_USERS, commitScanSections(mixed, P_CONFIG_ONLY, st));
}

void test_commit_sys_operator_cannot_change_net(void) {
    int st[SEC_COUNT];
    String esc("{\"net\":{\"ssid\":\"evil\",\"pass\":\"hunter2\"}}");
    TEST_ASSERT_EQUAL_INT(SEC_NET, commitScanSections(esc, P_CONFIG_ONLY, st));
}

/* The gate must not have become a wall: the same operator's own sections
 * still commit. Without this the tests above pass on a handler that refuses
 * every payload it is given. */
void test_commit_sys_operator_keeps_own_sections(void) {
    int st[SEC_COUNT];
    String ok("{\"sys\":{\"name\":\"SIMUT\",\"tz\":\"-3\"},\"alarms\":{\"sensors\":[]},"
              "\"slots\":[],\"calib\":{\"sensors\":[]}}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_OK, commitScanSections(ok, P_CONFIG_ONLY, st));
    /* And the offsets it hands the parsers are real. */
    TEST_ASSERT_TRUE(st[SEC_SYS] >= 0);
    TEST_ASSERT_TRUE(st[SEC_ALARMS] >= 0);
    TEST_ASSERT_TRUE(st[SEC_SLOTS] >= 0);
    TEST_ASSERT_TRUE(st[SEC_CALIB] >= 0);
    TEST_ASSERT_EQUAL_INT(-1, st[SEC_NET]);
    TEST_ASSERT_EQUAL_INT(-1, st[SEC_USERS]);
}

/* A pure user-manager could open /users, stage an account, and then have the
 * commit refused by the old route gate — the role could not do its one job. */
void test_commit_usermgr_can_commit_users_only(void) {
    int st[SEC_COUNT];
    String users("{\"users\":{\"actions\":[{\"type\":\"add\",\"name\":\"op\",\"perms\":3}]}}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_OK, commitScanSections(users, P_USERMGR_ONLY, st));
    /* But nothing else. */
    String andSys("{\"sys\":{\"name\":\"x\"},\"users\":{\"actions\":[]}}");
    TEST_ASSERT_EQUAL_INT(SEC_SYS, commitScanSections(andSys, P_USERMGR_ONLY, st));
}

void test_commit_netonly_can_commit_net_only(void) {
    int st[SEC_COUNT];
    String net("{\"net\":{\"ssid\":\"lab\",\"use_dhcp\":1}}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_OK, commitScanSections(net, P_NETONLY, st));
    String andUsers("{\"net\":{\"ssid\":\"lab\"},\"users\":{\"actions\":[]}}");
    TEST_ASSERT_EQUAL_INT(SEC_USERS, commitScanSections(andUsers, P_NETONLY, st));
}

void test_commit_admin_passes_everything(void) {
    int st[SEC_COUNT];
    String all("{\"sys\":{},\"slots\":[],\"calib\":{},\"alarms\":{},"
               "\"net\":{\"ssid\":\"lab\"},\"users\":{\"actions\":[]}}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_OK, commitScanSections(all, P_ADMIN, st));
    for (int i = 0; i < SEC_COUNT; i++) TEST_ASSERT_TRUE(st[i] >= 0);
}

/* The viewer never reaches commitScanSections — the route's front door turns
 * it away first — but the front door is the thing being asserted here. */
void test_commit_entry_perms_exclude_viewer(void) {
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)(P_VIEWER & commitEntryPerms()));
    TEST_ASSERT_TRUE((P_CONFIG_ONLY & commitEntryPerms()) != 0);
    TEST_ASSERT_TRUE((P_USERMGR_ONLY & commitEntryPerms()) != 0);
    TEST_ASSERT_TRUE((P_NETONLY & commitEntryPerms()) != 0);
}

/* The reason the scan is flat rather than depth-aware. A gate that walked
 * only top-level keys would see one `sys` section here and wave it through,
 * while the flat parser in WebManager_Commit.cpp finds `"users"` anywhere in
 * the body and acts on it. The refusal below IS the property. */
void test_commit_nested_users_does_not_evade(void) {
    int st[SEC_COUNT];
    String nested("{\"sys\":{\"users\":{\"actions\":[{\"type\":\"add\",\"name\":\"svc\",\"perms\":1023}]}}}");
    TEST_ASSERT_EQUAL_INT(SEC_USERS, commitScanSections(nested, P_CONFIG_ONLY, st));
    String inArray("{\"slots\":[{\"n\":\"a\"}],\"x\":[\"net\"]}");
    TEST_ASSERT_EQUAL_INT(SEC_NET, commitScanSections(inArray, P_CONFIG_ONLY, st));
}

/* An empty or unrecognised payload is a refusal, not a no-op commit: the
 * handler reboots the device at the end whether or not a field changed. */
void test_commit_empty_payload_is_refused(void) {
    int st[SEC_COUNT];
    String empty("{}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_EMPTY, commitScanSections(empty, P_ADMIN, st));
    String junk("{\"nope\":{\"a\":1}}");
    TEST_ASSERT_EQUAL_INT(COMMIT_AUTH_EMPTY, commitScanSections(junk, P_ADMIN, st));
    for (int i = 0; i < SEC_COUNT; i++) TEST_ASSERT_EQUAL_INT(-1, st[i]);
}

/* Denial must not truncate the map: the parsers read outStart[] and would
 * otherwise slice the wrong bytes if a future caller kept going after a 403. */
void test_commit_denial_still_fills_offsets(void) {
    int st[SEC_COUNT];
    String mixed("{\"sys\":{\"name\":\"x\"},\"users\":{\"actions\":[]},\"net\":{\"ssid\":\"l\"}}");
    TEST_ASSERT_EQUAL_INT(SEC_NET, commitScanSections(mixed, P_CONFIG_ONLY, st));
    TEST_ASSERT_TRUE(st[SEC_SYS] >= 0);
    TEST_ASSERT_TRUE(st[SEC_USERS] >= 0);
    TEST_ASSERT_TRUE(st[SEC_NET] >= 0);
}

/* Every row must name a real permission and a real section. A row added with
 * perm 0 would be a section nobody needs a bit for. */
void test_commit_section_table_is_sane(void) {
    for (int i = 0; i < SEC_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(kCommitSectionRules[i].needle);
        TEST_ASSERT_NOT_NULL(kCommitSectionRules[i].name);
        TEST_ASSERT_TRUE(kCommitSectionRules[i].perm != 0);
        /* The needle must be the quoted key the parser searches for. */
        TEST_ASSERT_EQUAL_CHAR('"', kCommitSectionRules[i].needle[0]);
    }
}


/* ===========================================================================
 * isSecretFsPath — the /config download guard (finding A-4)
 * ===========================================================================
 * Positive control included: the legitimate downloads (history, calib) must
 * still pass, or a guard that returns true for everything would satisfy the
 * "secrets are blocked" half alone and quietly break the file manager.
 */
void test_secret_path_blocks_config(void) {
    TEST_ASSERT_TRUE(isSecretFsPath("/config/system.bin"));
    TEST_ASSERT_TRUE(isSecretFsPath("/config/system.bak"));
    TEST_ASSERT_TRUE(isSecretFsPath("/config/t_cursor.bin"));
}

void test_secret_path_normalises_spelling(void) {
    TEST_ASSERT_TRUE(isSecretFsPath("config/system.bin"));   /* no leading slash */
    TEST_ASSERT_TRUE(isSecretFsPath("/CONFIG/system.bin"));  /* case */
    TEST_ASSERT_TRUE(isSecretFsPath("/Config/System.bin"));
}

void test_secret_path_allows_legit_downloads(void) {
    TEST_ASSERT_FALSE(isSecretFsPath("/history/20260101.h5"));
    TEST_ASSERT_FALSE(isSecretFsPath("/calib.csv"));
    TEST_ASSERT_FALSE(isSecretFsPath("/themes/dark.thm"));
    TEST_ASSERT_FALSE(isSecretFsPath("/lang/language_pt-BR.lng"));
}

void test_secret_path_no_sibling_overmatch(void) {
    /* "/config" alone is the dir, not a file under it (you cannot download a
     * dir); and a sibling that merely starts with "config" must not be caught. */
    TEST_ASSERT_FALSE(isSecretFsPath("/config"));
    TEST_ASSERT_FALSE(isSecretFsPath("/configuration/notes.txt"));
    TEST_ASSERT_FALSE(isSecretFsPath("/config-backup/x"));
    TEST_ASSERT_FALSE(isSecretFsPath(""));
}


/* ===========================================================================
 * isValidCorsOrigin — what may be written into Access-Control-Allow-Origin
 * ===========================================================================
 * The value reaches a response header verbatim, so the test that matters is
 * the injection one: a CR or LF inside it would end the header and let whoever
 * wrote /config/cors.txt append headers of their own to every response the
 * device sends. The rest of the cases are the ones that would otherwise be
 * debugged as "CORS is broken" — a trailing slash makes the browser compare
 * "http://x/" against its own "http://x", find them different, and block.
 */
void test_cors_origin_accepts_real_ones(void) {
    TEST_ASSERT_TRUE(isValidCorsOrigin("http://192.168.1.10:8080"));
    TEST_ASSERT_TRUE(isValidCorsOrigin("https://gerenciador.hospital.local"));
    TEST_ASSERT_TRUE(isValidCorsOrigin("https://frota.local:8443"));
    TEST_ASSERT_TRUE(isValidCorsOrigin("http://localhost:3000"));
    TEST_ASSERT_TRUE(isValidCorsOrigin("http://[2001:db8::1]:8080"));  /* IPv6 literal */
}

void test_cors_origin_rejects_header_injection(void) {
    /* The whole reason this function exists. */
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x\r\nSet-Cookie: a=b"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x\nAccess-Control-Allow-Origin: *"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x\r"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x\n"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x y"));      /* a space ends a header value too */
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://x\tz"));
}

void test_cors_origin_rejects_wildcard_and_schemes(void) {
    /* "*" is the thing this mechanism exists to avoid, and file:// is the
     * origin an operator gets by double-clicking the page instead of serving
     * it — "null" in the browser, and it must never be what the device trusts. */
    TEST_ASSERT_FALSE(isValidCorsOrigin("*"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://*"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("null"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("file:///home/op/frota.html"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("ftp://192.168.1.10"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("192.168.1.10:8080"));   /* no scheme */
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://"));             /* scheme only */
    TEST_ASSERT_FALSE(isValidCorsOrigin("https://"));
}

void test_cors_origin_rejects_path_and_size(void) {
    /* A path is not part of an origin: the browser would compare its own
     * "http://x" against a sent "http://x/" and block. */
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://192.168.1.10:8080/"));
    TEST_ASSERT_FALSE(isValidCorsOrigin("http://192.168.1.10/frota.html"));
    TEST_ASSERT_FALSE(isValidCorsOrigin(""));
    TEST_ASSERT_TRUE (isValidCorsOrigin("http://x"));            /* 8 chars: the floor, and valid */
    TEST_ASSERT_FALSE(isValidCorsOrigin("http:/x"));             /* 7: one slash short */

    /* The ceiling, checked from both sides. An off-by-one here rejects a
     * legitimate hostname, and the operator reads that as "CORS is broken on
     * this site" — the failure this whole page exists to stop producing. */
    String noLimite = "http://";
    while ((int)noLimite.length( ) < CORS_ORIGIN_MAX_LEN) noLimite += "a";
    TEST_ASSERT_EQUAL(CORS_ORIGIN_MAX_LEN, (int)noLimite.length( ));
    TEST_ASSERT_TRUE (isValidCorsOrigin(noLimite));
    TEST_ASSERT_FALSE(isValidCorsOrigin(noLimite + "a"));
}

void test_secret_path_traversal_is_callers_job(void) {
    /* isSecretFsPath does NOT resolve "..": a "/history/../config/system.bin"
     * does not start with "/config/" and returns false here. The caller
     * (handleDownload) rejects ".." before ever calling this — the two guards
     * are separate on purpose, and this pins that contract so a later reader
     * does not assume this function catches traversal. */
    TEST_ASSERT_FALSE(isSecretFsPath("/history/../config/system.bin"));
}

/* ===========================================================================
 * isSecretFsDir — the /api/ls directory guard (finding ACH-04)
 * ===========================================================================
 * isSecretFsPath matches only "/config/..." (files), so the bare "/config" a
 * directory listing receives would slip past it. isSecretFsDir folds the dir
 * and its contents into one check, and the ls handler calls it.
 */
void test_secret_dir_blocks_bare_and_nested(void) {
    TEST_ASSERT_TRUE(isSecretFsDir("/config"));
    TEST_ASSERT_TRUE(isSecretFsDir("config"));
    TEST_ASSERT_TRUE(isSecretFsDir("/CONFIG"));
    TEST_ASSERT_TRUE(isSecretFsDir("/config/system.bin"));
    TEST_ASSERT_TRUE(isSecretFsDir("config/system.bin"));
}

void test_secret_dir_allows_legit_dirs(void) {
    TEST_ASSERT_FALSE(isSecretFsDir("/"));
    TEST_ASSERT_FALSE(isSecretFsDir("/history"));
    TEST_ASSERT_FALSE(isSecretFsDir("/themes"));
    TEST_ASSERT_FALSE(isSecretFsDir("/lang"));
    TEST_ASSERT_FALSE(isSecretFsDir("/configuration"));
    TEST_ASSERT_FALSE(isSecretFsDir("/config-backup"));
    TEST_ASSERT_FALSE(isSecretFsDir(""));
}

/* ===========================================================================
 * isSafeDirPath — /api/mkdir folder-name guard (finding M-7)
 * ===========================================================================
 * Positive control included: the legitimate folder names must pass, or an
 * allowlist that rejects everything would satisfy the "XSS blocked" half alone
 * and make the Create-Folder button useless.
 */
void test_dirpath_accepts_legit(void) {
    TEST_ASSERT_TRUE(isSafeDirPath("test"));
    TEST_ASSERT_TRUE(isSafeDirPath("logs2"));
    TEST_ASSERT_TRUE(isSafeDirPath("my data"));
    TEST_ASSERT_TRUE(isSafeDirPath("sub-dir_1"));
    TEST_ASSERT_TRUE(isSafeDirPath("/backups/2026"));
    TEST_ASSERT_TRUE(isSafeDirPath("a.d"));
}

void test_dirpath_blocks_xss_bytes(void) {
    TEST_ASSERT_FALSE(isSafeDirPath("<img src=x onerror=alert(1)>"));
    TEST_ASSERT_FALSE(isSafeDirPath("a<b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a>b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a\"b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a'b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a&b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a`b"));
}

void test_dirpath_blocks_path_and_url_bytes(void) {
    TEST_ASSERT_FALSE(isSafeDirPath(".."));
    TEST_ASSERT_FALSE(isSafeDirPath("a/../b"));
    TEST_ASSERT_FALSE(isSafeDirPath("...."));         /* the replace("..","") bypass */
    TEST_ASSERT_FALSE(isSafeDirPath("a%2e"));
    TEST_ASSERT_FALSE(isSafeDirPath("a\\b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a:b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a|b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a?b"));
    TEST_ASSERT_FALSE(isSafeDirPath("a*b"));
}

void test_dirpath_blocks_empty_control_and_long(void) {
    TEST_ASSERT_FALSE(isSafeDirPath(""));
    TEST_ASSERT_FALSE(isSafeDirPath(NULL));
    char ctrl[4] = { 'a', 0x07, 'b', 0 };             /* bell */
    TEST_ASSERT_FALSE(isSafeDirPath(ctrl));
    char longName[110];
    for (int i = 0; i < 109; i++) longName[i] = 'a';
    longName[109] = '\0';
    TEST_ASSERT_FALSE(isSafeDirPath(longName));
}

/* ===========================================================================
 * passwordPolicyOk — server-side strength floor (finding A-5)
 * ===========================================================================
 * Positive control: strong passwords must pass, or a floor that rejects
 * everything would satisfy the "weak blocked" half alone and lock everyone out.
 */
void test_pwpolicy_accepts_strong(void) {
    TEST_ASSERT_TRUE(passwordPolicyOk("simut2026"));
    TEST_ASSERT_TRUE(passwordPolicyOk("Abc12345"));
    TEST_ASSERT_TRUE(passwordPolicyOk("a1b2c3d4"));
    TEST_ASSERT_TRUE(passwordPolicyOk("Longer P4ss with spaces"));
}

void test_pwpolicy_rejects_weak(void) {
    TEST_ASSERT_FALSE(passwordPolicyOk(""));
    TEST_ASSERT_FALSE(passwordPolicyOk(NULL));
    TEST_ASSERT_FALSE(passwordPolicyOk("short1"));      /* < 8 */
    TEST_ASSERT_FALSE(passwordPolicyOk("abcdefgh"));    /* no digit */
    TEST_ASSERT_FALSE(passwordPolicyOk("12345678"));    /* no letter */
    TEST_ASSERT_FALSE(passwordPolicyOk("!!!!!!!!"));    /* neither */
    TEST_ASSERT_FALSE(passwordPolicyOk("a1b2c3"));      /* 6 chars */
}


/* ===========================================================================
 *  hwId AS A KEY — isValidHwId (finding O-2)
 *
 *  An hwId is not free text. It is a JSON string in /api/status, the column
 *  header of the telemetry CSV, and a field name in the telemetry JSON. The
 *  old check was isValidCfgString, which only refuses control bytes, so `X\`
 *  was a legal ID: the dashboard stopped parsing for every user of the device
 *  AND the collector wrote a corrupt payload to disk, from one edit, with
 *  nothing naming the cause.
 * =========================================================================== */
void test_hwid_accepts_real_ids(void) {
    TEST_ASSERT_TRUE(isValidHwId("DHT2202"));      /* the auto-generated form */
    TEST_ASSERT_TRUE(isValidHwId("28FF0A1B"));     /* a DS18B20 ROM prefix */
    TEST_ASSERT_TRUE(isValidHwId("sala_2"));
    TEST_ASSERT_TRUE(isValidHwId("probe-A"));
    TEST_ASSERT_TRUE(isValidHwId("X"));            /* one char is enough */
    TEST_ASSERT_TRUE(isValidHwId("123456789012345"));   /* exactly 15 */
}

void test_hwid_rejects_key_breakers(void) {
    TEST_ASSERT_FALSE(isValidHwId(NULL));
    TEST_ASSERT_FALSE(isValidHwId(""));                  /* empty */
    TEST_ASSERT_FALSE(isValidHwId("1234567890123456"));  /* 16 */
    TEST_ASSERT_FALSE(isValidHwId("a\\b"));              /* escapes the quote */
    TEST_ASSERT_FALSE(isValidHwId("a\"b"));              /* closes the string */
    TEST_ASSERT_FALSE(isValidHwId("a,b"));               /* splits the CSV row */
    TEST_ASSERT_FALSE(isValidHwId("a.b"));               /* topic separator */
    TEST_ASSERT_FALSE(isValidHwId("a b"));
    TEST_ASSERT_FALSE(isValidHwId("a;b"));
    TEST_ASSERT_FALSE(isValidHwId("a\nb"));
}

/* ===========================================================================
 *  LANGUAGE-PACK IDENTITY — langIdentSanitize (V-04)
 *
 *  @NAME and @CODE come out of an uploaded file and land in /api/perms, the
 *  first request every page of the UI makes. A quote there took the entire
 *  interface down, and it survived reboots because a pack is only read at boot.
 * =========================================================================== */
void test_langident_strips_json_breakers(void) {
    char out[16];
    langIdentSanitize("Po\"rt\\ugu", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Portugu", out);
}

void test_langident_keeps_legitimate_names(void) {
    char out[32];
    /* UTF-8 stays: the real pack is named "Portugues (Brasil)" with accents,
     * and JSON carries those bytes without escaping. */
    const char* src = "Portugu\xc3\xaas (Brasil)";
    langIdentSanitize(src, strlen(src), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(src, out);
}

void test_langident_terminates_and_respects_cap(void) {
    char out[4];
    memset(out, 'X', sizeof(out));
    langIdentSanitize("abcdefgh", 8, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("abc", out);          /* truncated, terminated */

    /* An all-bad input must produce an empty string, not leave the buffer as
     * it found it — the caller prints whatever is there. */
    char out2[8];
    memset(out2, 'X', sizeof(out2));
    langIdentSanitize("\"\"\"", 3, out2, sizeof(out2));
    TEST_ASSERT_EQUAL_STRING("", out2);

    /* Zero cap must not write. Nothing to assert but the absence of a crash;
     * ASAN in the fuzz job is what actually watches this one. */
    langIdentSanitize("abc", 3, out2, 0);
}

/* ===========================================================================
 *  /download PER-PATH PERMISSIONS — downloadPermFor (finding O-1)
 *
 *  The near-misses are the point: a prefix rule that also matched
 *  "/historyx/" or a suffix rule that matched "system.blog.bak" would gate
 *  the wrong files, and a rule that missed "/HISTORY/" would gate none of
 *  them on a filesystem that does not care about case.
 * =========================================================================== */
void test_download_perm_gates_history_and_logs(void) {
    TEST_ASSERT_EQUAL_UINT16(PERM_HISTORY, downloadPermFor("/history/2026-09-07.h5"));
    TEST_ASSERT_EQUAL_UINT16(PERM_HISTORY, downloadPermFor("history/2026-09-07.h5"));
    TEST_ASSERT_EQUAL_UINT16(PERM_HISTORY, downloadPermFor("/HISTORY/day.h5"));
    TEST_ASSERT_EQUAL_UINT16(PERM_LOGS,    downloadPermFor("/system.blog"));
    TEST_ASSERT_EQUAL_UINT16(PERM_LOGS,    downloadPermFor("/system.old.blog"));
    TEST_ASSERT_EQUAL_UINT16(PERM_LOGS,    downloadPermFor("/SYSTEM.BLOG"));
}

void test_download_perm_leaves_ordinary_files_alone(void) {
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/calib.csv"));
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/lang/language_pt-BR.lng"));
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/themes/dark.thm"));
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/historyx/f.h5"));   /* not history */
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/system.blog.bak")); /* not a log */
    TEST_ASSERT_EQUAL_UINT16(0, downloadPermFor("/blog"));
}

/* ===========================================================================
 *  SETUP-AP KEY — apPskFromDigest (finding V-05)
 *
 *  The setup access point used to be open, so anyone in radio range reached
 *  the captive portal of a device whose Wi-Fi had just failed. It is WPA2 now,
 *  with a key derived from the board id.
 *
 *  The failure that matters here is not a weak key, it is a SHORT one: WPA2
 *  refuses a passphrase under 8 characters, and beginAP falls back to an open
 *  AP when it has no key — so a helper that quietly emitted a truncated string
 *  would restore the exact finding while looking like the fix.
 * =========================================================================== */
void test_ap_psk_shape(void) {
    uint8_t digest[32];
    for (size_t i = 0; i < sizeof(digest); i++) digest[i] = (uint8_t)(i * 7 + 3);
    char psk[AP_PSK_LEN + 1];

    TEST_ASSERT_TRUE(apPskFromDigest(digest, sizeof(digest), psk, sizeof(psk)));
    TEST_ASSERT_EQUAL_size_t(AP_PSK_LEN, strlen(psk));
    TEST_ASSERT_TRUE(AP_PSK_LEN >= 8 && AP_PSK_LEN <= 63);   /* what WPA2 accepts */
    for (size_t i = 0; i < AP_PSK_LEN; i++)
        TEST_ASSERT_NOT_NULL(strchr(AP_PSK_ALPHABET, psk[i]));

    /* Deterministic: the key is printed on a label and must survive a reboot
     * and a factory reset, because the board id does. */
    char again[AP_PSK_LEN + 1];
    TEST_ASSERT_TRUE(apPskFromDigest(digest, sizeof(digest), again, sizeof(again)));
    TEST_ASSERT_EQUAL_STRING(psk, again);

    /* And it follows the digest, or every board would ship the same key. */
    digest[0] ^= 0xFF;
    TEST_ASSERT_TRUE(apPskFromDigest(digest, sizeof(digest), again, sizeof(again)));
    TEST_ASSERT_TRUE(strcmp(psk, again) != 0);
}

void test_ap_psk_refuses_rather_than_truncates(void) {
    uint8_t digest[32];
    memset(digest, 0x5A, sizeof(digest));
    char psk[AP_PSK_LEN + 1];

    /* Buffer too small: empty string and false, never a short key. */
    memset(psk, 'X', sizeof(psk));
    TEST_ASSERT_FALSE(apPskFromDigest(digest, sizeof(digest), psk, AP_PSK_LEN));
    TEST_ASSERT_EQUAL_STRING("", psk);

    /* Not enough digest bytes: same answer. */
    memset(psk, 'X', sizeof(psk));
    TEST_ASSERT_FALSE(apPskFromDigest(digest, AP_PSK_LEN - 1, psk, sizeof(psk)));
    TEST_ASSERT_EQUAL_STRING("", psk);

    /* Null inputs must not write or crash. */
    TEST_ASSERT_FALSE(apPskFromDigest(NULL, 32, psk, sizeof(psk)));
    TEST_ASSERT_FALSE(apPskFromDigest(digest, sizeof(digest), NULL, 16));
    TEST_ASSERT_FALSE(apPskFromDigest(digest, sizeof(digest), psk, 0));
}

/* ===========================================================================
 *  AUTHENTICATION LOCKOUT — authLockoutMs (SystemDefs_Network.h)
 *
 *  Shared by the web login and the Bluetooth CLI. The backoff itself is
 *  unremarkable; what these tests exist for is the ceiling, because the
 *  version this replaced computed `(1U << failCount) * 1000` and clamped the
 *  PRODUCT afterwards. That holds up to 28 and then fails open: at 29, 30 and
 *  31 the multiplication wraps to exactly zero (2^29 * 1000 = 125 * 2^32), so
 *  the penalty was zero milliseconds and the attacker who sat through the
 *  escalation was handed free attempts; past 31 the shift is undefined.
 *
 *  test_lockout_never_falls_below_the_ceiling is that regression, and it is
 *  written over the whole uint8_t domain rather than the three known-bad
 *  values: an off-by-one in the cap would move the cliff, not remove it.
 * =========================================================================== */
void test_lockout_backoff_doubles(void) {
    TEST_ASSERT_EQUAL_UINT32(2000u,   authLockoutMs(1));
    TEST_ASSERT_EQUAL_UINT32(4000u,   authLockoutMs(2));
    TEST_ASSERT_EQUAL_UINT32(8000u,   authLockoutMs(3));
    TEST_ASSERT_EQUAL_UINT32(256000u, authLockoutMs(8));
}

void test_lockout_reaches_and_holds_the_ceiling(void) {
    /* 1<<9 = 512 s, already past the 300 s ceiling. */
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_MS, authLockoutMs(9));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_MS, authLockoutMs(AUTH_FAIL_CAP));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_MS, authLockoutMs(AUTH_FAIL_CAP + 1));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_MS, authLockoutMs(255));
}

void test_lockout_never_falls_below_the_ceiling(void) {
    /* Zero failures is the only input allowed to produce a short delay. */
    for (unsigned fc = 9; fc <= 255; fc++) {
        TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_MS, authLockoutMs((uint8_t)fc));
    }
    /* And nothing in the whole domain may produce zero, which is what the
     * overflow did: a zero penalty reads as "not locked" at every call site. */
    for (unsigned fc = 0; fc <= 255; fc++) {
        TEST_ASSERT_TRUE(authLockoutMs((uint8_t)fc) > 0u);
    }
}


/* ===========================================================================
 *  BOOLEANOS DO /api/commit_all — parseBoolStrict + jsonValuePos/RawToken/Flag
 *
 *  O achado: quatro campos do `sys` e dois do `net` liam booleano com
 *  `getNum(k) != "0"`. Qualquer grafia que nao fosse o literal `0` valia
 *  TRUE — inclusive o `false` que o proprio /api/config do aparelho emite.
 *  Consequencia direta: GET /api/config -> editar -> POST /api/commit_all
 *  ligava t_sec (cifra da telemetria), log, m_retain e ntp_enabled, e nao
 *  havia grafia booleana capaz de desligar nenhum deles.
 *
 *  Os `test_legacy_*` abaixo sao a TESTEMUNHA DE REGRESSAO: transliteracoes
 *  dos tres leitores removidos (mesma semantica, API disponivel no stub) que
 *  provam, em codigo, a resposta errada que cada um dava. Sao controle
 *  positivo do instrumento: se um dia passarem a concordar com o leitor novo,
 *  o teste novo perdeu o poder de detectar o bug e alguem precisa olhar.
 *  O controle forte e o A/B no ferro (tools/commit_bool_cases.py).
 * =========================================================================== */

/* --- transliteracoes do codigo REMOVIDO (nao chamar em producao) ---------- */

/* WebManager_Commit.cpp: sys getNum, ate 2026-08-18. */
static String legacy_getNum(const String& sys, const char* key) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    int p = sys.indexOf(pat);
    if (p < 0) return String();
    int vStart = p + (int)strlen(pat);
    while (vStart < (int)sys.length() && (sys[vStart] == ' ' || sys[vStart] == '\t')) vStart++;
    if (vStart >= (int)sys.length()) return String();
    if (sys[vStart] == '"') {
        int vEnd = sys.indexOf('"', vStart + 1);
        if (vEnd < 0) return String();
        return sys.substring(vStart + 1, vEnd);
    }
    int vEnd = vStart;
    while (vEnd < (int)sys.length() && sys[vEnd] != ',' && sys[vEnd] != '}') vEnd++;
    String v = sys.substring(vStart, vEnd);
    v.trim();
    return v;
}

/* WebManager_Commit.cpp: net getN, ate 2026-08-18 (sem o pulo de espaco). */
static String legacy_getN(const String& net, const char* key) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    int p = net.indexOf(pat);
    if (p < 0) return String();
    int vs = p + (int)strlen(pat);
    if (net[vs] == '"') {
        int ve = net.indexOf('"', vs + 1);
        if (ve < 0) return String();
        return net.substring(vs + 1, ve);
    }
    int ve = vs;
    while (ve < (int)net.length() && net[ve] != ',' && net[ve] != '}') ve++;
    return net.substring(vs, ve);
}

/* WebManager_Commit.cpp: getBool dos slots, ate 2026-08-18. */
static int legacy_getBool(const String& o, const char* key) {
    const int v = jsonValuePos(o, key);      /* mesma busca; o defeito era o teste */
    if (v < 0) return -1;
    return o.substring(v).startsWith("true") ? 1 : 0;
}

/* --- parseBoolStrict ------------------------------------------------------ */

void test_parseBool_accepts_both_spellings(void) {
    bool b = false;
    /* o que o /api/config emite e o que qualquer round-trip devolve */
    TEST_ASSERT_TRUE(parseBoolStrict(String("true"), b));  TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(parseBoolStrict(String("false"), b)); TEST_ASSERT_FALSE(b);
    /* o que os formularios da pagina emitem */
    TEST_ASSERT_TRUE(parseBoolStrict(String("1"), b));     TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(parseBoolStrict(String("0"), b));     TEST_ASSERT_FALSE(b);
    /* repl do Python escreve assim */
    TEST_ASSERT_TRUE(parseBoolStrict(String("True"), b));  TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(parseBoolStrict(String("FALSE"), b)); TEST_ASSERT_FALSE(b);
}

void test_parseBool_rejects_and_keeps_out(void) {
    /* Nao inventa valor: em tudo que nao entende, devolve false E deixa
     * `out` intacto — e o que permite ao chamador manter o valor gravado
     * em vez de gravar o oposto do pedido. */
    const char* junk[] = { "", " ", "2", "-1", "01", "yes", "on", "tru",
                           "truex", "falsey", "null", "0.0", "\"true\"" };
    for (size_t i = 0; i < sizeof(junk) / sizeof(junk[0]); i++) {
        bool sentinel = true;
        TEST_ASSERT_FALSE_MESSAGE(parseBoolStrict(String(junk[i]), sentinel), junk[i]);
        TEST_ASSERT_TRUE_MESSAGE(sentinel, junk[i]);
        sentinel = false;
        TEST_ASSERT_FALSE(parseBoolStrict(String(junk[i]), sentinel));
        TEST_ASSERT_FALSE(sentinel);
    }
}

/* --- jsonValuePos / jsonRawToken ------------------------------------------ */

void test_jsonValuePos_skips_whitespace(void) {
    /* JSON.stringify nunca poe espaco; json.dumps sempre poe. */
    String tight("{\"t_int\":0}");
    String spaced("{\"t_int\": 0}");
    String tabbed("{\"t_int\":\t0}");
    String wrapped("{\"t_int\":\n  0}");
    TEST_ASSERT_EQUAL_STRING("0", jsonRawToken(tight, "t_int").c_str());
    TEST_ASSERT_EQUAL_STRING("0", jsonRawToken(spaced, "t_int").c_str());
    TEST_ASSERT_EQUAL_STRING("0", jsonRawToken(tabbed, "t_int").c_str());
    TEST_ASSERT_EQUAL_STRING("0", jsonRawToken(wrapped, "t_int").c_str());
}

void test_jsonRawToken_shapes(void) {
    String o("{\"a\":1,\"b\":\"-3\",\"c\":12 ,\"d\":true}");
    TEST_ASSERT_EQUAL_STRING("1", jsonRawToken(o, "a").c_str());
    TEST_ASSERT_EQUAL_STRING("-3", jsonRawToken(o, "b").c_str());   /* aspas somem */
    TEST_ASSERT_EQUAL_STRING("12", jsonRawToken(o, "c").c_str());   /* espaco antes da virgula */
    TEST_ASSERT_EQUAL_STRING("true", jsonRawToken(o, "d").c_str()); /* fecha em '}' */
    TEST_ASSERT_EQUAL_STRING("", jsonRawToken(o, "zz").c_str());    /* ausente */
    TEST_ASSERT_EQUAL_INT(-1, jsonValuePos(o, "zz"));
}

void test_jsonValuePos_no_prefix_overmatch(void) {
    /* O needle carrega as aspas e os dois-pontos: "log": nao casa "logx":,
     * e um valor de texto so poderia conter aspas escapadas — que quebram
     * o needle. E por isso que a varredura PLANA (a mesma de
     * WebCommitSections.h) nao inventa campo. */
    String o("{\"logx\":1,\"t_glob\":\"x=\\\"log\\\":0\",\"log\":false}");
    const int v = jsonValuePos(o, "log");
    TEST_ASSERT_TRUE(v > 0);
    TEST_ASSERT_EQUAL_STRING("false", jsonRawToken(o, "log").c_str());
}

/* --- jsonFlag: tri-estado -------------------------------------------------- */

void test_jsonFlag_tristate(void) {
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(String("{\"log\":true}"), "log"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(String("{\"log\":false}"), "log"));
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(String("{\"log\":1}"), "log"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(String("{\"log\":0}"), "log"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(String("{\"log\": false}"), "log"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(String("{\"log\":\"0\"}"), "log"));
    TEST_ASSERT_EQUAL_INT(JSON_FLAG_ABSENT, jsonFlag(String("{\"x\":1}"), "log"));
    TEST_ASSERT_EQUAL_INT(JSON_FLAG_BAD, jsonFlag(String("{\"log\":2}"), "log"));
    TEST_ASSERT_EQUAL_INT(JSON_FLAG_BAD, jsonFlag(String("{\"log\":yes}"), "log"));
    TEST_ASSERT_EQUAL_INT(JSON_FLAG_BAD, jsonFlag(String("{\"log\":}"), "log"));
    /* Ausente e ilegivel sao AMBOS negativos: o chamador testa `>= 0` e
     * mantem o valor gravado nos dois casos. Sao distintos porque so o
     * segundo entra em "rejected":[...] na resposta do commit. */
    TEST_ASSERT_TRUE(jsonFlag(String("{\"x\":1}"), "log") < 0);
    TEST_ASSERT_TRUE(jsonFlag(String("{\"log\":2}"), "log") < 0);
}

/* --- o round-trip que estava quebrado ------------------------------------- */

/* Recorte literal do que /api/config emite (WebManager_Api.cpp): todos os
 * booleanos vao como literais JSON. E este corpo, devolvido ao
 * /api/commit_all, que o parser antigo lia ao contrario. */
static const char* kConfigGetSys =
    "{\"name\":\"SIMUT\",\"tz\":-3,\"log\":false,\"res\":12,\"s_int\":5000,"
    "\"t_transport\":0,\"t_sec\":false,\"t_cert\":false,\"ntp_enabled\":false,"
    "\"m_retain\":false}";

void test_config_roundtrip_no_longer_inverts(void) {
    for (const char* k : { "log", "t_sec", "ntp_enabled", "m_retain" }) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, jsonFlag(String(kConfigGetSys), k), k);
    }
    /* e o caminho antigo (formularios da pagina) segue valendo */
    String numeric("{\"log\":1,\"t_sec\":0,\"ntp_enabled\":1,\"m_retain\":0}");
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(numeric, "log"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(numeric, "t_sec"));
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(numeric, "ntp_enabled"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(numeric, "m_retain"));
}

/* --- controle positivo: os leitores removidos erravam mesmo --------------- */

void test_legacy_getNum_inverted_json_booleans(void) {
    /* `cfg.telEncryption = (getNum("t_sec") != "0")` sobre o corpo que o
     * proprio aparelho emite: LIGA a cifra quando o pedido era desligar. */
    const String sys(kConfigGetSys);
    TEST_ASSERT_FALSE(legacy_getNum(sys, "t_sec") == "0");   /* => ligava */
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(sys, "t_sec"));        /* agora desliga */

    /* `log:false` nao tinha como desligar o log por booleano. */
    TEST_ASSERT_FALSE(legacy_getNum(sys, "log") == "0");
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(sys, "log"));

    /* A/A: com `0` numerico os dois concordam — o teste nao reprova tudo. */
    String numeric("{\"t_sec\":0,\"log\":1}");
    TEST_ASSERT_TRUE(legacy_getNum(numeric, "t_sec") == "0");
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(numeric, "t_sec"));
    TEST_ASSERT_FALSE(legacy_getNum(numeric, "log") == "0");
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(numeric, "log"));
}

void test_legacy_getN_broke_twice_over(void) {
    /* A copia do `net` nunca aprendeu a pular espaco: alem da inversao do
     * booleano, o token vinha com o espaco colado. */
    String spaced("{\"use_dhcp\": 0,\"dns_auto\": 0}");
    TEST_ASSERT_EQUAL_STRING(" 0", legacy_getN(spaced, "use_dhcp").c_str());
    TEST_ASSERT_FALSE(legacy_getN(spaced, "use_dhcp") == "0");  /* => forcava DHCP */
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(spaced, "use_dhcp"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(spaced, "dns_auto"));
    /* e o literal JSON, igual ao que /api/network emite */
    String literal("{\"use_dhcp\":false,\"dns_auto\":false}");
    TEST_ASSERT_FALSE(legacy_getN(literal, "use_dhcp") == "0");
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(literal, "use_dhcp"));
}

void test_legacy_getBool_inverted_numeric_booleans(void) {
    /* O espelho do mesmo defeito, do outro lado: os slots so entendiam o
     * literal, entao `{"a":1}` DESATIVAVA o slot que pedia para ativar. */
    String slot("{\"i\":0,\"a\":1,\"al\":1}");
    TEST_ASSERT_EQUAL_INT(0, legacy_getBool(slot, "a"));    /* desativava */
    TEST_ASSERT_EQUAL_INT(0, legacy_getBool(slot, "al"));
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(slot, "a"));          /* agora ativa */
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(slot, "al"));
    /* A/A: com o literal os dois concordam. */
    String lit("{\"i\":0,\"a\":true,\"al\":false}");
    TEST_ASSERT_EQUAL_INT(1, legacy_getBool(lit, "a"));
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(lit, "a"));
    TEST_ASSERT_EQUAL_INT(0, legacy_getBool(lit, "al"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(lit, "al"));
}

void test_sounds_section_now_reads_numeric(void) {
    /* jsonBoolValue (secao `sounds` e alarms.active) conhecia so o literal e
     * caia no `fallback` — nem mudava, nem reclamava, e respondia 200.
     * jsonFlag le as duas grafias; ausente continua significando "manter". */
    String snd("{\"touch\":0,\"confirm\":1,\"mute\":false}");
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(snd, "touch"));
    TEST_ASSERT_EQUAL_INT(1, jsonFlag(snd, "confirm"));
    TEST_ASSERT_EQUAL_INT(0, jsonFlag(snd, "mute"));
    TEST_ASSERT_EQUAL_INT(JSON_FLAG_ABSENT, jsonFlag(snd, "web"));
}

/* =========================================================================== */
/*  HaDiscovery — Home Assistant MQTT Discovery formatters (HaDiscovery.h)     */
/* =========================================================================== */

void test_ha_sanitize_id(void) {
    char out[16];
    HaDiscovery::sanitizeId("t28FF64", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("t28FF64", out);
    HaDiscovery::sanitizeId("sala 1.b", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("sala_1_b", out);
    HaDiscovery::sanitizeId("A-z_0", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("A-z_0", out);
    /* truncates inside cap, always terminated */
    HaDiscovery::sanitizeId("abcdefghijklmnopqr", out, 4);
    TEST_ASSERT_EQUAL_STRING("abc", out);
    HaDiscovery::sanitizeId(nullptr, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_ha_key_templatable(void) {
    TEST_ASSERT_TRUE(HaDiscovery::keyTemplatable("t28FF64"));
    TEST_ASSERT_TRUE(HaDiscovery::keyTemplatable("tsensor sala")); /* space is fine in Jinja brackets */
    TEST_ASSERT_FALSE(HaDiscovery::keyTemplatable("t'quote"));
    TEST_ASSERT_FALSE(HaDiscovery::keyTemplatable("t\"dquote"));
    TEST_ASSERT_FALSE(HaDiscovery::keyTemplatable("t\\back"));
}

void test_ha_json_escape(void) {
    char out[32];
    HaDiscovery::jsonEscapeInto("Lab \"Frio\" \\", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Lab \\\"Frio\\\" \\\\", out);
    char ctl[2] = {(char)7, 0};
    HaDiscovery::jsonEscapeInto(ctl, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("\\u0007", out);
    /* no partial escape sequence when the cap cuts mid-replacement */
    HaDiscovery::jsonEscapeInto("ab\"cd", out, 4);
    TEST_ASSERT_EQUAL_STRING("ab", out);
}

void test_ha_config_topic(void) {
    char out[96];
    int n = HaDiscovery::configTopic(out, sizeof(out), "simut_a1b2c3", "t28FF64");
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/simut_a1b2c3/t28FF64/config", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), n);
}

/* Golden payload: pins the exact wire bytes, abbreviations included. If this
 * test moves, entities already registered in someone's HA move with it —
 * treat a diff here as a compatibility decision, not a formatting one. */
void test_ha_entity_config_golden(void) {
    HaDiscovery::EntityCtx ctx;
    ctx.nodeId = "simut_a1b2c3";
    ctx.stateTopic = "simut/data";
    ctx.availTopic = "simut/status";
    ctx.deviceName = "Lab \"Frio\"";
    ctx.swVersion = "9.9.9-test";
    ctx.configUrl = "http://192.168.3.24";
    char buf[768];
    int n = HaDiscovery::entityConfigJson(buf, sizeof(buf), ctx,
        "t28FF64", "t28FF64", "Sala Temperature", "temperature", "°C", 1);
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"Sala Temperature\","
        "\"uniq_id\":\"simut_a1b2c3_t28FF64\","
        "\"stat_t\":\"simut/data\","
        "\"val_tpl\":\"{{ value_json['t28FF64'] }}\","
        "\"unit_of_meas\":\"°C\","
        "\"dev_cla\":\"temperature\","
        "\"stat_cla\":\"measurement\","
        "\"sug_dsp_prc\":1,"
        "\"avty_t\":\"simut/status\","
        "\"avty_tpl\":\"{{ value_json.status }}\","
        "\"pl_avail\":\"online\","
        "\"pl_not_avail\":\"offline\","
        "\"dev\":{\"ids\":[\"simut_a1b2c3\"],\"name\":\"Lab \\\"Frio\\\"\","
        "\"mf\":\"SIMUT\",\"mdl\":\"Raspberry Pi Pico W\","
        "\"sw\":\"9.9.9-test\",\"cu\":\"http://192.168.3.24\"}}",
        buf);
}

void test_ha_entity_config_omissions(void) {
    HaDiscovery::EntityCtx ctx;
    ctx.nodeId = "n";
    ctx.stateTopic = "s/d";
    ctx.availTopic = "s/st";
    ctx.deviceName = "d";
    ctx.swVersion = "1";
    ctx.configUrl = "";   /* no IP yet → no cu */
    char buf[512];
    HaDiscovery::entityConfigJson(buf, sizeof(buf), ctx,
        "uX", "uX", "X Humidity", "humidity", "%", -1);
    TEST_ASSERT_NULL(strstr(buf, "sug_dsp_prc"));
    TEST_ASSERT_NULL(strstr(buf, "\"cu\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"dev_cla\":\"humidity\""));
}

void test_ha_entity_config_truncation_detectable(void) {
    HaDiscovery::EntityCtx ctx;
    ctx.nodeId = "simut_a1b2c3";
    ctx.stateTopic = "simut/data";
    ctx.availTopic = "simut/status";
    ctx.deviceName = "Device";
    ctx.swVersion = "9.9.9";
    ctx.configUrl = "http://192.168.3.24";
    char buf[64]; /* far too small on purpose */
    int n = HaDiscovery::entityConfigJson(buf, sizeof(buf), ctx,
        "t1", "t1", "Temp", "temperature", "°C", 1);
    TEST_ASSERT_TRUE(n >= (int)sizeof(buf)); /* snprintf contract → caller must skip */
}

/* =========================================================================== */
/*  B64Decode — strict base64 for HTTP Basic auth (B64Decode.h)                */
/* =========================================================================== */

void test_b64_decodes_credentials(void) {
    char out[32];
    TEST_ASSERT_EQUAL_INT(12, b64Decode("YWRtaW46c2VudGhh", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("admin:sentha", out);
    TEST_ASSERT_EQUAL_INT(2, b64Decode("YWI=", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ab", out);
    TEST_ASSERT_EQUAL_INT(1, b64Decode("YQ==", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a", out);
}

void test_b64_rejects_malformed(void) {
    char out[32];
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("", out, sizeof(out)));           /* empty */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("YWJjZ", out, sizeof(out)));      /* len % 4 */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("YW!j", out, sizeof(out)));       /* alphabet */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("YW=j", out, sizeof(out)));       /* '=' mid-group */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("====", out, sizeof(out)));       /* all pad */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("YQ==YQ==", out, sizeof(out)));   /* pad then data */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("AA==", out, sizeof(out)));       /* embedded NUL */
    TEST_ASSERT_EQUAL_INT(-1, b64Decode("YWRtaW46c2VudGhh", out, 8));     /* overflow */
}

/* =========================================================================== */
/*  PromMetrics — Prometheus exposition formatters (PromMetrics.h)             */
/* =========================================================================== */

void test_prom_escape_label(void) {
    char out[32];
    PromMetrics::escapeLabel("a\"b\\c\nd", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\nd", out);
    PromMetrics::escapeLabel("SALA 2 T5", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("SALA 2 T5", out);
}

void test_prom_lines_golden(void) {
    char out[96];
    PromMetrics::typeLine(out, sizeof(out), "simut_heap_free_bytes", "gauge");
    TEST_ASSERT_EQUAL_STRING("# TYPE simut_heap_free_bytes gauge\n", out);
    PromMetrics::lineU32(out, sizeof(out), "simut_heap_free_bytes", "", 40796);
    TEST_ASSERT_EQUAL_STRING("simut_heap_free_bytes 40796\n", out);
    PromMetrics::lineF(out, sizeof(out), "simut_temperature_celsius",
                       "slot=\"3\",hwid=\"STH0003\"", 25.5, 2);
    TEST_ASSERT_EQUAL_STRING(
        "simut_temperature_celsius{slot=\"3\",hwid=\"STH0003\"} 25.50\n", out);
    PromMetrics::lineI32(out, sizeof(out), "simut_wifi_rssi_dbm", "", -49);
    TEST_ASSERT_EQUAL_STRING("simut_wifi_rssi_dbm -49\n", out);
}

/* =========================================================================== */
/*  Syslog5424 — RFC 5424 line formatter (Syslog5424.h)                        */
/* =========================================================================== */

/* Fixed epoch for golden vectors: 2026-08-19T17:04:00Z. Native tests cannot
 * call time(); the formatter takes epoch as a parameter precisely so the wire
 * bytes are deterministic. */
static const time_t SYSLOG_TEST_EPOCH = 1787159040; /* 2026-08-19T17:04:00Z */

void test_syslog_priority(void) {
    /* PRI = facility(16)*8 + severity. DEBUG..FATAL → 7/6/4/3/2. */
    TEST_ASSERT_EQUAL_INT(128 + 7, Syslog5424::priority(0)); /* DEBUG */
    TEST_ASSERT_EQUAL_INT(128 + 6, Syslog5424::priority(1)); /* INFO  */
    TEST_ASSERT_EQUAL_INT(128 + 4, Syslog5424::priority(2)); /* WARN  */
    TEST_ASSERT_EQUAL_INT(128 + 3, Syslog5424::priority(3)); /* ERROR */
    TEST_ASSERT_EQUAL_INT(128 + 2, Syslog5424::priority(4)); /* FATAL */
    TEST_ASSERT_EQUAL_INT(128 + 5, Syslog5424::priority(9)); /* unknown → Notice */
}

void test_syslog_line_golden(void) {
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       2 /*WARN*/, "picofridge", "NET", 524 /*NET_PROVISIONAL_TIME*/,
                       -18, 0 /*core*/, 12345 /*uptime*/, SYSLOG_TEST_EPOCH,
                       "Provisional time in use", "");
    TEST_ASSERT_EQUAL_STRING(
        "<132>1 2026-08-19T17:04:00Z picofridge NET - 524 "
        "[simut@32473 ctx=\"-18\" core=\"0\" up=\"12345\"] Provisional time in use",
        out);
}

void test_syslog_line_with_extra(void) {
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       4 /*FATAL*/, "picofridge", "SYS", 1 /*SYS_BOOT*/,
                       -32767, 1 /*core*/, 7 /*uptime*/, SYSLOG_TEST_EPOCH,
                       "Boot", "watchdog TIMER");
    TEST_ASSERT_EQUAL_STRING(
        "<130>1 2026-08-19T17:04:00Z picofridge SYS - 1 "
        "[simut@32473 ctx=\"-32767\" core=\"1\" up=\"7\"] Boot: watchdog TIMER",
        out);
}

void test_syslog_timestamp_nilvalue_before_sync(void) {
    /* Below CLOCK_SYNCED_EPOCH the clock is provisional — TIMESTAMP must be
     * NILVALUE '-', never the build-epoch lie that time-travels at the SIEM. */
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       1 /*INFO*/, "dev", "CLI", 42, 0, 0, 3 /*uptime*/,
                       100 /*epoch: boot fallback*/, "hi", "");
    TEST_ASSERT_EQUAL_STRING(
        "<134>1 - dev CLI - 42 "
        "[simut@32473 ctx=\"0\" core=\"0\" up=\"3\"] hi",
        out);
}

void test_syslog_hostname_space_is_sheared(void) {
    /* A device name with a space would break the space-delimited header;
     * sanitizeToken maps it (and any non-PRINTUSASCII) to '-'. */
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       1, "sala 1", "NET", 5, 0, 0, 0, SYSLOG_TEST_EPOCH, "x", "");
    /* "sala 1" → "sala-1", still one token. */
    TEST_ASSERT_NOT_NULL(strstr(out, " sala-1 NET "));
    TEST_ASSERT_NULL(strstr(out, "sala 1"));
}

void test_syslog_empty_hostname_is_nilvalue(void) {
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       1, "", "", 0, 0, 0, 0, SYSLOG_TEST_EPOCH, "", "");
    /* Empty host and app both collapse to '-'; MSGID 0; empty MSG. */
    TEST_ASSERT_NOT_NULL(strstr(out, "Z - - - 0 ["));
}

void test_syslog_msg_control_bytes_become_space(void) {
    char out[256];
    Syslog5424::format(out, sizeof(out),
                       1, "dev", "SYS", 9, 0, 0, 0, SYSLOG_TEST_EPOCH,
                       "line1\nline2\ttab", "");
    /* No raw control byte survives into the datagram. */
    TEST_ASSERT_NULL(strchr(out, '\n'));
    TEST_ASSERT_NULL(strchr(out, '\t'));
    TEST_ASSERT_NOT_NULL(strstr(out, "line1 line2 tab"));
}

/* ===================================================================
 * ScreenRle — the wire format of /api/screen_stream
 *
 * The decoder below is written FROM THE FORMAT COMMENT in ScreenRle.h, not
 * from the encoder, and deliberately so: a round trip through two halves of
 * the same mistake proves nothing. It is the third implementation of the
 * format (the firmware encoder, this, and the JS in WebUI.h), and the one
 * that fails loudly when the other two drift.
 * =================================================================== */

/* Returns pixels decoded, or -1 on a malformed payload. */
static int rleDecode(const uint8_t* in, size_t len, uint16_t* out, size_t outCap) {
    if (len < 1) return -1;
    size_t o = 0, i = 0;
    const size_t ncol = (size_t)in[i++] + 1;
    if (len < 1 + ncol * 2) return -1;
    uint16_t pal[256];
    for (size_t k = 0; k < ncol; k++) {
        pal[k] = (uint16_t)(in[i] | (in[i + 1] << 8));
        i += 2;
    }
    while (i + 1 < len) {
        const size_t idx = in[i];
        const size_t run = (size_t)in[i + 1] + 1;
        i += 2;
        if (idx >= ncol) return -1;
        if (o + run > outCap) return -1;
        for (size_t r = 0; r < run; r++) out[o++] = pal[idx];
    }
    if (i != len) return -1;
    return (int)o;
}

/* Decoder for ENC_PAL_RLE4 — the third implementation of that format, next to
 * screenrle::encodeStrip4 and the JS in WebUI.h. Written against the spec in
 * ScreenRle.h rather than derived from the encoder, which is the only way a
 * round-trip test proves anything about the FORMAT and not just about one
 * function agreeing with itself. */
static int rle4Decode(const uint8_t* in, size_t len, uint16_t* out, size_t cap) {
    if (len < 3) return -1;
    const size_t ncol = (size_t)in[0] + 1;
    if (1 + ncol * 2 > len) return -1;
    uint16_t pal[16];
    for (size_t k = 0; k < ncol; k++)
        pal[k] = (uint16_t)(in[1 + k * 2] | (in[2 + k * 2] << 8));
    size_t i = 1 + ncol * 2, o = 0;
    while (i < len) {
        const uint8_t t = in[i++];
        const size_t k = t >> 4;
        size_t run;
        if ((t & 0x0F) == 0x0F) {
            if (i >= len) return -1;
            run = (size_t)in[i++] + 16;
        } else {
            run = (size_t)(t & 0x0F) + 1;
        }
        if (k >= ncol || o + run > cap) return -1;
        for (size_t r = 0; r < run; r++) out[o++] = pal[k];
    }
    return (int)o;
}

void test_screenrle4_uniform_strip(void) {
    /* 2,560 identical pixels. One palette entry, and the escape carries 270 at
     * a time, so ceil(2560/270) = 10 tokens of two bytes. */
    const size_t N = 320 * 8;
    static uint16_t px[320 * 8];
    for (size_t i = 0; i < N; i++) px[i] = 0x1234;
    static uint8_t out[320 * 8 * 2];

    const size_t n = screenrle::encodeStrip4(px, N, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x12, out[2]);

    static uint16_t back[320 * 8];
    TEST_ASSERT_EQUAL_INT((int)N, rle4Decode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle4_beats_the_byte_form_on_screen_like_content(void) {
    /* Same shape as the enc-1 round-trip test: bands, border, text specks. The
     * point of the format is that this case gets smaller, so the test asserts
     * it rather than trusting the measurement in the header. */
    const size_t W = 320, ROWS = 8, N = W * ROWS;
    static uint16_t px[320 * 8];
    for (size_t y = 0; y < ROWS; y++) {
        for (size_t x = 0; x < W; x++) {
            uint16_t c = (y < 2) ? 0x0000 : 0xFFFF;
            if (x < 4 || x >= W - 4) c = 0x07E0;
            if (y >= 4 && (x / 3) % 7 == 0) c = 0xF800;
            px[y * W + x] = c;
        }
    }
    static uint8_t out4[320 * 8 * 2], out1[320 * 8 * 2];
    const size_t n4 = screenrle::encodeStrip4(px, N, out4, sizeof(out4));
    const size_t n1 = screenrle::encodeStrip(px, N, out1, sizeof(out1));
    TEST_ASSERT_TRUE(n4 > 0);
    TEST_ASSERT_TRUE(n4 < n1);

    static uint16_t back[320 * 8];
    TEST_ASSERT_EQUAL_INT((int)N, rle4Decode(out4, n4, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle4_escape_boundary_runs(void) {
    /* The seam between the short form and the escape: 15 must stay one byte,
     * 16 must become the escape, and both must come back exactly. */
    for (size_t run = 1; run <= 40; run++) {
        static uint16_t px[64];
        for (size_t i = 0; i < run; i++) px[i] = 0xBEEF;
        px[run] = 0x0001;
        const size_t N = run + 1;
        uint8_t out[160];
        const size_t n = screenrle::encodeStrip4(px, N, out, sizeof(out));
        TEST_ASSERT_TRUE(n > 0);
        uint16_t back[64];
        TEST_ASSERT_EQUAL_INT((int)N, rle4Decode(out, n, back, N));
        TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
    }
}

void test_screenrle4_refuses_over_16_colours(void) {
    /* 17 distinct colours: the 4-bit index cannot name the last one, and the
     * caller must fall back to the byte form rather than get a wrong strip. */
    const size_t N = 17;
    uint16_t px[17];
    for (size_t i = 0; i < N; i++) px[i] = (uint16_t)(i * 7 + 1);
    /* 17*8 and not 17*4: the byte form needs 1 + 17*2 palette + 17*2 pairs = 69 B
     * for this input, and a 68 B buffer made it refuse for the RIGHT reason and
     * the wrong test. The cap is part of the contract, so the buffer has to be
     * big enough to leave only the colour ceiling under test. */
    uint8_t out[17 * 8];
    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip4(px, N, out, sizeof(out)));
    TEST_ASSERT_TRUE(screenrle::encodeStrip4(px, N - 1, out, sizeof(out)) > 0);
    /* and the byte form, whose index is a whole byte, still takes all 17 */
    TEST_ASSERT_TRUE(screenrle::encodeStrip(px, N, out, sizeof(out)) > 0);
}

void test_screenrle4_takes_alternating_pixels_that_enc1_refuses(void) {
    /* The pathological input for enc 1 is comfortable for enc 4: alternating
     * pixels cost one token each, so N bytes against 2N raw, where the byte form
     * would need 2N and refuse. Named for what it asserts — the earlier name
     * said "refuses" while the body asserts the opposite. */
    const size_t N = 1024;
    uint16_t px[1024];
    for (size_t i = 0; i < N; i++) px[i] = (uint16_t)((i & 1) ? 0x0000 : 0xFFFF);
    uint8_t out[1024 * 2];
    const size_t n = screenrle::encodeStrip4(px, N, out, N * 2);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < N * 2);
    uint16_t back[1024];
    TEST_ASSERT_EQUAL_INT((int)N, rle4Decode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle_uniform_strip(void) {
    /* One colour over a full 8-row strip. A strip is 320*8 = 2,560 PIXELS
     * (5,120 bytes raw), so it is one palette entry and 2560/256 = 10 pairs,
     * because the count byte carries at most 256 pixels. */
    const size_t N = 320 * 8;
    static uint16_t px[320 * 8];
    for (size_t i = 0; i < N; i++) px[i] = 0x1234;
    static uint8_t out[320 * 8 * 2];

    const size_t n = screenrle::encodeStrip(px, N, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(1 + 2 + 10 * 2, n);
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);          /* ncol-1 */
    TEST_ASSERT_EQUAL_UINT8(0x34, out[1]);       /* little-endian palette */
    TEST_ASSERT_EQUAL_UINT8(0x12, out[2]);
    TEST_ASSERT_EQUAL_UINT8(255, out[4]);        /* follow = 255 -> 256 px */

    static uint16_t back[320 * 8];
    TEST_ASSERT_EQUAL_INT((int)N, rleDecode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle_round_trip_screen_like(void) {
    /* Bands, a border and text-sized specks — the shape a settings screen
     * actually has: few colours, long horizontal runs, short breaks. */
    const size_t W = 320, ROWS = 8, N = W * ROWS;
    static uint16_t px[320 * 8];
    for (size_t y = 0; y < ROWS; y++) {
        for (size_t x = 0; x < W; x++) {
            uint16_t c = (y < 2) ? 0x0000 : 0xFFFF;
            if (x < 4 || x >= W - 4) c = 0x07E0;
            if (y >= 4 && (x / 3) % 7 == 0) c = 0xF800;
            px[y * W + x] = c;
        }
    }
    static uint8_t out[320 * 8 * 2];
    const size_t n = screenrle::encodeStrip(px, N, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < N * 2);                 /* it earned its place */
    TEST_ASSERT_EQUAL_UINT8(4, (uint8_t)(out[0] + 1)); /* bg, text, border, speck */

    static uint16_t back[320 * 8];
    TEST_ASSERT_EQUAL_INT((int)N, rleDecode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle_run_longer_than_256_splits(void) {
    /* 300 of one colour then 1 of another: 256 + 44, then the single. */
    const size_t N = 301;
    uint16_t px[301];
    for (size_t i = 0; i < 300; i++) px[i] = 0xAAAA;
    px[300] = 0x5555;
    uint8_t out[301 * 2];

    const size_t n = screenrle::encodeStrip(px, N, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(1 + 4 + 3 * 2, n);
    TEST_ASSERT_EQUAL_UINT8(255, out[6]);        /* 256 pixels */
    TEST_ASSERT_EQUAL_UINT8(43,  out[8]);        /* the remaining 44 */
    TEST_ASSERT_EQUAL_UINT8(0,   out[10]);       /* the lone pixel */

    uint16_t back[301];
    TEST_ASSERT_EQUAL_INT((int)N, rleDecode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle_refuses_when_not_smaller_than_raw(void) {
    /* The pathological case the cap exists for: no two neighbours alike, so
     * every pixel is its own pair and the encoding would be exactly twice the
     * raw size. The caller gets 0 and ships the strip raw. */
    const size_t N = 1024;
    uint16_t px[1024];
    for (size_t i = 0; i < N; i++) px[i] = (uint16_t)((i & 1) ? 0x0000 : 0xFFFF);
    uint8_t out[1024 * 2];

    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(px, N, out, N * 2));
}

void test_screenrle_refuses_over_256_colours(void) {
    /* 257 distinct colours: the index byte cannot name the last one. */
    const size_t N = 257;
    uint16_t px[257];
    for (size_t i = 0; i < N; i++) px[i] = (uint16_t)(i * 7 + 1);
    uint8_t out[257 * 4];

    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(px, N, out, sizeof(out)));

    /* One fewer colour and it encodes — so the refusal above is the ceiling
     * and not some other defect. */
    TEST_ASSERT_TRUE(screenrle::encodeStrip(px, N - 1, out, sizeof(out)) > 0);
}

void test_screenrle_palette_interns_repeated_colours(void) {
    /* A colour that comes back after an interruption must not take a second
     * palette slot — the whole saving depends on it. */
    const size_t N = 600;
    uint16_t px[600];
    for (size_t i = 0; i < N; i++) px[i] = (uint16_t)(((i / 100) % 2) ? 0x1111 : 0x2222);
    uint8_t out[600 * 2];

    const size_t n = screenrle::encodeStrip(px, N, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)(out[0] + 1));  /* two colours */
    TEST_ASSERT_EQUAL_UINT32(1 + 2 * 2 + 6 * 2, n);     /* six runs */

    uint16_t back[600];
    TEST_ASSERT_EQUAL_INT((int)N, rleDecode(out, n, back, N));
    TEST_ASSERT_EQUAL_UINT16_ARRAY(px, back, N);
}

void test_screenrle_rejects_empty_and_null(void) {
    uint16_t px[4] = {1, 2, 3, 4};
    uint8_t out[32];
    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(px, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(nullptr, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(px, 4, nullptr, 32));
    /* A cap that cannot even hold the palette header is a refusal, not a
     * buffer overrun. */
    TEST_ASSERT_EQUAL_UINT32(0, screenrle::encodeStrip(px, 4, out, 3));
}


/* parseFloat( ) took over from atof( ) on 2026-09-18 (lever 5 of the flash
 * diet). These pin the two behaviours it had to grow to do that, and the one
 * it deliberately did not: no exponent. */
void test_parsefloat_accepts_what_atof_accepted(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f,  parseFloat("+100"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f,    parseFloat("+1.5"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f,    parseFloat(" 3"));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -2.25f,  parseFloat("\t-2.25"));
    /* the JSON slot case: a number, then the rest of the object */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f,    parseFloat("3,\"hwId\":\"28ff\""));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f,    parseFloat(".5"));
    TEST_ASSERT_TRUE(isnan(parseFloat("")));
    TEST_ASSERT_TRUE(isnan(parseFloat("   ")));
    TEST_ASSERT_TRUE(isnan(parseFloat(nullptr)));
    /* no exponent, by design: reads the mantissa and stops at 'e' */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f,    parseFloat("1.5e3"));
}

void test_parsefloat_strict_agrees_with_it(void) {
    float out = -1;
    TEST_ASSERT_TRUE(parseFloatStrict(String("+100"), out));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 100.0f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("-0.05"), out));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, -0.05f, out);
    TEST_ASSERT_TRUE(parseFloatStrict(String("0.123456"), out));
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.123456f, out);
    /* the strict layer still rejects everything the parser would shrug at */
    TEST_ASSERT_FALSE(parseFloatStrict(String("1.5e3"), out));
    TEST_ASSERT_FALSE(parseFloatStrict(String(" 3"), out));
    TEST_ASSERT_FALSE(parseFloatStrict(String("0x1p3"), out));
    TEST_ASSERT_FALSE(parseFloatStrict(String("nan"), out));
    TEST_ASSERT_FALSE(parseFloatStrict(String("inf"), out));
}

/* ─────────────────────────────────────────────────────────────────────────
 * SimutTime — the fixed-offset replacement for newlib's TZ machinery.
 *
 * The reference is the host's own libc: timegm( ) and gmtime_r( ) do what the
 * firmware's localtime_r( )/mktime( ) must do once the offset is folded in, and
 * comparing against them is the only way to test this that does not restate
 * the implementation. The cases after the sweeps are the three things this
 * codebase actually asks mktime( ) for and would not notice losing: tm_wday on
 * a struct whose return value is discarded, tm_mday = 0 meaning "last day of
 * the previous month", and tm_mday += 1 rolling the month and the year.
 * ───────────────────────────────────────────────────────────────────────── */

static struct tm mkTm(int y, int mon, int mday, int h = 0, int mi = 0, int se = 0) {
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_sec  = se;
    t.tm_isdst = -1;
    return t;
}

void test_simuttime_localtime_matches_host(void) {
    const long offs[] = { 0, -3 * 3600L, 9 * 3600L, -11 * 3600L, 14 * 3600L };
    /* 2020-01-01 .. 2030-01-01, every 7 h 13 min — crosses every month, both
     * leap rules in range, and never lands on a round hour. */
    for (size_t k = 0; k < sizeof offs / sizeof offs[0]; k++) {
        for (time_t t = 1577836800; t < 1893456000; t += 26000) {
            struct tm mine, ref;
            simutLocalTimeTz(t, &mine, offs[k]);
            const time_t shifted = t + offs[k];
            gmtime_r(&shifted, &ref);
            TEST_ASSERT_EQUAL_INT(ref.tm_year, mine.tm_year);
            TEST_ASSERT_EQUAL_INT(ref.tm_mon,  mine.tm_mon);
            TEST_ASSERT_EQUAL_INT(ref.tm_mday, mine.tm_mday);
            TEST_ASSERT_EQUAL_INT(ref.tm_hour, mine.tm_hour);
            TEST_ASSERT_EQUAL_INT(ref.tm_min,  mine.tm_min);
            TEST_ASSERT_EQUAL_INT(ref.tm_sec,  mine.tm_sec);
            TEST_ASSERT_EQUAL_INT(ref.tm_wday, mine.tm_wday);
            TEST_ASSERT_EQUAL_INT(ref.tm_yday, mine.tm_yday);
            TEST_ASSERT_EQUAL_INT(0, mine.tm_isdst);
        }
    }
}

void test_simuttime_mktime_matches_host_timegm(void) {
    const long offs[] = { 0, -3 * 3600L, 5 * 3600L };
    for (size_t k = 0; k < sizeof offs / sizeof offs[0]; k++) {
        for (time_t t = 1577836800; t < 1893456000; t += 26000) {
            struct tm broken;
            simutLocalTimeTz(t, &broken, offs[k]);
            struct tm ref = broken;               /* timegm normalises in place too */
            const time_t got = simutMkTimeTz(&broken, offs[k]);
            TEST_ASSERT_EQUAL_INT64((long long)t, (long long)got);
            TEST_ASSERT_EQUAL_INT64((long long)(t + offs[k]), (long long)timegm(&ref));
        }
    }
}

void test_simuttime_wday_is_filled_for_a_discarded_return(void) {
    /* DisplayManager_Calendar: {year, month, mday=1}, ignore the epoch, read
     * tm_wday to know which column the 1st goes in. */
    struct tm t = mkTm(2026, 9, 1);
    simutMkTimeTz(&t, -3 * 3600L);
    TEST_ASSERT_EQUAL_INT(2, t.tm_wday);          /* 2026-09-01 is a Tuesday */

    struct tm u = mkTm(2024, 2, 29);              /* leap day, a Thursday */
    simutMkTimeTz(&u, 0);
    TEST_ASSERT_EQUAL_INT(4, u.tm_wday);
    TEST_ASSERT_EQUAL_INT(29, u.tm_mday);
    TEST_ASSERT_EQUAL_INT(1, u.tm_mon);
}

void test_simuttime_mday_zero_is_last_day_of_previous_month(void) {
    /* DisplayManager_Calendar again: day 0 of the NEXT month is how it asks
     * how many days the current one has. */
    struct tm sep = mkTm(2026, 10, 0);            /* mon = October, mday = 0 */
    simutMkTimeTz(&sep, -3 * 3600L);
    TEST_ASSERT_EQUAL_INT(30, sep.tm_mday);       /* September has 30 */
    TEST_ASSERT_EQUAL_INT(8, sep.tm_mon);         /* and we landed in it */

    struct tm feb = mkTm(2024, 3, 0);
    simutMkTimeTz(&feb, 0);
    TEST_ASSERT_EQUAL_INT(29, feb.tm_mday);       /* 2024 is a leap year */
    struct tm feb25 = mkTm(2025, 3, 0);
    simutMkTimeTz(&feb25, 0);
    TEST_ASSERT_EQUAL_INT(28, feb25.tm_mday);
    struct tm feb2100 = mkTm(2100, 3, 0);
    simutMkTimeTz(&feb2100, 0);
    TEST_ASSERT_EQUAL_INT(28, feb2100.tm_mday);   /* 2100 is not */
}

void test_simuttime_month_index_twelve_rolls_the_year(void) {
    /* The "next month" idiom sets tm_mon = 12 when the current month is
     * December, which is a month index one past the end of the year. */
    struct tm t = mkTm(2026, 13, 1);              /* mon - 1 == 12 */
    simutMkTimeTz(&t, -3 * 3600L);
    TEST_ASSERT_EQUAL_INT(2027 - 1900, t.tm_year);
    TEST_ASSERT_EQUAL_INT(0, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
}

void test_simuttime_day_walk_crosses_month_and_year(void) {
    /* WebManager_History walks a window with tm_mday += 1 and expects the
     * rest of the struct to follow. */
    struct tm t = mkTm(2026, 1, 31);
    t.tm_mday += 1;
    simutMkTimeTz(&t, -3 * 3600L);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mon);           /* February */

    struct tm y = mkTm(2026, 12, 31, 0, 0, 0);
    y.tm_mday += 1;
    simutMkTimeTz(&y, -3 * 3600L);
    TEST_ASSERT_EQUAL_INT(1, y.tm_mday);
    TEST_ASSERT_EQUAL_INT(0, y.tm_mon);
    TEST_ASSERT_EQUAL_INT(2027 - 1900, y.tm_year);
}

void test_simuttime_midnight_is_offset_from_utc(void) {
    /* h5DayWindowFromName: a file name becomes local midnight, and the whole
     * history gating depends on that being the LOCAL one. At -03, local
     * midnight is 03:00 UTC. */
    struct tm t = mkTm(2026, 9, 18);
    const time_t local = simutMkTimeTz(&t, -3 * 3600L);
    struct tm u = mkTm(2026, 9, 18);
    const time_t utc = simutMkTimeTz(&u, 0);
    TEST_ASSERT_EQUAL_INT64((long long)utc + 3 * 3600LL, (long long)local);
    TEST_ASSERT_EQUAL_INT64((long long)local + 86400LL,
                            (long long)(local + 86400));   /* the window's width */
}

void test_simuttime_days_from_civil_anchors(void) {
    TEST_ASSERT_EQUAL_INT64(0,     (long long)simutDaysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(-1,    (long long)simutDaysFromCivil(1969, 12, 31));
    TEST_ASSERT_EQUAL_INT64(19601, (long long)simutDaysFromCivil(2023, 9, 1));  /* 1693526400 / 86400 */
    /* day 0 and day 32 are legal inputs here: that is what makes normalisation
     * fall out of the arithmetic instead of needing a month table. */
    TEST_ASSERT_EQUAL_INT64((long long)simutDaysFromCivil(2026, 8, 31),
                            (long long)simutDaysFromCivil(2026, 9, 0));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();

    /* isValidIpv4 */
    RUN_TEST(test_isValidIpv4_valid);
    RUN_TEST(test_isValidIpv4_invalid_format);
    RUN_TEST(test_isValidIpv4_invalid_octets);
    RUN_TEST(test_isValidIpv4_size_bounds);

    /* isSafeUploadFilename */
    RUN_TEST(test_isSafeUploadFilename_valid);
    RUN_TEST(test_isSafeUploadFilename_traversal);
    RUN_TEST(test_isSafeUploadFilename_dangerous_chars);
    RUN_TEST(test_isSafeUploadFilename_size);

    /* isValidName / isValidCfgString / isInRange */
    RUN_TEST(test_isValidName_valid);
    RUN_TEST(test_isValidName_invalid);
    RUN_TEST(test_isValidCfgString);
    RUN_TEST(test_isInRange);
    RUN_TEST(test_tel_min_batch_from_legacy_ms);

    /* parseIntStrict */
    RUN_TEST(test_parseIntStrict_valid);
    RUN_TEST(test_parseIntStrict_invalid);
    RUN_TEST(test_parseFloatStrict_valid);    /* v3.36.3 (M7) */
    RUN_TEST(test_parseFloatStrict_invalid);
    RUN_TEST(test_parseFloatStrict_digit_ceiling);
    RUN_TEST(test_pem_cert_then_key);
    RUN_TEST(test_pem_key_then_cert);
    RUN_TEST(test_pem_chain_is_kept_whole);
    RUN_TEST(test_pem_three_key_spellings);
    RUN_TEST(test_pem_encrypted_key_is_named_not_parsed);
    RUN_TEST(test_pem_unterminated_block_is_not_a_block);
    RUN_TEST(test_pem_absent_blocks);  /* v3.36.3 (M7) */
    RUN_TEST(test_parseIntStrict_int32_boundaries);        /* issue #44 */
    RUN_TEST(test_parseIntStrict_overflow_rejected);       /* issue #44 */
    RUN_TEST(test_parseFloatStrict_overflow_rejected);     /* issue #44 */
    RUN_TEST(test_parseFloatStrict_large_finite_ok);       /* issue #44 */
    RUN_TEST(test_stub_toInt_saturates_like_target);       /* issue #44 */
    RUN_TEST(test_stub_toFloat_overflows_to_inf_like_target); /* issue #44 */

    /* timeReached / timeSince */
    RUN_TEST(test_timeReached_basic);
    RUN_TEST(test_timeReached_wrap_safe);
    RUN_TEST(test_timeSince_basic);

    /* dallasCrc8 */
    RUN_TEST(test_dallasCrc8_known_vectors);
    RUN_TEST(test_dallasCrc8_determinism);

    /* floatToI16 / i16ToFloat */
    RUN_TEST(test_floatToI16_basic);
    RUN_TEST(test_floatToI16_clamp);
    RUN_TEST(test_floatToI16_nan);
    RUN_TEST(test_i16ToFloat_basic);
    RUN_TEST(test_i16ToFloat_nan);

    /* parseFloat */
    RUN_TEST(test_parseFloat_basic);
    RUN_TEST(test_parseFloat_edge);
    RUN_TEST(test_floatToI16_roundtrip);

    /* channel table — a malformed new row must fail here, not in the field */
    RUN_TEST(test_channel_letters_unique);
    RUN_TEST(test_channel_keys_unique_and_resolvable);
    RUN_TEST(test_channel_range_fits_bit_width);
    RUN_TEST(test_channel_defaults_inside_sane_range);
    RUN_TEST(test_channel_slots_cover_table);
    RUN_TEST(test_channel_unknown_falls_back);

    RUN_TEST(test_tag_sec_is_not_sensor);
    RUN_TEST(test_tag_ota_has_its_own_id);
    RUN_TEST(test_tag_all_literals_used_in_firmware);
    RUN_TEST(test_tag_unknown_inputs);
    RUN_TEST(test_tag_id_to_string_roundtrip);

    /* calibration curves — the engine every corrected reading passes through */
    RUN_TEST(test_calibcurve_build_sorts_input);
    RUN_TEST(test_calibcurve_build_rejects_bad_input);
    RUN_TEST(test_calibcurve_apply_identity);
    RUN_TEST(test_calibcurve_apply_single_point);
    RUN_TEST(test_calibcurve_apply_two_points);
    RUN_TEST(test_calibcurve_apply_five_points);
    RUN_TEST(test_calibcurve_from_offset);
    RUN_TEST(test_calibcurve_encode_is_flat_csv_cells);
    RUN_TEST(test_calibcurve_encode_decode_roundtrip);
    RUN_TEST(test_calibcurve_decode_sorts_and_tolerates);
    RUN_TEST(test_calibcurve_decode_rejects_malformed);
    RUN_TEST(test_calibrow_parse_tail_shapes);
    RUN_TEST(test_calibrow_parse_tail_fallbacks);
    RUN_TEST(test_calibcurve_smooth_monotone_cubic);
    RUN_TEST(test_calibcurve_smooth_small_n_is_linear);
    RUN_TEST(test_calibrow_mode_token);
    RUN_TEST(test_calibrow_format_shapes);

    /* depth-aware JSON slicing — replaces the first-'}' walkers */
    RUN_TEST(test_jsonMatchEnd_flat);
    RUN_TEST(test_jsonMatchEnd_nested);
    RUN_TEST(test_jsonMatchEnd_brackets_inside_strings);
    RUN_TEST(test_jsonMatchEnd_escaped_quotes);
    RUN_TEST(test_jsonMatchEnd_invalid);

    /* /api/commit_all — booleanos: `false` nao pode ligar, `1` nao pode desligar */
    RUN_TEST(test_parseBool_accepts_both_spellings);
    RUN_TEST(test_parseBool_rejects_and_keeps_out);
    RUN_TEST(test_jsonValuePos_skips_whitespace);
    RUN_TEST(test_jsonRawToken_shapes);
    RUN_TEST(test_jsonValuePos_no_prefix_overmatch);
    RUN_TEST(test_jsonFlag_tristate);
    RUN_TEST(test_config_roundtrip_no_longer_inverts);
    RUN_TEST(test_legacy_getNum_inverted_json_booleans);
    RUN_TEST(test_legacy_getN_broke_twice_over);
    RUN_TEST(test_legacy_getBool_inverted_numeric_booleans);
    RUN_TEST(test_sounds_section_now_reads_numeric);

    /* /api/commit_all — one gate per section, not one per route */
    RUN_TEST(test_commit_sys_operator_cannot_add_users);
    RUN_TEST(test_commit_sys_operator_cannot_change_net);
    RUN_TEST(test_commit_sys_operator_keeps_own_sections);
    RUN_TEST(test_commit_usermgr_can_commit_users_only);
    RUN_TEST(test_commit_netonly_can_commit_net_only);
    RUN_TEST(test_commit_admin_passes_everything);
    RUN_TEST(test_commit_entry_perms_exclude_viewer);
    RUN_TEST(test_commit_nested_users_does_not_evade);
    RUN_TEST(test_commit_empty_payload_is_refused);
    RUN_TEST(test_commit_denial_still_fills_offsets);
    RUN_TEST(test_commit_section_table_is_sane);

    /* isSecretFsPath — /config download guard (A-4) */
    RUN_TEST(test_secret_path_blocks_config);
    RUN_TEST(test_secret_path_normalises_spelling);
    RUN_TEST(test_secret_path_allows_legit_downloads);
    RUN_TEST(test_secret_path_no_sibling_overmatch);
    RUN_TEST(test_cors_origin_accepts_real_ones);
    RUN_TEST(test_cors_origin_rejects_header_injection);
    RUN_TEST(test_cors_origin_rejects_wildcard_and_schemes);
    RUN_TEST(test_cors_origin_rejects_path_and_size);
    RUN_TEST(test_secret_path_traversal_is_callers_job);

    /* isSecretFsDir — /api/ls directory guard (ACH-04) */
    RUN_TEST(test_secret_dir_blocks_bare_and_nested);
    RUN_TEST(test_secret_dir_allows_legit_dirs);

    /* isSafeDirPath — /api/mkdir folder-name guard (M-7) */
    RUN_TEST(test_dirpath_accepts_legit);
    RUN_TEST(test_dirpath_blocks_xss_bytes);
    RUN_TEST(test_dirpath_blocks_path_and_url_bytes);
    RUN_TEST(test_dirpath_blocks_empty_control_and_long);

    /* passwordPolicyOk — server-side strength floor (A-5) */
    RUN_TEST(test_pwpolicy_accepts_strong);
    RUN_TEST(test_pwpolicy_rejects_weak);

    /* Authentication lockout — shared by web login and the Bluetooth CLI */
    RUN_TEST(test_lockout_backoff_doubles);
    RUN_TEST(test_lockout_reaches_and_holds_the_ceiling);
    RUN_TEST(test_lockout_never_falls_below_the_ceiling);

    /* hwId as a key, language-pack identity, per-path /download permissions */
    RUN_TEST(test_hwid_accepts_real_ids);
    RUN_TEST(test_hwid_rejects_key_breakers);
    RUN_TEST(test_langident_strips_json_breakers);
    RUN_TEST(test_langident_keeps_legitimate_names);
    RUN_TEST(test_langident_terminates_and_respects_cap);
    RUN_TEST(test_download_perm_gates_history_and_logs);
    RUN_TEST(test_download_perm_leaves_ordinary_files_alone);

    /* Setup-AP key */
    RUN_TEST(test_ap_psk_shape);
    RUN_TEST(test_ap_psk_refuses_rather_than_truncates);

    /* HaDiscovery — Home Assistant MQTT Discovery formatters */
    RUN_TEST(test_ha_sanitize_id);
    RUN_TEST(test_ha_key_templatable);
    RUN_TEST(test_ha_json_escape);
    RUN_TEST(test_ha_config_topic);
    RUN_TEST(test_ha_entity_config_golden);
    RUN_TEST(test_ha_entity_config_omissions);
    RUN_TEST(test_ha_entity_config_truncation_detectable);

    /* B64Decode + PromMetrics — /metrics auth and exposition format */
    RUN_TEST(test_b64_decodes_credentials);
    RUN_TEST(test_b64_rejects_malformed);
    RUN_TEST(test_prom_escape_label);
    RUN_TEST(test_prom_lines_golden);

    /* Syslog5424 — RFC 5424 line formatter */
    RUN_TEST(test_syslog_priority);
    RUN_TEST(test_syslog_line_golden);
    RUN_TEST(test_syslog_line_with_extra);
    RUN_TEST(test_syslog_timestamp_nilvalue_before_sync);
    RUN_TEST(test_syslog_hostname_space_is_sheared);
    RUN_TEST(test_syslog_empty_hostname_is_nilvalue);
    RUN_TEST(test_syslog_msg_control_bytes_become_space);

    /* ScreenRle — the TFT mirror's wire format */
    RUN_TEST(test_screenrle4_uniform_strip);
    RUN_TEST(test_screenrle4_beats_the_byte_form_on_screen_like_content);
    RUN_TEST(test_screenrle4_escape_boundary_runs);
    RUN_TEST(test_screenrle4_refuses_over_16_colours);
    RUN_TEST(test_screenrle4_takes_alternating_pixels_that_enc1_refuses);
    RUN_TEST(test_screenrle_uniform_strip);
    RUN_TEST(test_screenrle_round_trip_screen_like);
    RUN_TEST(test_screenrle_run_longer_than_256_splits);
    RUN_TEST(test_screenrle_refuses_when_not_smaller_than_raw);
    RUN_TEST(test_screenrle_refuses_over_256_colours);
    RUN_TEST(test_screenrle_palette_interns_repeated_colours);
    RUN_TEST(test_screenrle_rejects_empty_and_null);

    /* parseFloat — the atof replacement (lever 5 of the flash diet) */
    RUN_TEST(test_parsefloat_accepts_what_atof_accepted);
    RUN_TEST(test_parsefloat_strict_agrees_with_it);

    /* SimutTime — fixed-offset localtime/mktime (lever 4 of the flash diet) */
    RUN_TEST(test_simuttime_localtime_matches_host);
    RUN_TEST(test_simuttime_mktime_matches_host_timegm);
    RUN_TEST(test_simuttime_wday_is_filled_for_a_discarded_return);
    RUN_TEST(test_simuttime_mday_zero_is_last_day_of_previous_month);
    RUN_TEST(test_simuttime_month_index_twelve_rolls_the_year);
    RUN_TEST(test_simuttime_day_walk_crosses_month_and_year);
    RUN_TEST(test_simuttime_midnight_is_offset_from_utc);
    RUN_TEST(test_simuttime_days_from_civil_anchors);

    return UNITY_END();
}
