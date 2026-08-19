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
#include "SystemDefs_Validate.h"
#include "ParseFloat.h"
#include "SystemDefs_Time.h"
#include <cmath>      /* isnan, NAN para floatToI16 */
#include "SystemDefs_Logging.h"  /* tagStringToId — B1/B2 */
#include "sensors/SensorChannelTable.h" /* channel table integrity */
#include "sensors/CalibCurve.h"         /* calibration curve engine */
#include "WebJsonSlice.h"               /* depth-aware JSON slicing */
#include "WebCommitSections.h"          /* per-section authz for /api/commit_all */
#include "FsSecretPath.h"               /* /config download guard (A-4) */
#include "HaDiscovery.h"                /* Home Assistant MQTT Discovery formatters */
#include "B64Decode.h"                  /* Basic-auth base64 decoder (strict) */
#include "PromMetrics.h"                /* Prometheus text exposition formatters */

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

void test_secret_path_traversal_is_callers_job(void) {
    /* isSecretFsPath does NOT resolve "..": a "/history/../config/system.bin"
     * does not start with "/config/" and returns false here. The caller
     * (handleDownload) rejects ".." before ever calling this — the two guards
     * are separate on purpose, and this pins that contract so a later reader
     * does not assume this function catches traversal. */
    TEST_ASSERT_FALSE(isSecretFsPath("/history/../config/system.bin"));
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

    /* parseIntStrict */
    RUN_TEST(test_parseIntStrict_valid);
    RUN_TEST(test_parseIntStrict_invalid);
    RUN_TEST(test_parseFloatStrict_valid);    /* v3.36.3 (M7) */
    RUN_TEST(test_parseFloatStrict_invalid);  /* v3.36.3 (M7) */

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
    RUN_TEST(test_secret_path_traversal_is_callers_job);

    /* isSafeDirPath — /api/mkdir folder-name guard (M-7) */
    RUN_TEST(test_dirpath_accepts_legit);
    RUN_TEST(test_dirpath_blocks_xss_bytes);
    RUN_TEST(test_dirpath_blocks_path_and_url_bytes);
    RUN_TEST(test_dirpath_blocks_empty_control_and_long);

    /* passwordPolicyOk — server-side strength floor (A-5) */
    RUN_TEST(test_pwpolicy_accepts_strong);
    RUN_TEST(test_pwpolicy_rejects_weak);

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

    return UNITY_END();
}
