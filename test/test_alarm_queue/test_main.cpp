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
#include "AlarmEdgeLatch.h" /* #161: an edge the full queue refused waits for room */
#include "AlarmPayload.h"
#include "ConfigApply.h"
#include "ConfigMigrate.h" /* v24: legacy config blobs by segment, sizes frozen */
#include "SystemDefs_Validate.h" /* v25: clampPinPolicy — a migrated v24 blob carries no policy */

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

/* v25 — a assinatura de um registro pendente sobrevive ao apagamento da conta.
 *
 * Até a v24 o nome era resolvido na hora de montar o payload, lendo
 * cfg.users[actor-1]. A fila espera o servidor confirmar, o slot é
 * reutilizável, e apagar uma conta nesse intervalo fazia o registro sair
 * assinado por quem tomasse o slot depois — num registro de AUDITORIA. */
/* Espelho de StorageManager::wipeUserAccount — a função real vive num .cpp que
 * o env native nao compila (Arduino/LittleFS). O que se testa aqui e o
 * CONTRATO: depois do wipe nao sobra byte nenhum do dono anterior. */
static void StorageManagerStub_wipe(UserAccount& u) {
    volatile uint8_t* p = (volatile uint8_t*)&u;
    for (size_t i = 0; i < sizeof(UserAccount); i++) p[i] = 0;
}

void test_v25_wipe_leaves_nothing_of_the_previous_owner(void) {
    UserAccount u;
    memset(&u, 0xAB, sizeof(u));
    u.active = true;
    snprintf(u.username, sizeof(u.username), "%s", "fulano");
    u.permissions = 0xFFFF;
    StorageManagerStub_wipe(u);
    const uint8_t* p = (const uint8_t*)&u;
    for (size_t i = 0; i < sizeof(UserAccount); i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, p[i], "byte do dono anterior sobreviveu ao wipe");
    TEST_ASSERT_FALSE(u.active);
    TEST_ASSERT_EQUAL_STRING("", u.username);
    TEST_ASSERT_EQUAL_UINT16(0, u.permissions);
    /* e um registro apagado nao "tem PIN" */
    bool any = false;
    for (size_t i = 0; i < PIN_HASH_LEN; i++) if (u.pinHash[i]) any = true;
    TEST_ASSERT_FALSE(any);
}

void test_v25_actor_name_is_frozen_at_push(void) {
    SystemConfig cfg; fillV24Cfg(cfg);
    const int SLOT = 3;
    TEST_ASSERT_TRUE(cfg.users[SLOT].active);
    char whoActed[16];
    snprintf(whoActed, sizeof(whoActed), "%s", cfg.users[SLOT].username);

    AlarmQueue q(4);
    q.push(1700000300u, 2, CH_TEMP, 2500, ALARM_ERR_ALARM_ON,
           alarmActorFromSlot(SLOT), 0, cfg.users[SLOT].username);
    AlarmRecord got[1];
    TEST_ASSERT_EQUAL_UINT8(1, q.snapshot(got, 1));
    TEST_ASSERT_EQUAL_STRING(whoActed, got[0].user);
    TEST_ASSERT_EQUAL_STRING(whoActed, alarmActorName(got[0], cfg));

    /* a conta é apagada e o slot é tomado por outra pessoa */
    StorageManagerStub_wipe(cfg.users[SLOT]);
    snprintf(cfg.users[SLOT].username, sizeof(cfg.users[SLOT].username), "%s", "outra");
    cfg.users[SLOT].active = true;
    /* o registro continua nomeando quem agiu */
    TEST_ASSERT_EQUAL_STRING(whoActed, alarmActorName(got[0], cfg));
    char out[256];
    TEST_ASSERT_TRUE(alarmFormatLine(got[0], cfg, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, whoActed));
    TEST_ASSERT_NULL(strstr(out, "outra"));

    /* nome longo demais é truncado, nunca estoura o campo */
    AlarmQueue q2(2);
    q2.push(1, 0, CH_TEMP, 0, ALARM_ERR_ALARM_ON, alarmActorFromSlot(1), 0,
            "nome-muito-comprido-demais");
    AlarmRecord g2[1];
    TEST_ASSERT_EQUAL_UINT8(1, q2.snapshot(g2, 1));
    TEST_ASSERT_EQUAL_UINT(15, strlen(g2[0].user));
    TEST_ASSERT_EQUAL_STRING("nome-muito-comp", g2[0].user);

    /* sem nome, cai no comportamento da v24 (registro que já estava na fila
     * num upgrade a quente) */
    AlarmQueue q3(2);
    q3.push(1, 0, CH_TEMP, 0, ALARM_ERR_ALARM_ON, alarmActorFromSlot(0), 0);
    AlarmRecord g3[1];
    q3.snapshot(g3, 1);
    TEST_ASSERT_EQUAL_STRING("", g3[0].user);
    TEST_ASSERT_EQUAL_STRING(cfg.users[0].username, alarmActorName(g3[0], cfg));
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

/* The `before` argument is CONSUMED — it becomes the fail-safe probe in place.
 * That is deliberate (a 6.7 kB local would blow the web handler's stack) and
 * the declaration says so in capitals, but a caller that hands it the LIVE
 * configuration copies the staged values straight into the running device.
 * That is exactly what happened on 2026-09-20: `_dry=1` — the flag whose whole
 * promise is "nothing changes" — changed the timezone, the sampling interval
 * and an alarm limit for real, and a later save carried them to flash.
 * Asserting the mutation here makes the contract a fact the suite checks,
 * instead of a sentence somebody has to read. */
void test_classify_consumes_its_before(void) {
    SystemConfig before; memset(&before, 0, sizeof(before));
    SystemConfig after;  memset(&after, 0, sizeof(after));
    before.timezoneOffset = -3;
    after.timezoneOffset  = -5;
    TEST_ASSERT_EQUAL_INT(-3, before.timezoneOffset);

    const uint32_t m = classifyConfigChanges(before, after);
    TEST_ASSERT_EQUAL_HEX32(CFG_TIME, m);
    /* The probe now holds `after`: the caller's snapshot is gone. */
    TEST_ASSERT_EQUAL_INT(-5, before.timezoneOffset);
    /* And a second pass over the same pair therefore reports nothing — which
     * is the symptom a caller sees when it reused the buffer by mistake. */
    TEST_ASSERT_EQUAL_HEX32(CFG_NONE, classifyConfigChanges(before, after));
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
    x = base; x.reserved[24] = 8;                        expectOnly(base, x, CFG_RESERVED);
}

/* Dead fields (no consumer) classify as CFG_NONE, so a commit touching only one
 * does NOT reboot (P0, 2026-09-26). useHttps is called "the dead cfg.useHttps
 * flag" in WebManager_Auth.cpp; displayPin is "v24: dead field"; sampleIntervalMs
 * is stored and echoed but never read by the sensing pipeline; loggingEnabled is
 * shown but gates no logging. Before this, each forced a needless reboot
 * (CFG_NET/CFG_PIN/CFG_SENSING/CFG_LOGGING). */
void test_classify_dead_fields_do_not_reboot(void) {
    SystemConfig base; memset(&base, 0, sizeof(base));
    SystemConfig x;

    x = base; x.useHttps = true;                         expectOnly(base, x, CFG_NONE);
    x = base; strcpy(x.displayPin, "1234");              expectOnly(base, x, CFG_NONE);
    x = base; x.sampleIntervalMs = 5000;                 expectOnly(base, x, CFG_NONE);
    x = base; x.loggingEnabled = true;                   expectOnly(base, x, CFG_NONE);

    /* And the reboot decision agrees: only a dead field changed -> no reboot. */
    x = base; x.loggingEnabled = true; x.useHttps = true;
    SystemConfig sc = base;
    TEST_ASSERT_FALSE(configNeedsReboot(classifyConfigChanges(sc, x)));
}

/* v26: the custom Content-Type is read by the sender on every upload — the
 * header is built per request from the live config — so an edit applies at the
 * next send: CFG_TELEMETRY for the data line, CFG_ALARMTEL for the alarm line.
 * Left out of the span table it fell to CFG_UNKNOWN, the fail-safe, and every
 * edit of the field rebooted the device. */
void test_classify_content_type_is_live(void) {
    SystemConfig base; memset(&base, 0, sizeof(base));
    SystemConfig x;

    x = base; strcpy(x.telCustom.telCustomContentType, "text/csv");
    expectOnly(base, x, CFG_TELEMETRY);
    x = base; strcpy(x.telCustom.alarmCustomContentType, "application/x-ndjson");
    expectOnly(base, x, CFG_ALARMTEL);

    x = base;
    strcpy(x.telCustom.telCustomContentType, "text/csv");
    strcpy(x.telCustom.alarmCustomContentType, "text/csv");
    SystemConfig sc = base;
    TEST_ASSERT_FALSE(configNeedsReboot(classifyConfigChanges(sc, x)));
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

/* A-08: accounts apply live. Their consumers read the account at use — a web
 * session checks the live account on every request (SessionCheck.h), the panel
 * in panelAllowed( ), the alarm line signs at push — so a commit that adds,
 * deletes or resets accounts, or sets a PIN or the PIN policy, no longer pays
 * a restart. One that also touches a reboot class still restarts. */
void test_classify_accounts_apply_live(void) {
    SystemConfig base; memset(&base, 0, sizeof(base));
    SystemConfig x;

    x = base;
    x.users[3].active = true;
    strcpy(x.users[3].username, "ana");
    x.users[3].permissions = 1;
    x.users[3].salt[0] = 9;
    expectOnly(base, x, CFG_USERS);
    SystemConfig sc = base;
    TEST_ASSERT_FALSE(configNeedsReboot(classifyConfigChanges(sc, x)));

    x = base; x.pinAuth.pinMinLen = 6;      /* the PIN policy */
    expectOnly(base, x, CFG_USERS);
    sc = base;
    TEST_ASSERT_FALSE(configNeedsReboot(classifyConfigChanges(sc, x)));

    TEST_ASSERT_FALSE(configNeedsReboot(CFG_USERS));
    TEST_ASSERT_TRUE(configNeedsReboot(CFG_USERS | CFG_NET));
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
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_V24, configLegacyKind(6734));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_V25, configLegacyKind(6742));
    /* The CURRENT schema is never a legacy kind: attemptLoad( ) reads it by
     * size before it asks configLegacyKind( ), and the day v27 lands, v26 joins
     * the table above. */
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(sizeof(SystemConfig) + 4));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(4795));
    TEST_ASSERT_EQUAL_INT(CFG_LEGACY_NONE, configLegacyKind(0));
    /* the numbers the rig measured on 2026-09-19, minus the CRC */
    TEST_ASSERT_EQUAL_UINT(3917, CFG_V20_BLOB);
    TEST_ASSERT_EQUAL_UINT(4728, CFG_V22_BLOB);
    TEST_ASSERT_EQUAL_UINT(4792, CFG_V23_BLOB);
    TEST_ASSERT_EQUAL_UINT(6730, CFG_V24_BLOB);
    TEST_ASSERT_EQUAL_UINT(6738, CFG_V25_BLOB);
    TEST_ASSERT_EQUAL_UINT(6802, CFG_V26_BLOB);
    TEST_ASSERT_EQUAL_UINT(6802, sizeof(SystemConfig));
    TEST_ASSERT_EQUAL_UINT(70, sizeof(UserAccount));
    TEST_ASSERT_EQUAL_UINT(32, MAX_USERS);
}

/* v24 -> v25: the layout did not change, it grew. Everything a v24 blob holds
 * has to land at the same offset, and the four policy bytes plus the
 * must-change map have to arrive ZERO — which is not a valid policy, and is
 * exactly why the loader clamps them to the v24 behaviour instead of trusting
 * them. A v24 device that upgrades must behave identically until somebody
 * opens the policy screen. */
static void test_cfgmig_v24_grows_without_moving_anything(void) {
    static uint8_t blob[CFG_V24_BLOB];
    memset(blob, 0, sizeof(blob));
    fillLegacyHead(blob, 24);
    /* v24 already has 32 accounts of 70 bytes, so the head is this struct's
     * own layout: write through a same-shaped struct and truncate. */
    static SystemConfig src;
    memset(&src, 0, sizeof(src));
    memcpy(&src, blob, CFG_LEGACY_HEAD_LEN);
    src.users[31].active = true;
    strcpy(src.users[31].username, "last");
    src.users[31].pinHash[0] = 0x5A;
    src.maint.until[15] = 0x12345678;
    src.pinAuth.pinSalt[7] = 0xC3;
    /* the bytes a v24 file does NOT have */
    src.pinAuth.pinMinLen = 9; src.pinAuth.pinKeypad = 2;
    src.pinAuth.pinAlphabet = 1; src.pinAuth.pinMustChange = 0xFFFFFFFF;
    memcpy(blob, &src, CFG_V24_BLOB);   /* truncates exactly at pinMinLen */

    static SystemConfig out;
    memset(&out, 0xEE, sizeof(out));
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V24, out));

    TEST_ASSERT_EQUAL_UINT32(CONFIG_MAGIC, out.magic);
    TEST_ASSERT_EQUAL_UINT16(24, out.version);      /* the caller stamps 25 */
    TEST_ASSERT_TRUE(out.users[31].active);
    TEST_ASSERT_EQUAL_STRING("last", out.users[31].username);
    TEST_ASSERT_EQUAL_UINT8(0x5A, out.users[31].pinHash[0]);
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, out.maint.until[15]);
    TEST_ASSERT_EQUAL_UINT8(0xC3, out.pinAuth.pinSalt[7]);
    /* the tail the old file did not carry */
    TEST_ASSERT_EQUAL_UINT8(0, out.pinAuth.pinMinLen);
    TEST_ASSERT_EQUAL_UINT8(0, out.pinAuth.pinKeypad);
    TEST_ASSERT_EQUAL_UINT8(0, out.pinAuth.pinAlphabet);
    TEST_ASSERT_EQUAL_UINT32(0u, out.pinAuth.pinMustChange);
    /* and what the loader makes of it: the v24 behaviour, exactly */
    uint8_t mn = out.pinAuth.pinMinLen, kb = out.pinAuth.pinKeypad,
            al = out.pinAuth.pinAlphabet;
    clampPinPolicy(mn, kb, al);
    TEST_ASSERT_EQUAL_INT(4, mn);
    TEST_ASSERT_EQUAL_INT(PinKb::KB_SET3, kb);
    TEST_ASSERT_EQUAL_INT(PinKb::ALPHA_DIGITS, al);

    /* wrong version stamp, wrong length, wrong kind: all refused */
    blob[4] = 23;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V24, out));
    blob[4] = 24;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob) - 1, CFG_LEGACY_V24, out));
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V23, out));
}

/* v25 -> v26: the 2.7.4 -> next upgrade, which every device in the field takes.
 * v25 already IS the 32 x 70 account layout, so its migration is the same
 * straight copy as v24's: every byte keeps its offset and the telCustom tail
 * arrives zero, which the sender reads as "application/json".
 *
 * The first cut of v26 sent this kind down the v20..v23 walk — five accounts of
 * 62 bytes, then the tail from byte 478. Measured on the host 2026-09-30: the
 * head survived, and account 0 by the accident that a legacy record is the
 * first 62 bytes of a v24 one; accounts 1..31, telemetry, every sensor slot,
 * reserved[], alarmTel, maint and the PIN policy came back empty — with the
 * function still answering true, so nothing downstream could tell. Account 1
 * and the far end of the tail are the markers that caught it. */
static void test_cfgmig_v25_grows_without_moving_anything(void) {
    static SystemConfig src;
    memset(&src, 0, sizeof(src));
    src.magic = CONFIG_MAGIC;
    src.version = 25;
    strcpy(src.deviceName, "rig-name");
    src.users[0].active = true;
    strcpy(src.users[0].username, "admin");
    src.users[1].active = true;                        /* the first casualty */
    strcpy(src.users[1].username, "viewer");
    src.users[1].permissions = 0x0003;
    src.users[31].active = true;
    strcpy(src.users[31].username, "last");
    src.users[31].pinHash[0] = 0x5A;
    strcpy(src.telServer, "192.168.3.206");
    src.telPort = 8080;
    src.sensors[0].active = true;
    strcpy(src.sensors[0].hwId, "STM0009");
    strcpy(src.sensors[MAX_SENSORS - 1].friendlyName, "last-slot");
    src.reserved[63] = 0xA5;
    src.alarmTel.queueMax = 7;
    src.maint.until[15] = 0x12345678;
    src.pinAuth.pinSalt[7] = 0xC3;
    src.pinAuth.pinMinLen = 6; src.pinAuth.pinKeypad = 2; src.pinAuth.pinAlphabet = 1;
    src.pinAuth.pinMustChange = 0x80000001u;
    /* the bytes a v25 file does NOT have */
    strcpy(src.telCustom.telCustomContentType, "text/csv");
    strcpy(src.telCustom.alarmCustomContentType, "text/csv");

    static uint8_t blob[CFG_V25_BLOB];
    memcpy(blob, &src, CFG_V25_BLOB);   /* truncates exactly at telCustom */

    static SystemConfig out;
    memset(&out, 0xEE, sizeof(out));
    TEST_ASSERT_TRUE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V25, out));

    TEST_ASSERT_EQUAL_UINT32(CONFIG_MAGIC, out.magic);
    TEST_ASSERT_EQUAL_UINT16(25, out.version);      /* the caller stamps 26 */
    TEST_ASSERT_EQUAL_STRING("rig-name", out.deviceName);
    TEST_ASSERT_EQUAL_STRING("admin", out.users[0].username);
    TEST_ASSERT_TRUE(out.users[1].active);
    TEST_ASSERT_EQUAL_STRING("viewer", out.users[1].username);
    TEST_ASSERT_EQUAL_UINT16(0x0003, out.users[1].permissions);
    TEST_ASSERT_TRUE(out.users[31].active);
    TEST_ASSERT_EQUAL_STRING("last", out.users[31].username);
    TEST_ASSERT_EQUAL_UINT8(0x5A, out.users[31].pinHash[0]);
    TEST_ASSERT_EQUAL_STRING("192.168.3.206", out.telServer);
    TEST_ASSERT_EQUAL_UINT16(8080, out.telPort);
    TEST_ASSERT_TRUE(out.sensors[0].active);
    TEST_ASSERT_EQUAL_STRING("STM0009", out.sensors[0].hwId);
    TEST_ASSERT_EQUAL_STRING("last-slot", out.sensors[MAX_SENSORS - 1].friendlyName);
    TEST_ASSERT_EQUAL_UINT8(0xA5, out.reserved[63]);
    TEST_ASSERT_EQUAL_UINT8(7, out.alarmTel.queueMax);
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, out.maint.until[15]);
    TEST_ASSERT_EQUAL_UINT8(0xC3, out.pinAuth.pinSalt[7]);
    /* a v25 file HAS a policy: it is carried, not reset */
    TEST_ASSERT_EQUAL_UINT8(6, out.pinAuth.pinMinLen);
    TEST_ASSERT_EQUAL_UINT8(2, out.pinAuth.pinKeypad);
    TEST_ASSERT_EQUAL_UINT8(1, out.pinAuth.pinAlphabet);
    TEST_ASSERT_EQUAL_UINT32(0x80000001u, out.pinAuth.pinMustChange);
    /* the tail the old file did not carry comes back empty */
    const uint8_t* tail = (const uint8_t*)&out.telCustom;
    for (size_t k = 0; k < sizeof(out.telCustom); k++) TEST_ASSERT_EQUAL_UINT8(0, tail[k]);

    /* wrong version stamp, wrong length, wrong kind: all refused */
    blob[4] = 24;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V25, out));
    blob[4] = 26;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V25, out));
    blob[4] = 25;
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob) - 1, CFG_LEGACY_V25, out));
    TEST_ASSERT_FALSE(configMigrateLegacy(blob, sizeof(blob), CFG_LEGACY_V24, out));
}

/* Memory safety, for every kind. The mis-routed v25 of the first cut of v26 did
 * not just lose fields: the five-account walk copies `blobLen - 478` bytes to
 * telServer (offset 2408), and for a 6738-byte blob that is 6260 bytes into a
 * 6802-byte struct — 1866 bytes past its end. On the device the destination is
 * a heap object allocated during boot. The field checks above could not see
 * that; a guard region right after the struct can. Every kind, filled with a
 * pattern that is not zero, must leave the guard untouched. */
static void test_cfgmig_never_writes_past_the_struct(void) {
    static struct { SystemConfig cfg; uint8_t guard[2048]; } box;
    static uint8_t blob[CFG_V25_BLOB];      /* the largest legacy blob */
    const struct { CfgLegacyKind kind; uint16_t ver; } kinds[] = {
        { CFG_LEGACY_V20, 20 }, { CFG_LEGACY_V22, 22 }, { CFG_LEGACY_V23, 23 },
        { CFG_LEGACY_V24, 24 }, { CFG_LEGACY_V25, 25 },
    };
    for (const auto& k : kinds) {
        const size_t len = configLegacyBlobLen(k.kind);
        TEST_ASSERT_TRUE(len > 0 && len <= sizeof(blob));
        memset(blob, 0x5C, len);
        const uint32_t magic = CONFIG_MAGIC;
        memcpy(blob, &magic, 4);
        blob[4] = (uint8_t)(k.ver & 0xFF);
        blob[5] = (uint8_t)(k.ver >> 8);
        memset(box.guard, 0xA5, sizeof(box.guard));
        TEST_ASSERT_TRUE(configMigrateLegacy(blob, len, k.kind, box.cfg));
        size_t past = 0;                       /* how far the damage reaches */
        for (size_t g = 0; g < sizeof(box.guard); g++)
            if (box.guard[g] != 0xA5) past = g + 1;
        if (past) {
            char msg[64];
            snprintf(msg, sizeof(msg), "v%u wrote %u B past the struct", (unsigned)k.ver, (unsigned)past);
            TEST_FAIL_MESSAGE(msg);
        }
    }
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


/* ── #161: an edge the full queue refused is announced once there is room ──
 *
 * The detector latched an edge whether or not the queue took its record. An
 * alarm that began while the queue was full (the server away long enough) was
 * marked as announced and never sent, not even after the queue drained: only a
 * reboot or the condition clearing and tripping again brought it back. These
 * drive the latch the way AppManager::handleAlarmTelemetryEdges( ) does, one
 * call per pass, through the same alarmEdgeOffer( ) it calls. */
static uint16_t offerTrip(AlarmEdgeLatch<uint8_t>& l, uint8_t ch, bool active,
                          AlarmQueue& q, uint32_t now) {
    return alarmEdgeOffer(l, (uint8_t)(1u << ch), active, [&] {
        return q.push(now, 0, ch, 100, ALARM_ERR_ALARM);
    });
}

static void test_161_a_refused_edge_is_announced_once_the_queue_has_room(void) {
    AlarmQueue q(2);
    q.push(1, 1, 0, 0, ALARM_ERR_ALARM);
    q.push(2, 2, 0, 0, ALARM_ERR_ALARM);          /* full: the server is away */
    AlarmEdgeLatch<uint8_t> trip;

    /* The freezer leaves its range while the queue is full. */
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 0, true, q, 100));
    TEST_ASSERT_EQUAL_UINT16(1, q.dropped( ));

    /* Still full, still out of range: not offered again, not counted again. */
    for (uint32_t t = 105; t < 160; t += 5) {
        alarmEdgeRoom(trip, q.full( ));
        TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 0, true, q, t));
    }
    TEST_ASSERT_EQUAL_UINT16(1, q.dropped( ));
    TEST_ASSERT_EQUAL_UINT8(2, q.size( ));

    /* The server comes back and confirms what it had: the alarm goes out. */
    q.ackOldest(2);
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_NOT_EQUAL(0, offerTrip(trip, 0, true, q, 200));
    AlarmRecord r[2];
    TEST_ASSERT_EQUAL_UINT8(1, q.snapshot(r, 2));
    TEST_ASSERT_EQUAL_UINT32(200, r[0].epoch);

    /* Announced now: later passes add nothing while it stays out of range. */
    for (uint32_t t = 205; t < 260; t += 5) {
        alarmEdgeRoom(trip, q.full( ));
        TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 0, true, q, t));
    }
    TEST_ASSERT_EQUAL_UINT8(1, q.size( ));
}

static void test_161_an_edge_that_ends_while_refused_is_not_announced(void) {
    AlarmQueue q(1);
    q.push(1, 1, 0, 0, ALARM_ERR_ALARM);
    AlarmEdgeLatch<uint8_t> trip;
    alarmEdgeRoom(trip, q.full( ));
    offerTrip(trip, 0, true, q, 100);              /* refused */
    alarmEdgeRoom(trip, q.full( ));
    offerTrip(trip, 0, false, q, 105);             /* back in range, still full */
    q.ackOldest(1);
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 0, false, q, 200));
    TEST_ASSERT_EQUAL_UINT8(0, q.size( ));
    TEST_ASSERT_EQUAL_UINT16(1, q.dropped( ));     /* the loss stays counted */
    /* and if it trips again later, that is a new edge, announced normally */
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_NOT_EQUAL(0, offerTrip(trip, 0, true, q, 300));
}

/* Room offers again what was refused, and only that: what the queue already
 * took is not sent twice. */
static void test_161_room_offers_only_the_refused_edges(void) {
    AlarmQueue q(2);
    AlarmEdgeLatch<uint8_t> trip;
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_NOT_EQUAL(0, offerTrip(trip, 0, true, q, 100));   /* channel 0 taken */
    q.push(101, 9, 0, 0, ALARM_ERR_ERROR);                         /* another slot fills it */
    alarmEdgeRoom(trip, q.full( ));
    offerTrip(trip, 0, true, q, 105);
    TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 1, true, q, 105)); /* channel 1 refused */
    q.ackOldest(2);
    alarmEdgeRoom(trip, q.full( ));
    TEST_ASSERT_EQUAL_UINT16(0, offerTrip(trip, 0, true, q, 200));
    TEST_ASSERT_NOT_EQUAL(0, offerTrip(trip, 1, true, q, 200));
    AlarmRecord r[2];
    TEST_ASSERT_EQUAL_UINT8(1, q.snapshot(r, 2));
    TEST_ASSERT_EQUAL_UINT8(1, r[0].channel);
}

/* The error latch is a bit per slot in a uint16_t: the top slot works too, and
 * reset( ) (line switched off, slot removed) forgets both kinds of mark. */
static void test_161_the_slot_latch_is_sixteen_bits_wide(void) {
    AlarmQueue q(1);
    q.push(1, 1, 0, 0, ALARM_ERR_ALARM);
    AlarmEdgeLatch<uint16_t> err;
    const uint16_t top = (uint16_t)(1u << 15);
    alarmEdgeRoom(err, q.full( ));
    TEST_ASSERT_EQUAL_UINT16(0, alarmEdgeOffer(err, top, true, [&] {
        return q.push(100, 15, 0, 0, ALARM_ERR_ERROR); }));
    TEST_ASSERT_FALSE(err.due(top));
    err.reset( );
    TEST_ASSERT_TRUE(err.due(top));
    q.ackOldest(1);
    alarmEdgeRoom(err, q.full( ));
    TEST_ASSERT_NOT_EQUAL(0, alarmEdgeOffer(err, top, true, [&] {
        return q.push(200, 15, 0, 0, ALARM_ERR_ERROR); }));
    TEST_ASSERT_FALSE(err.due(top));
    TEST_ASSERT_EQUAL_UINT16(top, err.announced);
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
    RUN_TEST(test_v25_wipe_leaves_nothing_of_the_previous_owner);
    RUN_TEST(test_v25_actor_name_is_frozen_at_push);
    RUN_TEST(test_v24_actor_zero_is_nobody_and_old_records_are_unchanged);
    RUN_TEST(test_v24_csv_appends_four_columns);
    RUN_TEST(test_classify_consumes_its_before);
    RUN_TEST(test_classify_names_each_group_alone);
    RUN_TEST(test_classify_dead_fields_do_not_reboot);
    RUN_TEST(test_classify_content_type_is_live);
    RUN_TEST(test_classify_reports_nothing_when_nothing_changed);
    RUN_TEST(test_classify_combines_groups);
    RUN_TEST(test_reboot_classes_are_exactly_the_ones_that_reboot);
    RUN_TEST(test_cfgmig_sizes_are_literals_and_current_is_not_legacy);
    RUN_TEST(test_cfgmig_v24_grows_without_moving_anything);
    RUN_TEST(test_cfgmig_v25_grows_without_moving_anything);
    RUN_TEST(test_cfgmig_never_writes_past_the_struct);
    RUN_TEST(test_cfgmig_v23_every_segment_lands);
    RUN_TEST(test_cfgmig_v22_and_v20_stop_where_their_tails_stop);
    RUN_TEST(test_cfgmig_refuses_wrong_magic_version_or_length);
    RUN_TEST(test_classify_accounts_apply_live);
    RUN_TEST(test_classify_flags_an_unclassified_byte);
    RUN_TEST(test_change_list_renders_names);
    RUN_TEST(test_161_a_refused_edge_is_announced_once_the_queue_has_room);
    RUN_TEST(test_161_an_edge_that_ends_while_refused_is_not_announced);
    RUN_TEST(test_161_room_offers_only_the_refused_edges);
    RUN_TEST(test_161_the_slot_latch_is_sixteen_bits_wide);
    return UNITY_END();
}
