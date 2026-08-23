/**
 * @file AlarmQueue.h
 * @brief Fila em RAM da segunda linha de telemetria — alarmes (v21).
 *
 * @details Buffer circular ESTÁTICO (zero heap) que é, ao mesmo tempo, o
 * repositório dos alarmes pendentes e o conjunto "aguardando confirmação
 * de recebimento". O envio consome um snapshot da fila; a confirmação
 * (2xx no HTTP, ACK por aplicação no MQTT) remove pelo seq. Sem cursor
 * persistido: o que existe na RAM é exatamente o que ainda não foi
 * confirmado — perder a fila num reboot é o comportamento contratado
 * (R3 da proposta, docs/analysis/ANALISE_TELEMETRIA_ALARMES.md).
 *
 * Política de estouro: drop-newest. O registro mais ANTIGO é o mais
 * provável de ainda representar o estado de alarme vigente; descartar o
 * mais velho esconderia alarme jamais confirmado. Cada descarte soma em
 * dropped( ) e vira métrica alarmDropped.
 *
 * seq: uint16 monotônico por boot, começa em 1, pula o 0 no wrap.
 * 0 é o valor "inválido" (push recusado). É a chave do ACK — resolve
 * duplicatas e registros idênticos (epoch,slot,channel) sem ambiguidade.
 *
 * Header-only e sem dependências de hardware além de SystemDefs_Records.h
 * (constantes), para ser testável no env native (test_alarm_queue).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include "SystemDefs_Records.h" /* ALARM_QUEUE_MAX, HIST_NAN_SENTINEL */

/** bit0: o sensor estava em falha quando o alarme foi gerado (valor = sentinela). */
#define ALARM_FLAG_ERR 0x01

/** Um registro da fila de alarmes. Layout em ordem natural (sem pack):
 * epoch alinhado a 4 — 12 B por registro na prática. */
struct AlarmRecord {
	uint32_t epoch;   /**< timestamp do disparo (time(nullptr)) */
	uint16_t seq;     /**< sequência do boot — chave da confirmação; 0 = inválido */
	int16_t  value;   /**< valor ×escala do canal; HIST_NAN_SENTINEL quando err */
	uint8_t  slot;    /**< 0..15 */
	uint8_t  channel; /**< CH_TEMP/CH_HUM/CH_PRESS/CH_LUX */
	uint8_t  flags;   /**< bitmask ALARM_FLAG_* */
};

class AlarmQueue {
public:
	/** capacity vem da config (alarmQueueMax); internamente limitada a
	 * ALARM_QUEUE_MAX. 0 é promovido a 1. */
	explicit AlarmQueue(uint8_t capacity);

	uint8_t capacity( ) const { return _cap; }
	uint8_t size( ) const { return _count; }
	bool empty( ) const { return _count == 0; }
	bool full( ) const { return _count >= _cap; }
	uint16_t dropped( ) const { return _dropped; }

	/** Enfileira um alarme. Atribui o seq e o retorna; 0 quando recusado
	 * (fila cheia — drop-newest, dropped( ) incrementa). */
	uint16_t push(uint32_t epoch, uint8_t slot, uint8_t channel, int16_t value, bool err);

	/** Copia até maxN registros em ordem de chegada para dst (sem remover).
	 * @return quantidade copiada. */
	uint8_t snapshot(AlarmRecord* dst, uint8_t maxN) const;

	/** Confirmação de recebimento: remove os registros cujo seq está em
	 * seqs[]. @return quantidade removida. */
	uint8_t ack(const uint16_t* seqs, uint8_t n);

	/** Fallback: descarta os n mais antigos sem confirmação (uso manual via CLI). */
	void ackOldest(uint8_t n);

	/** Reconfigura a capacidade em runtime (boot: cfg.alarmQueueMax).
	 * Encolher com registros na fila descarta os MAIS ANTIGOS para caber. */
	void setCapacity(uint8_t capacity) {
		uint8_t newCap = (capacity == 0) ? 1
			: (capacity > ALARM_QUEUE_MAX ? ALARM_QUEUE_MAX : capacity);
		if (newCap < _count) ackOldest((uint8_t)(_count - newCap));
		_cap = newCap;
	}

	/** Esvazia a fila sem confirmar. */
	void clear( );

private:
	AlarmRecord _buf[ALARM_QUEUE_MAX];
	uint8_t _cap;
	uint8_t _count;
	uint8_t _head;      /**< índice do registro mais antigo */
	uint16_t _nextSeq;  /**< próximo seq a atribuir (1..65535, 0 reservado) */
	uint16_t _dropped;

	bool seqInList(uint16_t seq, const uint16_t* seqs, uint8_t n) const;
};

inline AlarmQueue::AlarmQueue(uint8_t capacity) {
	_cap = (capacity == 0) ? 1 : (capacity > ALARM_QUEUE_MAX ? ALARM_QUEUE_MAX : capacity);
	_count = 0;
	_head = 0;
	_nextSeq = 1;
	_dropped = 0;
}

inline bool AlarmQueue::seqInList(uint16_t seq, const uint16_t* seqs, uint8_t n) const {
	for (uint8_t i = 0; i < n; i++) {
		if (seqs[i] == seq) return true;
	}
	return false;
}

inline uint16_t AlarmQueue::push(uint32_t epoch, uint8_t slot, uint8_t channel,
                                 int16_t value, bool err) {
	if (_count >= _cap) {
		/* drop-newest: recusa o registro novo. Ver doc do header. */
		_dropped++;
		return 0;
	}
	uint16_t seq = _nextSeq;
	_nextSeq++;
	if (_nextSeq == 0) _nextSeq = 1; /* 0 é reservado como inválido */

	uint8_t idx = (uint8_t)((_head + _count) % _cap);
	_buf[idx].epoch = epoch;
	_buf[idx].seq = seq;
	_buf[idx].value = value;
	_buf[idx].slot = slot;
	_buf[idx].channel = channel;
	_buf[idx].flags = err ? ALARM_FLAG_ERR : 0;
	_count++;
	return seq;
}

inline uint8_t AlarmQueue::snapshot(AlarmRecord* dst, uint8_t maxN) const {
	if (!dst || maxN == 0) return 0;
	uint8_t n = (maxN < _count) ? maxN : _count;
	for (uint8_t i = 0; i < n; i++) {
		dst[i] = _buf[(uint8_t)((_head + i) % _cap)];
	}
	return n;
}

inline uint8_t AlarmQueue::ack(const uint16_t* seqs, uint8_t n) {
	if (_count == 0 || n == 0 || !seqs) return 0;
	/* Compacta em ordem: percorre os ocupados, mantém os não confirmados,
	 * reconstrói o anel com _head = 0. O(cap) — cap <= 64. */
	AlarmRecord keep[ALARM_QUEUE_MAX];
	uint8_t kept = 0;
	for (uint8_t i = 0; i < _count; i++) {
		const AlarmRecord& r = _buf[(uint8_t)((_head + i) % _cap)];
		if (seqInList(r.seq, seqs, n)) continue;
		keep[kept++] = r;
	}
	uint8_t removed = _count - kept;
	for (uint8_t i = 0; i < kept; i++) _buf[i] = keep[i];
	_count = kept;
	_head = 0;
	return removed;
}

inline void AlarmQueue::ackOldest(uint8_t n) {
	if (n >= _count) { _count = 0; _head = 0; return; }
	_head = (uint8_t)((_head + n) % _cap);
	_count = (uint8_t)(_count - n);
}

inline void AlarmQueue::clear( ) {
	_count = 0;
	_head = 0;
}

static_assert(sizeof(AlarmRecord) <= 16, "AlarmRecord must stay small — RAM queue");

/** Extrai até maxN seqs de um payload de ACK por aplicação, no formato
 * {"seq":[1,2,3]} (chaves extras e espaços são tolerados). Para no
 * primeiro caractere inesperado dentro da lista — um ACK truncado não
 * confirma metade errada. seqs > 65535 são ignorados (não existem na fila).
 * @return quantidade extraída (0 quando não há '[' ou nenhum número). */
inline uint8_t alarmParseSeqList(const uint8_t* data, size_t len, uint16_t* out, uint8_t maxN) {
	if (!data || len == 0 || !out || maxN == 0) return 0;
	size_t i = 0;
	while (i < len && data[i] != '[') i++;
	if (i >= len) return 0;
	i++; /* dentro da lista */
	uint8_t n = 0;
	while (i < len && n < maxN) {
		/* pula até um dígito ou o fechamento */
		while (i < len && (data[i] < '0' || data[i] > '9')) {
			if (data[i] == ']') return n;
			i++;
		}
		if (i >= len) return n;
		uint32_t v = 0;
		while (i < len && data[i] >= '0' && data[i] <= '9') {
			v = v * 10 + (uint32_t)(data[i] - '0');
			if (v > 65535) v = 65536; /* satura: valor inválido, será ignorado */
			i++;
		}
		if (v >= 1 && v <= 65535) out[n++] = (uint16_t)v;
		/* próximo elemento: exige ',' (ou aceita o fechamento) */
		while (i < len && (data[i] == ' ' || data[i] == '	')) i++;
		if (i < len && data[i] == ']') return n;
		if (i < len && data[i] == ',') { i++; continue; }
		return n; /* caractere inesperado: ACK malformado, para por aqui */
	}
	return n;
}
