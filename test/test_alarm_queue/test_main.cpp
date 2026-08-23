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

/* ── FIFO e push básico ─────────────────────────────────────────────────── */
static void test_push_fifo_order(void) {
    AlarmQueue q(8);
    uint16_t s0 = q.push(1000, 0, 0 /*CH_TEMP*/, 1234, false);
    uint16_t s1 = q.push(1001, 1, 1 /*CH_HUM*/, -999, false);
    uint16_t s2 = q.push(1002, 2, 0, 567, true);

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
    for (int i = 0; i < 4; i++) q.push(100 + i, i, 0, 1, false);
    TEST_ASSERT_TRUE(q.full());
}

/* ── estouro: drop-newest ───────────────────────────────────────────────── */
static void test_overflow_drop_newest(void) {
    AlarmQueue q(3);
    q.push(100, 0, 0, 1, false);
    q.push(101, 1, 0, 2, false);
    q.push(102, 2, 0, 3, false);

    /* cheio: novo registro recusado, seq 0, dropped incrementa */
    uint16_t refused = q.push(103, 3, 0, 4, false);
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
        uint16_t s = q.push(i, 0, 0, 1, false);
        TEST_ASSERT_EQUAL_UINT16(i + 1, s);
    }
    q.ackOldest(4);
    /* força wrap: 65535 é o último válido */
    /* drena até perto do limite sem depender de estado interno: usa ackOldest */
    /* Como _nextSeq é privado, exercitamos o wrap via ack + push até 65535. */
    for (uint32_t i = 4; i < 65534; i++) {
        q.push(i, 0, 0, 1, false);
        q.ackOldest(1);
    }
    uint16_t s = q.push(1, 0, 0, 1, false);
    TEST_ASSERT_EQUAL_UINT16(65535, s);
    q.ackOldest(1);
    s = q.push(2, 0, 0, 1, false);
    /* 65535 + 1 == 0 é reservado → pula para 1 */
    TEST_ASSERT_EQUAL_UINT16(1, s);
}

/* ── ack por seq ────────────────────────────────────────────────────────── */
static void test_ack_by_seq(void) {
    AlarmQueue q(8);
    q.push(100, 0, 0, 1, false);   /* seq 1 */
    q.push(101, 1, 0, 2, false);   /* seq 2 */
    q.push(102, 2, 0, 3, false);   /* seq 3 */
    q.push(103, 3, 0, 4, false);   /* seq 4 */

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
    for (int i = 0; i < 5; i++) q.push(100 + i, 0, 0, 1, false);
    q.ackOldest(2);
    TEST_ASSERT_EQUAL_UINT8(3, q.size());
    AlarmRecord out[8];
    uint8_t n = q.snapshot(out, 8);
    TEST_ASSERT_EQUAL_UINT32(102, out[0].epoch);
    (void)n;

    q.ackOldest(10); /* além do tamanho → esvazia */
    TEST_ASSERT_TRUE(q.empty());

    for (int i = 0; i < 3; i++) q.push(200 + i, 0, 0, 1, false);
    q.clear();
    TEST_ASSERT_TRUE(q.empty());
    TEST_ASSERT_EQUAL_UINT8(0, q.snapshot(out, 8));

    /* a fila volta a funcionar depois do clear; seq continua monotônico
     * pelo boot (5 já usados + 3 do lote anterior = 9) */
    uint16_t s = q.push(300, 1, 1, 5, true);
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
    for (int i = 0; i < 4; i++) q.push(100 + i, i, 0, 1, false);
    uint16_t seqs[] = {1, 2};
    q.ack(seqs, 2);
    /* capacidade 4 com 2 ocupados: só mais 2 cabem; o 3º é recusado */
    q.push(200, 5, 0, 1, false);
    q.push(201, 5, 0, 1, false);
    uint16_t refused = q.push(202, 5, 0, 1, false);
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
    return UNITY_END();
}
