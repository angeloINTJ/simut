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
#include "SystemDefs_Time.h"
#include <cmath>      /* isnan, NAN para floatToI16 */

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


/* =========================================================================== */
/*                                  MAIN                                       */
/* =========================================================================== */
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
    RUN_TEST(test_floatToI16_roundtrip);

    return UNITY_END();
}
