/**
 * @file AlarmPayload.h
 * @brief Formatadores da segunda linha de telemetria (alarmes, v21).
 *
 * @details Funções livres header-only, SEM dependências de hardware além de
 * SystemDefs_Records.h (constantes + SystemConfig) — mesmo padrão de
 * HaDiscovery.h/PromMetrics.h/Syslog5424.h: o código de produção vive aqui e
 * os testes nativos (test_alarm_queue) cobrem exatamente o que roda no ferro.
 *
 * Contrato do template de linha (tokens, entre chaves):
 *   {TS}    epoch Unix
 *   {ID}    id completo com prefixo da grandeza: t{hwid} / u{hwid} /
 *           p{hwid} / l{hwid}; sem hwId, {letra}{slot}
 *   {HWID}  hwId cru do slot
 *   {SLOT}  índice do slot (0..15)
 *   {CH}    letra do canal (t/u/p/l)
 *   {VAL}   valor formatado com os decimais do canal — VAZIO em falha
 *           (aliases minúsculos {val}/{err}/{seq} aceitos)
 *   {ERR}   código de status (alarmErrCodeStr): "" (limite ativo),
 *           "err" (erro ativo), "sil"/"err_sil" (silenciado),
 *           "off"/"err_off" (desativado) — VAZIO quando ok
 *   {SEQ}   sequência do boot (chave da confirmação de recebimento)
 *
 * Formas compostas: quando a chave tem o MESMO nome do token
 * ("val":{val}, "err":"{err}"), a chave inteira é removida se o token está
 * ausente — o template default produz JSON válido nos vários casos:
 *   ok:  {"ts":...,"id":"tX","val":25.30,"seq":1}
 *   err: {"ts":...,"id":"tX","err":"err","seq":2}
 *   sil: {"ts":...,"id":"tX","err":"sil","seq":3}
 *   off: {"ts":...,"id":"tX","err":"off","seq":4}
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include "SystemDefs_Records.h"
#include "sensors/SensorChannelTable.h"

/** id com prefixo da grandeza — a mesma convenção das chaves JSON da linha
 * convencional (TelemetryManager::formatLineJsonBuf) e do HA discovery. */
inline void alarmBuildId(const AlarmRecord& rec, const SystemConfig& cfg, char* dst, size_t cap) {
	const ChannelInfo& ci = channelInfo(rec.channel);
	if (rec.slot < MAX_SENSORS && cfg.sensors[rec.slot].hwId[0]) {
		snprintf(dst, cap, "%c%s", ci.letter, cfg.sensors[rec.slot].hwId);
	} else {
		snprintf(dst, cap, "%c%u", ci.letter, (unsigned)rec.slot);
	}
}

/** Decimais do valor por canal — MESMOS da linha convencional
 * (TelemetryManager::formatLineJsonBuf: temp %.2f, umidade/pressão %.1f).
 * O preset de display (ci.display.decimals) é do TFT e não casa com o
 * formato histórico dos payloads. */
inline int alarmChannelDecimals(uint8_t channel) {
	return (channel == CH_TEMP) ? 2 : 1;
}

/** Código de status do campo {err} (vocabulário em AlarmQueue.h). */
inline const char* alarmErrCodeStr(uint8_t errCode) {
	switch (errCode) {
		case ALARM_ERR_ERROR:   return "err";
		case ALARM_ERR_SIL:     return "sil";
		case ALARM_ERR_ERR_SIL: return "err_sil";
		case ALARM_ERR_OFF:     return "off";
		case ALARM_ERR_ERR_OFF: return "err_off";
		default:                return "";
	}
}

/** Linha CSV fixa: seq;ts;id;v — o valor vira o código de status quando o
 * registro é um marcador (erro ou ação de silenciar/desativar), mesmo padrão
 * do "err" original. */
inline int alarmFormatCsvLine(const AlarmRecord& rec, const SystemConfig& cfg, char* dest, size_t cap) {
	const ChannelInfo& ci = channelInfo(rec.channel);
	char idBuf[24];
	alarmBuildId(rec, cfg, idBuf, sizeof(idBuf));
	if (rec.errCode != ALARM_ERR_NONE) {
		return snprintf(dest, cap, "%u;%lu;%s;%s", (unsigned)rec.seq,
		                (unsigned long)rec.epoch, idBuf, alarmErrCodeStr(rec.errCode));
	}
	return snprintf(dest, cap, "%u;%lu;%s;%.*f", (unsigned)rec.seq,
	                (unsigned long)rec.epoch, idBuf,
	                alarmChannelDecimals(rec.channel),
	                (double)((float)rec.value / ci.scale));
}

/** Formata uma linha de alarme pelo template cfg.alarmTel.lineTemplate
 * (contrato no topo do arquivo). Zero alocação: só o buffer de destino. */
inline int alarmFormatLine(const AlarmRecord& rec, const SystemConfig& cfg,
                           char* dest, size_t cap) {
	if (cap == 0) return 0;
	dest[0] = '\0';

	char tsBuf[16];
	snprintf(tsBuf, sizeof(tsBuf), "%lu", (unsigned long)rec.epoch);
	char seqBuf[8];
	snprintf(seqBuf, sizeof(seqBuf), "%u", (unsigned)rec.seq);
	char slotBuf[4];
	snprintf(slotBuf, sizeof(slotBuf), "%u", (unsigned)rec.slot);
	const ChannelInfo& ci = channelInfo(rec.channel);

	char idBuf[24];
	alarmBuildId(rec, cfg, idBuf, sizeof(idBuf));
	char hwidBuf[17] = {0};
	if (rec.slot < MAX_SENSORS) strlcpy(hwidBuf, cfg.sensors[rec.slot].hwId, sizeof(hwidBuf));
	char chBuf[2];
	chBuf[0] = ci.letter; chBuf[1] = '\0';

	/* Código de status do {err} — "" para limite ativo; "err"/"sil"/"off"
	 * com os sufixos de ação. O valor só existe quando NÃO é marcador. */
	const bool isErr = (rec.flags & ALARM_FLAG_ERR) != 0;
	char valBuf[16] = "";
	char errBuf[8] = "";
	strlcpy(errBuf, alarmErrCodeStr(rec.errCode), sizeof(errBuf));
	if (!isErr && rec.value != HIST_NAN_SENTINEL) {
		snprintf(valBuf, sizeof(valBuf), "%.*f",
		         alarmChannelDecimals(rec.channel), (double)((float)rec.value / ci.scale));
	}

	const char* tpl = cfg.alarmTel.lineTemplate;
	const size_t tplLen = strnlen(tpl, sizeof(cfg.alarmTel.lineTemplate));
	size_t di = 0;
	size_t ti = 0;

	while (ti < tplLen && di + 1 < cap) {
		char c = tpl[ti];
		if (c != '{') { dest[di++] = c; ti++; continue; }

		const size_t remaining = tplLen - ti;
		const char* val = nullptr;
		char compKey[8] = {0};
		size_t compKeyLen = 0;
		size_t tokenChars = 0;

		if (remaining >= 4 && memcmp(tpl + ti, "{TS}", 4) == 0) {
			val = tsBuf; tokenChars = 4;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{ID}", 4) == 0) {
			val = idBuf; tokenChars = 4;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{HWID}", 6) == 0) {
			val = hwidBuf; tokenChars = 6;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{SLOT}", 6) == 0) {
			val = slotBuf; tokenChars = 6;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{CH}", 4) == 0) {
			val = chBuf; tokenChars = 4;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{VAL}", 5) == 0) {
			val = valBuf;
			compKeyLen = 3; memcpy(compKey, "VAL", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{val}", 5) == 0) {
			/* alias minúsculo — o compKey segue a grafia do token, então a
			 * forma composta "val":{val} remove a chave "val" */
			val = valBuf;
			compKeyLen = 3; memcpy(compKey, "val", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{ERR}", 5) == 0) {
			val = errBuf;
			compKeyLen = 3; memcpy(compKey, "ERR", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{err}", 5) == 0) {
			val = errBuf;
			compKeyLen = 3; memcpy(compKey, "err", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{SEQ}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "SEQ", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{seq}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "seq", 4);
			tokenChars = 5;
		}

		if (tokenChars == 0) {
			/* '{' sem token conhecido: emite literal, avança 1 */
			dest[di++] = c;
			ti++;
			continue;
		}

		/* Forma composta "<compKey>":{<tok>} — só quando o token a suporta. */
		bool matchedBare = false;
		if (compKeyLen > 0) {
			const size_t p2 = compKeyLen + 3; /* "<k>": */
			if (ti >= p2) {
				const char* p = tpl + ti - p2;
				if (p[0] == '"' &&
				    memcmp(p + 1, compKey, compKeyLen) == 0 &&
				    memcmp(p + 1 + compKeyLen, "\":", 2) == 0) {
					matchedBare = true;
				}
			}
		}

		if (matchedBare) {
			if (val && val[0] != '\0') {
				size_t vl = strlen(val);
				if (di + vl >= cap) vl = cap - 1 - di;
				memcpy(dest + di, val, vl);
				di += vl;
			} else {
				/* valor ausente: desfaz a chave já emitida */
				const size_t undo = compKeyLen + 3;
				if (di >= undo) di -= undo;
			}
		} else {
			const char* emit = (val && val[0] != '\0') ? val : "";
			size_t el = strlen(emit);
			if (di + el >= cap) el = cap - 1 - di;
			memcpy(dest + di, emit, el);
			di += el;
		}

		ti += tokenChars;
	}

	/* Limpeza in-place idêntica à linha convencional: ",," "{," ",}" etc. */
	size_t r = 0, w = 0;
	while (r < di) {
		char c = dest[r++];
		if (c == ',') {
			if (w == 0) continue;
			char prev = dest[w-1];
			if (prev == ',' || prev == '{' || prev == '[') continue;
		} else if ((c == '}' || c == ']') && w > 0 && dest[w-1] == ',') {
			w--;
		}
		dest[w++] = c;
	}
	dest[w] = '\0';
	return (int)w;
}
