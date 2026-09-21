/**
 * @file ConfigApply.h
 * @brief Qual parte da configuração mudou, e se dá para aplicar sem reiniciar.
 *
 * @details `commit_all` sempre reiniciou. O motivo nunca foi que a configuração
 * precisa disso — é que ninguém sabia dizer QUAL campo tinha mudado, e reiniciar
 * era a única forma de garantir que todo subsistema releria tudo. Um gestor de
 * frota pagava um reboot para mexer num limite de alarme.
 *
 * Este módulo responde a pergunta. Recebe a configuração ANTES e DEPOIS do
 * parser e devolve um bitmask de classes tocadas; o handler aplica ao vivo as
 * que sabe aplicar e só reinicia se sobrar alguma que exija.
 *
 * ── AS DUAS INVARIANTES QUE FAZEM ISTO SER SEGURO ──────────────────────────
 *
 * **1. A classificação é por COMPARAÇÃO, não por instrumentação do parser.**
 * Um parser instrumentado esquece um campo no dia em que alguém acrescenta um
 * `cfg.x = ...` e não mexe na lista; a comparação não tem como esquecer, porque
 * olha os bytes.
 *
 * **2. O que não for reconhecido FORÇA REBOOT**, que é o comportamento de hoje.
 * Depois de comparar e copiar todo intervalo da tabela abaixo, o ANTES tem de
 * ficar byte a byte igual ao DEPOIS. Se sobrar diferença, algum campo mudou
 * fora de toda classe conhecida → CFG_UNKNOWN → reboot. Acrescentar um campo ao
 * schema sem tocar nesta tabela degrada para o comportamento antigo, nunca para
 * uma aplicação errada.
 *
 * A tabela é a razão de ser deste arquivo. A primeira versão tinha trinta
 * funções escritas à mão, um par por classe — uma que comparava e uma que
 * copiava — e o comentário delas admitia que as duas metades podiam divergir.
 * Com um intervalo por linha, comparar e copiar são o MESMO laço sobre a MESMA
 * tabela, e divergir deixa de ser possível. Custou 4,5 kB a menos de flash, o
 * que foi o sintoma que mandou reescrever.
 *
 * ⚠️ Campos de texto são comparados por memcmp do array INTEIRO, não por
 * strncmp. É correto porque `safeCopy` usa `strncpy`, que zera o resto do
 * destino. Um campo escrito por outro caminho, que deixe lixo depois do '\0',
 * aparece como mudança — ou seja, um reboot a mais. Direção segura.
 *
 * ── O QUE ESTÁ AO VIVO, E POR QUE SÓ ISSO ──────────────────────────────────
 *
 * Uma classe só entra na lista de "ao vivo" depois que alguém LEU o consumidor
 * e confirmou que ele relê a configuração — ou que existe uma função de
 * aplicação que empurra o valor novo (WebManager::applyConfigLive). A lista
 * começa curta de propósito: o pior erro possível aqui é aplicar ao vivo algo
 * que precisava de reboot, porque o aparelho fica num estado que ninguém
 * consegue reproduzir. Ampliar é barato e se faz uma classe por vez.
 *
 * Header-only e sem dependência de hardware, como AlarmPayload.h e Syslog5424.h:
 * o código que roda no ferro é o que os testes nativos exercitam.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "SystemDefs_Records.h"

/** Classes de mudança. Uma por subsistema que reage de forma diferente. */
enum ConfigChange : uint32_t {
	CFG_NONE       = 0,
	/* ── aplicáveis ao vivo ── */
	CFG_ALARMS     = 1u << 0,  /**< limites por canal + alarmsActive por slot */
	CFG_MAINT      = 1u << 1,  /**< janelas de manutenção */
	CFG_ALARMTEL   = 1u << 2,  /**< 2ª linha: enabled/queue/modo/templates/path */
	CFG_TELEMETRY  = 1u << 3,  /**< linha convencional pelo lado HTTP */
	CFG_DISPLAY    = 1u << 4,  /**< tema e idioma */
	/* ── exigem reboot ── */
	CFG_NET        = 1u << 16, /**< Wi-Fi, IP, DHCP, HTTPS */
	CFG_IDENTITY   = 1u << 17, /**< nome do aparelho (mDNS, SSID do AP) */
	CFG_USERS      = 1u << 18, /**< contas */
	CFG_SLOTS      = 1u << 19, /**< provisionamento de sensor: tipo, pinos, ROM */
	CFG_SENSING    = 1u << 20, /**< cadência de leitura, resolução do DS18 */
	CFG_MQTT       = 1u << 21, /**< transporte, credenciais MQTT, TLS da telemetria */
	CFG_TIME       = 1u << 22, /**< fuso e servidor NTP */
	CFG_LOGGING    = 1u << 23, /**< liga/desliga o log */
	CFG_PIN        = 1u << 24, /**< PIN do display */
	CFG_RESERVED   = 1u << 25, /**< overlays de reserved[]: porta web, HA, syslog */
	CFG_UNKNOWN    = 1u << 31, /**< algo fora de toda classe — fail-safe */
};

/** Tudo que NÃO pode ser aplicado ao vivo hoje. CFG_UNKNOWN está aqui por
 *  construção: é o balde do que ninguém classificou. */
constexpr uint32_t CFG_REBOOT_CLASSES =
	CFG_NET | CFG_IDENTITY | CFG_USERS | CFG_SLOTS | CFG_SENSING |
	CFG_MQTT | CFG_TIME | CFG_LOGGING | CFG_PIN | CFG_RESERVED | CFG_UNKNOWN;

/** Nome curto e estável de uma classe — é o que a resposta HTTP devolve em
 *  "applied" e em "reboot_for", e o que um gestor de frota loga. */
inline const char* configChangeName(uint32_t bit) {
	switch (bit) {
		case CFG_ALARMS:    return "alarms";
		case CFG_MAINT:     return "maint";
		case CFG_ALARMTEL:  return "alarm_tel";
		case CFG_TELEMETRY: return "telemetry";
		case CFG_DISPLAY:   return "display";
		case CFG_NET:       return "net";
		case CFG_IDENTITY:  return "identity";
		case CFG_USERS:     return "users";
		case CFG_SLOTS:     return "slots";
		case CFG_SENSING:   return "sensing";
		case CFG_MQTT:      return "mqtt";
		case CFG_TIME:      return "time";
		case CFG_LOGGING:   return "logging";
		case CFG_PIN:       return "display_pin";
		case CFG_RESERVED:  return "web";
		case CFG_UNKNOWN:   return "unclassified";
		default:            return "";
	}
}

/** Um intervalo de bytes de SystemConfig e a classe a que pertence. */
struct CfgSpan { uint16_t off; uint16_t len; uint32_t cls; };

#define CFG_FIELD(f, c) { (uint16_t)offsetof(SystemConfig, f), \
                          (uint16_t)sizeof(((SystemConfig*)0)->f), (c) }

/** A tabela. Toda a configuração que NÃO está em sensors[] — cada campo uma
 *  linha, na ordem da declaração, para que ler as duas lado a lado seja
 *  suficiente para auditar a cobertura.
 *
 *  `magic` e `version` estão de fora e são copiados à parte: não são
 *  configuração, mudam por migração e não por edição. */
inline const CfgSpan* cfgSpans(size_t& n) {
	static const CfgSpan T[] = {
		CFG_FIELD(deviceName,        CFG_IDENTITY),
		CFG_FIELD(wifiSsid,          CFG_NET),
		CFG_FIELD(wifiPass,          CFG_NET),
		CFG_FIELD(useDhcp,           CFG_NET),
		CFG_FIELD(staticIp,          CFG_NET),
		CFG_FIELD(staticMask,        CFG_NET),
		CFG_FIELD(staticGateway,     CFG_NET),
		CFG_FIELD(staticDns,         CFG_NET),
		CFG_FIELD(useHttps,          CFG_NET),
		CFG_FIELD(users,             CFG_USERS),
		CFG_FIELD(telServer,         CFG_TELEMETRY),
		CFG_FIELD(telPort,           CFG_TELEMETRY),
		CFG_FIELD(telPath,           CFG_TELEMETRY),
		CFG_FIELD(telApiKey,         CFG_TELEMETRY),
		CFG_FIELD(telInterval,       CFG_TELEMETRY),
		CFG_FIELD(telBatchSize,      CFG_TELEMETRY),
		/* telEncryption é MQTT e não TELEMETRY: o certificado é lido uma única
		 * vez no TelemetryManager::begin( ) (_hasCert), então ligá-lo ao vivo
		 * deixaria o aparelho tentando TLS sem cert carregado. */
		CFG_FIELD(telEncryption,     CFG_MQTT),
		CFG_FIELD(telMode,           CFG_TELEMETRY),
		CFG_FIELD(telGlobalTemplate, CFG_TELEMETRY),
		CFG_FIELD(telLineTemplate,   CFG_TELEMETRY),
		CFG_FIELD(telLineSeparator,  CFG_TELEMETRY),
		/* idem: PubSubClient::setServer( ) só é chamado no begin( ). */
		CFG_FIELD(telTransport,      CFG_MQTT),
		CFG_FIELD(mqttTopic,         CFG_MQTT),
		CFG_FIELD(mqttUser,          CFG_MQTT),
		CFG_FIELD(mqttPass,          CFG_MQTT),
		CFG_FIELD(mqttQos,           CFG_MQTT),
		CFG_FIELD(mqttRetain,        CFG_MQTT),
		CFG_FIELD(mqttClientId,      CFG_MQTT),
		CFG_FIELD(mqttKeepAlive,     CFG_MQTT),
		CFG_FIELD(timezoneOffset,    CFG_TIME),
		CFG_FIELD(sampleIntervalMs,  CFG_SENSING),
		CFG_FIELD(loggingEnabled,    CFG_LOGGING),
		CFG_FIELD(ds18Resolution,    CFG_SENSING),
		/* sensors[] não entra aqui — ver cfgSensorSpans. */
		CFG_FIELD(themeIndex,        CFG_DISPLAY),
		CFG_FIELD(displayPin,        CFG_PIN), /* v24: dead field, still a span */
		CFG_FIELD(displayLang,       CFG_DISPLAY),
		CFG_FIELD(ntpServer,         CFG_TIME),
		CFG_FIELD(reserved,          CFG_RESERVED),
		CFG_FIELD(alarmTel,          CFG_ALARMTEL),
		CFG_FIELD(maint,             CFG_MAINT),
		/* v25: o salt continua imutável, mas a POLÍTICA de PIN mora aqui e
		 * MUDA por commit. CFG_USERS não está em CFG_REBOOT_CLASSES, e é o
		 * certo: quem valida PIN lê a política a cada uso, então um teclado
		 * novo vale na próxima tela desenhada. Reiniciar aqui derrubaria o
		 * aparelho por uma configuração que não exige nada disso. */
		CFG_FIELD(pinAuth,           CFG_USERS),
	};
	n = sizeof(T) / sizeof(T[0]);
	return T;
}

#define CFG_SENSOR_FIELD(f, c) { (uint16_t)offsetof(SensorRecord, f), \
                                 (uint16_t)sizeof(((SensorRecord*)0)->f), (c) }

/** Os campos de UM SensorRecord. O ponto de partir o registro em dois é este:
 *  limite de alarme é editável ao vivo, provisionamento (tipo, pinos, ROM)
 *  reconstrói o pipeline de sensores e exige reboot — e os dois moram no mesmo
 *  struct. `offsetof` aqui é relativo ao registro; o laço soma a base do slot. */
inline const CfgSpan* cfgSensorSpans(size_t& n) {
	static const CfgSpan T[] = {
		CFG_SENSOR_FIELD(active,           CFG_SLOTS),
		CFG_SENSOR_FIELD(sensorType,       CFG_SLOTS),
		CFG_SENSOR_FIELD(pins,             CFG_SLOTS),
		CFG_SENSOR_FIELD(rom,              CFG_SLOTS),
		CFG_SENSOR_FIELD(hwId,             CFG_SLOTS),
		CFG_SENSOR_FIELD(friendlyName,     CFG_SLOTS),
		CFG_SENSOR_FIELD(provisionEpoch,   CFG_SLOTS),
		CFG_SENSOR_FIELD(chMin,            CFG_ALARMS),
		CFG_SENSOR_FIELD(chMax,            CFG_ALARMS),
		CFG_SENSOR_FIELD(alarmsActive,     CFG_ALARMS),
		CFG_SENSOR_FIELD(channelBitWidth,  CFG_SLOTS),
	};
	n = sizeof(T) / sizeof(T[0]);
	return T;
}

/**
 * O bitmask das classes que diferem entre `before` e `after`.
 *
 * ⚠️ `before` é CONSUMIDO: vira a sonda do fail-safe no lugar, e sai desta
 * função sem valor. Assinatura assim, e não com uma cópia local, porque
 * SystemConfig tem 6.738 bytes (era 4.792 quando isto foi escrito) e isto roda dentro de um handler web — a versão
 * com `SystemConfig probe = before;` na pilha compilava, passava nos testes
 * nativos (onde a pilha é do host) e teria estourado no ferro. O chamador já
 * tem a cópia no heap; ela é o rascunho.
 */
inline uint32_t classifyConfigChanges(SystemConfig& before, const SystemConfig& after) {
	uint32_t m = CFG_NONE;
	uint8_t* A = (uint8_t*)&before;
	const uint8_t* B = (const uint8_t*)&after;

	size_t n = 0;
	const CfgSpan* sp = cfgSpans(n);
	for (size_t i = 0; i < n; i++) {
		if (memcmp(A + sp[i].off, B + sp[i].off, sp[i].len) != 0) {
			m |= sp[i].cls;
			memcpy(A + sp[i].off, B + sp[i].off, sp[i].len);
		}
	}

	size_t sn = 0;
	const CfgSpan* ss = cfgSensorSpans(sn);
	for (uint8_t s = 0; s < MAX_SENSORS; s++) {
		const size_t base = offsetof(SystemConfig, sensors) + (size_t)s * sizeof(SensorRecord);
		for (size_t i = 0; i < sn; i++) {
			const size_t off = base + ss[i].off;
			if (memcmp(A + off, B + off, ss[i].len) != 0) {
				m |= ss[i].cls;
				memcpy(A + off, B + off, ss[i].len);
			}
		}
	}

	before.magic = after.magic;
	before.version = after.version;

	/* O fail-safe: depois de copiar toda linha das duas tabelas, o que sobrar
	 * diferente é campo que ninguém classificou. */
	if (memcmp(&before, &after, sizeof(before)) != 0) m |= CFG_UNKNOWN;
	return m;
}

/** Precisa reiniciar para que o que mudou valha? */
inline bool configNeedsReboot(uint32_t mask) {
	return (mask & CFG_REBOOT_CLASSES) != 0;
}

/** Lista as classes do mask como um array JSON, sem os colchetes: `"a","b"`.
 *  Devolve o número de bytes escritos (sem o terminador). Trunca em vez de
 *  estourar, e nunca deixa um token pela metade — um 200 com JSON inválido é
 *  mais caro de diagnosticar do que uma lista curta. */
inline size_t configChangeList(uint32_t mask, char* dst, size_t cap) {
	size_t o = 0;
	if (cap == 0) return 0;
	dst[0] = '\0';
	for (uint8_t b = 0; b < 32; b++) {
		const uint32_t bit = 1u << b;
		if (!(mask & bit)) continue;
		const char* nm = configChangeName(bit);
		const size_t nl = strlen(nm);
		if (!nl) continue;
		const size_t need = nl + 2 + (o ? 1 : 0); /* "x" e a vírgula */
		if (o + need + 1 > cap) break;
		if (o) dst[o++] = ',';
		dst[o++] = '"';
		memcpy(dst + o, nm, nl); o += nl;
		dst[o++] = '"';
		dst[o] = '\0';
	}
	return o;
}
