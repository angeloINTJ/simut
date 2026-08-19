/**
 * @file HaDiscovery.h
 * @brief Pure formatters for Home Assistant MQTT Discovery config messages.
 * @details Builds the retained JSON payloads HA reads from
 * `homeassistant/sensor/<node>/<object>/config` so a SIMUT publishing JSON
 * telemetry over MQTT shows up as a device with one entity per measurement —
 * no YAML on the HA side. Everything here is snprintf into caller buffers
 * with zero Arduino dependencies, so test/test_ha_discovery compiles this
 * header natively and pins the exact bytes on the wire.
 *
 * Which entities exist for a given sensor table is TelemetryManager's
 * decision (publishHaDiscovery), because it must mirror formatLineJsonBuf —
 * the discovery message is a promise about keys that formatter emits.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sensors/SensorChannels.h" /* CH_* ids for deviceClass() */

namespace HaDiscovery {

/** Discovery prefix HA subscribes to by default. Not configurable on
 *  purpose: the HA side default covers practically every install, and a
 *  config knob would cost a field + UI + two pack strings for a value
 *  nobody changes. */
constexpr const char* PREFIX = "homeassistant";

/**
 * @brief Copy `src` into `dst` keeping only [A-Za-z0-9_-], '_' otherwise.
 *
 * HA object ids and unique ids live in this alphabet. hwIds are only
 * validated as printable (isValidCfgString), so "sala 1" is a legal hwId —
 * it sanitizes to "sala_1" for the topic while the value_template keeps the
 * raw key the telemetry JSON actually carries.
 */
inline void sanitizeId(const char* src, char* dst, size_t cap) {
	if (cap == 0) return;
	size_t o = 0;
	for (size_t i = 0; src && src[i] && o < cap - 1; i++) {
		char c = src[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		       || (c >= '0' && c <= '9') || c == '_' || c == '-';
		dst[o++] = ok ? c : '_';
	}
	dst[o] = '\0';
}

/**
 * @brief True if `key` can be quoted verbatim inside `value_json['<key>']`.
 *
 * A quote or backslash in a hwId would shear the Jinja string (and already
 * shears the telemetry JSON itself, whose keys are emitted unescaped) — an
 * entity for such a slot would be a promise about a payload that does not
 * parse, so the caller skips it instead.
 */
inline bool keyTemplatable(const char* key) {
	for (size_t i = 0; key && key[i]; i++) {
		if (key[i] == '\'' || key[i] == '"' || key[i] == '\\') return false;
	}
	return true;
}

/** @brief JSON-escape `src` into `dst` (quotes, backslash, control chars). */
inline void jsonEscapeInto(const char* src, char* dst, size_t cap) {
	if (cap == 0) return;
	size_t o = 0;
	for (size_t i = 0; src && src[i]; i++) {
		unsigned char c = (unsigned char)src[i];
		const char* rep = nullptr;
		char unibuf[8];
		if (c == '"') rep = "\\\"";
		else if (c == '\\') rep = "\\\\";
		else if (c < 32) { snprintf(unibuf, sizeof(unibuf), "\\u%04x", c); rep = unibuf; }
		size_t need = rep ? strlen(rep) : 1;
		if (o + need >= cap) break;
		if (rep) { memcpy(dst + o, rep, need); o += need; }
		else dst[o++] = (char)c;
	}
	dst[o] = '\0';
}

/**
 * @brief HA `device_class` for a channel id.
 *
 * The static_assert is the same contract the channel table enforces: a new
 * measurement axis must decide its HA class here on the day its row lands,
 * or the build says so — a zero-filled slot would publish a null class
 * silently instead.
 */
inline const char* deviceClass(uint8_t ch) {
	static const char* const MAP[CH_COUNT] = {
		/* CH_TEMP  */ "temperature",
		/* CH_HUM   */ "humidity",
		/* CH_PRESS */ "pressure",
		/* CH_LUX   */ "illuminance",
	};
	static_assert(CH_COUNT == 4, "new channel: add its HA device_class to MAP above");
	return (ch < CH_COUNT) ? MAP[ch] : "";
}

/** @brief Config topic: `homeassistant/sensor/<nodeId>/<objectId>/config`. */
inline int configTopic(char* out, size_t cap, const char* nodeId, const char* objectId) {
	return snprintf(out, cap, "%s/sensor/%s/%s/config", PREFIX, nodeId, objectId);
}

/** Fields shared by every entity of the device — built once per refresh. */
struct EntityCtx {
	const char* nodeId;     /**< Sanitized MQTT client id; device identifier. */
	const char* stateTopic; /**< Resolved data topic (mqttDataTopic).         */
	const char* availTopic; /**< Resolved LWT topic (mqttStatusTopic).        */
	const char* deviceName; /**< cfg.deviceName — JSON-escaped here.          */
	const char* swVersion;  /**< SIMUT_VERSION.                               */
	const char* configUrl;  /**< "http://<ip>[:port]", or "" to omit.         */
};

/**
 * @brief One entity's retained config payload, abbreviated keys.
 *
 * `valueKey` is the RAW telemetry JSON key ("t28FF64"), `objectId` its
 * sanitized twin for ids. The availability pair mirrors the LWT payloads
 * mqttEnsureConnected publishes ({"status":"online"/"offline"}).
 * `suggPrecision` < 0 omits the hint.
 *
 * @return snprintf semantics: value >= cap means the payload was truncated
 * and must not be published.
 */
inline int entityConfigJson(char* out, size_t cap, const EntityCtx& ctx,
                            const char* objectId, const char* valueKey,
                            const char* name, const char* devClass,
                            const char* unit, int8_t suggPrecision) {
	char nameEsc[96];
	char devNameEsc[64];
	jsonEscapeInto(name, nameEsc, sizeof(nameEsc));
	jsonEscapeInto(ctx.deviceName, devNameEsc, sizeof(devNameEsc));

	char prcBuf[24];
	if (suggPrecision >= 0) snprintf(prcBuf, sizeof(prcBuf), "\"sug_dsp_prc\":%d,", suggPrecision);
	else prcBuf[0] = '\0';

	char urlBuf[64];
	if (ctx.configUrl && ctx.configUrl[0]) snprintf(urlBuf, sizeof(urlBuf), ",\"cu\":\"%s\"", ctx.configUrl);
	else urlBuf[0] = '\0';

	return snprintf(out, cap,
		"{\"name\":\"%s\","
		"\"uniq_id\":\"%s_%s\","
		"\"stat_t\":\"%s\","
		"\"val_tpl\":\"{{ value_json['%s'] }}\","
		"\"unit_of_meas\":\"%s\","
		"\"dev_cla\":\"%s\","
		"\"stat_cla\":\"measurement\","
		"%s"
		"\"avty_t\":\"%s\","
		"\"avty_tpl\":\"{{ value_json.status }}\","
		"\"pl_avail\":\"online\","
		"\"pl_not_avail\":\"offline\","
		"\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mf\":\"SIMUT\","
		"\"mdl\":\"Raspberry Pi Pico W\",\"sw\":\"%s\"%s}}",
		nameEsc, ctx.nodeId, objectId, ctx.stateTopic, valueKey, unit,
		devClass, prcBuf, ctx.availTopic, ctx.nodeId, devNameEsc,
		ctx.swVersion, urlBuf);
}

} /* namespace HaDiscovery */
