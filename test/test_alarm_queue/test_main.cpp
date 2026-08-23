/**
 * @file    test/test_alarm_queue/test_main.cpp
 * @brief   Testes host-side da fila de alarmes da 2ª linha de telemetria (v21).
 * @details Roda via `pio test -e native_alarmqueue` (sem HW). Cobre:
 *            · FIFO / ordem de chegada (snapshot)
 *            · capacidade configurada + clamp em ALARM_QUEUE_MAX
 *            · estouro com drop-newest + contador dropped( )
 *            · seq monotônico, wrap pulando o 0, 0 = push recusado
 *            · ack por lista de seq (remove do meio, preserva ordem)
 *            · ackOldest / clear
 *          AlarmQueue.h é header-only e só depende de SystemDefs_Records.h
 *          (constantes) — compila no host com os stubs de test/native_stubs.
 *
 * @project SIMUT — v21 segunda linha de telemetria
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include <unity.h>
#include "AlarmQueue.h"
#include "AlarmPayload.h"

/* ── FIFO e push básico ─────────────────────────────────────────────────── */
static void test_push_fifo_order(void) {
    AlarmQueue q(8);
    uint16_t s0 = q.push(1000, 0, 0 /*CH_TEMP*/, 1234, ALARM_ERR_ALARM);
    uint16_t s1 = q.push(1001, 1, 1 /*CH_HUM*/, -999, ALARM_ERR_ALARM);
    uint16_t s2 = q.push(1002, 2, 0, 567, ALARM_ERR_ERROR);

    TEST_ASSERT_EQUAL_UINT16(1, s0);
    TEST_ASSERT_EQUAL_UINT16(2, s1);
    TEST_ASSERT_EQUAL_UINT16(3, s2);
    TEST_ASSERT_EQUAL_UINT8(3, q.size());

    AlarmRecord out[4];
    uint8_t n = q.snapshot(out, 4);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT32(1000, out[0].epoch);
    TEST_ASSERT_EQUAL_UINT16(1, out[0].seq);
    TEST_ASSERT_EQUAL_UINT32(1001, out[1].epoch);
    TEST_ASSERT_EQUAL_UINT32(1002, out[2].epoch);
    /* registro de erro carrega a flag + sentinela de valor */
    TEST_ASSERT_EQUAL_UINT8(ALARM_FLAG_ERR, out[2].flags);
    TEST_ASSERT_TRUE(out[0].flags == 0);
    /* snapshot NÃO remove */
    TEST_ASSERT_EQUAL_UINT8(3, q.size());
}

/* ── capacidade e clamp ─────────────────────────────────────────────────── */
static void test_capacity_clamp(void) {
    AlarmQueue q0(0);
    TEST_ASSERT_EQUAL_UINT8(1, q0.capacity());

    AlarmQueue qBig(ALARM_QUEUE_MAX + 40);
    TEST_ASSERT_EQUAL_UINT8(ALARM_QUEUE_MAX, qBig.capacity());

    AlarmQueue q(4);
    TEST_ASSERT_EQUAL_UINT8(4, q.capacity());
    for (int i = 0; i < 4; i++) q.push(100 + i, i, 0, 1, ALARM_ERR_ALARM);
    TEST_ASSERT_TRUE(q.full());
}

/* ── estouro: drop-newest ───────────────────────────────────────────────── */
static void test_overflow_drop_newest(void) {
    AlarmQueue q(3);
    q.push(100, 0, 0, 1, ALARM_ERR_ALARM);
    q.push(101, 1, 0, 2, ALARM_ERR_ALARM);
    q.push(102, 2, 0, 3, ALARM_ERR_ALARM);

    /* cheio: novo registro recusado, seq 0, dropped incrementa */
    uint16_t refused = q.push(103, 3, 0, 4, ALARM_ERR_ALARM);
    TEST_ASSERT_EQUAL_UINT16(0, refused);
    TEST_ASSERT_EQUAL_UINT16(1, q.dropped());
    TEST_ASSERT_EQUAL_UINT8(3, q.size());

    /* os TRÊS originais continuam na fila (nenhum descarte silencioso) */
    AlarmRecord out[4];
    uint8_t n = q.snapshot(out, 4);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT32(100, out[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(102, out[2].epoch);
}

/* ── seq: monotônico + wrap pulando 0 ───────────────────────────────────── */
static void test_seq_wrap_skips_zero(void) {
    AlarmQueue q(4);
    for (int i = 0; i < 4; i++) {
        uint16_t s = q.push(i, 0, 0, 1, ALARM_ERR_ALARM);
        TEST_ASSERT_EQUAL_UINT16(i + 1, s);
    }
    q.ackOldest(4);
    /* força wrap: 65535 é o último válido */
    /* drena até perto do limite sem depender de estado interno: usa ackOldest */
    /* Como _nextSeq é privado, exercitamos o wrap via ack + push até 65535. */
    for (uint32_t i = 4; i < 65534; i++) {
        q.push(i, 0, 0, 1, ALARM_ERR_ALARM);
        q.ackOldest(1);
    }
    uint16_t s = q.push(1, 0, 0, 1, ALARM_ERR_ALARM);
    TEST_ASSERT_EQUAL_UINT16(65535, s);
    q.ackOldest(1);
    s = q.push(2, 0, 0, 1, ALARM_ERR_ALARM);
    /* 65535 + 1 == 0 é reservado → pula para 1 */
    TEST_ASSERT_EQUAL_UINT16(1, s);
}

/* ── ack por seq ────────────────────────────────────────────────────────── */
static void test_ack_by_seq(void) {
    AlarmQueue q(8);
    q.push(100, 0, 0, 1, ALARM_ERR_ALARM);   /* seq 1 */
    q.push(101, 1, 0, 2, ALARM_ERR_ALARM);   /* seq 2 */
    q.push(102, 2, 0, 3, ALARM_ERR_ALARM);   /* seq 3 */
    q.push(103, 3, 0, 4, ALARM_ERR_ALARM);   /* seq 4 */

    /* confirma o do MEIO (2) e um inexistente (99) — não deve remover nada por 99 */
    uint16_t seqs[] = {2, 99};
    uint8_t removed = q.ack(seqs, 2);
    TEST_ASSERT_EQUAL_UINT8(1, removed);
    TEST_ASSERT_EQUAL_UINT8(3, q.size());

    AlarmRecord out[4];
    uint8_t n = q.snapshot(out, 4);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT16(1, out[0].seq); /* ordem preservada */
    TEST_ASSERT_EQUAL_UINT16(3, out[1].seq);
    TEST_ASSERT_EQUAL_UINT16(4, out[2].seq);

    /* confirma todos */
    uint16_t all[] = {1, 3, 4};
    removed = q.ack(all, 3);
    TEST_ASSERT_EQUAL_UINT8(3, removed);
    TEST_ASSERT_TRUE(q.empty());
}

/* ── ackOldest / clear ──────────────────────────────────────────────────── */
static void test_ack_oldest_and_clear(void) {
    AlarmQueue q(8);
    for (int i = 0; i < 5; i++) q.push(100 + i, 0, 0, 1, ALARM_ERR_ALARM);
    q.ackOldest(2);
    TEST_ASSERT_EQUAL_UINT8(3, q.size());
    AlarmRecord out[8];
    uint8_t n = q.snapshot(out, 8);
    TEST_ASSERT_EQUAL_UINT32(102, out[0].epoch);
    (void)n;

    q.ackOldest(10); /* além do tamanho → esvazia */
    TEST_ASSERT_TRUE(q.empty());

    for (int i = 0; i < 3; i++) q.push(200 + i, 0, 0, 1, ALARM_ERR_ALARM);
    q.clear();
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT8(0, q.snapshot(out, 8));

    /* a fila volta a funcionar depois do clear; seq continua monotônico
     * pelo boot (5 já usados + 3 do lote anterior = 9) */
    uint16_t s = q.push(300, 1, 1, 5, ALARM_ERR_ERROR);
    TEST_ASSERT_EQUAL_UINT16(9, s);
    TEST_ASSERT_EQUAL_UINT16(0, q.dropped()); /* capacidade 8: nada foi recusado */
}

/* ── parser do ACK por aplicação ───────────────────────────────────────── */
static void test_ack_parser(void) {
    uint16_t out[8];
    const uint8_t p1[] = "{\"seq\":[1,2,3]}";
    uint8_t n = alarmParseSeqList(p1, sizeof(p1) - 1, out, 8);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT16(1, out[0]);
    TEST_ASSERT_EQUAL_UINT16(2, out[1]);
    TEST_ASSERT_EQUAL_UINT16(3, out[2]);

    const uint8_t p2[] = "{\"seq\": [ 12 , 65535 , 9 ] }";
    n = alarmParseSeqList(p2, sizeof(p2) - 1, out, 8);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT16(12, out[0]);
    TEST_ASSERT_EQUAL_UINT16(65535, out[1]);
    TEST_ASSERT_EQUAL_UINT16(9, out[2]);

    /* vazio / ausente */
    const uint8_t p3[] = "{\"seq\":[]}";
    TEST_ASSERT_EQUAL_UINT8(0, alarmParseSeqList(p3, sizeof(p3) - 1, out, 8));
    const uint8_t p4[] = "{}";
    TEST_ASSERT_EQUAL_UINT8(0, alarmParseSeqList(p4, sizeof(p4) - 1, out, 8));

    /* truncado no meio: para no caractere inesperado, sem inventar número */
    const uint8_t p5[] = "{\"seq\":[1,2,x]}";
    n = alarmParseSeqList(p5, sizeof(p5) - 1, out, 8);
    TEST_ASSERT_EQUAL_UINT8(2, n);

    /* overflow de número (70000) é ignorado */
    const uint8_t p6[] = "{\"seq\":[70000,5]}";
    n = alarmParseSeqList(p6, sizeof(p6) - 1, out, 8);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_EQUAL_UINT16(5, out[0]);

    /* cap de maxN */
    const uint8_t p7[] = "{\"seq\":[1,2,3,4,5,6]}";
    n = alarmParseSeqList(p7, sizeof(p7) - 1, out, 3);
    TEST_ASSERT_EQUAL_UINT8(3, n);
    TEST_ASSERT_EQUAL_UINT16(3, out[2]);
}

/* ── wrap do anel com ack parcial ───────────────────────────────────────── */
static void test_ring_reuse_after_ack(void) {
    AlarmQueue q(4);
    for (int i = 0; i < 4; i++) q.push(100 + i, i, 0, 1, ALARM_ERR_ALARM);
    uint16_t seqs[] = {1, 2};
    q.ack(seqs, 2);
    /* capacidade 4 com 2 ocupados: só mais 2 cabem; o 3º é recusado */
    q.push(200, 5, 0, 1, ALARM_ERR_ALARM);
    q.push(201, 5, 0, 1, ALARM_ERR_ALARM);
    uint16_t refused = q.push(202, 5, 0, 1, ALARM_ERR_ALARM);
    TEST_ASSERT_EQUAL_UINT16(0, refused);
    TEST_ASSERT_EQUAL_UINT16(1, q.dropped());
    TEST_ASSERT_EQUAL_UINT8(4, q.size());
    AlarmRecord out[8];
    uint8_t n = q.snapshot(out, 8);
    TEST_ASSERT_EQUAL_UINT8(4, n);
    /* ordem: 3,4 (antigos sobreviventes) + 200,201 */
    TEST_ASSERT_EQUAL_UINT32(102, out[0].epoch);
    TEST_ASSERT_EQUAL_UINT32(103, out[1].epoch);
    TEST_ASSERT_EQUAL_UINT32(200, out[2].epoch);
    TEST_ASSERT_EQUAL_UINT32(201, out[3].epoch);
}


/* ===========================================================================
 * ALARM PAYLOAD — vetores dourados (AlarmPayload.h)
 * Cobre EXATAMENTE o formatador que roda no ferro: template default (ok/err),
 * remoção composta, tokens individuais, CSV e fallback de id sem hwId.
 * ========================================================================= */

static void fillDemoCfg(SystemConfig& cfg) {
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.sensors[0].hwId, "SENSOR1", sizeof(cfg.sensors[0].hwId) - 1);
    strncpy(cfg.alarmTel.lineTemplate,
            "{\"ts\":{TS},\"id\":\"{ID}\",\"val\":{val},\"alarm\":{alarm},\"err\":{err},\"seq\":{seq}}",
            sizeof(cfg.alarmTel.lineTemplate) - 1);
}

static void test_alarm_line_default_ok(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    /* borda de limite: val + alarm presente, err removido (dominio ausente) */
    AlarmRecord rec = { 1756250000, 1, 2530, 0, 0 /*CH_TEMP*/, 0, ALARM_ERR_ALARM };
    char out[256];
    int n = alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250000,\"id\":\"tSENSOR1\",\"val\":25.30,\"alarm\":\"alarm\",\"seq\":1}",
        out);
}

static void test_alarm_line_default_err(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    /* falha de hardware: val e alarm ausentes, err presente */
    AlarmRecord rec = { 1756250100, 2, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERROR };
    char out[256];
    alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250100,\"id\":\"tSENSOR1\",\"err\":\"err\",\"seq\":2}",
        out);
}

static void test_alarm_line_individual_tokens(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    strncpy(cfg.alarmTel.lineTemplate, "{CH};{SLOT};{HWID};{VAL};{ERR}",
            sizeof(cfg.alarmTel.lineTemplate) - 1);
    AlarmRecord rec = { 1756250200, 7, 1013 /* CH_HUM: scale 10 -> 101.3 */,
                        0 /* slot 0 = hwId SENSOR1 */, 1 /*CH_HUM*/, 0, ALARM_ERR_ALARM };
    char out[128];
    alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("u;0;SENSOR1;101.3;", out);

    /* registro de falha: VAL vazio, ERR presente (com aspas JSON) */
    rec.flags = ALARM_FLAG_ERR;
    rec.errCode = ALARM_ERR_ERROR;
    rec.value = HIST_NAN_SENTINEL;
    alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("u;0;SENSOR1;;\"err\"", out);
}

static void test_alarm_line_uppercase_compound(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    /* chave == nome do token, token SEM aspas: a chave e removida quando o
     * token esta ausente (forma composta). */
    strncpy(cfg.alarmTel.lineTemplate, "{\"VAL\":{VAL},\"ERR\":{ERR}}",
            sizeof(cfg.alarmTel.lineTemplate) - 1);
    AlarmRecord ok = { 1, 1, 2530, 0, 0, 0, ALARM_ERR_ALARM };
    AlarmRecord er = { 2, 2, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERROR };
    char out[128];
    alarmFormatLine(ok, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("{\"VAL\":25.30}", out);
    alarmFormatLine(er, cfg, out, sizeof(out));
    /* {ERR} emite o codigo COM aspas (JSON valido); chave VAL removida */
    TEST_ASSERT_EQUAL_STRING("{\"ERR\":\"err\"}", out);
}

static void test_alarm_line_id_fallback_no_hwid(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    cfg.sensors[0].hwId[0] = '\0';
    strncpy(cfg.alarmTel.lineTemplate, "{ID}", sizeof(cfg.alarmTel.lineTemplate) - 1);
    AlarmRecord rec = { 1, 1, 2530, 0, 0, 0, ALARM_ERR_ALARM };
    char out[32];
    alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("t0", out);

    /* pressao: letra p */
    rec.channel = 2; /* CH_PRESS */
    alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("p0", out);
}

static void test_alarm_line_csv(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    AlarmRecord ok = { 1756250300, 4, 2530, 0, 0, 0, ALARM_ERR_ALARM };
    AlarmRecord er = { 1756250400, 5, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERROR };
    char out[64];
    alarmFormatCsvLine(ok, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("4;1756250300;tSENSOR1;25.30", out);
    alarmFormatCsvLine(er, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("5;1756250400;tSENSOR1;err", out);
}

static void test_alarm_line_action_codes(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);

    /* limite silenciado: marcador, alarm = alarm_sil (err removido) */
    AlarmRecord sil = { 1756250500, 6, HIST_NAN_SENTINEL, 0, 0, 0, ALARM_ERR_ALARM_SIL };
    char out[256];
    alarmFormatLine(sil, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250500,\"id\":\"tSENSOR1\",\"alarm\":\"alarm_sil\",\"seq\":6}",
        out);

    /* erro silenciado: err = err_sil (alarm removido) */
    AlarmRecord esil = { 1756250600, 7, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_SIL };
    alarmFormatLine(esil, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250600,\"id\":\"tSENSOR1\",\"err\":\"err_sil\",\"seq\":7}",
        out);

    /* limite desativado: alarm = alarm_off */
    AlarmRecord off = { 1756250700, 8, HIST_NAN_SENTINEL, 0, 0, 0, ALARM_ERR_ALARM_OFF };
    alarmFormatLine(off, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250700,\"id\":\"tSENSOR1\",\"alarm\":\"alarm_off\",\"seq\":8}",
        out);

    /* erro desativado: err = err_off */
    AlarmRecord eoff = { 1756250800, 9, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_OFF };
    alarmFormatLine(eoff, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1756250800,\"id\":\"tSENSOR1\",\"err\":\"err_off\",\"seq\":9}",
        out);
}

static void test_alarm_csv_action_codes(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    char out[64];
    AlarmRecord sil = { 1756250500, 6, HIST_NAN_SENTINEL, 0, 0, 0, ALARM_ERR_ALARM_SIL };
    alarmFormatCsvLine(sil, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("6;1756250500;tSENSOR1;alarm_sil", out);

    AlarmRecord esil = { 1756250600, 7, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_SIL };
    alarmFormatCsvLine(esil, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("7;1756250600;tSENSOR1;err_sil", out);

    AlarmRecord off = { 1756250700, 8, HIST_NAN_SENTINEL, 0, 0, 0, ALARM_ERR_ALARM_OFF };
    alarmFormatCsvLine(off, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("8;1756250700;tSENSOR1;alarm_off", out);

    AlarmRecord eoff = { 1756250800, 9, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_OFF };
    alarmFormatCsvLine(eoff, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("9;1756250800;tSENSOR1;err_off", out);
}
static void test_alarm_line_literal_braces_passthrough(void) {
    SystemConfig cfg;
    fillDemoCfg(cfg);
    strncpy(cfg.alarmTel.lineTemplate, "{[notatoken]:1}",
            sizeof(cfg.alarmTel.lineTemplate) - 1);
    AlarmRecord rec = { 1, 1, 2530, 0, 0, 0 };
    char out[64];
    alarmFormatLine(rec, cfg, out, sizeof(out));
    /* '{' sem token conhecido é emitido literalmente (mesmo contrato da
     * linha convencional — JSON aninhado não quebra o walk) */
    TEST_ASSERT_EQUAL_STRING("{[notatoken]:1}", out);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_push_fifo_order);
    RUN_TEST(test_capacity_clamp);
    RUN_TEST(test_overflow_drop_newest);
    RUN_TEST(test_seq_wrap_skips_zero);
    RUN_TEST(test_ack_by_seq);
    RUN_TEST(test_ack_oldest_and_clear);
    RUN_TEST(test_ring_reuse_after_ack);
    RUN_TEST(test_ack_parser);
    RUN_TEST(test_alarm_line_default_ok);
    RUN_TEST(test_alarm_line_default_err);
    RUN_TEST(test_alarm_line_individual_tokens);
    RUN_TEST(test_alarm_line_uppercase_compound);
    RUN_TEST(test_alarm_line_id_fallback_no_hwid);
    RUN_TEST(test_alarm_line_csv);
    RUN_TEST(test_alarm_line_action_codes);
    RUN_TEST(test_alarm_csv_action_codes);
    RUN_TEST(test_alarm_line_literal_braces_passthrough);
    return UNITY_END();
}
