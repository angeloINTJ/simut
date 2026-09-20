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
 *           (aliases minúsculos {val}/{seq} aceitos)
 *   {ALARM} código do domínio LIMITE com aspas JSON: "alarm", "alarm_sil",
 *           "alarm_off", "alarm_on" (reativado), "alarm_lim" (limites
 *           alterados) — VAZIO em registros de falha (alias {alarm})
 *   {ERR}   código do domínio FALHA de hardware com aspas JSON: "err",
 *           "err_sil", "err_off" — VAZIO em registros de limite (alias {err})
 *   {MAINT} código do domínio MANUTENÇÃO com aspas JSON: "maint_on" ao entrar
 *           na janela, "maint_off" ao sair (por comando ou por prazo) — VAZIO
 *           em todo o resto (alias {maint})
 *   {LO} {HI} os limites novos, com os decimais do canal — SÓ em "alarm_lim"
 *           (aliases {lo}/{hi})
 *   {UNTIL} epoch do fim previsto da janela — SÓ em "maint_on" (alias {until});
 *           resolução de um minuto, ver AlarmRecord::value2
 *   {USER}  nome do usuário que causou o registro — VAZIO quando foi o
 *           aparelho (borda de leitura, vencimento de prazo) (alias {user})
 *   {SEQ}   sequência do boot (chave da confirmação de recebimento)
 *
 * Formas compostas: quando a chave tem o MESMO nome do token e o token está
 * SEM aspas ("alarm":{alarm}, "err":{err}), a chave inteira é removida se o
 * token está ausente — o template default produz JSON válido e só a chave
 * do domínio relevante aparece:
 *   limite:      {"ts":...,"id":"tX","val":25.30,"alarm":"alarm","seq":1}
 *   lim. sil:    {"ts":...,"id":"tX","alarm":"alarm_sil","seq":2}
 *   falha:       {"ts":...,"id":"tX","err":"err","seq":3}
 *   err. desat:  {"ts":...,"id":"tX","err":"err_off","seq":4}
 *   manutenção:  {"ts":...,"id":"tX","maint":"maint_on","until":1789800000,"user":"joao","seq":5}
 *   fim da man.: {"ts":...,"id":"tX","maint":"maint_off","seq":6}
 *   limites:     {"ts":...,"id":"tX","alarm":"alarm_lim","lo":10.00,"hi":30.00,"user":"joao","seq":7}
 *   reativado:   {"ts":...,"id":"tX","alarm":"alarm_on","user":"joao","seq":8}
 *
 * ⚠️ Um template CUSTOM escrito antes da v23/v24 não tem {MAINT}, {LO}/{HI},
 * {UNTIL} nem {USER} e emite esses registros sem os campos. O default ganhou
 * os tokens, e um template que ainda era EXATAMENTE o default antigo é
 * trocado pelo novo na migração (StorageManager::upgradeDefaultAlarmTemplate);
 * quem mantém template próprio precisa acrescentá-los. GET /api/alarms também
 * reporta a janela, para um servidor que prefira perguntar a interpretar.
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

/** Código do campo "alarm" (domínio de LIMITE) — "" quando o registro é
 * de falha de hardware. */
inline const char* alarmCodeAlarmField(uint8_t errCode) {
	switch (errCode) {
		case ALARM_ERR_ALARM:     return "alarm";
		case ALARM_ERR_ALARM_SIL: return "alarm_sil";
		case ALARM_ERR_ALARM_OFF: return "alarm_off";
		case ALARM_ERR_ALARM_ON:  return "alarm_on";
		case ALARM_ERR_ALARM_LIM: return "alarm_lim";
		default:                  return "";
	}
}

/** Código do campo "err" (domínio de FALHA de hardware) — "" quando o
 * registro é de limite. */
inline const char* alarmCodeErrField(uint8_t errCode) {
	switch (errCode) {
		case ALARM_ERR_ERROR:   return "err";
		case ALARM_ERR_ERR_SIL: return "err_sil";
		case ALARM_ERR_ERR_OFF: return "err_off";
		default:                return "";
	}
}

/** Código do campo "maint" (domínio de MANUTENÇÃO, v23) — "" quando o registro
 * é de limite ou de falha. Domínio próprio e não um valor a mais em "err":
 * manutenção é o oposto de uma falha — é o aparelho dizendo que NÃO há nada a
 * tratar — e um servidor que casa por campo não deve precisar saber distinguir
 * "err":"maint" de um erro de verdade. */
inline const char* alarmCodeMaintField(uint8_t errCode) {
	switch (errCode) {
		case ALARM_ERR_MAINT_ON:  return "maint_on";
		case ALARM_ERR_MAINT_OFF: return "maint_off";
		default:                  return "";
	}
}

/** Único código que carrega valor de LEITURA ({val}): a borda de limite
 * ("alarm"). Ações (sil/off/on) e falhas (err*) são marcadores sem valor;
 * "alarm_lim" carrega os dois LIMITES em {lo}/{hi}, não uma leitura. */
inline bool alarmCodeHasValue(uint8_t errCode) {
	return errCode == ALARM_ERR_ALARM;
}

/** Nome do usuário por trás do registro, "" quando foi o aparelho. */
inline const char* alarmActorName(const AlarmRecord& rec, const SystemConfig& cfg) {
	if (rec.actor == ALARM_ACTOR_NONE || rec.actor > MAX_USERS) return "";
	const UserAccount& u = cfg.users[rec.actor - 1];
	return u.active ? u.username : "";
}

/** Epoch do fim previsto de uma janela ("maint_on"), 0 nos demais códigos. */
inline uint32_t alarmMaintUntil(const AlarmRecord& rec) {
	if (rec.errCode != ALARM_ERR_MAINT_ON) return 0;
	return rec.epoch + (uint32_t)(uint16_t)rec.value2 * 60u;
}

/** Formata os dois limites de um "alarm_lim" com os decimais do canal. */
inline void alarmFormatLimits(const AlarmRecord& rec, char* lo, size_t loCap, char* hi, size_t hiCap) {
	lo[0] = '\0'; hi[0] = '\0';
	if (rec.errCode != ALARM_ERR_ALARM_LIM) return;
	const ChannelInfo& ci = channelInfo(rec.channel);
	snprintf(lo, loCap, "%.*f", alarmChannelDecimals(rec.channel), (double)((float)rec.value / ci.scale));
	snprintf(hi, hiCap, "%.*f", alarmChannelDecimals(rec.channel), (double)((float)rec.value2 / ci.scale));
}

/** Linha CSV fixa: seq;ts;id;v;user;lo;hi;until — o valor vira o código de
 * status quando o registro é um marcador (erro, ação, manutenção), mesmo
 * padrão do "err" original. As quatro colunas da v24 vêm depois e ficam vazias
 * quando não se aplicam: quem lia quatro colunas continua lendo as mesmas
 * quatro. */
inline int alarmFormatCsvLine(const AlarmRecord& rec, const SystemConfig& cfg, char* dest, size_t cap) {
	const ChannelInfo& ci = channelInfo(rec.channel);
	char idBuf[24];
	alarmBuildId(rec, cfg, idBuf, sizeof(idBuf));
	char v[16];
	if (alarmCodeHasValue(rec.errCode)) {
		snprintf(v, sizeof(v), "%.*f", alarmChannelDecimals(rec.channel),
		         (double)((float)rec.value / ci.scale));
	} else {
		/* marcador: o código do domínio (alarm*, err* ou maint*) no lugar do valor */
		const char* marker = alarmCodeAlarmField(rec.errCode);
		if (marker[0] == '\0') marker = alarmCodeErrField(rec.errCode);
		if (marker[0] == '\0') marker = alarmCodeMaintField(rec.errCode);
		strlcpy(v, marker, sizeof(v));
	}
	char lo[16], hi[16], until[12] = "";
	alarmFormatLimits(rec, lo, sizeof(lo), hi, sizeof(hi));
	const uint32_t u = alarmMaintUntil(rec);
	if (u) snprintf(until, sizeof(until), "%lu", (unsigned long)u);
	return snprintf(dest, cap, "%u;%lu;%s;%s;%s;%s;%s;%s", (unsigned)rec.seq,
	                (unsigned long)rec.epoch, idBuf, v, alarmActorName(rec, cfg), lo, hi, until);
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

	/* Três domínios: "alarm" (limite), "err" (falha de hardware) e "maint"
	 * (janela de manutenção). Os tokens emitem o valor COM ASPAS (JSON
	 * válido) — a forma composta "chave":{token} sem aspas remove a chave
	 * quando o domínio está ausente, e cada registro pertence a UM domínio,
	 * então as outras duas chaves somem sozinhas. O valor de leitura só
	 * existe na borda de limite. */
	char valBuf[16] = "";
	char alarmTok[16] = "";
	char errTok[16] = "";
	char maintTok[16] = "";
	const char* af = alarmCodeAlarmField(rec.errCode);
	const char* ef = alarmCodeErrField(rec.errCode);
	const char* mf = alarmCodeMaintField(rec.errCode);
	if (af[0] != '\0') snprintf(alarmTok, sizeof(alarmTok), "\"%s\"", af);
	if (ef[0] != '\0') snprintf(errTok, sizeof(errTok), "\"%s\"", ef);
	if (mf[0] != '\0') snprintf(maintTok, sizeof(maintTok), "\"%s\"", mf);
	if (alarmCodeHasValue(rec.errCode) && rec.value != HIST_NAN_SENTINEL) {
		snprintf(valBuf, sizeof(valBuf), "%.*f",
		         alarmChannelDecimals(rec.channel), (double)((float)rec.value / ci.scale));
	}
	/* v24: os campos das ações identificadas. Cada um só existe no código que
	 * o carrega; nos demais fica vazio e a forma composta remove a chave. */
	char loBuf[16], hiBuf[16];
	alarmFormatLimits(rec, loBuf, sizeof(loBuf), hiBuf, sizeof(hiBuf));
	char untilBuf[12] = "";
	{
		const uint32_t u = alarmMaintUntil(rec);
		if (u) snprintf(untilBuf, sizeof(untilBuf), "%lu", (unsigned long)u);
	}
	char userTok[24] = "";
	{
		const char* un = alarmActorName(rec, cfg);
		if (un[0] != '\0') snprintf(userTok, sizeof(userTok), "\"%s\"", un);
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
		} else if (remaining >= 7 && memcmp(tpl + ti, "{ALARM}", 7) == 0) {
			val = alarmTok;
			compKeyLen = 5; memcpy(compKey, "ALARM", 6);
			tokenChars = 7;
		} else if (remaining >= 7 && memcmp(tpl + ti, "{alarm}", 7) == 0) {
			val = alarmTok;
			compKeyLen = 5; memcpy(compKey, "alarm", 6);
			tokenChars = 7;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{ERR}", 5) == 0) {
			val = errTok;
			compKeyLen = 3; memcpy(compKey, "ERR", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{err}", 5) == 0) {
			val = errTok;
			compKeyLen = 3; memcpy(compKey, "err", 4);
			tokenChars = 5;
		} else if (remaining >= 7 && memcmp(tpl + ti, "{MAINT}", 7) == 0) {
			val = maintTok;
			compKeyLen = 5; memcpy(compKey, "MAINT", 6);
			tokenChars = 7;
		} else if (remaining >= 7 && memcmp(tpl + ti, "{maint}", 7) == 0) {
			val = maintTok;
			compKeyLen = 5; memcpy(compKey, "maint", 6);
			tokenChars = 7;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{SEQ}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "SEQ", 4);
			tokenChars = 5;
		} else if (remaining >= 5 && memcmp(tpl + ti, "{seq}", 5) == 0) {
			val = seqBuf;
			compKeyLen = 3; memcpy(compKey, "seq", 4);
			tokenChars = 5;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{LO}", 4) == 0) {
			val = loBuf; compKeyLen = 2; memcpy(compKey, "LO", 3); tokenChars = 4;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{lo}", 4) == 0) {
			val = loBuf; compKeyLen = 2; memcpy(compKey, "lo", 3); tokenChars = 4;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{HI}", 4) == 0) {
			val = hiBuf; compKeyLen = 2; memcpy(compKey, "HI", 3); tokenChars = 4;
		} else if (remaining >= 4 && memcmp(tpl + ti, "{hi}", 4) == 0) {
			val = hiBuf; compKeyLen = 2; memcpy(compKey, "hi", 3); tokenChars = 4;
		} else if (remaining >= 7 && memcmp(tpl + ti, "{UNTIL}", 7) == 0) {
			val = untilBuf; compKeyLen = 5; memcpy(compKey, "UNTIL", 6); tokenChars = 7;
		} else if (remaining >= 7 && memcmp(tpl + ti, "{until}", 7) == 0) {
			val = untilBuf; compKeyLen = 5; memcpy(compKey, "until", 6); tokenChars = 7;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{USER}", 6) == 0) {
			val = userTok; compKeyLen = 4; memcpy(compKey, "USER", 5); tokenChars = 6;
		} else if (remaining >= 6 && memcmp(tpl + ti, "{user}", 6) == 0) {
			val = userTok; compKeyLen = 4; memcpy(compKey, "user", 5); tokenChars = 6;
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
