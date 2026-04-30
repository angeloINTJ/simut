/**
 * @file    HistoryCodec.h
 * @brief   Encoder/decoder para historico binario v2 (delta + sensor-mask + anchor).
 * @details Formato substitui os 28 B/record fixos da v1 por:
 *            - 16 B header por arquivo (magic SIM2 + version + anchorPeriod)
 *            - 1 anchor (record completo 28 B) a cada N=60 records
 *            - 59 deltas variaveis entre anchors (tipico ~7-12 B cada)
 *
 *          Compressao tipica 3-4x para uso comum (poucos sensores ativos +
 *          variacao temperatura pequena entre samples). Reader e linear:
 *          decode rapido (varint + zigzag), compativel com chunked-read.
 *
 *          Sem retro-compatibilidade: arquivos v1 (28 B fixos) precisam ser
 *          convertidos via tools/history_v1_to_v2.py antes de upload.
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs.h"

/* ======================================================================== */
/*                       FORMATO DE ARQUIVO V2                              */
/* ======================================================================== */

constexpr char     HIST_V2_MAGIC[4]      = {'S','I','M','2'};
constexpr uint16_t HIST_V2_VERSION       = 0x0002;
constexpr uint16_t HIST_V2_ANCHOR_PERIOD = 60;            /* 1 anchor + 59 deltas (= 1 hora @ 1 min) */
constexpr size_t   HIST_V2_HEADER_SIZE   = 16;
constexpr size_t   HIST_V2_MAX_DELTA_SIZE = 40;           /* worst-case: 2B mask + 5B Δepoch + 12*3B varints */

struct __attribute__((packed)) HistoryFileHeaderV2 {
    char     magic[4];        /* "SIM2" */
    uint16_t version;         /* 0x0002 */
    uint16_t anchorPeriod;    /* 60 */
    uint32_t flags;           /* reservado, 0 */
    uint32_t recordCount;     /* opcional, 0 = desconhecido */
};
static_assert(sizeof(HistoryFileHeaderV2) == HIST_V2_HEADER_SIZE,
              "HistoryFileHeaderV2 deve ter 16 bytes");

/* ======================================================================== */
/*                       ESTADO DO CODEC                                    */
/* ======================================================================== */

/** Mantem o ultimo valor valido de cada campo entre records consecutivos.
 *  fieldHasValid[0]=ambientTemp, [1]=ambientHum, [2..11]=sensors[0..9].
 *  Quando false, o proximo delta com bit setado encoda o valor ABSOLUTO
 *  (nao delta). */
struct HistoryCodecState {
    BinaryHistoryRecord lastValid;
    bool     fieldHasValid[2 + MAX_SENSORS];   /* 12 bools */
    uint16_t recordsSinceAnchor;
    bool     initialized;
};

void historyCodecReset(HistoryCodecState& s);

/* ======================================================================== */
/*                       ENCODER / DECODER                                  */
/* ======================================================================== */

/** Encoda um record. Decide automaticamente entre anchor (28 B fixo) ou
 *  delta variavel com base em recordsSinceAnchor.
 *
 *  @param rec Record de entrada (in-memory uncompressed).
 *  @param s   Estado do encoder (atualizado in-place).
 *  @param buf Buffer de saida (deve ter >= HIST_V2_MAX_DELTA_SIZE bytes ou
 *             sizeof(BinaryHistoryRecord) — o que for maior).
 *  @param bufSize Tamanho do buffer.
 *  @param outIsAnchor (out, opcional) true se este record foi emitido como
 *             anchor.
 *  @return Bytes escritos no buffer, ou 0 em erro (buffer pequeno).
 */
size_t historyEncodeRecord(const BinaryHistoryRecord& rec,
                            HistoryCodecState& s,
                            uint8_t* buf, size_t bufSize,
                            bool* outIsAnchor = nullptr);

/** Decoda um record. Caller informa se o proximo record e anchor (com
 *  base no contador do arquivo: 1o, 61o, 121o, etc).
 *
 *  @param buf Bytes lidos do arquivo (suficientes para 1 record).
 *  @param bufLen Tamanho do buffer.
 *  @param s Estado do decoder (atualizado in-place).
 *  @param outRec Record reconstruido.
 *  @param isAnchor true se este record e anchor (28 B fixo).
 *  @return Bytes consumidos, ou 0 em erro (buffer truncado).
 */
size_t historyDecodeRecord(const uint8_t* buf, size_t bufLen,
                            HistoryCodecState& s,
                            BinaryHistoryRecord& outRec,
                            bool isAnchor);

/* ======================================================================== */
/*                       VARINT (zigzag, 7 bits/byte)                       */
/* ======================================================================== */

/** Escreve int32 como zigzag varint. Retorna bytes escritos (1..5). */
size_t writeVarintZ(int32_t v, uint8_t* buf);

/** Le zigzag varint. Retorna bytes consumidos (0 = erro/truncado). */
size_t readVarintZ(const uint8_t* buf, size_t bufLen, int32_t& out);
