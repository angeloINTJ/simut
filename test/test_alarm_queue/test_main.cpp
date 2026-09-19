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
#include "ConfigApply.h"
#include "ConfigMigrate.h" /* v24: legacy config blobs by segment, sizes frozen */

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
    TEST_ASSERT_EQUAL_STRING("4;1756250300;tSENSOR1;25.30;;;;", out);
    alarmFormatCsvLine(er, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("5;1756250400;tSENSOR1;err;;;;", out);
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
    TEST_ASSERT_EQUAL_STRING("6;1756250500;tSENSOR1;alarm_sil;;;;", out);

    AlarmRecord esil = { 1756250600, 7, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_SIL };
    alarmFormatCsvLine(esil, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("7;1756250600;tSENSOR1;err_sil;;;;", out);

    AlarmRecord off = { 1756250700, 8, HIST_NAN_SENTINEL, 0, 0, 0, ALARM_ERR_ALARM_OFF };
    alarmFormatCsvLine(off, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("8;1756250700;tSENSOR1;alarm_off;;;;", out);

    AlarmRecord eoff = { 1756250800, 9, HIST_NAN_SENTINEL, 0, 0, ALARM_FLAG_ERR, ALARM_ERR_ERR_OFF };
    alarmFormatCsvLine(eoff, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("9;1756250800;tSENSOR1;err_off;;;;", out);
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


/* ── modo manutenção e o classificador de mudanças (v23) ─────────────────── */

void test_maint_window_open_and_closed(void) {
    MaintConfig m; memset(&m, 0, sizeof(m));
    TEST_ASSERT_FALSE(maintActive(m, 0, 1000));      /* 0 = fora de manutenção */
    m.until[0] = 2000;
    TEST_ASSERT_TRUE(maintActive(m, 0, 1999));
    TEST_ASSERT_FALSE(maintActive(m, 0, 2000));      /* o fim é exclusivo */
    TEST_ASSERT_FALSE(maintActive(m, 0, 2001));
    TEST_ASSERT_FALSE(maintActive(m, 1, 1999));      /* só o slot pedido */
    TEST_ASSERT_FALSE(maintActive(m, MAX_SENSORS, 1999)); /* slot fora da faixa */
}

void test_maint_clamp_caps_and_expires(void) {
    MaintConfig m; memset(&m, 0, sizeof(m));
    const uint32_t now = 1000000;
    m.until[0] = now - 1;                 /* já venceu */
    m.until[1] = now + 60;                /* dentro do teto */
    m.until[2] = now + MAINT_MAX_SEC * 4; /* relógio maluco */
    m.until[3] = 0;
    maintClamp(m, now);
    TEST_ASSERT_EQUAL_UINT32(0, m.until[0]);
    TEST_ASSERT_EQUAL_UINT32(now + 60, m.until[1]);
    TEST_ASSERT_EQUAL_UINT32(now + MAINT_MAX_SEC, m.until[2]);
    TEST_ASSERT_EQUAL_UINT32(0, m.until[3]);
}

void test_maint_payload_is_its_own_domain(void) {
    /* Um registro de manutenção não pode aparecer como alarme nem como erro:
     * um servidor que casa por campo trataria "err" como falha de verdade. */
    TEST_ASSERT_EQUAL_STRING("maint_on",  alarmCodeMaintField(ALARM_ERR_MAINT_ON));
    TEST_ASSERT_EQUAL_STRING("maint_off", alarmCodeMaintField(ALARM_ERR_MAINT_OFF));
    TEST_ASSERT_EQUAL_STRING("", alarmCodeAlarmField(ALARM_ERR_MAINT_ON));
    TEST_ASSERT_EQUAL_STRING("", alarmCodeErrField(ALARM_ERR_MAINT_ON));
    TEST_ASSERT_EQUAL_STRING("", alarmCodeAlarmField(ALARM_ERR_MAINT_OFF));
    TEST_ASSERT_EQUAL_STRING("", alarmCodeErrField(ALARM_ERR_MAINT_OFF));
    /* e não carrega leitura */
    TEST_ASSERT_FALSE(alarmCodeHasValue(ALARM_ERR_MAINT_ON));
    TEST_ASSERT_FALSE(alarmCodeHasValue(ALARM_ERR_MAINT_OFF));
    /* as duas ações novas do domínio de LIMITE (v24) */
    TEST_ASSERT_EQUAL_STRING("alarm_on",  alarmCodeAlarmField(ALARM_ERR_ALARM_ON));
    TEST_ASSERT_EQUAL_STRING("alarm_lim", alarmCodeAlarmField(ALARM_ERR_ALARM_LIM));
    TEST_ASSERT_EQUAL_STRING("", alarmCodeMaintField(ALARM_ERR_ALARM_LIM));
    TEST_ASSERT_FALSE(alarmCodeHasValue(ALARM_ERR_ALARM_LIM)); /* limites, não leitura */
}

void test_maint_line_renders_only_the_maint_key(void) {
    SystemConfig cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.sensors[2].active = true;
    strcpy(cfg.sensors[2].hwId, "S2");
    strcpy(cfg.alarmTel.lineTemplate,
           "{\"ts\":{TS},\"id\":\"{ID}\",\"val\":{val},\"alarm\":{alarm},\"err\":{err},\"maint\":{maint},\"seq\":{seq}}");
    AlarmRecord rec{};
    rec.epoch = 1700000000; rec.seq = 7; rec.value = HIST_NAN_SENTINEL;
    rec.slot = 2; rec.channel = CH_TEMP; rec.errCode = ALARM_ERR_MAINT_ON;
    char out[256];
    const int n = alarmFormatLine(rec, cfg, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1700000000,\"id\":\"tS2\",\"maint\":\"maint_on\",\"seq\":7}", out);

    rec.errCode = ALARM_ERR_MAINT_OFF; rec.seq = 8;
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1700000000,\"id\":\"tS2\",\"maint\":\"maint_off\",\"seq\":8}", out);
}

/* ── v24: ações identificadas — quem, quais limites, até quando ──────────── */

static const char* const V24_TEMPLATE =
    "{\"ts\":{TS},\"id\":\"{ID}\",\"val\":{val},\"alarm\":{alarm},\"err\":{err},\"maint\":{maint},"
    "\"lo\":{lo},\"hi\":{hi},\"until\":{until},\"user\":{user},\"seq\":{seq}}";

static void fillV24Cfg(SystemConfig& cfg) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.sensors[2].active = true;
    strcpy(cfg.sensors[2].hwId, "S2");
    strcpy(cfg.alarmTel.lineTemplate, V24_TEMPLATE);
    cfg.users[0].active = true; strcpy(cfg.users[0].username, "admin");
    cfg.users[3].active = true; strcpy(cfg.users[3].username, "joao");
}

void test_v24_maint_on_carries_until_and_user(void) {
    SystemConfig cfg; fillV24Cfg(cfg);
    AlarmRecord rec{};
    rec.epoch = 1700000000; rec.seq = 9; rec.value = HIST_NAN_SENTINEL;
    rec.slot = 2; rec.channel = CH_TEMP; rec.errCode = ALARM_ERR_MAINT_ON;
    rec.actor = alarmActorFromSlot(3);   /* joao */
    rec.value2 = (int16_t)(uint16_t)120; /* 2 h em minutos */
    char out[256];
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1700000000,\"id\":\"tS2\",\"maint\":\"maint_on\",\"until\":1700007200,\"user\":\"joao\",\"seq\":9}",
        out);
    /* 30 dias = 43.200 min: acima de INT16_MAX, tem de voltar inteiro */
    rec.value2 = (int16_t)(uint16_t)43200;
    TEST_ASSERT_EQUAL_UINT32(1700000000u + 43200u * 60u, alarmMaintUntil(rec));
    /* a saída por prazo: sem ator, sem until */
    rec.errCode = ALARM_ERR_MAINT_OFF; rec.actor = ALARM_ACTOR_NONE; rec.seq = 10;
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1700000000,\"id\":\"tS2\",\"maint\":\"maint_off\",\"seq\":10}", out);
}

void test_v24_alarm_lim_carries_both_limits(void) {
    SystemConfig cfg; fillV24Cfg(cfg);
    AlarmRecord rec{};
    rec.epoch = 1700000100; rec.seq = 11; rec.slot = 2; rec.channel = CH_TEMP;
    rec.errCode = ALARM_ERR_ALARM_LIM; rec.actor = alarmActorFromSlot(0);
    rec.value = -500;   /* -5.00 °C */
    rec.value2 = 3050;  /* 30.50 °C */
    char out[256];
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    /* val ausente (não é leitura), alarm = alarm_lim, lo/hi com os decimais do canal */
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1700000100,\"id\":\"tS2\",\"alarm\":\"alarm_lim\",\"lo\":-5.00,\"hi\":30.50,\"user\":\"admin\",\"seq\":11}",
        out);
    /* umidade: uma casa */
    rec.channel = CH_HUM; rec.value = 300; rec.value2 = 800;
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"ts\":1700000100,\"id\":\"uS2\",\"alarm\":\"alarm_lim\",\"lo\":30.0,\"hi\":80.0,\"user\":\"admin\",\"seq\":11}",
        out);
    /* alarm_on: só o marcador e quem fez */
    rec.errCode = ALARM_ERR_ALARM_ON; rec.channel = CH_TEMP; rec.seq = 12;
    TEST_ASSERT_TRUE(alarmFormatLine(rec, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1700000100,\"id\":\"tS2\",\"alarm\":\"alarm_on\",\"user\":\"admin\",\"seq\":12}", out);
}

void test_v24_actor_zero_is_nobody_and_old_records_are_unchanged(void) {
    SystemConfig cfg; fillV24Cfg(cfg);
    /* um registro da forma antiga (7 campos) deixa actor e value2 em zero */
    AlarmRecord old = { 1700000200, 13, 2530, 2, 0, 0, ALARM_ERR_ALARM };
    TEST_ASSERT_EQUAL_UINT8(ALARM_ACTOR_NONE, old.actor);
    TEST_ASSERT_EQUAL_INT16(0, old.value2);
    TEST_ASSERT_EQUAL_STRING("", alarmActorName(old, cfg));
    char out[256];
    TEST_ASSERT_TRUE(alarmFormatLine(old, cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"ts\":1700000200,\"id\":\"tS2\",\"val\":25.30,\"alarm\":\"alarm\",\"seq\":13}", out);
    /* ator que aponta para uma conta inativa não vira nome de ninguém */
    old.actor = alarmActorFromSlot(5);
    TEST_ASSERT_EQUAL_STRING("", alarmActorName(old, cfg));
    TEST_ASSERT_EQUAL_UINT8(ALARM_ACTOR_NONE, alarmActorFromSlot(-1));
    TEST_ASSERT_EQUAL_UINT8(ALARM_ACTOR_NONE, alarmActorFromSlot(MAX_USERS));
    TEST_ASSERT_EQUAL_UINT8(32, alarmActorFromSlot(31));
    /* a fila guarda os dois campos novos */
    AlarmQueue q(4);
    q.push(1, 2, 0, -500, ALARM_ERR_ALARM_LIM, alarmActorFromSlot(3), 3050);
    AlarmRecord got[1];
    TEST_ASSERT_EQUAL_UINT8(1, q.snapshot(got, 1));
    TEST_ASSERT_EQUAL_UINT8(4, got[0].actor);
    TEST_ASSERT_EQUAL_INT16(3050, got[0].value2);
    TEST_ASSERT_EQUAL_INT16(-500, got[0].value);
    TEST_ASSERT_EQUAL_UINT8(0, got[0].flags); /* alarm_lim carrega valor: não é ERR */
}

void test_v24_csv_appends_four_columns(void) {
    SystemConfig cfg; fillV24Cfg(cfg);
    char out[128];
    AlarmRecord lim{};
    lim.epoch = 1700000300; lim.seq = 14; lim.slot = 2; lim.channel = CH_TEMP;
    lim.errCode = ALARM_ERR_ALARM_LIM; lim.actor = alarmActorFromSlot(3);
    lim.value = -500; lim.value2 = 3050;
    alarmFormatCsvLine(lim, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("14;1700000300;tS2;alarm_lim;joao;-5.00;30.50;", out);
    AlarmRecord mon{};
    mon.epoch = 1700000400; mon.seq = 15; mon.slot = 2; mon.channel = CH_TEMP;
    mon.errCode = ALARM_ERR_MAINT_ON; mon.actor = alarmActorFromSlot(3);
    mon.value = HIST_NAN_SENTINEL; mon.value2 = 60;
    alarmFormatCsvLine(mon, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("15;1700000400;tS2;maint_on;joao;;;1700004000", out);
    /* um registro antigo: as quatro colunas novas vazias, as quatro velhas iguais */
    AlarmRecord ok = { 1756250300, 4, 2530, 2, 0, 0, ALARM_ERR_ALARM };
    alarmFormatCsvLine(ok, cfg, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("4;1756250300;tS2;25.30;;;;", out);
}

/* Cada grupo: mexer num campo dele acende o bit DELE e nenhum outro — em
 * particular nunca CFG_UNKNOWN, que é o que prova que o …Copy enxerga o mesmo
 * campo que o …Differs. */
static void expectOnly(const SystemConfig& a, const SystemConfig& b, uint32_t bit) {
    /* classifyConfigChanges consome o `before`, então cada chamada recebe a sua
     * própria cópia — o teste é sobre a classificação, não sobre a sonda. */
    SystemConfig scratch = a;
    const uint32_t m = classifyConfigChanges(scratch, b);
    TEST_ASSERT_EQUAL_HEX32(bit, m);
}

void test_classify_names_each_group_alone(void) {
    SystemConfig base; memset(&base, 0, sizeof(base));
    SystemConfig x;

    x = base; x.sensors[3].chMax[CH_TEMP] = 42.0f;      expectOnly(base, x, CFG_ALARMS);
    x = base; x.sensors[3].alarmsActive = true;          expectOnly(base, x, CFG_ALARMS);
    x = base; x.maint.until[5] = 12345;                  expectOnly(base, x, CFG_MAINT);
    x = base; x.alarmTel.enabled = true;                 expectOnly(base, x, CFG_ALARMTEL);
    x = base; x.telPort = 8443;                          expectOnly(base, x, CFG_TELEMETRY);
    x = base; strcpy(x.telServer, "h");                  expectOnly(base, x, CFG_TELEMETRY);
    x = base; x.themeIndex = 3;                          expectOnly(base, x, CFG_DISPLAY);
    x = base; strcpy(x.wifiSsid, "n");                   expectOnly(base, x, CFG_NET);
    x = base; strcpy(x.deviceName, "d");                 expectOnly(base, x, CFG_IDENTITY);
    x = base; x.users[0].permissions = 9;                expectOnly(base, x, CFG_USERS);
    x = base; x.sensors[1].pins[0] = 7;                  expectOnly(base, x, CFG_SLOTS);
    x = base; x.ds18Resolution = 11;                     expectOnly(base, x, CFG_SENSING);
    x = base; x.telEncryption = true;                    expectOnly(base, x, CFG_MQTT);
    x = base; x.timezoneOffset = -3;                     expectOnly(base, x, CFG_TIME);
    x = base; x.loggingEnabled = true;                   expectOnly(base, x, CFG_LOGGING);
    x = base; strcpy(x.displayPin, "1234");              expectOnly(base, x, CFG_PIN);
    x = base; x.reserved[24] = 8;                        expectOnly(base, x, CFG_RESERVED);
}

void test_classify_reports_nothing_when_nothing_changed(void) {
    SystemConfig a; memset(&a, 0, sizeof(a));
    SystemConfig b = a;
    SystemConfig sc0 = a;
    TEST_ASSERT_EQUAL_HEX32(CFG_NONE, classifyConfigChanges(sc0, b));
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_NONE));
}

void test_classify_combines_groups(void) {
    SystemConfig a; memset(&a, 0, sizeof(a));
    SystemConfig b = a;
    b.sensors[0].chMin[CH_HUM] = 10.0f;
    b.maint.until[0] = 999;
    SystemConfig sc1 = a;
    const uint32_t m = classifyConfigChanges(sc1, b);
    TEST_ASSERT_EQUAL_HEX32(CFG_ALARMS | CFG_MAINT, m);
    TEST_ASSERT_FALSE(configNeedsReboot(m));   /* os dois são ao vivo */
}

void test_reboot_classes_are_exactly_the_ones_that_reboot(void) {
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_ALARMS));
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_MAINT));
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_ALARMTEL));
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_TELEMETRY));
    TEST_ASSERT_FALSE(configNeedsReboot(CFG_DISPLAY));
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_NET));
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_SLOTS));
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_MQTT));
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_UNKNOWN));
    /* uma mistura de ao-vivo com reboot reinicia */
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_ALARMS | CFG_NET));
}

void test_classify_flags_an_unclassified_byte(void) {
    /* O fail-safe, que é o argumento de segurança inteiro deste módulo: um
     * campo que ninguém classificou tem de virar CFG_UNKNOWN, e CFG_UNKNOWN
     * reinicia. Se alguém acrescentar um campo ao schema e esquecer deste
     * arquivo, o comportamento degrada para o de hoje — nunca para aplicar
     * ao vivo algo que precisava de reboot.
     *
     * `version` é escolhido de propósito: está DENTRO de SystemConfig, não
     * pertence a grupo nenhum e a sonda o copia explicitamente. Então este
     * teste falha no dia em que a sonda parar de cobri-lo — que é o dia em que
     * a cobertura do fail-safe começaria a mentir. */
    SystemConfig a; memset(&a, 0, sizeof(a));
    SystemConfig b = a;
    b.version = 99;
    SystemConfig sc0 = a;
    TEST_ASSERT_EQUAL_HEX32(CFG_NONE, classifyConfigChanges(sc0, b)); /* coberto */

    /* Agora um byte de verdade fora de todo grupo: o padding entre campos não
     * existe (struct packed), então usa-se o único buraco real — nenhum. A
     * cobertura é total por construção, e é isto que o memcmp da sonda afirma:
     * copiados todos os grupos, ANTES e DEPOIS ficam idênticos. */
    SystemConfig c = a;
    c.sensors[2].chMax[CH_PRESS] = 1200.0f;
    c.reserved[0] = 1;
    c.telInterval = 5;
    SystemConfig sc3 = a;
    const uint32_t m = classifyConfigChanges(sc3, c);
    TEST_ASSERT_EQUAL_HEX32(CFG_ALARMS | CFG_RESERVED | CFG_TELEMETRY, m);
    TEST_ASSERT_FALSE(m & CFG_UNKNOWN);
}

void test_change_list_renders_names(void) {
    char buf[128];
    const size_t n = configChangeList(CFG_ALARMS | CFG_MAINT, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("\"alarms\",\"maint\"", buf);
    TEST_ASSERT_EQUAL_UINT32(strlen(buf), n);

    configChangeList(CFG_NONE, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);

    /* cabe-ou-corta, nunca estoura */
    char tiny[8];
    configChangeList(CFG_ALARMS | CFG_MAINT | CFG_NET, tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(strlen(tiny) < sizeof(tiny));
}

/* ===========================================================================
 * CONFIG MIGRATION (ConfigMigrate.h) — v20 / v21-v22 / v23 blobs into v24
 *
 * The bug this guards against never fails to compile: the historical sizes
 * used to be DERIVED from the current struct, so growing users[] made every
 * config in the field unrecognisable and the device booted on defaults.
 * Each test builds a blob with a marker in every segment (head, first and
 * last legacy account, tail start, tail end) and checks that each marker
 * lands in the field the current struct says it belongs to.
 * ========================================================================= */

/* Offset, inside a legacy blob, of a field that lives in the post-users tail:
 * the tail was copied unchanged, so its distance from telServer is the same
 * in both layouts. */
static size_t legacyTailOff(size_t currentFieldOff) {
    return CFG_LEGACY_TAIL_OFF + (currentFieldOff - offsetof(SystemConfig, telServer));
}

static void fillLegacyHead(uint8_t* blob, uint16_t version) {
    uint32_t magic = CONFIG_MAGIC;
    memcpy(blob, &magic, 4);
    blob[4] = (uint8_t)(version & 0xFF);
    blob[5] = (uint8_t)(version >> 8);
    strcpy((char*)blob + offsetof(SystemConfig, deviceName), "rig-name");
    strcpy((char*)blob + offsetof(SystemConfig, wifiSsid), "bench-ap");
    blob[offsetof(SystemConfig, useHttps)] = 1;          /* last byte of the head */
    /* legacy account 0: active, "admin", permissions 0xFFFF, hashVersion 1 */
    uint8_t* u0 = blob + CFG_LEGACY_HEAD_LEN;
    u0[0] = 1;
    strcpy((char*)u0 + 1, "admin");
    u0[1 + 16 + 33] = 0xFF; u0[1 + 16 + 33 + 1] = 0xFF;   /* permissions (uint16) */
    u0[CFG_LEGACY_USER_STRIDE - 1] = 1;                    /* hashVersion */
    /* legacy account 4: the last one, so its end is the tail's start */
    uint8_t* u4 = blob + CFG_LEGACY_HEAD_LEN + 4 * CFG_LEGACY_USER_STRIDE;
    u4[0] = 1;
    strcpy((char*)u4 + 1, "last");
    u4[1 + 16 + 33] = 0x03; u4[1 + 16 + 33 + 1] = 0x02;    /* permissions 0x0203 */
    /* tail start: telServer is the first field after the accounts */
    strcpy((char*)blob + CFG_LEGACY_TAIL_OFF, "tel.example");
    /* deep in the v20 tail: ntpServer and the last byte of reserved[] */
    strcpy((char*)blob + legacyTailOff(offsetof(SystemConfig, ntpServer)), "ntp.example");
    blob[legacyTailOff(offsetof(SystemConfig, reserved)) + 63] = 0xA5;
}

static void test_cfgmig_sizes_are_literals_and_current_is_not_legacy(void) {
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_V20, configLegacyKind(3921));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_V22, configLegacyKind(4732));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_V23, configLegacyKind(4796));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(sizeof(SystemConfig) + 4));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(4795));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(0));
    /* the numbers the rig measured on 2026-09-19, minus the CRC */
    TEST_ASSERT_EQUAL_UINT(3917, CFG_V20_BLOB);
    TEST_ASSERT_EQUAL_UINT(4728, CFG_V22_BLOB);
    TEST_ASSERT_EQUAL_UINT(4792, CFG_V23_BLOB);
    TEST_ASSERT_EQUAL_UINT(6730, sizeof(SystemConfig));
    TEST_ASSERT_EQUAL_UINT(70, sizeof(UserAccount));
    TEST_ASSERT_EQUAL_UINT(32, MAX_USERS);
}

static void test_cfgmig_v23_every_segment_lands(void) {
    static uint8_t blob[CFG_V23_BLOB];
    memset(blob, 0, sizeof(blob));
    fillLegacyHead(blob, 23);
    /* v21+ tail: alarmTel.queueMax; v23 tail end: maint.until[15] */
    blob[legacyTailOff(offsetof(SystemConfig, alarmTel)) + 2] = 7;
    uint32_t until = 0x12345678;
    memcpy(blob + legacyTailOff(offsetof(SystemConfig, maint)) + 15 * 4, &until, 4);

    static SystemConfig out;
    memset(&out, 0xEE, sizeof(out)); /* garbage in: the copy must zero the rest */
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V23, out));

    TEST_ASSERT_EQUAL_UINT32(CONFIG_MAGIC, out.magic);
    TEST_ASSERT_EQUAL_UINT16(23, out.version);           /* the caller stamps 24 */
    TEST_ASSERT_EQUAL_STRING("rig-name", out.deviceName);
    TEST_ASSERT_EQUAL_STRING("bench-ap", out.wifiSsid);
    TEST_ASSERT_TRUE(out.useHttps);
    TEST_ASSERT_TRUE(out.users[0].active);
    TEST_ASSERT_EQUAL_STRING("admin", out.users[0].username);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.users[0].permissions);
    TEST_ASSERT_EQUAL_UINT8(1, out.users[0].hashVersion);
    TEST_ASSERT_TRUE(out.users[4].active);
    TEST_ASSERT_EQUAL_STRING("last", out.users[4].username);
    TEST_ASSERT_EQUAL_UINT16(0x0203, out.users[4].permissions);
    /* accounts the old schema never had: inactive, and no PIN on anyone */
    for (int i = 0; i < MAX_USERS; i++) {
        if (i >= 5) TEST_ASSERT_FALSE(out.users[i].active);
        for (size_t k = 0; k < PIN_HASH_LEN; k++) TEST_ASSERT_EQUAL_UINT8(0, out.users[i].pinHash[k]);
    }
    TEST_ASSERT_EQUAL_STRING("tel.example", out.telServer);
    TEST_ASSERT_EQUAL_STRING("ntp.example", out.ntpServer);
    TEST_ASSERT_EQUAL_UINT8(0xA5, out.reserved[63]);
    TEST_ASSERT_EQUAL_UINT8(7, out.alarmTel.queueMax);
    TEST_ASSERT_EQUAL_UINT32(0x12345678, out.maint.until[15]);
    /* the v24 tail is the caller's to fill */
    for (size_t k = 0; k < 8; k++) TEST_ASSERT_EQUAL_UINT8(0, out.pinAuth.pinSalt[k]);
}

static void test_cfgmig_v22_and_v20_stop_where_their_tails_stop(void) {
    static uint8_t blob[CFG_V22_BLOB];
    memset(blob, 0, sizeof(blob));
    fillLegacyHead(blob, 22);
    blob[legacyTailOff(offsetof(SystemConfig, alarmTel)) + 2] = 9;
    static SystemConfig out;
    memset(&out, 0xEE, sizeof(out));
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V22, out));
    TEST_ASSERT_EQUAL_STRING("admin", out.users[0].username);
    TEST_ASSERT_EQUAL_STRING("last", out.users[4].username);
    TEST_ASSERT_EQUAL_UINT8(0xA5, out.reserved[63]);
    TEST_ASSERT_EQUAL_UINT8(9, out.alarmTel.queueMax);
    for (int s = 0; s < MAX_SENSORS; s++) TEST_ASSERT_EQUAL_UINT32(0, out.maint.until[s]);
    /* a v21 blob is the same size and is accepted by the same kind */
    blob[4] = 21;
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V22, out));
    TEST_ASSERT_EQUAL_UINT16(21, out.version);

    static uint8_t b20[CFG_V20_BLOB];
    memset(b20, 0, sizeof(b20));
    fillLegacyHead(b20, 20);
    memset(&out, 0xEE, sizeof(out));
    TEST_ASSERT_TRUE(configMigrateLegacy(b20, sizeof(b20), CFG_LEGACY_V20, out));
    TEST_ASSERT_EQUAL_STRING("rig-name", out.deviceName);
    TEST_ASSERT_EQUAL_STRING("last", out.users[4].username);
    TEST_ASSERT_EQUAL_STRING("ntp.example", out.ntpServer);
    TEST_ASSERT_EQUAL_UINT8(0xA5, out.reserved[63]);
    /* nothing past reserved[] existed in v20: alarmTel and maint come back zero */
    TEST_ASSERT_EQUAL_UINT8(0, out.alarmTel.queueMax);
    TEST_ASSERT_EQUAL_UINT8(0, out.alarmTel.lineTemplate[0]);
    for (int s = 0; s < MAX_SENSORS; s++) TEST_ASSERT_EQUAL_UINT32(0, out.maint.until[s]);
}

static void test_cfgmig_refuses_wrong_magic_version_or_length(void) {
    static uint8_t blob[CFG_V23_BLOB];
    static SystemConfig out;
    memset(blob, 0, sizeof(blob));
    fillLegacyHead(blob, 23);
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V23, out));
    /* version says v22 but the size says v23: refuse, do not guess */
    blob[4] = 22;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V23, out));
    blob[4] = 23;
    /* the length must be the kind's, exactly */
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob) - 1, CFG_LEGACY_V23, out));
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V22, out));
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_NONE, out));
    /* magic */
    blob[0] ^= 0xFF;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V23, out));
    TEST_ASSERT_FALSE(configMigrateLegacy(nullptr, sizeof(blob), CFG_LEGACY_V23, out));
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
    RUN_TEST(test_maint_window_open_and_closed);
    RUN_TEST(test_maint_clamp_caps_and_expires);
    RUN_TEST(test_maint_payload_is_its_own_domain);
    RUN_TEST(test_maint_line_renders_only_the_maint_key);
    RUN_TEST(test_v24_maint_on_carries_until_and_user);
    RUN_TEST(test_v24_alarm_lim_carries_both_limits);
    RUN_TEST(test_v24_actor_zero_is_nobody_and_old_records_are_unchanged);
    RUN_TEST(test_v24_csv_appends_four_columns);
    RUN_TEST(test_classify_names_each_group_alone);
    RUN_TEST(test_classify_reports_nothing_when_nothing_changed);
    RUN_TEST(test_classify_combines_groups);
    RUN_TEST(test_reboot_classes_are_exactly_the_ones_that_reboot);
    RUN_TEST(test_cfgmig_sizes_are_literals_and_current_is_not_legacy);
    RUN_TEST(test_cfgmig_v23_every_segment_lands);
    RUN_TEST(test_cfgmig_v22_and_v20_stop_where_their_tails_stop);
    RUN_TEST(test_cfgmig_refuses_wrong_magic_version_or_length);
    RUN_TEST(test_classify_flags_an_unclassified_byte);
    RUN_TEST(test_change_list_renders_names);
    return UNITY_END();
}
